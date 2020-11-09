//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2020 Ripple Labs Inc.

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
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/PeerSet.h>

namespace ripple {

LedgerDeltaAcquire::LedgerDeltaAcquire(
    Application& app,
    InboundLedgers& inboundLedgers,
    LedgerReplayer& replayer,
    uint256 const& ledgerHash,
    std::uint32_t ledgerSeq,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          ledgerHash,
          LedgerReplayParameters::SUB_TASK_TIMEOUT,
          {jtREPLAY_TASK,
           "LedgerReplayDelta",
           LedgerReplayParameters::MAX_QUEUED_TASKS},
          app.journal("LedgerReplayDelta"))
    , inboundLedgers_(inboundLedgers)
    , replayer_(replayer)
    , ledgerSeq_(ledgerSeq)
    , peerSet_(std::move(peerSet))
{
    JLOG(m_journal.debug()) << "Delta ctor " << mHash << " Seq "
                            << ledgerSeq;  // TODO remove after test
}

LedgerDeltaAcquire::~LedgerDeltaAcquire()
{
    JLOG(m_journal.trace())
        << "Delta dtor " << mHash;  // TODO remove after test
    replayer_.removeLedgerDeltaAcquire(mHash);
}

void
LedgerDeltaAcquire::init(int numPeers)
{
    ScopedLockType sl(mLock);
    fullLedger_ = app_.getLedgerMaster().getLedgerByHash(mHash);
    if (fullLedger_)
    {
        mComplete = true;
        JLOG(m_journal.trace()) << "init with an existing ledger " << mHash;
        notifyTasks(sl);
    }
    else
    {
        trigger(numPeers, sl);
        setTimer();
    }
}

void
LedgerDeltaAcquire::trigger(std::size_t limit, ScopedLockType& sl)
{
    if (isDone())
        return;

    fullLedger_ = app_.getLedgerMaster().getLedgerByHash(mHash);
    if (!fullLedger_ && fallBack_)
    {
        fullLedger_ = inboundLedgers_.acquire(
            mHash,
            ledgerSeq_,
            InboundLedger::Reason::GENERIC);  // TODO reason for other use cases
    }
    if (fullLedger_)
    {
        mComplete = true;
        notifyTasks(sl);
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
                JLOG(m_journal.trace())
                    << "Add a no feature peer " << peer->id() << " for "
                    << mHash;  // TODO remove after test
                if (++noFeaturePeerCount >=
                    LedgerReplayParameters::MAX_NO_FEATURE_PEER_COUNT)
                {
                    JLOG(m_journal.debug()) << "Fall back for " << mHash;
                    mTimerInterval =
                        LedgerReplayParameters::SUB_TASK_FALLBACK_TIMEOUT;
                    fallBack_ = true;
                }
            }
        });

    if (fallBack_)
        inboundLedgers_.acquire(
            mHash, ledgerSeq_, InboundLedger::Reason::GENERIC);
}

void
LedgerDeltaAcquire::onTimer(bool progress, ScopedLockType& sl)
{
    JLOG(m_journal.trace()) << "mTimeouts=" << mTimeouts << " for " << mHash;
    if (mTimeouts > LedgerReplayParameters::SUB_TASK_MAX_TIMEOUTS)
    {
        mFailed = true;
        JLOG(m_journal.debug()) << "too many timeouts " << mHash;
        notifyTasks(sl);
    }
    else
    {
        trigger(1, sl);
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

    if (info.seq == ledgerSeq_)
    {
        // create a temp ledger for building a LedgerReplay object later
        replayTemp_ =
            std::make_shared<Ledger>(info, app_.config(), app_.getNodeFamily());
        if (replayTemp_)
        {
            mComplete = true;
            orderedTxns_ = std::move(orderedTxns);
            JLOG(m_journal.debug()) << "ready to replay " << mHash;
            notifyTasks(sl);
            return;
        }
    }

    mFailed = true;
    JLOG(m_journal.error())
        << "failed to create a (info only) ledger from verified data " << mHash;
    notifyTasks(sl);
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
            onLedgerBuilt(sl, reason);
    }
    if (mFailed)
    {
        JLOG(m_journal.debug())
            << "task added to a failed LedgerDeltaAcquire " << mHash;
        notifyTask(sl, task);
    }
    else if (mComplete)
        notifyTask(sl, task);
}

std::shared_ptr<Ledger const>
LedgerDeltaAcquire::tryBuild(std::shared_ptr<Ledger const> const& parent)
{
    ScopedLockType sl(mLock);

    if (fullLedger_)
        return fullLedger_;

    if (mFailed || !mComplete || !replayTemp_)
        return {};

    assert(parent->seq() + 1 == replayTemp_->seq());
    assert(parent->info().hash == replayTemp_->info().parentHash);
    // build ledger
    LedgerReplay replayData(parent, replayTemp_, std::move(orderedTxns_));
    fullLedger_ = buildLedger(replayData, tapNONE, app_, m_journal);
    if (fullLedger_ && fullLedger_->info().hash == mHash)
    {
        JLOG(m_journal.info()) << "Built " << mHash;
        onLedgerBuilt(sl);
        return fullLedger_;
    }
    else
    {
        mFailed = true;  // mComplete == true too now
        JLOG(m_journal.error()) << "tryBuild failed " << mHash
                                << " with parent " << parent->info().hash;
        notifyTasks(sl);
        return {};
    }
}

void
LedgerDeltaAcquire::onLedgerBuilt(
    ScopedLockType& sl,
    std::optional<InboundLedger::Reason> reason)
{
    JLOG(m_journal.debug())
        << "onLedgerBuilt " << mHash << (reason ? " for a new reason" : "");

    std::vector<InboundLedger::Reason> reasons(
        reasons_.begin(), reasons_.end());
    bool firstTime = true;
    if (reason)  // small chance
    {
        reasons.clear();
        reasons.push_back(*reason);
        firstTime = false;
    }
    app_.getJobQueue().addJob(
        jtREPLAY_TASK,
        "onLedgerBuilt",
        [=, ledger = this->fullLedger_, &app = this->app_](Job&) {
            for (auto reason : reasons)
            {
                switch (reason)
                {
                    case InboundLedger::Reason::GENERIC:
                        app.getLedgerMaster().storeLedger(ledger);
                        break;
                    default:
                        // TODO for other use cases
                        break;
                }
            }

            if (firstTime)
                app.getLedgerMaster().tryAdvance();
        });
}

void
LedgerDeltaAcquire::notifyTasks(ScopedLockType& sl)
{
    assert(isDone());
    bool failed = mFailed;
    std::vector<std::weak_ptr<LedgerReplayTask>> tasks(
        tasks_.begin(), tasks_.end());
    sl.unlock();
    for (auto& t : tasks)
    {
        if (auto sptr = t.lock(); sptr)
        {
            if (failed)
                sptr->cancel();
            else
                sptr->deltaReady(mHash);  // mHash is const
        }
    }
    sl.lock();
}

void
LedgerDeltaAcquire::notifyTask(
    ScopedLockType& sl,
    std::shared_ptr<LedgerReplayTask>& task)
{
    assert(isDone());
    bool failed = mFailed;
    sl.unlock();
    if (failed)
        task->cancel();
    else
        task->deltaReady(mHash);  // mHash is const
    sl.lock();
}

}  // namespace ripple
