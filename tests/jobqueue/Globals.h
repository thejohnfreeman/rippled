//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef TESTS_JOBQUEUE_GLOBALS_H
#define TESTS_JOBQUEUE_GLOBALS_H

#include <xrpl/basics/Log.h>  // Logs
#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/jobqueue/JobQueue.h>
#include <xrpl/jobqueue/NullPerfLog.h>

#include <memory>

namespace ripple {

/*
 * Members are references so that getters can be non-virtual. We could hold
 * them directly, but then we'd lose the flexibility to use subtypes, and the
 * constructor would be cluttered with all of their collective parameters.
 *
 * The class itself has a virtual destructor so that the total size required
 * to hold the member implementations can be determined elsewhere, by the
 * class implementation.
 *
 * Callers are expected to construct an object via `make(...)`, with
 * a `Config` parameter that supports aggregate initialization to mimic named
 * parameters, and default construction for ease.
 */
class Globals
{
private:
    std::shared_ptr<beast::insight::Collector> collector_ =
        beast::insight::NullCollector::New();
    beast::Journal journal_{};
    Logs logs_{beast::severities::kFatal};
    perf::NullPerfLog perflog_{};
    JobQueue jobQueue_;

public:
    struct Config
    {
        int threadCount = 4;
        static Config
        make()
        {
            return {};
        }
    };

    Globals(Config const& config);

public:
    static std::unique_ptr<Globals>
    make(Config const& config = Config::make());

    JobQueue&
    jobQueue()
    {
        return jobQueue_;
    }
};

}  // namespace ripple

#endif
