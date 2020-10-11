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

#include <ripple/app/ledger/InboundLedger.h>
#include <ripple/app/ledger/LedgerReplayTask.h>
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/app/ledger/impl/LedgerDeltaAcquire.h>
#include <ripple/app/ledger/impl/SkipListAcquire.h>
#include <ripple/core/JobQueue.h>

namespace ripple {

SkipListAcquire::SkipListAcquire(
    Application& app,
    LedgerReplayer& replayer,
    uint256 const& ledgerHash,
    std::uint32_t ledgerSeq,
    std::unique_ptr<PeerSet>&& peerSet)
    : TimeoutCounter(
          app,
          ledgerHash,
          LEDGER_REPLAY_TIMEOUT,
          app.journal("SkipListAcquire"))
    , replayer_(replayer)
    , ledgerSeq_(ledgerSeq)
    , peerSet_(std::move(peerSet))
{
    JLOG(m_journal.debug())
        << "Acquire skip list " << mHash << " Seq " << ledgerSeq;
}

SkipListAcquire::~SkipListAcquire()
{
    replayer_.removeSkipListAcquire(mHash);
    JLOG(m_journal.trace()) << "SkipList dtor, remove myself " << mHash;
}

void
SkipListAcquire::init(int numPeers)
{
    if (auto const l = app_.getLedgerMaster().getLedgerByHash(mHash); l)
    {
        auto const hashIndex = l->read(keylet::skip());
        if (hashIndex && hashIndex->isFieldPresent(sfHashes))
        {
            auto const& slist = hashIndex->getFieldV256(sfHashes).value();
            if (!slist.empty())
            {
                ScopedLockType sl(mLock);
                mComplete = true;
                skipList_ = slist;
                ledgerSeq_ = l->seq();
                for (auto& t : tasks_)
                {
                    t->updateSkipList(mHash, ledgerSeq_, skipList_);
                }
                JLOG(m_journal.trace())
                    << "Acquire skip list from existing ledger " << mHash;
            }
        }
    }

    ScopedLockType sl(mLock);
    if (!mComplete)
    {
        addPeers(numPeers);
        setTimer();
    }
}

void
SkipListAcquire::addPeers(std::size_t limit)
{
    peerSet_->addPeers(
        limit,
        [this](auto peer) { return peer->hasLedger(mHash, ledgerSeq_); },
        [this](auto peer) {
            if (isDone() || !skipList_.empty() || !peer)  // TODO need??
                return;

            JLOG(m_journal.trace())
                << "Add a peer " << peer->id() << " for " << mHash;
            protocol::TMProofPathRequest request;
            request.set_ledgerhash(mHash.data(), mHash.size());
            request.set_key(
                keylet::skip().key.data(), keylet::skip().key.size());
            request.set_type(protocol::TMLedgerMapType::lmAS_NODE);
            peerSet_->sendRequest(request, protocol::mtProofPathRequest, peer);
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
    JLOG(m_journal.trace()) << "mTimeouts=" << mTimeouts << " for " << mHash;
    if (mTimeouts > LEDGER_REPLAY_MAX_TIMEOUTS)
    {
        mFailed = true;
        for (auto& t : tasks_)
            t->cancel();
        return;
    }

    addPeers(1);
}

std::weak_ptr<TimeoutCounter>
SkipListAcquire::pmDowncast()
{
    return shared_from_this();
}

void
SkipListAcquire::processData(
    std::uint32_t ledgerSeq,
    std::shared_ptr<SHAMapItem const> const& item)
{
    JLOG(m_journal.trace()) << "got data for " << mHash;

    if (auto sle = std::make_shared<SLE>(
            SerialIter{item->data(), item->size()}, item->key());
        sle)
    {
        ScopedLockType sl(mLock);
        if (mComplete)
            return;
        mComplete = true;
        skipList_ = sle->getFieldV256(sfHashes).value();
        ledgerSeq_ = ledgerSeq;
        JLOG(m_journal.debug()) << "Skip list received " << mHash;
        for (auto& t : tasks_)
        {
            t->updateSkipList(mHash, ledgerSeq_, skipList_);
        }
    }
}

bool
SkipListAcquire::addTask(std::shared_ptr<LedgerReplayTask>& task)
{
    ScopedLockType sl(mLock);
    if (mFailed)
    {
        task->cancel();
        return false;
    }
    for (auto const& t : tasks_)
    {
        if (task->getTaskTaskParameter()
                .canMergeInto(  // TODO consider no merge
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

// void
// SkipListAcquire::removeTask(std::shared_ptr<LedgerReplayTask> const& task)
//{
//    JLOG(m_journal.debug()) << "remove task " << mHash;
//    ScopedLockType sl(mLock);
//    tasks_.erase(task);
//}

hash_set<std::shared_ptr<LedgerReplayTask>>
SkipListAcquire::getAllTasks()
{
    ScopedLockType sl(mLock);
    return tasks_;
}

}  // namespace ripple
