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
    if (isDone())
        return;

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
LedgerReplayTask::trigger()
{
    if (mComplete)
    {
        JLOG(m_journal.info()) << "trigger after complete";
        return;
    }
    if (mFailed)
    {
        JLOG(m_journal.info()) << "trigger after fail";
        return;
    }

    if (ledgers_.empty())  // nextSeqToBuild_ == parameter_.startLedgerSeq)
    {
        auto l = app_.getInboundLedgers().acquire(
            parameter_.startLedgerHash,
            parameter_.startLedgerSeq,
            InboundLedger::Reason::GENERIC);
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
LedgerReplayTask::done()
{
    skipListAcquirer_ = nullptr;
    ledgers_.clear();
    deltas_.clear();
    if (mFailed)
    {
        JLOG(m_journal.warn()) << "Failed";
    }
    if (mComplete)
    {
        JLOG(m_journal.info()) << "Completed";
    }
}

void
LedgerReplayTask::tryAdvance(std::shared_ptr<Ledger const> const& ledger)
{
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
LedgerReplayTask::subTaskFailed(SubTaskType type)
{
    mFailed = true;
    //    JLOG(m_journal.warn()) << "Failed due to " << type;
    done();
}

}  // namespace ripple

// TODO useful for build txMap to verify Txns in Delta responses
// SHAMapAddNode
// LedgerReplayTask::takeNodes(
//    const std::list<SHAMapNodeID>& nodeIDs,
//    const std::list<Blob>& data,
//    std::shared_ptr<Peer> const& peer)
//{
//    ScopedLockType sl(mLock);
//
//    if (mComplete)
//    {
//        JLOG(m_journal.trace()) << "TX set complete";
//        return SHAMapAddNode();
//    }
//
//    if (mFailed)
//    {
//        JLOG(m_journal.trace()) << "TX set failed";
//        return SHAMapAddNode();
//    }
//
//    try
//    {
//        if (nodeIDs.empty())
//            return SHAMapAddNode::invalid();
//
//        std::list<SHAMapNodeID>::const_iterator nodeIDit = nodeIDs.begin();
//        std::list<Blob>::const_iterator nodeDatait = data.begin();
//        ConsensusTransSetSF sf(app_, app_.getTempNodeCache());
//
//        while (nodeIDit != nodeIDs.end())
//        {
//            if (nodeIDit->isRoot())
//            {
//                if (mHaveRoot)
//                    JLOG(m_journal.debug())
//                        << "Got root TXS node, already have it";
//                else if (!mMap->addRootNode(
//                                  SHAMapHash{mHash},
//                                  makeSlice(*nodeDatait),
//                                  nullptr)
//                              .isGood())
//                {
//                    JLOG(m_journal.warn()) << "TX acquire got bad root node";
//                }
//                else
//                    mHaveRoot = true;
//            }
//            else if (!mMap->addKnownNode(*nodeIDit, makeSlice(*nodeDatait),
//            &sf)
//                          .isGood())
//            {
//                JLOG(m_journal.warn()) << "TX acquire got bad non-root node";
//                return SHAMapAddNode::invalid();
//            }
//
//            ++nodeIDit;
//            ++nodeDatait;
//        }
//
//        trigger(peer);
//        mProgress = true;
//        return SHAMapAddNode::useful();
//    }
//    catch (std::exception const&)
//    {
//        JLOG(m_journal.error()) << "Peer sends us junky transaction node
//        data"; return SHAMapAddNode::invalid();
//    }
//}
