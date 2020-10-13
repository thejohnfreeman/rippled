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

#ifndef RIPPLE_APP_LEDGER_TIMEOUTCOUNTER_H_INCLUDED
#define RIPPLE_APP_LEDGER_TIMEOUTCOUNTER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/overlay/Peer.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <mutex>
#include <set>

namespace ripple {

/**
    This class is an "active" object. It maintains its own timer
    and dispatches work to a job queue. Implementations derive
    from this class and override the abstract hook functions in
    the base.
*/
class TimeoutCounter
{
protected:
    using ScopedLockType = std::unique_lock<std::recursive_mutex>;

    TimeoutCounter(
        Application& app,
        uint256 const& hash,
        std::chrono::milliseconds interval,
        beast::Journal journal);

    virtual ~TimeoutCounter() = 0;

    /** Hook called from invokeOnTimer(). */
    virtual void
    onTimer(bool progress, ScopedLockType&) = 0;

    /** Queue a job to call invokeOnTimer(). */
    virtual void
    queueJob() = 0;

    /** Return a weak pointer to this. */
    virtual std::weak_ptr<TimeoutCounter>
    pmDowncast() = 0;

    bool
    isDone() const
    {
        return mComplete || mFailed;
    }

    /** Schedule a call to queueJob() after mTimerInterval. */
    void
    setTimer();

    // Used in this class for access to boost::asio::io_service and
    // ripple::Overlay. Used in subtypes for the kitchen sink.
    Application& app_;
    beast::Journal m_journal;
    std::recursive_mutex mLock;

    /** The hash of the object (in practice, always a ledger) we are trying to
     * fetch. */
    uint256 const mHash;
    int mTimeouts;
    bool mComplete;
    bool mFailed;
    /** Whether forward progress has been made. */
    bool mProgress;

    /** Calls onTimer() if in the right state. */
    void
    invokeOnTimer();

private:
    /** The minimum time to wait between calls to execute(). */
    std::chrono::milliseconds mTimerInterval;
    boost::asio::basic_waitable_timer<std::chrono::steady_clock> mTimer;
};

}  // namespace ripple

#endif
