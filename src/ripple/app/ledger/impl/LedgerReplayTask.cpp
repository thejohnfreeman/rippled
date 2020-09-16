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

using namespace std::chrono_literals;
// Timeout interval in milliseconds
auto constexpr ACQUIRE_TIMEOUT = 250ms;

enum {
    NORM_TIMEOUTS = 4,
    MAX_TIMEOUTS = 20,
};

LedgerReplayTask::LedgerReplayTask(
    Application& app,
    std::shared_ptr<SkipListAcquire>& skipListAcquirer,
    TaskParameter&& parameter)
    : PeerSet(
          app,
          parameter.finishLedgerHash,
          ACQUIRE_TIMEOUT,
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
}

void
LedgerReplayTask::trigger()
{
    if (isDone())
        return;

    if (ledgers_.empty())
    {
        auto l =
            app_.getLedgerMaster().getLedgerByHash(parameter_.startLedgerHash);
        if (!l)
        {
            l = app_.getInboundLedgers().acquire(
                parameter_.startLedgerHash,
                parameter_.startLedgerSeq,
                InboundLedger::Reason::GENERIC);
        }
        if (l)
        {
            tryAdvance(l);
        }
        else
        {
            setTimer();
        }
    }
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
    if (mTimeouts > MAX_TIMEOUTS)
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
            mFailed = true;
    }

    app_.getLedgerReplayer().createDeltas(shared_from_this());
}

void
LedgerReplayTask::done()
{
    skipListAcquirer_ = nullptr;
    ledgers_.clear();
    deltas_.clear();
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
LedgerReplayTask::tryAdvance(std::shared_ptr<Ledger const> const& ledger)
{
    JLOG(m_journal.trace()) << "LedgerReplayTask " << mHash << " tryAdvance "
                            << ledger->info().hash;

    // TODO for call from LedgerDeltaAcquire to be coded
    ScopedLockType sl(mLock);
    if (ledgers_.empty())
    {
        if (ledger->info().hash == parameter_.startLedgerHash)
            ledgers_.emplace_back(ledger);
        else
            return;
    }
    else
    {
        if (ledger->info().parentHash == ledgers_.back()->info().hash)
            ledgers_.emplace_back(ledger);
        else
            return;
    }

    while (ledgers_.size() <= deltas_.size())
    {
        auto l = deltas_[ledgers_.size() - 1]->tryBuild(ledgers_.back());
        if (l)
            ledgers_.emplace_back(l);
        else
            break;
    }

    if (ledgers_.size() == deltas_.size() + 1)
    {
        mComplete = true;
        done();
    }
}

void
LedgerReplayTask::subTaskFailed(uint256 const& hash)
{
    JLOG(m_journal.warn()) << "sub task failed " << hash;
    ScopedLockType sl(mLock);
    mFailed = true;
    done();
}

}  // namespace ripple
