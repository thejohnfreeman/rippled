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

#include <ripple/app/ledger/impl/TimeoutCounter.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/Overlay.h>

namespace ripple {

using namespace std::chrono_literals;

TimeoutCounter::TimeoutCounter(
    Application& app,
    uint256 const& hash,
    std::chrono::milliseconds interval,
    beast::Journal journal)
    : app_(app)
    , m_journal(journal)
    , mHash(hash)
    , mTimeouts(0)
    , mComplete(false)
    , mFailed(false)
    , mProgress(false)
    , mTimerInterval(interval)
    , mTimer(app_.getIOService())
{
    assert((mTimerInterval > 10ms) && (mTimerInterval < 30s));
}

TimeoutCounter::~TimeoutCounter() = default;

void
TimeoutCounter::setTimer()
{
    mTimer.expires_after(mTimerInterval);
    mTimer.async_wait(
        [wptr = pmDowncast()](boost::system::error_code const& ec) {
            if (ec == boost::asio::error::operation_aborted)
                return;

            if (auto ptr = wptr.lock())
                ptr->queueJob();
        });
}

void
TimeoutCounter::invokeOnTimer()
{
    ScopedLockType sl(mLock);

    if (isDone())
        return;

    if (!mProgress)
    {
        ++mTimeouts;
        JLOG(m_journal.debug()) << "Timeout(" << mTimeouts << ") "
                                << " acquiring " << mHash;
        onTimer(false, sl);
    }
    else
    {
        mProgress = false;
        onTimer(true, sl);
    }

    if (!isDone())
        setTimer();
}

}  // namespace ripple
