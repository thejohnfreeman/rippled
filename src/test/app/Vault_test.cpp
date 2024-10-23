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

#include <test/jtx/Account.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/vault.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>

namespace ripple {

class Vault_test : public beast::unit_test::suite
{
    TEST_CASE(CreateUpdateDelete)
    {
        using namespace test::jtx;
        testcase("Create / Update / Delete");
        Env env{*this};

        Account issuer{"issuer"};
        Account owner{"owner"};
        env.fund(XRP(1000), issuer, owner);
        env.close();
        auto vault = env.vault();

        SUBCASE("IOU vault")
        {
            Asset asset = issuer["IOU"];
            auto tx = vault.create({.owner = owner, .asset = asset});
            // vault.id available starting here

            SUBCASE("happy path")
            {
                env(tx);
                env.close();
            }

            SUBCASE("insufficient fee")
            {
                env(tx, fee(env.current()->fees().base), ter(telINSUF_FEE_P));
                env.close();
            }

            SUBCASE("insufficient reserve")
            {
                // It is possible to construct a complicated mathematical
                // expression for this amount, but it is sadly not easy.
                env(pay(owner, issuer, XRP(775)));
                env.close();
                env(tx, ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }

            SUBCASE("global freeze")
            {
                env(fset(issuer, asfGlobalFreeze));
                env.close();
                env(tx, ter(tecFROZEN));
                env.close();
            }

            SUBCASE("data too large")
            {
                tx[sfData] = blob257;
                env(tx, ter(temMALFORMED));
                env.close();
            }
        }

        SKIP("MPT vault")
        {
            MPTTester mptt{env, issuer, {.fund = false}};
            mptt.create();
            Asset asset = mptt.issuanceID();
            auto tx = vault.create({.owner = owner, .asset = asset});

            SUBCASE("happy path")
            {
                env(tx);
                env.close();
            }

            SUBCASE("metadata too large")
            {
                // tx[sfMPTokenMetadata] = blob1025;
                // env(tx, ter(temMALFORMED));
                env.close();
            }
        }

        // (create) => no sfVaultID
        // (update) => sfVaultID
        // TODO: VaultSet (create) succeed
        // TODO: VaultSet (update) succeed
        // TODO: VaultSet (update) fail: wrong owner
        // TODO: VaultSet (update) fail: Data too large
        // TODO: VaultSet (update) fail: Metadata present
        // TODO: VaultSet (update) fail: AssetMaximum < AssetTotal
        // TODO: VaultSet (update) fail: tfPrivate flag
        // TODO: VaultSet (update) fail: tfShareNonTransferable flag
        // TODO: Payment to VaultSet.PA fail
        // TODO: VaultDelete succeed
        // TODO: VaultDelete fail: missing vault
        // TODO: VaultSet (update) fail: missing vault

        // TODO: VaultSet (create) fail: Asset is MPT but !CanTransfer
        // TODO: VaultSet (create) fail: Asset is MPT but Locked

        BEAST_EXPECT(true);
    }

public:
    void
    run() override
    {
        EXECUTE(CreateUpdateDelete);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Vault, tx, ripple, 1);

}  // namespace ripple
