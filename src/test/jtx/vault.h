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

#ifndef RIPPLE_TEST_JTX_VAULT_H_INCLUDED
#define RIPPLE_TEST_JTX_VAULT_H_INCLUDED

#include <test/jtx/Account.h>
#include <xrpl/json/json_value.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>

#include <optional>
#include <tuple>

namespace ripple {
namespace test {
namespace jtx {

class Env;

struct Vault
{
    Env& env;

    struct CreateArgs
    {
        Account owner;
        Asset asset;
        std::optional<std::uint32_t> flags{};
    };

    /** Return a VaultCreate transaction and the Vault's expected keylet. */
    std::tuple<Json::Value, Keylet>
    create(CreateArgs const& args);

    struct SetArgs;
    Json::Value
    set(SetArgs const& args);

    struct DeleteArgs
    {
        Account owner;
        uint256 id;
    };

    Json::Value
    del(DeleteArgs const& args);
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif