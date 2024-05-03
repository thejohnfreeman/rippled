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

#include <xrpl/jobqueue/NullPerfLog.h>
#include <xrpl/jobqueue/Workers.h>

#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

using namespace ripple;

struct TestCallback : Workers::Callback
{
    void
    processTask(int instance) override
    {
        std::lock_guard lk{mut};
        if (--count == 0)
            cv.notify_all();
    }

    std::condition_variable cv;
    std::mutex mut;
    int count = 0;
};

void
testThreads(int const tc1, int const tc2, int const tc3)
{
    CAPTURE(tc1);
    CAPTURE(tc2);
    CAPTURE(tc3);

    TestCallback cb;
    std::unique_ptr<perf::PerfLog> perfLog =
        std::make_unique<perf::NullPerfLog>();

    Workers w(cb, perfLog.get(), "Test", tc1);
    CHECK(w.getNumberOfThreads() == tc1);

    auto testForThreadCount = [&cb, &w](int const threadCount) {
        // Prepare the callback.
        cb.count = threadCount;

        // Execute the test.
        w.setNumberOfThreads(threadCount);
        CHECK(w.getNumberOfThreads() == threadCount);

        for (int i = 0; i < threadCount; ++i)
            w.addTask();

        // 10 seconds should be enough to finish on any system
        //
        using namespace std::chrono_literals;
        std::unique_lock<std::mutex> lk{cb.mut};
        bool const signaled =
            cb.cv.wait_for(lk, 10s, [&cb] { return cb.count == 0; });
        CHECK(signaled);
        CHECK(cb.count == 0);
    };
    testForThreadCount(tc1);
    testForThreadCount(tc2);
    testForThreadCount(tc3);
    w.stop();

    // We had better finished all our work!
    CHECK(cb.count == 0);
}

TEST_CASE("Workers")
{
    testThreads(0, 0, 0);
    testThreads(1, 0, 1);
    testThreads(2, 1, 2);
    testThreads(4, 3, 5);
    testThreads(16, 4, 15);
    testThreads(64, 3, 65);
}
