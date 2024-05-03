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

#ifndef XRPL_NODESTORE_BACKEND_MEMORYFACTORY_H_INCLUDED
#define XRPL_NODESTORE_BACKEND_MEMORYFACTORY_H_INCLUDED

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Scheduler.h>

#include <boost/beast/core/string.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ripple {
namespace NodeStore {

struct MemoryDB;

/**
 * If you want the `MemoryFactory` available in your program,
 * you need to allocate and register it yourself,
 * but it can be as simple as defining a single static `MemoryFactory`.
 */
class MemoryFactory : public Factory
{
private:
    std::mutex mutex_;
    std::map<std::string, MemoryDB, boost::beast::iless> map_;

public:
    MemoryFactory();
    ~MemoryFactory() override;

    std::string
    getName() const override;

    std::unique_ptr<Backend>
    createInstance(
        std::size_t keyBytes,
        Section const& keyValues,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) override;

    MemoryDB&
    open(std::string const& path);
};

}  // namespace NodeStore
}  // namespace ripple

#endif
