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
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/LedgerDeltaAcquire.h>
#include <ripple/app/ledger/impl/SkipListAcquire.h>
#include <ripple/core/JobQueue.h>

namespace ripple {

LedgerReplayer::LedgerReplayer(Application& app, clock_type& clock)
    : app_(app), clock_(clock), j_(app.journal("LedgerReplayer"))
{
    JLOG(j_.debug()) << "LedgerReplayer constructed";
}

void
LedgerReplayer::replay(LedgerReplayTask::TaskParameter&& parameter)
{
    if (!parameter.isValid())
    {
        JLOG(j_.warn()) << "Bad LedgerReplayTask parameter";
        return;
    }

    JLOG(j_.info()) << "Replay " << parameter.finishLedgerHash;
    bool needInit = false;
    std::shared_ptr<SkipListAcquire> skipList;
    {
        std::lock_guard<std::mutex> lock(lock_);
        auto i = skipLists_.find(parameter.finishLedgerHash);
        if (i != skipLists_.end())
        {
            skipList = i->second.lock();
            if (!skipList)
            {
                skipLists_.erase(i);
                assert(false);
            }
        }

        if (!skipList)
        {
            skipList = std::make_shared<SkipListAcquire>(
                app_, parameter.finishLedgerHash, parameter.finishLedgerSeq);
            skipLists_.emplace(parameter.finishLedgerHash, skipList);
            // TODO if parameter passed in is full already, don't init
            needInit = true;
            JLOG(j_.trace())
                << "Add SkipListAcquire " << parameter.finishLedgerHash;
        }
    }

    if (needInit)
        skipList->init(1);

    auto task = std::make_shared<LedgerReplayTask>(
        app_, skipList, std::move(parameter));
    if (skipList->addTask(task))
        task->init();
}

void
LedgerReplayer::createDeltas(std::shared_ptr<LedgerReplayTask> task)
{
    {
        // TODO check if the last closed or validated ledger l the local node
        // has
        // is in the skip list and is an ancestor of parameter.startLedger that
        // has to be downloaded, if so expand the task to start with l.
    }
    auto const& parameter = task->getTaskTaskParameter();
    if (parameter.ledgersToBuild > 1)
    {
        auto skipListItem = std::find(
            parameter.skipList.begin(),
            parameter.skipList.end(),
            parameter.startLedgerHash);
        if (skipListItem == parameter.skipList.end())
        {
            assert(false);
            return;
        }
        if (++skipListItem == parameter.skipList.end())
        {
            assert(false);
            return;
        }
        for (std::uint32_t seq = parameter.startLedgerSeq + 1;
             seq <= parameter.finishLedgerSeq &&
             skipListItem < parameter.skipList.end();
             ++seq, ++skipListItem)
        {
            bool newDelta = false;
            std::shared_ptr<LedgerDeltaAcquire> delta;
            {
                std::lock_guard<std::mutex> lock(lock_);
                auto i = deltas_.find(*skipListItem);
                if (i != deltas_.end())
                {
                    delta = i->second.lock();
                    if (!delta)
                    {
                        deltas_.erase(i);
                        assert(false);
                    }
                }

                if (!delta)
                {
                    delta = std::make_shared<LedgerDeltaAcquire>(
                        app_, *skipListItem, seq);
                    deltas_.emplace(*skipListItem, delta);
                    newDelta = true;
                }
            }

            if (newDelta)  // TODO rate limit ?? by only init a subset
                delta->init(1);

            task->pushBackDeltaAcquire(delta);
            delta->addTask(task);
        }
    }
}

void
LedgerReplayer::gotSkipList(
    LedgerInfo const& info,
    std::shared_ptr<SHAMapItem const> const& item)
{
    std::shared_ptr<SkipListAcquire> skipList;
    {
        std::lock_guard<std::mutex> lock(lock_);
        auto i = skipLists_.find(info.hash);
        if (i == skipLists_.end())
            return;
        skipList = i->second.lock();
        if (!skipList)
        {
            skipLists_.erase(i);
            assert(false);
            return;
        }
    }
    skipList->processData(info.seq, item);
}

void
LedgerReplayer::gotReplayDelta(
    LedgerInfo const& info,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& txns)
{
    std::shared_ptr<LedgerDeltaAcquire> delta;
    {
        std::lock_guard<std::mutex> lock(lock_);
        auto i = deltas_.find(info.hash);
        if (i == deltas_.end())
            return;
        delta = i->second.lock();
        if (!delta)
        {
            deltas_.erase(i);
            assert(false);
            return;
        }
    }
    delta->processData(info, std::move(txns));
}

}  // namespace ripple
