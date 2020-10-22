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

#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerReplayTask.h>
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/LedgerDeltaAcquire.h>
#include <ripple/app/ledger/impl/SkipListAcquire.h>
#include <ripple/core/JobQueue.h>

namespace ripple {

LedgerReplayTask::TaskParameter::TaskParameter(
    InboundLedger::Reason r,
    uint256 const& finishLedgerHash,
    std::uint32_t totalNumLedgers)
    : reason(r), finishHash(finishLedgerHash), totalLedgers(totalNumLedgers)
{
}

bool
LedgerReplayTask::TaskParameter::update(
    uint256 const& hash,
    std::uint32_t seq,
    std::vector<uint256> const& sList)
{
    if (finishHash != hash || sList.size() + 1 < totalLedgers)
        return false;

    finishSeq = seq;
    skipList = sList;
    skipList.emplace_back(finishHash);
    startHash = skipList[skipList.size() - totalLedgers];
    assert(startHash.isNonZero());
    startSeq = finishSeq - totalLedgers + 1;
    full = true;
    return true;
}

bool
LedgerReplayTask::TaskParameter::canMergeInto(TaskParameter const& existingTask)
{
    if (full)
    {
        return reason == existingTask.reason &&
            startSeq >= existingTask.startSeq &&
            finishSeq <= existingTask.finishSeq;
    }
    else
    {
        return reason == existingTask.reason &&
            finishHash == existingTask.finishHash &&
            totalLedgers == existingTask.totalLedgers;
    }
}

LedgerReplayTask::LedgerReplayTask(
    Application& app,
    InboundLedgers& inboundLedgers,
    std::shared_ptr<SkipListAcquire>& skipListAcquirer,
    TaskParameter&& parameter)
    : TimeoutCounter(
          app,
          parameter.finishHash,
          LedgerReplayer::TASK_TIMEOUT,
          app.journal("LedgerReplayTask"))
    , inboundLedgers_(inboundLedgers)
    , parameter_(parameter)
    , skipListAcquirer_(skipListAcquirer)
{
    JLOG(m_journal.trace()) << "Task ctor " << mHash;
}

LedgerReplayTask::~LedgerReplayTask()
{
    JLOG(m_journal.trace()) << "Task dtor " << mHash;
}

void
LedgerReplayTask::init()
{
    JLOG(m_journal.debug()) << "Task start " << mHash;
    ScopedLockType sl(mLock);
    trigger(sl);
    if (!isDone())
        setTimer();
}

void
LedgerReplayTask::trigger(ScopedLockType& peerSetLock)
{
    JLOG(m_journal.trace()) << "trigger " << mHash;
    if (isDone() || !parameter_.full)
        return;

    if (!parent_)
    {
        parent_ = app_.getLedgerMaster().getLedgerByHash(parameter_.startHash);
        if (!parent_)
        {
            parent_ = inboundLedgers_.acquire(
                parameter_.startHash,
                parameter_.startSeq,
                InboundLedger::Reason::GENERIC);
        }
        if (parent_)
        {
            JLOG(m_journal.trace())
                << "Got start ledger " << parameter_.startHash << " for task "
                << mHash;
        }
    }

    tryAdvance(peerSetLock);
}

void
LedgerReplayTask::deltaReady()
{
    JLOG(m_journal.trace()) << "A delta ready for task " << mHash;
    ScopedLockType sl(mLock);
    tryAdvance(sl);
}

void
LedgerReplayTask::tryAdvance(ScopedLockType& peerSetLock)
{
    JLOG(m_journal.trace())
        << "tryAdvance task " << mHash << " deltaIndex=" << deltaToBuild
        << " totalDeltas=" << deltas_.size();

    bool shouldTry = !isDone() && parent_ && parameter_.full &&
        parameter_.totalLedgers - 1 == deltas_.size();
    if (!shouldTry)
        return;

    while (deltaToBuild < deltas_.size())
    {
        assert(parent_->seq() + 1 == deltas_[deltaToBuild]->ledgerSeq_);
        if (auto l = deltas_[deltaToBuild]->tryBuild(parent_); l)
        {
            JLOG(m_journal.debug())
                << "Task " << mHash << " got ledger " << l->info().hash
                << " deltaIndex=" << deltaToBuild
                << " totalDeltas=" << deltas_.size();
            parent_ = l;
            ++deltaToBuild;
        }
        else
            break;
    }

    if (deltaToBuild >= deltas_.size())
    {
        mComplete = true;
        JLOG(m_journal.info()) << "Completed " << mHash;
    }
}

void
LedgerReplayTask::updateSkipList(
    uint256 const& hash,
    std::uint32_t seq,
    std::vector<ripple::uint256> const& sList)
{
    {
        ScopedLockType sl(mLock);
        if (!parameter_.update(hash, seq, sList))
        {
            JLOG(m_journal.error()) << "Parameter update failed " << mHash;
            mFailed = true;
            return;
        }
    }

    app_.getLedgerReplayer().createDeltas(shared_from_this());
}

void
LedgerReplayTask::queueJob()
{
    if (app_.getJobQueue().getJobCountTotal(jtREPLAY_TASK) >
        LedgerReplayer::MAX_QUEUED_TASKS)
    {
        JLOG(m_journal.debug())
            << "Deferring LedgerReplayTask timer due to load";
        setTimer();
        return;
    }

    std::weak_ptr<LedgerReplayTask> wptr = shared_from_this();
    app_.getJobQueue().addJob(jtREPLAY_TASK, "LedgerReplayTask", [wptr](Job&) {
        if (auto sptr = wptr.lock(); sptr)
            sptr->invokeOnTimer();
    });
}

void
LedgerReplayTask::onTimer(bool progress, ScopedLockType& psl)
{
    JLOG(m_journal.trace()) << "mTimeouts=" << mTimeouts << " for " << mHash;
    if (mTimeouts >
        parameter_.totalLedgers * LedgerReplayer::TASK_MAX_TIMEOUTS_MULTIPLIER)
    {
        mFailed = true;
        JLOG(m_journal.debug())
            << "LedgerReplayTask Failed, too many timeouts " << mHash;
    }
    else
    {
        trigger(psl);
    }
}

std::weak_ptr<TimeoutCounter>
LedgerReplayTask::pmDowncast()
{
    return shared_from_this();
}

void
LedgerReplayTask::addDelta(std::shared_ptr<LedgerDeltaAcquire> const& delta)
{
    JLOG(m_journal.trace())
        << "addDelta task " << mHash << " deltaIndex=" << deltaToBuild
        << " totalDeltas=" << deltas_.size();
    ScopedLockType sl(mLock);
    assert(
        deltas_.empty() || deltas_.back()->ledgerSeq_ + 1 == delta->ledgerSeq_);
    deltas_.push_back(delta);
}

void
LedgerReplayTask::cancel()
{
    ScopedLockType sl(mLock);
    mFailed = true;
    JLOG(m_journal.info()) << "Cancel Task " << mHash;
}

bool
LedgerReplayTask::finished()
{
    ScopedLockType sl(mLock);
    return isDone();
}

}  // namespace ripple
