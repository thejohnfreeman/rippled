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

#ifndef RIPPLE_APP_LEDGER_SKIPLISTACQUIRE_H_INCLUDED
#define RIPPLE_APP_LEDGER_SKIPLISTACQUIRE_H_INCLUDED

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/impl/TimeoutCounter.h>
#include <ripple/app/main/Application.h>
#include <ripple/shamap/SHAMap.h>
#include <queue>

namespace ripple {
class InboundLedgers;
class LedgerReplayer;
class LedgerReplayTask;
class PeerSet;
namespace test {
class LedgerReplayClient;
}  // namespace test

/**
 * Manage the retrieval of a skip list in a ledger from the network.
 * Before asking peers, always check if the local node has the ledger.
 */
class SkipListAcquire final
    : public TimeoutCounter,
      public std::enable_shared_from_this<SkipListAcquire>,
      public CountedObject<SkipListAcquire>
{
public:
    /**
     * Constructor
     * @param app  Application reference
     * @param inboundLedgers  InboundLedgers reference
     * @param replayer  LedgerReplayer reference
     * @param ledgerHash  hash of the ledger that has the skip list
     * @param peerSet  manage a set of peers that we will ask for the skip list
     */
    SkipListAcquire(
        Application& app,
        InboundLedgers& inboundLedgers,
        LedgerReplayer& replayer,
        uint256 const& ledgerHash,
        std::unique_ptr<PeerSet> peerSet);

    ~SkipListAcquire() override;

    /**
     * Start the SkipListAcquire task
     * @param numPeers  number of peers to try initially
     */
    void
    init(int numPeers);

    /**
     * Process the data extracted from a peer's reply
     * @param ledgerSeq  sequence number of the ledger that has the skip list
     * @param item  holder of the skip list
     * @note ledgerSeq and item must have been verified against the ledger hash
     */
    void
    processData(
        std::uint32_t ledgerSeq,
        std::shared_ptr<SHAMapItem const> const& item);

    /**
     * Add a LedgerReplayTask to this SkipListAcquire subtask
     * @param task  the LedgerReplayTask
     * @return true if the task should init, false otherwise (i.e.
     *         the SkipListAcquire subtask has failed already)
     */
    bool
    addTask(std::shared_ptr<LedgerReplayTask>& task);

    static char const*
    getCountedObjectName()
    {
        return "SkipListAcquire";
    }

private:
    void
    queueJob() override;

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    std::weak_ptr<TimeoutCounter>
    pmDowncast() override;

    /**
     * Trigger another round
     * @param limit  number of new peers to send the request
     * @param sl  lock. this function must be called with the lock
     */
    void
    trigger(std::size_t limit, ScopedLockType& sl);

    /**
     * Retrieve the skip list from the ledger
     * @param ledger  the ledger that has the skip list
     * @param sl  lock. this function must be called with the lock
     */
    void
    retrieveSkipList(
        std::shared_ptr<Ledger const> const& ledger,
        ScopedLockType& sl);

    /**
     * Process the skip list
     * @param skipList  skip list
     * @param ledgerSeq  sequence number of the ledger that has the skip list
     * @param sl  lock. this function must be called with the lock
     */
    void
    onSkipListAcquired(
        std::vector<uint256> const& skipList,
        std::uint32_t ledgerSeq,
        ScopedLockType& sl);

    /**
     * Notify existing LedgerReplayTasks that this subtask is done
     * @param sl  lock. this function must be called with the lock
     */
    void
    notifyTasks(ScopedLockType& sl);

    InboundLedgers& inboundLedgers_;
    LedgerReplayer& replayer_;
    std::uint32_t ledgerSeq_ = 0;
    std::unique_ptr<PeerSet> peerSet_;
    std::vector<ripple::uint256> skipList_;
    std::list<std::weak_ptr<LedgerReplayTask>> tasks_;
    std::uint32_t noFeaturePeerCount = 0;
    bool fallBack_ = false;

    friend class test::LedgerReplayClient;
};

}  // namespace ripple

#endif
