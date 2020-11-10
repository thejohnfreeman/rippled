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

#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/LedgerDeltaAcquire.h>
#include <ripple/app/ledger/impl/SkipListAcquire.h>
#include <ripple/core/JobQueue.h>

namespace ripple {

LedgerReplayer::LedgerReplayer(
    Application& app,
    InboundLedgers& inboundLedgers,
    std::unique_ptr<PeerSetBuilder> peerSetBuilder,
    Stoppable& parent)
    : Stoppable("LedgerReplayer", parent)
    , app_(app)
    , inboundLedgers_(inboundLedgers)
    , peerSetBuilder_(std::move(peerSetBuilder))
    , j_(app.journal("LedgerReplayer"))
{
}

LedgerReplayer::~LedgerReplayer()
{
    std::unique_lock<std::recursive_mutex> lock(lock_);
    tasks_.clear();
}

void
LedgerReplayer::replay(
    InboundLedger::Reason r,
    uint256 const& finishLedgerHash,
    std::uint32_t totalNumLedgers)
{
    assert(
        finishLedgerHash.isNonZero() && totalNumLedgers > 0 &&
        totalNumLedgers <= LedgerReplayParameters::MAX_TASK_SIZE);

    LedgerReplayTask::TaskParameter parameter(
        r, finishLedgerHash, totalNumLedgers);

    std::shared_ptr<LedgerReplayTask> task = {};
    std::shared_ptr<SkipListAcquire> skipList = {};
    bool skipListNeedInit = false;
    {
        std::unique_lock<std::recursive_mutex> lock(lock_);
        if (tasks_.size() >= LedgerReplayParameters::MAX_TASKS)
        {
            JLOG(j_.info()) << "Too many replay tasks, dropping new task "
                            << parameter.finishHash;
            return;
        }

        for (auto const& t : tasks_)
        {
            if (parameter.canMergeInto(t->getTaskParameter()))
            {
                JLOG(j_.info()) << "Task " << parameter.finishHash << " with "
                                << totalNumLedgers
                                << " ledgers merged into an existing task.";
                return;
            }
        }
        JLOG(j_.info()) << "Replay " << totalNumLedgers
                        << " ledgers. Finish ledger hash "
                        << parameter.finishHash;

        auto i = skipLists_.find(parameter.finishHash);
        if (i != skipLists_.end())
        {
            skipList = i->second.lock();
            if (!skipList)
            {
                skipLists_.erase(i);
            }
        }

        if (!skipList)
        {
            skipList = std::make_shared<SkipListAcquire>(
                app_,
                inboundLedgers_,
                *this,
                parameter.finishHash,
                peerSetBuilder_->build());
            skipLists_.emplace(parameter.finishHash, skipList);
            skipListNeedInit = true;
        }

        task = std::make_shared<LedgerReplayTask>(
            app_, inboundLedgers_, *this, skipList, std::move(parameter));
        tasks_.push_back(task);
    }

    bool taskNeedInit = skipList->addTask(task);
    if (skipListNeedInit)
        skipList->init(1);
    // task init after skipList init, could save a timeout
    if (taskNeedInit)
        task->init();
}

void
LedgerReplayer::createDeltas(std::shared_ptr<LedgerReplayTask> task)
{
    {
        // TODO for use cases like Consensus (i.e. totalLedgers = 1 or small):
        // check if the last closed or validated ledger l the local node has
        // is in the skip list and is an ancestor of parameter.startLedger
        // that has to be downloaded, if so expand the task to start with l.
    }

    auto const& parameter = task->getTaskParameter();
    JLOG(j_.trace()) << "Creating " << parameter.totalLedgers - 1 << " deltas";
    if (parameter.totalLedgers > 1)
    {
        auto skipListItem = std::find(
            parameter.skipList.begin(),
            parameter.skipList.end(),
            parameter.startHash);
        if (skipListItem == parameter.skipList.end() ||
            ++skipListItem == parameter.skipList.end())
        {
            JLOG(j_.error()) << "Task parameter error when creating deltas "
                             << parameter.finishHash;
            return;
        }

        for (std::uint32_t seq = parameter.startSeq + 1;
             seq <= parameter.finishSeq &&
             skipListItem != parameter.skipList.end();
             ++seq, ++skipListItem)
        {
            bool newDelta = false;
            std::shared_ptr<LedgerDeltaAcquire> delta = {};
            {
                std::unique_lock<std::recursive_mutex> lock(lock_);
                auto i = deltas_.find(*skipListItem);
                if (i != deltas_.end())
                {
                    delta = i->second.lock();
                    if (!delta)
                    {
                        deltas_.erase(i);
                    }
                }

                if (!delta)
                {
                    delta = std::make_shared<LedgerDeltaAcquire>(
                        app_,
                        inboundLedgers_,
                        *this,
                        *skipListItem,
                        seq,
                        peerSetBuilder_->build());
                    deltas_.emplace(*skipListItem, delta);
                    newDelta = true;
                }
            }

            task->addDelta(delta);
            delta->addTask(task);
            if (newDelta)
                delta->init(1);
        }
    }
}

void
LedgerReplayer::gotSkipList(
    LedgerInfo const& info,
    std::shared_ptr<SHAMapItem const> const& item)
{
    std::shared_ptr<SkipListAcquire> skipList = {};
    {
        std::unique_lock<std::recursive_mutex> lock(lock_);
        auto i = skipLists_.find(info.hash);
        if (i == skipLists_.end())
            return;
        skipList = i->second.lock();
        if (!skipList)
        {
            skipLists_.erase(i);
            return;
        }
    }

    if (skipList)
        skipList->processData(info.seq, item);
}

void
LedgerReplayer::gotReplayDelta(
    LedgerInfo const& info,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& txns)
{
    std::shared_ptr<LedgerDeltaAcquire> delta = {};
    {
        std::unique_lock<std::recursive_mutex> lock(lock_);
        auto i = deltas_.find(info.hash);
        if (i == deltas_.end())
            return;
        delta = i->second.lock();
        if (!delta)
        {
            deltas_.erase(i);
            return;
        }
    }

    if (delta)
        delta->processData(info, std::move(txns));
}

void
LedgerReplayer::sweep()
{
    std::unique_lock<std::recursive_mutex> lock(lock_);
    JLOG(j_.debug()) << "Sweeping, LedgerReplayer has " << tasks_.size()
                     << " tasks, " << skipLists_.size() << " skipLists, and "
                     << deltas_.size() << " deltas.";
    for (auto it = tasks_.begin(); it != tasks_.end();)
    {
        if ((*it)->finished())
        {
            JLOG(j_.info())
                << "Sweep task " << (*it)->getTaskParameter().finishHash;
            it = tasks_.erase(it);
        }
        else
            ++it;
    }
}

void
LedgerReplayer::onStop()
{
    {
        std::unique_lock<std::recursive_mutex> lock(lock_);
        for (auto& t : tasks_)
        {
            t->cancel();
        }
        tasks_.clear();
    }

    stopped();
    JLOG(j_.info()) << "Stopped";
}

}  // namespace ripple
