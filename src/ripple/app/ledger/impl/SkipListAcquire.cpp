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
#include <ripple/app/ledger/LedgerReplayTask.h>
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/LedgerDeltaAcquire.h>
#include <ripple/app/ledger/impl/SkipListAcquire.h>
#include <ripple/core/JobQueue.h>

namespace ripple {

using namespace std::chrono_literals;
// Timeout interval in milliseconds
auto constexpr ACQUIRE_TIMEOUT = 250ms;

enum {
    NORM_TIMEOUTS = 4,
    MAX_TIMEOUTS = 20,
};

SkipListAcquire::SkipListAcquire(
    Application& app,
    uint256 const& ledgerHash,
    std::uint32_t ledgerSeq)
    : PeerSet(app, ledgerHash, ACQUIRE_TIMEOUT, app.journal("SkipListAcquire"))
    , ledgerSeq_(ledgerSeq)
{
}

SkipListAcquire::~SkipListAcquire()
{
    app_.getLedgerReplayer().removeSkipListAcquire(mHash);
}

void
SkipListAcquire::init(int numPeers)
{
    ScopedLockType sl(mLock);
    addPeers(numPeers);
    setTimer();
}

void
SkipListAcquire::addPeers(std::size_t limit)
{
    PeerSet::addPeers(limit, [this](auto peer) {
        return peer->hasLedger(mHash, ledgerSeq_);
    });
}

void
SkipListAcquire::queueJob()
{
    app_.getJobQueue().addJob(
        jtREPLAY_TASK, "SkipListAcquire", [ptr = shared_from_this()](Job&) {
            ptr->invokeOnTimer();
        });
}

void
SkipListAcquire::onTimer(bool progress, ScopedLockType& psl)
{
    if (isDone())
        return;

    if (mTimeouts > MAX_TIMEOUTS)
    {
        mFailed = true;
        return;
    }

    addPeers(1);
}

void
SkipListAcquire::onPeerAdded(std::shared_ptr<Peer> const& peer)
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

    JLOG(m_journal.trace())
        << "SkipListAcquire::trigger " << (peer ? "havePeer" : "noPeer")
        << " hash " << mHash;
    protocol::TMProofPathRequest request;
    request.set_ledgerhash(mHash.data(), mHash.size());
    request.set_key(keylet::skip().key.data(), keylet::skip().key.size());
    request.set_type(protocol::TMLedgerMapType::lmAS_NODE);
    auto packet =
        std::make_shared<Message>(request, protocol::mtProofPathRequest);
    sendRequest(packet, peer);
}

std::weak_ptr<PeerSet>
SkipListAcquire::pmDowncast()
{
    return shared_from_this();
}

void
SkipListAcquire::processData(Blob const& data)
{
    // deserialize the skip list
    // TODO shorten the deserialize code?? possible exceptions??
    auto node = SHAMapAbstractNode::makeFromWire(makeSlice(data));
    if (!node || !node->isLeaf())
        return;
    auto item = static_cast<SHAMapTreeNode*>(node.get())->peekItem();
    if (!item)
        return;
    auto sle = std::make_shared<SLE>(
        SerialIter{item->data(), item->size()}, item->key());
    if (!sle)
        return;

    ScopedLockType sl(mLock);
    if (mComplete)
        return;
    mComplete = true;
    skipList_ = sle->getFieldV256(sfHashes).value();
    for (auto& t : tasks_)
    {
        t->updateSkipList(mHash, ledgerSeq_, skipList_);
    }
    JLOG(m_journal.debug()) << "SkipListAcquire received " << mHash;
}

bool
SkipListAcquire::addTask(std::shared_ptr<LedgerReplayTask>& task)
{
    ScopedLockType sl(mLock);
    if (mFailed)
    {
        return false;
    }
    for (auto const& t : tasks_)
    {
        if (task->getTaskTaskParameter().canMergeInto(
                t->getTaskTaskParameter()))
            return false;
    }
    auto r = tasks_.emplace(task);
    assert(r.second);
    if (mComplete)
    {
        task->updateSkipList(mHash, ledgerSeq_, skipList_);
    }
    return true;
}

}  // namespace ripple
