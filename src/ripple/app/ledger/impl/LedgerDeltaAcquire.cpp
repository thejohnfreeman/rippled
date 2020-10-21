//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/BuildLedger.h>
#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/LedgerReplay.h>
#include <ripple/app/ledger/LedgerReplayTask.h>
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/LedgerDeltaAcquire.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Overlay.h>

#include <memory>

namespace ripple {

LedgerDeltaAcquire::LedgerDeltaAcquire(
    Application& app,
    InboundLedgers& inboundLedgers,
    uint256 const& ledgerHash,
    std::uint32_t ledgerSeq,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          ledgerHash,
          LedgerReplayer::SUB_TASK_TIMEOUT,
          app.journal("LedgerReplayDelta"))
    , inboundLedgers_(inboundLedgers)
    , ledgerSeq_(ledgerSeq)
    , peerSet_(std::move(peerSet))
{
    JLOG(m_journal.debug()) << "Delta ctor " << mHash << " Seq " << ledgerSeq;
}

LedgerDeltaAcquire::~LedgerDeltaAcquire()
{
    JLOG(m_journal.trace()) << "Delta dtor " << mHash;
    app_.getLedgerReplayer().removeLedgerDeltaAcquire(mHash);
}

void
LedgerDeltaAcquire::init(int numPeers)
{
    ScopedLockType sl(mLock);
    fullLedger_ = app_.getLedgerMaster().getLedgerByHash(mHash);
    if (fullLedger_)
    {
        mComplete = true;
        JLOG(m_journal.trace()) << "Acquire existing ledger " << mHash;
        notifyTasks(sl);
    }
    else
    {
        trigger(numPeers, sl);
        setTimer();
    }
}

void
LedgerDeltaAcquire::trigger(std::size_t limit, ScopedLockType& psl)
{
    if (isDone())
        return;

    fullLedger_ = app_.getLedgerMaster().getLedgerByHash(mHash);
    if (!fullLedger_ && fallBack_)
    {
        fullLedger_ = inboundLedgers_.acquire(
            mHash,
            ledgerSeq_,
            InboundLedger::Reason::GENERIC);  // TODO reason[0]?
    }
    if (fullLedger_)
    {
        mComplete = true;
        notifyTasks(psl);
        return;
    }

    if (fallBack_)
        return;

    peerSet_->addPeers(
        limit,
        [this](auto peer) {
            return peer->supportsFeature(ProtocolFeature::LedgerReplay) &&
                peer->hasLedger(mHash, ledgerSeq_);
        },
        [this](auto peer) {
            if (peer->supportsFeature(ProtocolFeature::LedgerReplay))
            {
                JLOG(m_journal.trace())
                    << "Add a peer " << peer->id() << " for " << mHash;
                protocol::TMReplayDeltaRequest request;
                request.set_ledgerhash(mHash.data(), mHash.size());
                peerSet_->sendRequest(
                    request, protocol::mtREPLAY_DELTA_REQ, peer);
            }
            else
            {
                JLOG(m_journal.trace()) << "Fall back for " << mHash;
                fallBack_ = true;
            }
        });
}

void
LedgerDeltaAcquire::queueJob()
{
    if (app_.getJobQueue().getJobCountTotal(jtREPLAY_TASK) >
        LedgerReplayer::MAX_QUEUED_TASKS)
    {
        JLOG(m_journal.debug())
            << "Deferring LedgerDeltaAcquire timer due to load";
        setTimer();
        return;
    }

    std::weak_ptr<LedgerDeltaAcquire> wptr = shared_from_this();
    app_.getJobQueue().addJob(
        jtREPLAY_TASK, "LedgerDeltaAcquire", [wptr](Job&) {
            if (auto sptr = wptr.lock(); sptr)
                sptr->invokeOnTimer();
        });
}

void
LedgerDeltaAcquire::onTimer(bool progress, ScopedLockType& psl)
{
    JLOG(m_journal.trace()) << "mTimeouts=" << mTimeouts << " for " << mHash;
    if (mTimeouts > LedgerReplayer::SUB_TASK_MAX_TIMEOUTS)
    {
        mFailed = true;
        notifyTasks(psl);
    }
    else
    {
        trigger(1, psl);
    }
}

std::weak_ptr<TimeoutCounter>
LedgerDeltaAcquire::pmDowncast()
{
    return shared_from_this();
}

void
LedgerDeltaAcquire::processData(
    LedgerInfo const& info,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns)
{
    ScopedLockType sl(mLock);
    JLOG(m_journal.trace()) << "got data for " << mHash;
    if (isDone())
        return;

    // create a temp ledger for building a LedgerReplay object later
    replayTemp_ =
        std::make_shared<Ledger>(info, app_.config(), app_.getNodeFamily());
    if (replayTemp_)
    {
        mComplete = true;
        orderedTxns_ = std::move(orderedTxns);
        JLOG(m_journal.debug()) << "ready to replay " << mHash;
        notifyTasks(sl);
    }
    else
    {
        mFailed = true;
        notifyTasks(sl);
    }
}

void
LedgerDeltaAcquire::addTask(std::shared_ptr<LedgerReplayTask>& task)
{
    ScopedLockType sl(mLock);
    tasks_.emplace_back(task);

    auto reason = task->getTaskParameter().reason;
    if (reasons_.count(reason) == 0)
    {
        reasons_.emplace(reason);
        if (fullLedger_)
            onLedgerBuilt(reason);
    }
    if (mFailed)
        task->cancel();
    else if (mComplete)
        task->deltaReady();
}

std::shared_ptr<Ledger const>
LedgerDeltaAcquire::tryBuild(std::shared_ptr<Ledger const> const& parent)
{
    ScopedLockType sl(mLock);

    if (fullLedger_)
        return fullLedger_;

    if (mFailed || !mComplete || !replayTemp_)
        return {};

    assert(
        parent->seq() + 1 == replayTemp_->seq() &&
        parent->info().hash == replayTemp_->info().parentHash);
    // build ledger
    LedgerReplay replayData(parent, replayTemp_, std::move(orderedTxns_));
    fullLedger_ = buildLedger(replayData, tapNONE, app_, m_journal);
    if (fullLedger_ && fullLedger_->info().hash == mHash)
    {
        JLOG(m_journal.info()) << "Built " << mHash;
        onLedgerBuilt();
        return fullLedger_;
    }
    else
    {
        mFailed = true;  // mComplete == true now
        JLOG(m_journal.error())
            << "tryBuild failed " << mHash << " parent " << parent->info().hash;
        notifyTasks(sl);
        return {};
    }
}

void
LedgerDeltaAcquire::onLedgerBuilt(std::optional<InboundLedger::Reason> reason)
{
    JLOG(m_journal.debug())
        << "onLedgerBuilt " << mHash << (reason ? " for a reason" : "");

    auto store = [&](InboundLedger::Reason reason) {
        switch (reason)
        {  // TODO from InboundLedger::done(), ask Mickey to review
            case InboundLedger::Reason::SHARD:
                app_.getShardStore()->setStored(fullLedger_);
                [[fallthrough]];
            case InboundLedger::Reason::HISTORY:
                app_.getInboundLedgers().onLedgerFetched();
                break;
            default:
                app_.getLedgerMaster().storeLedger(fullLedger_);
                break;
        }
    };

    if (reason)
    {
        if (reason == InboundLedger::Reason::HISTORY &&
            reasons_.count(InboundLedger::Reason::SHARD))
            // best effort only,
            // there could be a case that store() gets called twice for both
            // HISTORY and SHARD, if a SHARD task is added after the ledger has
            // been built
            return;

        store(*reason);
        JLOG(m_journal.info()) << "stored " << mHash << " for reason";
    }
    else
    {
        for (auto reason : reasons_)
        {
            if (reason == InboundLedger::Reason::HISTORY &&
                reasons_.count(InboundLedger::Reason::SHARD))
                continue;
            store(reason);
        }

        app_.getLedgerMaster().checkAccept(fullLedger_);
        app_.getLedgerMaster().tryAdvance();
        JLOG(m_journal.info()) << "stored " << mHash;
    }
}

void
LedgerDeltaAcquire::notifyTasks(ScopedLockType& psl)
{
    for (auto& t : tasks_)
    {
        if (auto sptr = t.lock(); sptr)
        {
            if (mFailed)
                sptr->cancel();
            else
                sptr->deltaReady();
        }
    }
}

}  // namespace ripple
