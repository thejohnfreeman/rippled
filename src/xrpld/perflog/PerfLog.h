//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef XRPLD_PERFLOG_PERFLOG_H
#define XRPLD_PERFLOG_PERFLOG_H

#include <xrpld/core/Config.h>
#include <xrpl/jobqueue/JobTypes.h>
#include <xrpl/jobqueue/PerfLog.h>
#include <xrpl/json/json_value.h>

#include <boost/filesystem.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace beast {
class Journal;
}

namespace ripple {
class Application;
namespace perf {

PerfLog::Setup
setup_PerfLog(Section const& section, boost::filesystem::path const& configDir);

std::unique_ptr<PerfLog>
make_PerfLog(
    PerfLog::Setup const& setup,
    Application& app,
    beast::Journal journal,
    std::function<void()>&& signalStop);

template <typename Func, class Rep, class Period>
auto
measureDurationAndLog(
    Func&& func,
    const std::string& actionDescription,
    std::chrono::duration<Rep, Period> maxDelay,
    const beast::Journal& journal)
{
    auto start_time = std::chrono::high_resolution_clock::now();

    auto result = func();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    if (duration > maxDelay)
    {
        JLOG(journal.warn())
            << actionDescription << " took " << duration.count() << " ms";
    }

    return result;
}

}  // namespace perf
}  // namespace ripple

#endif  // RIPPLE_BASICS_PERFLOG_H
