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

#ifndef RIPPLE_APP_LEDGER_LEDGERREPLAYTASK_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERREPLAYTASK_H_INCLUDED

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/main/Application.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/shamap/SHAMap.h>
#include <queue>

namespace ripple {

using namespace std::chrono_literals;
// Timeout interval in milliseconds
auto constexpr LEDGER_REPLAY_TIMEOUT = 250ms;

enum {
    LEDGER_REPLAY_NORM_TIMEOUTS = 4,
    LEDGER_REPLAY_MAX_TIMEOUTS = 20,
};

class LedgerDeltaAcquire;
class SkipListAcquire;

class LedgerReplayTask final
    : public PeerSet,  // TODO using PeerSet's timer, change??
      public std::enable_shared_from_this<LedgerReplayTask>,
      public CountedObject<LedgerReplayTask>
{
public:
    struct TaskParameter
    {
        InboundLedger::Reason reason;
        uint256 finishLedgerHash;
        uint256 startLedgerHash = {};
        std::uint32_t finishLedgerSeq = 0;
        std::uint32_t startLedgerSeq = 0;
        std::uint32_t ledgersToBuild = 0;  // including the start and the finish
        std::vector<uint256> skipList = {};

        bool
        isValid() const  // TODO name
        {
            if (finishLedgerHash.isZero())
                return false;
            if (ledgersToBuild > 256)
                return false;

            if (startLedgerSeq != 0 && finishLedgerSeq != 0)
            {
                auto size = finishLedgerSeq - startLedgerSeq + 1;
                if (size > 256)
                    return false;
                if (ledgersToBuild != 0 && ledgersToBuild != size)
                    return false;
            }
            return true;
        }

        bool
        update(
            uint256 const& hash,
            std::uint32_t seq,
            std::vector<uint256> const& sList)
        {
            if (!isValid())
                return false;

            if (finishLedgerHash != hash)
                return false;
            if (finishLedgerSeq != 0 && finishLedgerSeq != seq)
                return false;
            finishLedgerSeq = seq;

            skipList = sList;
            skipList.emplace_back(finishLedgerHash);
            if (startLedgerHash.isZero() && !startLedgerSeq && !ledgersToBuild)
            {
                ledgersToBuild = 1;
                startLedgerSeq = finishLedgerSeq;
                startLedgerHash = finishLedgerHash;
            }
            else
            {
                if (startLedgerHash.isNonZero())
                {
                    auto i = std::find(
                        skipList.begin(), skipList.end(), startLedgerHash);
                    std::uint32_t size = skipList.end() - i;
                    if (size == 0)
                        return false;
                    if (ledgersToBuild == 0)
                        ledgersToBuild = size;
                    if (startLedgerSeq == 0)
                        startLedgerSeq = finishLedgerSeq - size + 1;
                }
                else
                {
                    if (ledgersToBuild != 0 && startLedgerSeq == 0)
                        startLedgerSeq = finishLedgerSeq - ledgersToBuild + 1;
                    if (ledgersToBuild == 0 && startLedgerSeq != 0)
                        ledgersToBuild = finishLedgerSeq - startLedgerSeq + 1;
                    if (ledgersToBuild > skipList.size())
                        return false;
                    if (auto i = std::find(
                            skipList.begin(), skipList.end(), startLedgerHash);
                        i != skipList.end() && *i != startLedgerHash)
                        return false;
                    else
                        startLedgerHash =
                            skipList[skipList.size() - ledgersToBuild];
                }
            }

            if (!isValid())
                return false;
            return true;
        }

        bool
        canMergeInto(TaskParameter const& existingTask)
        {
            // TODO
            return false;
        }
    };

    static char const*
    getCountedObjectName()
    {
        return "LedgerReplayTask";
    }

    using pointer = std::shared_ptr<LedgerReplayTask>;

    LedgerReplayTask(
        Application& app,
        std::shared_ptr<SkipListAcquire>& skipListAcquirer,
        TaskParameter&& parameter);

    ~LedgerReplayTask() = default;

    void
    init();

    void
    updateSkipList(
        uint256 const& hash,
        std::uint32_t seq,
        std::vector<ripple::uint256> const& data);

    void
    tryAdvance(std::optional<std::shared_ptr<Ledger const> const> ledger);

    TaskParameter&
    getTaskTaskParameter()
    {
        return parameter_;
    }

    void
    subTaskFailed(uint256 const& hash);

private:
    void
    queueJob() override;

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    void
    onPeerAdded(std::shared_ptr<Peer> const& peer) override
    {
    }

    std::weak_ptr<PeerSet>
    pmDowncast() override;

    void
    done();

    void
    trigger();

    TaskParameter parameter_;
    std::shared_ptr<SkipListAcquire> skipListAcquirer_;
    // when it's done, ledgers_.size() == deltas_.size() + 1
    // TODO keeping too many ledgers??
    std::vector<std::shared_ptr<Ledger const>> ledgers_;
    std::vector<std::shared_ptr<LedgerDeltaAcquire>> deltas_;
};

}  // namespace ripple

#endif

//    enum class SubTaskType {
//        START_LEDGER,  // Acquiring the first ledger of the task
//        SKIP_LIST,     // Acquiring the skip list
//        LEDGER_DELTA,  // Acquiring a ledger delta
//    };
//
