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
    LedgerReplayer& replayer,
    uint256 const& ledgerHash,
    std::uint32_t ledgerSeq,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          ledgerHash,
          LEDGER_REPLAY_TIMEOUT,
          app.journal("LedgerDeltaAcquire"))
    , replayer_(replayer)
    , ledgerSeq_(ledgerSeq)
    , peerSet_(std::move(peerSet))
{
    JLOG(m_journal.debug())
        << "Acquire ledger " << mHash << " Seq " << ledgerSeq;
}

LedgerDeltaAcquire::~LedgerDeltaAcquire()
{
    replayer_.removeLedgerDeltaAcquire(mHash);
    JLOG(m_journal.trace()) << "Delta dtor, remove myself " << mHash;
}

void
LedgerDeltaAcquire::init(int numPeers)
{
    auto l = app_.getLedgerMaster().getLedgerByHash(mHash);
    ScopedLockType sl(mLock);
    if (l)
    {
        mComplete = true;
        replay_ = l;
        ledgerBuilt_ = true;
        JLOG(m_journal.trace()) << "Acquire existing ledger " << mHash;
    }
    else
    {
        addPeers(numPeers);
        setTimer();
    }
}

void
LedgerDeltaAcquire::addPeers(std::size_t limit)
{
    peerSet_->addPeers(
        limit,
        [this](auto peer) { return peer->hasLedger(mHash, ledgerSeq_); },
        [this](auto peer) {
            if (isDone() || replay_ || !peer)  // TODO need??
                return;

            JLOG(m_journal.trace())
                << "Add a peer " << peer->id() << " for " << mHash;
            protocol::TMReplayDeltaRequest request;
            request.set_ledgerhash(mHash.data(), mHash.size());
            peerSet_->sendRequest(
                request, protocol::mtReplayDeltaRequest, peer);
        });
}

void
LedgerDeltaAcquire::queueJob()
{
    app_.getJobQueue().addJob(
        jtREPLAY_DELTA, "LedgerDeltaAcquire", [ptr = shared_from_this()](Job&) {
            ptr->invokeOnTimer();
        });
}

void
LedgerDeltaAcquire::onTimer(bool progress, ScopedLockType& psl)
{
    JLOG(m_journal.trace()) << "mTimeouts=" << mTimeouts << " for " << mHash;
    if (mTimeouts > LEDGER_REPLAY_MAX_TIMEOUTS)
    {
        mFailed = true;
        for (auto& t : tasks_)
            t->cancel();
        return;
    }

    addPeers(1);
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
    JLOG(m_journal.trace()) << "got data for " << mHash;
    assert(info.seq == ledgerSeq_);

    // create a temp ledger for building a LedgerReplay object later
    if (auto rp =
            std::make_shared<Ledger>(info, app_.config(), app_.getNodeFamily());
        rp)
    {
        ScopedLockType sl(mLock);
        if (mComplete)
            return;
        mComplete = true;
        mFailed = false;
        replay_ = rp;
        orderedTxns_ = std::move(orderedTxns);
        JLOG(m_journal.debug()) << "ready to replay " << mHash;
        for (auto& t : tasks_)
            t->tryAdvance({});
    }
}

void
LedgerDeltaAcquire::addTask(std::shared_ptr<LedgerReplayTask>& task)
{
    ScopedLockType sl(mLock);
    auto r = tasks_.emplace(task);
    assert(r.second);
    auto reason = task->getTaskTaskParameter().reason;
    if (reasons_.count(reason) == 0)
    {
        reasons_.emplace(reason);
        if (ledgerBuilt_)
            onLedgerBuilt(reason);
    }
    if (mFailed)
        task->cancel();
}

// void
// LedgerDeltaAcquire::removeTask(std::shared_ptr<LedgerReplayTask>const& task)
//{
//    ScopedLockType sl(mLock);
//    tasks_.erase(task);
//}

std::shared_ptr<Ledger const>
LedgerDeltaAcquire::tryBuild(std::shared_ptr<Ledger const> const& parent)
{
    ScopedLockType sl(mLock);

    if (ledgerBuilt_)
        return replay_;

    if (mFailed || !mComplete || !replay_)
        return {};

    // build ledger
    assert(parent->seq() + 1 == replay_->seq());
    assert(parent->info().hash == replay_->info().parentHash);
    LedgerReplay replayData(parent, replay_, std::move(orderedTxns_));
    auto const l = buildLedger(replayData, tapNONE, app_, m_journal);
    if (!l)
    {
        JLOG(m_journal.error())
            << "tryBuild failed " << mHash << " parent " << parent->info().hash;
        orderedTxns_ = replayData.orderedTxns();
        return {};
    }
    assert(mHash == l->info().hash);

    replay_ = l;
    ledgerBuilt_ = true;
    JLOG(m_journal.info()) << "Built " << mHash;
    onLedgerBuilt();
    return l;
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
                app_.getShardStore()->setStored(replay_);
                [[fallthrough]];
            case InboundLedger::Reason::HISTORY:
                app_.getInboundLedgers().onLedgerFetched();
                break;
            default:
                app_.getLedgerMaster().storeLedger(replay_);
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

        app_.getLedgerMaster().checkAccept(replay_);
        app_.getLedgerMaster().tryAdvance();
        JLOG(m_journal.info()) << "stored " << mHash;
    }
}

}  // namespace ripple
