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

#ifndef RIPPLE_APP_LEDGER_LEDGERFORWARDREPLAYER_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERFORWARDREPLAYER_H_INCLUDED

#include <ripple/app/ledger/LedgerReplayTask.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/core/Stoppable.h>

#include <memory>
#include <mutex>

namespace ripple {

/**
 * Manages the lifetime of ledger replay tasks.
 */
class LedgerReplayer : public Stoppable
{
public:
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

    LedgerReplayer(
        Application& app,
        std::unique_ptr<PeerSetBuilder> peerSetBuilder,
        Stoppable& parent);
    ~LedgerReplayer() = default;

    void
    replay(
        InboundLedger::Reason r,
        uint256 const& finishLedgerHash,
        std::uint32_t totalNumLedgers);

    void
    createDeltas(std::shared_ptr<LedgerReplayTask> task);

    void
    gotSkipList(
        LedgerInfo const& info,
        std::shared_ptr<SHAMapItem const> const& data);

    void
    gotReplayDelta(
        LedgerInfo const& info,
        std::map<std::uint32_t, std::shared_ptr<STTx const>>&& txns);

    void
    removeLedgerDeltaAcquire(uint256 const& hash)
    {
        std::lock_guard<std::mutex> lock(lock_);
        deltas_.erase(hash);
    }

    void
    removeSkipListAcquire(uint256 const& hash)
    {
        std::lock_guard<std::mutex> lock(lock_);
        skipLists_.erase(hash);
    }

    void
    onStop() override;

private:
    mutable std::mutex lock_;
    hash_map<uint256, std::weak_ptr<LedgerDeltaAcquire>> deltas_;
    hash_map<uint256, std::weak_ptr<SkipListAcquire>> skipLists_;
    Application& app_;
    std::unique_ptr<PeerSetBuilder> peerSetBuilder_;
    beast::Journal j_;

    friend class LedgerForwardReplay_test;
};

}  // namespace ripple

#endif
