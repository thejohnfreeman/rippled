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
    InboundLedgers& inboundLedgers,
    LedgerReplayer& replayer,
    uint256 const& ledgerHash,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          ledgerHash,
          LedgerReplayer::SUB_TASK_TIMEOUT,
          app.journal("LedgerReplaySkipList"))
    , inboundLedgers_(inboundLedgers)
    , replayer_(replayer)
    , peerSet_(std::move(peerSet))
{
    JLOG(m_journal.debug()) << "SkipList ctor " << mHash;
}

SkipListAcquire::~SkipListAcquire()
{
    JLOG(m_journal.trace()) << "SkipList dtor " << mHash;
    replayer_.removeSkipListAcquire(mHash);
}

void
SkipListAcquire::init(int numPeers)
{
    ScopedLockType sl(mLock);
    if (auto const l = app_.getLedgerMaster().getLedgerByHash(mHash); l)
    {
        retrieveSkipList(l, sl);
        return;
    }

    trigger(numPeers, sl);
    setTimer();
}

void
SkipListAcquire::trigger(std::size_t limit, ScopedLockType& psl)
{
    if (isDone())
        return;

    auto l = app_.getLedgerMaster().getLedgerByHash(mHash);
    if (!l && fallBack_)
    {
        l = inboundLedgers_.acquire(
            mHash, ledgerSeq_, InboundLedger::Reason::GENERIC);
    }
    if (l)
    {
        retrieveSkipList(l, psl);
        return;
    }

    if (fallBack_)
        return;

    peerSet_->addPeers(
        limit,
        [this](auto peer) {
            return peer->supportsFeature(ProtocolFeature::LedgerReplay) &&
                peer->hasLedger(mHash, ledgerSeq_);
        },
        [this](auto peer) {
            if (peer->supportsFeature(ProtocolFeature::LedgerReplay))
            {
                JLOG(m_journal.trace())
                    << "Add a peer " << peer->id() << " for " << mHash;
                protocol::TMProofPathRequest request;
                request.set_ledgerhash(mHash.data(), mHash.size());
                request.set_key(
                    keylet::skip().key.data(), keylet::skip().key.size());
                request.set_type(protocol::TMLedgerMapType::lmAS_NODE);
                peerSet_->sendRequest(
                    request, protocol::mtPROOF_PATH_REQ, peer);
            }
            else
            {
                JLOG(m_journal.trace()) << "Fall back for " << mHash;
                fallBack_ = true;
            }
        });
}

void
SkipListAcquire::queueJob()
{
    if (app_.getJobQueue().getJobCountTotal(jtREPLAY_TASK) >
        LedgerReplayer::MAX_QUEUED_TASKS)
    {
        JLOG(m_journal.debug())
            << "Deferring SkipListAcquire timer due to load";
        setTimer();
        return;
    }

    std::weak_ptr<SkipListAcquire> wptr = shared_from_this();
    app_.getJobQueue().addJob(jtREPLAY_TASK, "SkipListAcquire", [wptr](Job&) {
        if (auto sptr = wptr.lock(); sptr)
            sptr->invokeOnTimer();
    });
}

void
SkipListAcquire::onTimer(bool progress, ScopedLockType& psl)
{
    JLOG(m_journal.trace()) << "mTimeouts=" << mTimeouts << " for " << mHash;
    if (mTimeouts > LedgerReplayer::SUB_TASK_MAX_TIMEOUTS)
    {
        mFailed = true;
        JLOG(m_journal.debug()) << "too many timeouts " << mHash;
        notifyTasks(psl);
    }
    else
    {
        trigger(1, psl);
    }
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
    assert(ledgerSeq != 0 && item);
    ScopedLockType sl(mLock);
    if (isDone())
        return;

    JLOG(m_journal.trace()) << "got data for " << mHash;
    try
    {
        if (auto sle = std::make_shared<SLE>(
                SerialIter{item->data(), item->size()}, item->key());
            sle)
        {
            if (auto const& skipList = sle->getFieldV256(sfHashes).value();
                !skipList.empty())
                onSkipListAcquired(skipList, ledgerSeq, sl);
            return;
        }
    }
    catch (...)
    {
    }

    mFailed = true;
    JLOG(m_journal.error())
        << "failed to retrieve Skip list from verified data " << mHash;
    notifyTasks(sl);
}

bool
SkipListAcquire::addTask(std::shared_ptr<LedgerReplayTask>& task)
{
    ScopedLockType sl(mLock);
    tasks_.emplace_back(task);
    if (mFailed)
    {
        JLOG(m_journal.debug())
            << "task added to a failed SkipListAcquire " << mHash;
        task->cancel();
        return false;
    }
    else
    {
        if (mComplete)
        {
            task->updateSkipList(mHash, ledgerSeq_, skipList_);
        }
        return true;
    }
}

void
SkipListAcquire::retrieveSkipList(
    std::shared_ptr<Ledger const> const& ledger,
    ScopedLockType& psl)
{
    if (auto const hashIndex = ledger->read(keylet::skip());
        hashIndex && hashIndex->isFieldPresent(sfHashes))
    {
        auto const& slist = hashIndex->getFieldV256(sfHashes).value();
        if (!slist.empty())
        {
            onSkipListAcquired(slist, ledger->seq(), psl);
            return;
        }
    }

    mFailed = true;
    JLOG(m_journal.error())
        << "failed to retrieve Skip list from a ledger " << mHash;
    notifyTasks(psl);
}

void
SkipListAcquire::onSkipListAcquired(
    std::vector<uint256> const& skipList,
    std::uint32_t ledgerSeq,
    ScopedLockType& psl)
{
    mComplete = true;
    skipList_ = skipList;
    ledgerSeq_ = ledgerSeq;
    JLOG(m_journal.debug()) << "Skip list acquired " << mHash;
    notifyTasks(psl);
}

void
SkipListAcquire::notifyTasks(ScopedLockType& psl)
{
    for (auto& t : tasks_)
    {
        if (auto sptr = t.lock(); sptr)
        {
            if (mFailed)
                sptr->cancel();
            else
                sptr->updateSkipList(mHash, ledgerSeq_, skipList_);
        }
    }
}

}  // namespace ripple
