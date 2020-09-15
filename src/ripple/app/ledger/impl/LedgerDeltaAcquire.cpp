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

#include <ripple/app/ledger/BuildLedger.h>
#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerReplay.h>
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/LedgerDeltaAcquire.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/overlay/Overlay.h>

#include <memory>

namespace ripple {

using namespace std::chrono_literals;

// Timeout interval in milliseconds
auto constexpr ACQUIRE_TIMEOUT = 250ms;

enum {
    NORM_TIMEOUTS = 4,
    MAX_TIMEOUTS = 20,
};

LedgerDeltaAcquire::LedgerDeltaAcquire(
    Application& app,
    uint256 const& ledgerHash,
    std::uint32_t ledgerSeq)
    : PeerSet(
          app,
          ledgerHash,
          ACQUIRE_TIMEOUT,
          app.journal("LedgerDeltaAcquire"))
    , ledgerSeq_(ledgerSeq)
{
}

LedgerDeltaAcquire::~LedgerDeltaAcquire()
{
    app_.getLedgerReplayer().removeLedgerDeltaAcquire(mHash);
}

void
LedgerDeltaAcquire::init(int numPeers)
{
    ScopedLockType sl(mLock);
    addPeers(numPeers);
    setTimer();
}

void
LedgerDeltaAcquire::queueJob()
{
    app_.getJobQueue().addJob(
        jtREPLAY_DELTA_REQUEST,
        "LedgerDeltaAcquire",
        [ptr = shared_from_this()](Job&) { ptr->invokeOnTimer(); });
}

void
LedgerDeltaAcquire::onTimer(bool progress, ScopedLockType& psl)
{
    if (mTimeouts > MAX_TIMEOUTS)
    {
        mFailed = true;
        for (auto& t : tasks_)
            t->subTaskFailed(LedgerReplayTask::SubTaskType::LEDGER_DELTA);
        return;
    }

    addPeers(1);
}

std::weak_ptr<PeerSet>
LedgerDeltaAcquire::pmDowncast()
{
    return shared_from_this();
}

void
LedgerDeltaAcquire::trigger(std::shared_ptr<Peer> const& peer)
{
    if (mComplete)
    {
        // JLOG(m_journal.info()) << "trigger after complete";
        return;
    }
    if (mFailed)
    {
        // JLOG(m_journal.info()) << "trigger after fail";
        return;
    }

    if (!replay_)
    {
        JLOG(m_journal.trace())
            << "LedgerDeltaAcquire::trigger " << (peer ? "havePeer" : "noPeer")
            << " hash " << mHash;
        protocol::TMReplayDeltaRequest request;
        request.set_ledgerhash(mHash.data(), mHash.size());
        auto packet =
            std::make_shared<Message>(request, protocol::mtReplayDeltaRequest);
        sendRequest(packet, peer);
    }
}

void
LedgerDeltaAcquire::processData(
    LedgerInfo const& info,
    std::map<std::uint32_t, std::shared_ptr<STTx const>>&& orderedTxns)
{
    // create LedgerReplay object
    auto rp =
        std::make_shared<Ledger>(info, app_.config(), app_.getNodeFamily());
    if (rp)
        return;

    ScopedLockType sl(mLock);
    if (mComplete)
        return;
    replay_ = rp;
    mComplete = true;
    JLOG(m_journal.debug()) << "LedgerDeltaAcquire received " << mHash;
}

std::shared_ptr<Ledger const>
LedgerDeltaAcquire::tryBuild(std::shared_ptr<Ledger const> const& parent)
{
    ScopedLockType sl(mLock);

    if (ledgerBuilt_)
    {
        return replay_;
    }

    if (mFailed)
    {
        return {};
    }

    if (!mComplete || !replay_)
    {
        return {};
    }

    // build ledger TODO jobQueue ??
    assert(parent->info().hash == replay_->info().parentHash);
    LedgerReplay replayData(parent, replay_, std::move(orderedTxns_));
    auto const l = buildLedger(replayData, tapNONE, app_, m_journal);
    if (!l)
    {
        JLOG(m_journal.error()) << "tryBuild failed";
        orderedTxns_ = replayData.orderedTxns();
        return {};
    }
    assert(mHash == l->info().hash);

    replay_ = l;
    ledgerBuilt_ = true;
    onLedgerBuilt({});
    return l;
}

void
LedgerDeltaAcquire::addTask(std::shared_ptr<LedgerReplayTask>& task)
{
    ScopedLockType sl(mLock);
    auto r = tasks_.emplace(task);
    assert(r.second);
    auto reason = task->getTaskTaskParameter().reason;
    if (reasons_.count(reason) == 0)
    {
        reasons_.emplace(reason);
        if (ledgerBuilt_)
            onLedgerBuilt(reason);
    }
    if (mFailed)
        task->subTaskFailed(LedgerReplayTask::SubTaskType::LEDGER_DELTA);
}

void
LedgerDeltaAcquire::onLedgerBuilt(std::optional<InboundLedger::Reason> reason)
{
    if (reason)
    {
        // TODO just for this reason
    }
    else
    {
        // TODO for all reasons in reasons_
    }
}

void
LedgerDeltaAcquire::addPeers(std::size_t limit)
{
    PeerSet::addPeers(limit, [this](auto peer) {
        return peer->hasLedger(mHash, ledgerSeq_);
    });
}
}  // namespace ripple
