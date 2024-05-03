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

#ifndef XRPL_NODESTORE_BACKEND_ROCKSDBFACTORY_H_INCLUDED
#define XRPL_NODESTORE_BACKEND_ROCKSDBFACTORY_H_INCLUDED

#if RIPPLE_ROCKSDB_AVAILABLE

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/rocksdb.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

namespace ripple {
namespace NodeStore {

class RocksDBEnv : public rocksdb::EnvWrapper
{
public:
    RocksDBEnv() : EnvWrapper(rocksdb::Env::Default())
    {
    }

    struct ThreadParams
    {
        ThreadParams(void (*f_)(void*), void* a_) : f(f_), a(a_)
        {
        }

        void (*f)(void*);
        void* a;
    };

    static void
    thread_entry(void* ptr)
    {
        ThreadParams* const p(reinterpret_cast<ThreadParams*>(ptr));
        void (*f)(void*) = p->f;
        void* a(p->a);
        delete p;

        static std::atomic<std::size_t> n;
        std::size_t const id(++n);
        std::stringstream ss;
        ss << "rocksdb #" << id;
        beast::setCurrentThreadName(ss.str());

        (*f)(a);
    }

    void
    StartThread(void (*f)(void*), void* a) override
    {
        ThreadParams* const p(new ThreadParams(f, a));
        EnvWrapper::StartThread(&RocksDBEnv::thread_entry, p);
    }
};

class RocksDBFactory : public Factory
{
public:
    RocksDBEnv m_env;

    RocksDBFactory();
    ~RocksDBFactory() override;

    std::string
    getName() const override;

    std::unique_ptr<Backend>
    createInstance(
        std::size_t keyBytes,
        Section const& keyValues,
        std::size_t,
        Scheduler& scheduler,
        beast::Journal journal) override;
};

}  // namespace NodeStore
}  // namespace ripple

#endif

#endif
