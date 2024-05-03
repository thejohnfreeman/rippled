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

#ifndef TESTS_JOBQUEUE_NULLPERFLOG_H
#define TESTS_JOBQUEUE_NULLPERFLOG_H

#include <xrpl/jobqueue/Job.h>  // JobType
#include <xrpl/jobqueue/PerfLog.h>
#include <xrpl/json/json_value.h>

#include <cstdint>
#include <string>

namespace ripple {
namespace perf {

struct NullPerfLog : public perf::PerfLog
{
    void
    rpcStart(std::string const& method, std::uint64_t requestId) override
    {
    }
    void
    rpcFinish(std::string const& method, std::uint64_t requestId) override
    {
    }
    void
    rpcError(std::string const& method, std::uint64_t requestId) override
    {
    }
    void
    jobQueue(JobType const type) override
    {
    }
    void
    jobStart(
        JobType const type,
        microseconds dur,
        steady_time_point startTime,
        int instance) override
    {
    }
    void
    jobFinish(JobType const type, microseconds dur, int instance) override
    {
    }
    Json::Value
    countersJson() const override
    {
        return {};
    }
    Json::Value
    currentJson() const override
    {
        return {};
    }
    void
    resizeJobs(int const resize) override
    {
    }
    void
    rotate() override
    {
    }
};

}  // namespace perf
}  // namespace ripple

#endif
