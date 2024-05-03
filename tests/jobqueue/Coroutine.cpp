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

#include <tests/jobqueue/Globals.h>
#include <xrpl/jobqueue/JobQueue.h>

#include <doctest/doctest.h>

#include <condition_variable>
#include <mutex>

using namespace std::chrono_literals;
using namespace ripple;

class gate
{
private:
    std::condition_variable cv_;
    std::mutex mutex_;
    bool signaled_ = false;

public:
    // Thread safe, blocks until signaled or period expires.
    // Returns `true` if signaled.
    template <class Rep, class Period>
    bool
    wait_for(std::chrono::duration<Rep, Period> const& rel_time)
    {
        std::unique_lock<std::mutex> lk(mutex_);
        auto b = cv_.wait_for(lk, rel_time, [this] { return signaled_; });
        signaled_ = false;
        return b;
    }

    void
    signal()
    {
        std::lock_guard lk(mutex_);
        signaled_ = true;
        cv_.notify_all();
    }
};

TEST_SUITE_BEGIN("Coroutine");

TEST_CASE("correct order")
{
    auto globals = Globals::make();
    auto& jq = globals->jobQueue();
    gate g1, g2;
    std::shared_ptr<JobQueue::Coro> c;
    jq.postCoro(jtCLIENT, "Coroutine-Test", [&](auto const& cr) {
        c = cr;
        g1.signal();
        c->yield();
        g2.signal();
    });
    CHECK(g1.wait_for(5s));
    c->join();
    c->post();
    CHECK(g2.wait_for(5s));
}

TEST_CASE("incorrect order")
{
    auto globals = Globals::make();
    auto& jq = globals->jobQueue();
    gate g;
    jq.postCoro(jtCLIENT, "Coroutine-Test", [&](auto const& c) {
        c->post();
        c->yield();
        g.signal();
    });
    CHECK(g.wait_for(5s));
}

TEST_CASE("thread-local storage")
{
    // Test fails with multiple threads.
    auto globals = Globals::make({.threadCount = 1});
    auto& jq = globals->jobQueue();

    static int const N = 4;
    std::array<std::shared_ptr<JobQueue::Coro>, N> a;

    LocalValue<int> lv(-1);
    CHECK(*lv == -1);

    gate g;
    jq.addJob(jtCLIENT, "LocalValue-Test", [&]() {
        CHECK(*lv == -1);
        *lv = -2;
        CHECK(*lv == -2);
        g.signal();
    });
    CHECK(g.wait_for(5s));
    CHECK(*lv == -1);

    for (int i = 0; i < N; ++i)
    {
        jq.postCoro(jtCLIENT, "Coroutine-Test", [&, id = i](auto const& c) {
            a[id] = c;
            g.signal();
            c->yield();

            CHECK(*lv == -1);
            *lv = id;
            CHECK(*lv == id);
            g.signal();
            c->yield();

            CHECK(*lv == id);
        });
        CHECK(g.wait_for(5s));
        a[i]->join();
    }
    for (auto const& c : a)
    {
        c->post();
        CHECK(g.wait_for(5s));
        c->join();
    }
    for (auto const& c : a)
    {
        c->post();
        c->join();
    }

    jq.addJob(jtCLIENT, "LocalValue-Test", [&]() {
        CHECK(*lv == -2);
        g.signal();
    });
    CHECK(g.wait_for(5s));
    CHECK(*lv == -1);
}

TEST_SUITE_END();
