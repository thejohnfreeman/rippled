//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/VaultDelete.h>

#include <xrpld/app/tx/detail/MPTokenIssuanceDestroy.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultDelete::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (auto const ter = preflight1(ctx))
        return ter;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    return preflight2(ctx);
}

TER
VaultDelete::preclaim(PreclaimContext const& ctx)
{
    auto const vault = ctx.view.read(keylet::vault(ctx.tx[sfVaultID]));
    if (!vault)
        return tecOBJECT_NOT_FOUND;
    if (vault->at(sfOwner) != ctx.tx[sfAccount])
        return tecNO_PERMISSION;
    if (vault->at(sfAssetAvailable) != 0)
        return tecHAS_OBLIGATIONS;
    return tesSUCCESS;
}

TER
VaultDelete::doApply()
{
    auto const vault = view().peek(keylet::vault(ctx_.tx[sfVaultID]));
    if (!vault)
        return tecOBJECT_NOT_FOUND;

    auto const ownerId = vault->at(sfOwner);
    if (!view().dirRemove(
            keylet::ownerDir(ownerId),
            vault->at(sfOwnerNode),
            vault->key(),
            false))
        return tefBAD_LEDGER;
    auto const owner = view().peek(keylet::account(ownerId));
    if (!owner)
        return tefBAD_LEDGER;
    adjustOwnerCount(view(), owner, -1, j_);

    // TODO: Delete vault->at(sfAccount)
    // TODO: deauthorizeHolding(vault->at(sfAsset))
    // TODO: Delete vault->at(sfMPTokenIssuanceID)

    view().erase(vault);

    return tesSUCCESS;
}

}  // namespace ripple