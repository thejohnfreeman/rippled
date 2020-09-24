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

LedgerReplayTask::LedgerReplayTask(
    Application& app,
    std::shared_ptr<SkipListAcquire>& skipListAcquirer,
    TaskParameter&& parameter)
    : PeerSet(
          app,
          parameter.finishHash,
          LEDGER_REPLAY_TIMEOUT,
          app.journal("LedgerReplayTask"))
    , parameter_(parameter)
    , skipListAcquirer_(skipListAcquirer)
{
}

void
LedgerReplayTask::init()
{
    ScopedLockType sl(mLock);
    trigger();
    JLOG(m_journal.debug()) << "Task started " << mHash;
}

void
LedgerReplayTask::trigger()
{
    if (isDone())
        return;

    if (parameter_.startHash.isNonZero())
    {
        //if (ledgers_.empty())
        if(!parent)
        {
            auto l = app_.getLedgerMaster().getLedgerByHash(
                parameter_.startHash);
            if (!l)
            {
                l = app_.getInboundLedgers().acquire(
                    parameter_.startHash,
                    parameter_.startSeq,
                    InboundLedger::Reason::GENERIC);
            }
            if (l)
            {
                JLOG(m_journal.trace())
                    << "Got start ledger " << parameter_.startHash << " for " << mHash;
                tryAdvance(l);
            }
        }
        else
        {
            tryAdvance({});
        }
    }

    if (!isDone())
        setTimer();
}

void
LedgerReplayTask::queueJob()
{
    app_.getJobQueue().addJob(
        jtREPLAY_TASK, "LedgerReplayTask", [ptr = shared_from_this()](Job&) {
            ptr->invokeOnTimer();
        });
}

void
LedgerReplayTask::onTimer(bool progress, ScopedLockType& psl)
{
    JLOG(m_journal.trace()) << "mTimeouts=" << mTimeouts << " for " << mHash;
    if (mTimeouts > LEDGER_REPLAY_MAX_TIMEOUTS)
    {
        mFailed = true;
        done();
        return;
    }
    trigger();
}

std::weak_ptr<PeerSet>
LedgerReplayTask::pmDowncast()
{
    return shared_from_this();
}

void
LedgerReplayTask::updateSkipList(
    uint256 const& hash,
    std::uint32_t seq,
    std::vector<ripple::uint256> const& data)
{
    {
        ScopedLockType sl(mLock);
        if (!parameter_.update(hash, seq, data))
        {
            JLOG(m_journal.error()) << "Parameter update failed " << mHash;
            mFailed = true;
        }
    }

    app_.getLedgerReplayer().createDeltas(shared_from_this());
}

void
LedgerReplayTask::done()
{
    skipListAcquirer_ = nullptr;//TODO make sure no access after this point

    if (mFailed)
    {
        JLOG(m_journal.warn()) << "LedgerReplayTask Failed " << mHash;
    }
    if (mComplete)
    {
        JLOG(m_journal.info()) << "LedgerReplayTask Completed " << mHash;
    }
}

void
LedgerReplayTask::pushBackDeltaAcquire(
    std::shared_ptr<LedgerDeltaAcquire> delta)
{
    assert(
        deltas_.empty() || deltas_.back()->ledgerSeq_ + 1 == delta->ledgerSeq_);
    deltas_.emplace_back(std::move(delta));
}

void
LedgerReplayTask::tryAdvance(
    std::optional<std::shared_ptr<Ledger const> const> ledger)
{
    JLOG(m_journal.trace()) << "tryAdvance " << mHash;
    // TODO for call from LedgerDeltaAcquire, to be coded
    ScopedLockType sl(mLock);
    if (ledger)
    {
        if ((!parent && parameter_.startHash == (*ledger)->info().hash) ||
            (parent && parent->info().hash == (*ledger)->info().parentHash))
        {
            parent = *ledger;
            ++deltaToBuild;
        }
        else
            return;
    }

    if(deltaToBuild >= 0)
    {
        while (deltaToBuild < deltas_.size())
        {
            assert(parent->seq() + 1 == deltas_[deltaToBuild]->ledgerSeq_);
            if (auto l = deltas_[deltaToBuild]->tryBuild(parent); l)
            {
                parent = l;
                ++deltaToBuild;
            }
            else
                break;
        }
    }

    if (deltaToBuild >= deltas_.size())
    {
        mComplete = true;
        done();
    }
}

void
LedgerReplayTask::subTaskFailed(uint256 const& hash)
{
    JLOG(m_journal.warn()) << "Task " << mHash << " subtask failed " << hash;
    ScopedLockType sl(mLock);
    mFailed = true;
    done();
}

}  // namespace ripple
