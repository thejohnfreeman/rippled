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

#ifndef RIPPLE_APP_LEDGER_LEDGERREPLAYTASK_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERREPLAYTASK_H_INCLUDED

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/impl/TimeoutCounter.h>
#include <ripple/app/main/Application.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/shamap/SHAMap.h>
#include <queue>

namespace ripple {

using namespace std::chrono_literals;

class LedgerDeltaAcquire;
class SkipListAcquire;
namespace test {
class LedgerReplayClient;
}  // namespace test

class LedgerReplayTask final
    : public TimeoutCounter,
      public std::enable_shared_from_this<LedgerReplayTask>,
      public CountedObject<LedgerReplayTask>
{
public:
    struct TaskParameter
    {
        // input
        InboundLedger::Reason reason;
        uint256 finishHash;
        std::uint32_t totalLedgers;  // including the start and the finish
        // to be filled
        std::uint32_t finishSeq = 0;
        std::vector<uint256> skipList = {};  // including the finishHash
        uint256 startHash = {};
        std::uint32_t startSeq = 0;
        bool full = false;

        TaskParameter(
            InboundLedger::Reason r,
            uint256 const& finishLedgerHash,
            std::uint32_t totalNumLedgers);

        /**
         * @note called with validated data
         * @param hash
         * @param seq
         * @param sList
         * @return
         */
        bool
        update(
            uint256 const& hash,
            std::uint32_t seq,
            std::vector<uint256> const& sList);

        bool
        canMergeInto(TaskParameter const& existingTask);
    };

    static char const*
    getCountedObjectName()
    {
        return "LedgerReplayTask";
    }

    LedgerReplayTask(
        Application& app,
        InboundLedgers& inboundLedgers,
        LedgerReplayer& replayer,
        std::shared_ptr<SkipListAcquire>& skipListAcquirer,
        TaskParameter&& parameter);

    ~LedgerReplayTask();

    void
    init();

    void
    updateSkipList(
        uint256 const& hash,
        std::uint32_t seq,
        std::vector<ripple::uint256> const& sList);

    void
    addDelta(std::shared_ptr<LedgerDeltaAcquire> const& delta);

    TaskParameter const&
    getTaskParameter() const
    {
        return parameter_;
    }

    void
    deltaReady(uint256 const& deltaHash);

    void
    cancel();

    bool
    finished();

private:
    void
    queueJob() override;

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;

    void
    trigger(ScopedLockType& peerSetLock);

    void
    tryAdvance(ScopedLockType& peerSetLock);

    InboundLedgers& inboundLedgers_;
    LedgerReplayer& replayer_;
    TaskParameter parameter_;
    std::shared_ptr<SkipListAcquire> skipListAcquirer_;
    std::shared_ptr<Ledger const> parent_ = {};
    uint32_t deltaToBuild = 0;  // should not build until have parent
    std::vector<std::shared_ptr<LedgerDeltaAcquire>> deltas_;

    friend class test::LedgerReplayClient;
};

}  // namespace ripple

#endif
