//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <xrpl/basics/ClosureCounter.h>

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ripple;

// A class used to test argument passing.
class TrackedString
{
public:
    int copies = {0};
    int moves = {0};
    std::string str;

    TrackedString() = delete;

    explicit TrackedString(char const* rhs) : str(rhs)
    {
    }

    // Copy constructor
    TrackedString(TrackedString const& rhs)
        : copies(rhs.copies + 1), moves(rhs.moves), str(rhs.str)
    {
    }

    // Move constructor
    TrackedString(TrackedString&& rhs) noexcept
        : copies(rhs.copies), moves(rhs.moves + 1), str(std::move(rhs.str))
    {
    }

    // Delete copy and move assignment.
    TrackedString&
    operator=(TrackedString const& rhs) = delete;

    // String concatenation
    TrackedString&
    operator+=(char const* rhs)
    {
        str += rhs;
        return *this;
    }

    friend TrackedString
    operator+(TrackedString const& s, char const* rhs)
    {
        TrackedString ret{s};
        ret.str += rhs;
        return ret;
    }
};

TEST_CASE("construct: () => void")
{
    // Count closures that return void and take no arguments.
    ClosureCounter<void> voidCounter;
    CHECK(voidCounter.count() == 0);

    int evidence = 0;
    // Make sure voidCounter.wrap works with an rvalue closure.
    auto wrapped = voidCounter.wrap([&evidence]() { ++evidence; });
    CHECK(voidCounter.count() == 1);
    CHECK(evidence == 0);
    CHECK(wrapped);

    // wrapped() should be callable with no arguments.
    (*wrapped)();
    CHECK(evidence == 1);
    (*wrapped)();
    CHECK(evidence == 2);

    // Destroying the contents of wrapped should decrement voidCounter.
    wrapped = std::nullopt;
    CHECK(voidCounter.count() == 0);
}

TEST_CASE("construct: (int) => void")
{
    // Count closures that return void and take one int argument.
    ClosureCounter<void, int> setCounter;
    CHECK(setCounter.count() == 0);

    int evidence = 0;
    // Make sure setCounter.wrap works with a non-const lvalue closure.
    auto setInt = [&evidence](int i) { evidence = i; };
    auto wrapped = setCounter.wrap(setInt);

    CHECK(setCounter.count() == 1);
    CHECK(evidence == 0);
    CHECK(wrapped);

    // wrapped() should be callable with one integer argument.
    (*wrapped)(5);
    CHECK(evidence == 5);
    (*wrapped)(11);
    CHECK(evidence == 11);

    // Destroying the contents of wrapped should decrement setCounter.
    wrapped = std::nullopt;
    CHECK(setCounter.count() == 0);
}

TEST_CASE("construct: (int, int) => int")
{
    // Count closures that return int and take two int arguments.
    ClosureCounter<int, int, int> sumCounter;
    CHECK(sumCounter.count() == 0);

    // Make sure sumCounter.wrap works with a const lvalue closure.
    auto const sum = [](int ii, int jj) { return ii + jj; };
    auto wrapped = sumCounter.wrap(sum);

    CHECK(sumCounter.count() == 1);
    CHECK(wrapped);

    // wrapped() should be callable with two integers.
    CHECK((*wrapped)(5, 2) == 7);
    CHECK((*wrapped)(2, -8) == -6);

    // Destroying the contents of wrapped should decrement sumCounter.
    wrapped = std::nullopt;
    CHECK(sumCounter.count() == 0);
}

TEST_CASE("wrapper: pass by value")
{
    // Pass by value.
    ClosureCounter<TrackedString, TrackedString> strCounter;
    CHECK(strCounter.count() == 0);

    auto wrapped = strCounter.wrap([](TrackedString in) { return in += "!"; });

    CHECK(strCounter.count() == 1);
    CHECK(wrapped);

    TrackedString const strValue("value");
    TrackedString const result = (*wrapped)(strValue);
    CHECK(result.copies == 2);
    CHECK(result.moves == 1);
    CHECK(result.str == "value!");
    CHECK(strValue.str.size() == 5);
}

TEST_CASE("wrapper: pass const lvalue")
{
    // Use a const lvalue argument.
    ClosureCounter<TrackedString, TrackedString const&> strCounter;
    CHECK(strCounter.count() == 0);

    auto wrapped =
        strCounter.wrap([](TrackedString const& in) { return in + "!"; });

    CHECK(strCounter.count() == 1);
    CHECK(wrapped);

    TrackedString const strConstLValue("const lvalue");
    TrackedString const result = (*wrapped)(strConstLValue);
    CHECK(result.copies == 1);
    // CHECK (result.moves == ?); // moves VS == 1, gcc == 0
    CHECK(result.str == "const lvalue!");
    CHECK(strConstLValue.str.size() == 12);
}

