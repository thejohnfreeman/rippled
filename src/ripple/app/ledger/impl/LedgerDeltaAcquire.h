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

#ifndef RIPPLE_APP_LEDGER_LEDGERDELTAACQUIRE_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERDELTAACQUIRE_H_INCLUDED

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/main/Application.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/shamap/SHAMap.h>

namespace ripple {

class LedgerReplayTask;

// A ledger delta (header and transactions) we are trying to acquire
class LedgerDeltaAcquire final
    : public PeerSet,
      public std::enable_shared_from_this<LedgerDeltaAcquire>,
      public CountedObject<LedgerDeltaAcquire>
{
public:
    static char const*
    getCountedObjectName()
    {
        return "LedgerDeltaAcquire";
    }

    using pointer = std::shared_ptr<LedgerDeltaAcquire>;

    LedgerDeltaAcquire(
        Application& app,
        uint256 const& ledgerHash,
        std::uint32_t ledgerSeq);

    ~LedgerDeltaAcquire() override;

    void
    init(int numPeers);

    void
    processData(
        LedgerInfo const& info,
        std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns);

    std::shared_ptr<Ledger const>
    tryBuild(std::shared_ptr<Ledger const> const& parent);

    void
    addTask(std::shared_ptr<LedgerReplayTask>& task);

private:
    void
    queueJob() override;

    void
    onTimer(bool progress, ScopedLockType& peerSetLock) override;

    void
    onPeerAdded(std::shared_ptr<Peer> const& peer) override;

    std::weak_ptr<PeerSet>
    pmDowncast() override;

    void
    addPeers(std::size_t limit);

    void
    onLedgerBuilt(std::optional<InboundLedger::Reason> reason = {});

    std::uint32_t ledgerSeq_;
    std::shared_ptr<Ledger const> replay_;
    std::map<std::uint32_t, std::shared_ptr<STTx const>> orderedTxns_;
    // TODO call tryAdvance() for all in tasks_ ??
    // std::shared_ptr<Ledger const> parent_;
    hash_set<std::shared_ptr<LedgerReplayTask>> tasks_;
    std::set<InboundLedger::Reason> reasons_;
    bool ledgerBuilt_ = false;
};

}  // namespace ripple

#endif
