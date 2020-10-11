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
#include <ripple/app/ledger/impl/TimeoutCounter.h>
#include <ripple/app/main/Application.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/shamap/SHAMap.h>
#include <queue>

namespace ripple {

using namespace std::chrono_literals;
// Timeout interval in milliseconds
// TODO
auto constexpr LEDGER_REPLAY_TIMEOUT = 100ms;

enum {
    LEDGER_REPLAY_MAX_TIMEOUTS = 10,
};

class LedgerDeltaAcquire;
class SkipListAcquire;
namespace test {
class LedgerForwardReplay_test;
}  // namespace test

class LedgerReplayTask final
    : public TimeoutCounter,
      public std::enable_shared_from_this<LedgerReplayTask>,
      public CountedObject<LedgerReplayTask>
{
public:
    struct TaskParameter
    {
        // TODO refactor after coding usa cases

        InboundLedger::Reason reason;
        uint256 finishHash;
        uint256 startHash = {};
        std::uint32_t finishSeq = 0;
        std::uint32_t startSeq = 0;
        std::uint32_t totalLedgers = 0;  // including the start and the finish
        std::vector<uint256> skipList = {};

        TaskParameter(
            InboundLedger::Reason r,
            uint256 const& finishLedgerHash,
            std::uint32_t totalNumLedgers)
            : reason(r)
            , finishHash(finishLedgerHash)
            , totalLedgers(totalNumLedgers)
        {
            assert(isValid());
        }

        bool
        isValid() const
        {
            if (finishHash.isZero())
                return false;
            if (totalLedgers > 256)
                return false;

            if (startSeq != 0 && finishSeq != 0)
            {
                auto size = finishSeq - startSeq + 1;
                if (size > 256)
                    return false;
                if (totalLedgers != 0 && totalLedgers != size)
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

            if (finishHash != hash)
                return false;
            if (finishSeq != 0 && finishSeq != seq)
                return false;
            finishSeq = seq;

            skipList = sList;
            skipList.emplace_back(finishHash);
            if (startHash.isZero() && !startSeq && !totalLedgers)
            {
                startHash = finishHash;
                startSeq = seq;
                totalLedgers = 1;
            }
            else
            {
                if (startHash.isNonZero())
                {
                    auto i =
                        std::find(skipList.begin(), skipList.end(), startHash);
                    std::uint32_t size = skipList.end() - i;
                    if (size == 0)
                        return false;
                    if (totalLedgers == 0)
                        totalLedgers = size;
                    if (startSeq == 0)
                        startSeq = finishSeq - size + 1;
                }
                else
                {
                    if (totalLedgers != 0 && startSeq == 0)
                        startSeq = finishSeq - totalLedgers + 1;
                    if (totalLedgers == 0 && startSeq != 0)
                        totalLedgers = finishSeq - startSeq + 1;
                    if (totalLedgers > skipList.size())
                        return false;
                    if (auto i = std::find(
                            skipList.begin(), skipList.end(), startHash);
                        i != skipList.end() && *i != startHash)
                        return false;
                    else
                        startHash = skipList[skipList.size() - totalLedgers];
                }
            }

            if (!isValid())
                return false;
            return true;
        }

        bool
        canMergeInto(TaskParameter const& existingTask)
        {
            if (reason == existingTask.reason &&
                startSeq >= existingTask.startSeq &&
                finishSeq <= existingTask.finishSeq)
                return true;
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
        std::vector<ripple::uint256> const& data);

    void
    pushBackDeltaAcquire(std::shared_ptr<LedgerDeltaAcquire> delta);

    void
    tryAdvance(std::optional<std::shared_ptr<Ledger const> const> ledger);

    TaskParameter&
    getTaskTaskParameter()
    {
        return parameter_;
    }

    void
    cancel();

private:
    void
    queueJob() override;

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;

    void
    done();

    void
    trigger();

    LedgerReplayer& replayer_;
    TaskParameter parameter_;
    std::shared_ptr<SkipListAcquire> skipListAcquirer_;
    std::shared_ptr<Ledger const> parent = {};
    int deltaToBuild = -1;  // should not build until have parent
    std::vector<std::shared_ptr<LedgerDeltaAcquire>> deltas_;

    friend class test::LedgerForwardReplay_test;
};

}  // namespace ripple

#endif
