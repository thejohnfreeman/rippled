//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/MPTokenIssuanceCreate.h>
#include <xrpld/app/tx/detail/VaultCreate.h>
#include <xrpld/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
VaultCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSingleAssetVault))
        return temDISABLED;

    if (auto const ter = preflight1(ctx))
        return ter;

    if (ctx.tx.getFlags() & tfVaultCreateMask)
        return temINVALID_FLAG;

    if (auto const data = ctx.tx[~sfData])
    {
        if (data->length() > maxVaultDataLength)
            return temMALFORMED;
    }

    return preflight2(ctx);
}

XRPAmount
VaultCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // One reserve increment is typically much greater than one base fee.
    return view.fees().increment;
}

TER
VaultCreate::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
VaultCreate::doApply()
{
    // All return codes in `doApply` must be `tec`, `ter`, or `tes`.
    // As we move checks into `preflight` and `preclaim`,
    // we can consider downgrading them to `tef` or `tem`.

    auto const& tx = ctx_.tx;
    auto const& ownerId = account_;
    auto sequence = tx.getSequence();

    auto owner = view().peek(keylet::account(ownerId));
    auto vault = std::make_shared<SLE>(keylet::vault(ownerId, sequence));

    if (auto ter = dirLink(view(), ownerId, vault))
        return ter;
    // Should the next 3 lines be folded into `dirLink`?
    adjustOwnerCount(view(), owner, 1, j_);
    auto ownerCount = owner->at(sfOwnerCount);
    if (mPriorBalance < view().fees().accountReserve(ownerCount))
        return tecINSUFFICIENT_RESERVE;

    auto maybePseudo = createPseudoAccount(view(), vault->key());
    if (!maybePseudo)
        return maybePseudo.error();
    auto& pseudo = *maybePseudo;
    auto pseudoId = pseudo->at(sfAccount);

    if (auto ter = authorizeHolding(view(), pseudoId, tx[sfAsset], j_))
        return ter;

    auto txFlags = tx.getFlags();
    std::uint32_t mptFlags = 0;
    if (!(txFlags & tfVaultShareNonTransferable))
        mptFlags |= (lsfMPTCanEscrow | lsfMPTCanTrade | lsfMPTCanTransfer);
    if (txFlags & tfVaultPrivate)
        mptFlags |= lsfMPTRequireAuth;

    auto maybeShare = MPTokenIssuanceCreate::create(
        view(),
        j_,
        {
            .account = pseudoId,
            .sequence = 1,
            .flags = mptFlags,
            .metadata = tx[~sfMPTokenMetadata],
        });
    if (!maybeShare)
        return maybeShare.error();
    auto& share = *maybeShare;

    vault->at(sfFlags) = txFlags & tfVaultPrivate;
    vault->at(sfSequence) = sequence;
    vault->at(sfOwner) = ownerId;
    vault->at(sfAccount) = pseudoId;
    vault->at(sfAsset) = tx[sfAsset];
    // Leave default values for AssetTotal and AssetAvailable, both zero.
    if (auto value = tx[~sfAssetMaximum])
        vault->at(sfAssetMaximum) = *value;
    vault->at(sfMPTokenIssuanceID) = share;
    if (auto value = tx[~sfData])
        vault->at(sfData) = *value;
    // No `LossUnrealized`.
    view().insert(vault);

    return tesSUCCESS;
}

}  // namespace ripple
