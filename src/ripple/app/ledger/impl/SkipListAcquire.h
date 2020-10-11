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

#ifndef RIPPLE_APP_LEDGER_SKIPLISTACQUIRE_H_INCLUDED
#define RIPPLE_APP_LEDGER_SKIPLISTACQUIRE_H_INCLUDED

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/impl/TimeoutCounter.h>
#include <ripple/app/main/Application.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/shamap/SHAMap.h>
#include <queue>

namespace ripple {

class LedgerReplayTask;
namespace test {
class LedgerForwardReplay_test;
}  // namespace test

class SkipListAcquire final
    : public TimeoutCounter,
      public std::enable_shared_from_this<SkipListAcquire>,
      public CountedObject<SkipListAcquire>
{
public:
    static char const*
    getCountedObjectName()
    {
        return "SkipListAcquire";
    }

    using pointer = std::shared_ptr<SkipListAcquire>;

    SkipListAcquire(
        Application& app,
        LedgerReplayer& replayer,
        uint256 const& ledgerHash,
        std::uint32_t ledgerSeq,
        std::unique_ptr<PeerSet>&& peerSet);

    ~SkipListAcquire() override;

    void
    init(int numPeers);

    void
    processData(
        std::uint32_t ledgerSeq,
        std::shared_ptr<SHAMapItem const> const& item);

    bool
    addTask(std::shared_ptr<LedgerReplayTask>& task);

    //    void
    //    removeTask(std::shared_ptr<LedgerReplayTask>const& task);

    hash_set<std::shared_ptr<LedgerReplayTask>>
    getAllTasks();

private:
    void
    queueJob() override;

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;

    void
    addPeers(std::size_t limit);

    LedgerReplayer& replayer_;
    std::uint32_t ledgerSeq_;
    std::unique_ptr<PeerSet> peerSet_;
    std::vector<ripple::uint256> skipList_;
    hash_set<std::shared_ptr<LedgerReplayTask>> tasks_;

    friend class test::LedgerForwardReplay_test;
};

}  // namespace ripple

#endif