TEST_CASE("wrapper: pass non-const lvalue")
{
    // Use a non-const lvalue argument.
    ClosureCounter<TrackedString, TrackedString&> strCounter;
    CHECK(strCounter.count() == 0);

    auto wrapped = strCounter.wrap([](TrackedString& in) { return in += "!"; });

    CHECK(strCounter.count() == 1);
    CHECK(wrapped);

    TrackedString strLValue("lvalue");
    TrackedString const result = (*wrapped)(strLValue);
    CHECK(result.copies == 1);
    CHECK(result.moves == 0);
    CHECK(result.str == "lvalue!");
    CHECK(strLValue.str == result.str);
}

TEST_CASE("wrapper: pass rvalue")
{
    // Use an rvalue argument.
    ClosureCounter<TrackedString, TrackedString&&> strCounter;
    CHECK(strCounter.count() == 0);

    auto wrapped = strCounter.wrap([](TrackedString&& in) {
        // Note that none of the compilers noticed that in was
        // leaving scope.  So, without intervention, they would
        // do a copy for the return (June 2017).  An explicit
        // std::move() was required.
        return std::move(in += "!");
    });

    CHECK(strCounter.count() == 1);
    CHECK(wrapped);

    // Make the string big enough to (probably) avoid the small string
    // optimization.
    TrackedString strRValue("rvalue abcdefghijklmnopqrstuvwxyz");
    TrackedString const result = (*wrapped)(std::move(strRValue));
    CHECK(result.copies == 0);
    CHECK(result.moves == 1);
    CHECK(result.str == "rvalue abcdefghijklmnopqrstuvwxyz!");
    CHECK(strRValue.str.size() == 0);
}

TEST_CASE("reference counting")
{
    // Verify reference counting.
    ClosureCounter<void> voidCounter;
    CHECK(voidCounter.count() == 0);
    {
        auto wrapped1 = voidCounter.wrap([]() {});
        CHECK(voidCounter.count() == 1);
        {
            // Copy should increase reference count.
            auto wrapped2(wrapped1);
            CHECK(voidCounter.count() == 2);
            {
                // Move should increase reference count.
                auto wrapped3(std::move(wrapped2));
                CHECK(voidCounter.count() == 3);
                {
                    // An additional closure also increases count.
                    auto wrapped4 = voidCounter.wrap([]() {});
                    CHECK(voidCounter.count() == 4);
                }
                CHECK(voidCounter.count() == 3);
            }
            CHECK(voidCounter.count() == 2);
        }
        CHECK(voidCounter.count() == 1);
    }
    CHECK(voidCounter.count() == 0);

    // Join with 0 count should not stall.
    using namespace std::chrono_literals;
    beast::Journal j;
    voidCounter.join("testWrap", 1ms, j);

    // Wrapping a closure after join() should return std::nullopt.
    CHECK(voidCounter.wrap([]() {}) == std::nullopt);
}

TEST_CASE("wait on join")
{
    // Verify reference counting.
    ClosureCounter<void> voidCounter;
    CHECK(voidCounter.count() == 0);

    auto wrapped = (voidCounter.wrap([]() {}));
    CHECK(voidCounter.count() == 1);

    // Calling join() now should stall, so do it on a different thread.
    std::atomic<bool> threadExited{false};
    std::thread localThread([&voidCounter, &threadExited]() {
        // Should stall after calling join.
        using namespace std::chrono_literals;
        beast::Journal j;
        voidCounter.join("testWaitOnJoin", 1ms, j);
        threadExited.store(true);
    });

    // Wait for the thread to call voidCounter.join().
    while (!voidCounter.joined())
        ;

    // The thread should still be active after waiting 5 milliseconds.
    // This is not a guarantee that join() stalled the thread, but it
    // improves confidence.
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(5ms);
    CHECK(threadExited == false);

    // Destroy the contents of wrapped and expect the thread to exit
    // (asynchronously).
    wrapped = std::nullopt;
    CHECK(voidCounter.count() == 0);

    // Wait for the thread to exit.
    while (threadExited == false)
        ;
    localThread.join();
}
