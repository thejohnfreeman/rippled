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
}

void
LedgerReplayer::replay(LedgerReplayTask::TaskParameter&& parameter)
{
    if (!parameter.isValid())
    {
        JLOG(j_.warn()) << "LedgerReplayer: bad parameter";
        return;
    }

    bool needInit = false;
    std::shared_ptr<SkipListAcquire> skipList;
    {
        std::lock_guard<std::recursive_mutex> lock(lock_);
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
        for (std::uint32_t seq = parameter.startLedgerSeq + 1;
             seq <= parameter.finishLedgerSeq &&
             skipListItem < parameter.skipList.end();
             ++seq, ++skipListItem)
        {
            bool newDelta = false;
            std::shared_ptr<LedgerDeltaAcquire> delta;
            {
                std::lock_guard<std::recursive_mutex> lock(lock_);
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

            delta->addTask(task);
        }
    }
}

void
LedgerReplayer::gotProofPath(
    std::shared_ptr<protocol::TMProofPathResponse> response)
{
    protocol::TMProofPathResponse const& reply = *response;
    if (!reply.has_ledgerheader() || reply.has_error() ||
        reply.path_size() == 0)
        return;
    // deserialize the header
    auto info = deserializeHeader(
        {reply.ledgerheader().data(), reply.ledgerheader().size()});
    if (calculateLedgerHash(info) != info.hash)
        return;

    // verify the skip list
    std::vector<Blob> path;
    path.reserve(reply.path_size());
    for (int i = 0; i < reply.path_size(); ++i)
    {
        path.emplace_back(reply.path(i).begin(), reply.path(i).end());
    }
    if (!SHAMap::verifyProofPath(info.accountHash, keylet::skip().key, path))
        return;

    std::shared_ptr<SkipListAcquire> skipList;
    {
        std::lock_guard<std::recursive_mutex> lock(lock_);
        auto i = skipLists_.find(info.hash);
        if (i == skipLists_.end())
            return;
        auto skipList = i->second.lock();
        if (!skipList)
        {
            skipLists_.erase(i);
            assert(false);
            return;
        }
    }
    skipList->processData(path.front());
}

void
LedgerReplayer::gotReplayDelta(
    std::shared_ptr<protocol::TMReplayDeltaResponse> response)
{
    auto const& reply = *response;

    if (!reply.has_ledgerheader() || reply.has_error())
        return;

    // verify the header
    auto info = deserializeHeader(
        {reply.ledgerheader().data(), reply.ledgerheader().size()});
    if (calculateLedgerHash(info) != info.hash)
        return;

    // TODO verify the transactions hash
    auto numTxns = reply.transaction_size();
    std::map<std::uint32_t, std::shared_ptr<STTx const>> orderedTxns;
    try
    {
        for (int i = 0; i < numTxns; ++i)
        {
            SerialIter sit(makeSlice(reply.transaction(i)));
            auto tx = std::make_shared<STTx const>(sit);
            if (!tx)
                return;
            orderedTxns.emplace(i, std::move(tx));
        }
    }
    catch (std::exception const&)
    {
        JLOG(j_.error()) << "Peer sends us junky ledger delta data";
        return;
    }

    std::shared_ptr<LedgerDeltaAcquire> delta;
    {
        std::lock_guard<std::recursive_mutex> lock(lock_);
        auto i = deltas_.find(info.hash);
        if (i == deltas_.end())
            return;
        auto delta = i->second.lock();
        if (!delta)
        {
            deltas_.erase(i);
            assert(false);
            return;
        }
    }
    delta->processData(info, std::move(orderedTxns));
}

}  // namespace ripple

//    bool
//    gotLedgerData(
//        LedgerHash const& hash,
//        std::shared_ptr<Peer> peer,
//        std::shared_ptr<protocol::TMLedgerData> packet_ptr)
//    {
//        protocol::TMLedgerData& packet = *packet_ptr;
//
//        JLOG(j_.trace()) << "Got data (" << packet.nodes().size()
//                         << ") for acquiring ledger: " << hash;
//
//        auto ledger = find(hash);
//
//        if (!ledger)
//        {
//            JLOG(j_.trace()) << "Got data for ledger we're no longer
//            acquiring";
//
//            // If it's state node data, stash it because it still might be
//            // useful.
//            if (packet.type() == protocol::liAS_NODE)
//            {
//                app_.getJobQueue().addJob(
//                    jtLEDGER_DATA, "gotStaleData", [this, packet_ptr](Job&) {
//                        gotStaleData(packet_ptr);
//                    });
//            }
//
//            return false;
//        }
//
//        // Stash the data for later processing and see if we need to dispatch
//        if (ledger->gotData(std::weak_ptr<Peer>(peer), packet_ptr))
//            app_.getJobQueue().addJob(
//                jtLEDGER_DATA, "processLedgerData", [this, hash](Job&) {
//                    doLedgerData(hash);
//                });
//
//        return true;
//    }
