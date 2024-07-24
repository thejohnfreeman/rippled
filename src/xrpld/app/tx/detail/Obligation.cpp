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

#include <xrpld/app/tx/detail/Obligation.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

/**
 * An Obligation object contains a chain of Transfer objects,
 * where each Transfer represents an obligation on a sender account to
 * transfer an amount of assets to a receiver account at a future date.
 *
 * The chain always has at least two Transfers.
 * Each Transfer names one sending account and one amount.
 * The receiving account is the sending account of
 * the next Transfer object in the chain.
 *
 * If the Obligation is escrowed,
 * then each sender account is checked that it holds the amount it will send
 * before the ObligationCreate transaction can complete succesfully.
 * Otherwise, they are checked before the ObligationFinish transaction
 * can complete successfully.
 *
 * The amounts are delivered to the receivers only after an ObligationFinish
 * transaction completes successfully.
 * If the Obligation is escrowed,
 * then the amounts are withdrawn from the senders when the ObligationCreate
 * transaction completes successfully.
 * Otherwise, they are withdrawn when the ObligationFinish transaction
 * completes successfully.
 *
 * Accounts may send a zero amount.
 * Accounts that are sending a non-zero account must appear in the signer list
 * of the ObligationCreate transaction.
 *
 * Obligations that remain on the ledger after their cancel time
 * can be removed by any account that submits an ObligationCancel transaction.
 * When an escrowed Obligation is canceled,
 * the withdrawn amounts are returned to their sending accounts.
 */

namespace ripple {

NotTEC
ObligationCreate::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureObligations))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    // Must have both a finish and a cancel time.
    // Cancel time must be strictly after finish time.
    if (!ctx.tx[~sfCancelAfter] || !ctx.tx[~sfFinishAfter] ||
        ctx.tx[sfCancelAfter] <= ctx.tx[sfFinishAfter])
    {
        return temBAD_EXPIRATION;
    }

    if (!ctx.tx.isFieldPresent(sfTransfers))
        return temMALFORMED;

    auto transfers = ctx.tx.getFieldArray(sfTransfers);
    if (transfers.size() < 2)
        return temMALFORMED;

    for (auto const& wrapper : transfers)
    {
        auto transfer = dynamic_cast<STObject const*>(&wrapper);
        if (!transfer->isFieldPresent(sfAccount) || !transfer->isFieldPresent(sfAmount))
            return temMALFORMED;
        // Should we check that no other fields are passed?
    }

    // Check that non-zero senders appear in the signer list.
    // Some other function is checking that the signatures are valid.

    return preflight2(ctx);
}

TER
ObligationCreate::preclaim(PreclaimContext const& ctx)
{
    std::uint32_t const uTxFlags = ctx.tx.getFlags();

    if (uTxFlags & tfEscrowed) {
        // Check that senders exist and hold enough assets.
        assert(ctx.tx.isFieldPresent(sfTransfers));
        for (auto const& wrapper : ctx.tx.getFieldArray(sfTransfers))
        {
            auto transfer = dynamic_cast<STObject const*>(&wrapper);
            assert(transfer->isFieldPresent(sfAmount));
            if (!ctx.tx[sfAmount]) {
                // Zero amount.
                // A sender account can be missing in the ledger as long it is
                // sending nothing.
                // It will be created as long as it is receiving enough XRP to
                // fulfill the reserve requirement.
                continue;
            }
            assert(transfer->isFieldPresent(sfAccount));
            auto const sender = ctx.view.read(keylet::account(ctx.tx[sfAccount]));
            if (!sender)
                return tecCLAIM;
            // This is mildly annoying...
            if (sender->isFieldPresent(sfAMMID))
                return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
ObligationCreate::doApply()
{
    // Create the object and insert it.
    auto const account = ctx_.tx[sfAccount];

    // If escrowed == true, withdraw the amounts now.

    auto seq = ctx_.tx.getSeqProxy().value();
    Keylet const keylet = keylet::obligation(account, seq);
    auto const sle = std::make_shared<SLE>(keylet);
    (*sle)[sfOwner] = account;
    (*sle)[sfSequence] = seq;
    (*sle)[sfFlags] = ctx_.tx[sfFlags];
    sle->setFieldArray(sfTransfers, ctx_.tx.getFieldArray(sfTransfers));
    (*sle)[sfFinishAfter] = ctx_.tx[sfFinishAfter];
    (*sle)[sfCancelAfter] = ctx_.tx[sfCancelAfter];

    ctx_.view().insert(sle);

    // Add to owner directory.
    // Increase reserve.

    return tesSUCCESS;
}

NotTEC
ObligationFinish::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
ObligationFinish::doApply()
{
    Keylet const keylet{ltOBLIGATION, ctx_.tx[sfObjectID]};
    auto sle = ctx_.view().peek(keylet);

    if (!sle)
        return tecOBJECT_NOT_FOUND;

    auto const now = ctx_.view().info().parentCloseTime;
    // Too soon.
    if ((*sle)[~sfFinishAfter] && !after(now, (*sle)[sfFinishAfter]))
        return tecNO_PERMISSION;

    auto const transfers = ctx_.tx.getFieldArray(sfTransfers);
    auto sender = transfers[0];
    std::size_t i = 1;
    auto receiver = transfers[i];
    while (true)
    {
        // If escrowed == false, withdraw the amount now.

        // Deliver the amount to the receiver.

        // After we've delivered to the first sender, we're done.
        if (i == 0)
            break;

        sender = receiver;
        i = (i + 1) % transfers.size();
        receiver = transfers[i];
    }

    // Call update() on all the receivers.
    // Remove the obligation.
    // Call delete()

    return tesSUCCESS;
}

NotTEC
ObligationCancel::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
ObligationCancel::doApply()
{
    Keylet const keylet{ltOBLIGATION, ctx_.tx[sfObjectID]};
    auto sle = ctx_.view().peek(keylet);

    if (!sle)
        return tecOBJECT_NOT_FOUND;

    auto const now = ctx_.view().info().parentCloseTime;
    // Too soon.
    if ((*sle)[~sfCancelAfter] && !after(now, (*sle)[sfCancelAfter]))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

}
