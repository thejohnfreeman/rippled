//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_ACCT_ROOT_H_INCLUDED
#define RIPPLE_PROTOCOL_ACCT_ROOT_H_INCLUDED

#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STLedgerEntry.h>

namespace ripple {

class AcctRootImpl final : public STLedgerEntry
{
private:
    // Inherit constructors.
    using STLedgerEntry::STLedgerEntry;

    // Friends who construct.
    friend class ReadView;
    friend class ApplyView;

public:
    [[nodiscard]] AccountID
    accountID() const
    {
        return at(sfAccount);
    }

    [[nodiscard]] std::uint32_t
    sequence() const
    {
        return at(sfSequence);
    }

    void
    setSequence(std::uint32_t seq)
    {
        at(sfSequence) = seq;
    }

    [[nodiscard]] STAmount
    balance() const
    {
        return at(sfBalance);
    }

    void
    setBalance(STAmount const& amount)
    {
        at(sfBalance) = amount;
    }

    [[nodiscard]] std::uint32_t
    ownerCount() const
    {
        return at(sfOwnerCount);
    }

    void
    setOwnerCount(std::uint32_t newCount)
    {
        at(sfOwnerCount) = newCount;
    }

    [[nodiscard]] std::uint32_t
    previousTxnID() const
    {
        return at(sfOwnerCount);
    }

    void
    setPreviousTxnID(uint256 prevTxID)
    {
        at(sfPreviousTxnID) = prevTxID;
    }

    [[nodiscard]] std::uint32_t
    previousTxnLgrSeq() const
    {
        return at(sfPreviousTxnLgrSeq);
    }

    void
    setPreviousTxnLgrSeq(std::uint32_t prevTxLgrSeq)
    {
        at(sfPreviousTxnLgrSeq) = prevTxLgrSeq;
    }

    [[nodiscard]] std::optional<uint256>
    accountTxnID() const
    {
        return at(~sfAccountTxnID);
    }

    void
    setAccountTxnID(uint256 const& newAcctTxnID)
    {
        this->setOptional(sfAccountTxnID, newAcctTxnID);
    }

    void
    clearAccountTxnID()
    {
        this->clearOptional(sfAccountTxnID);
    }

    [[nodiscard]] std::optional<AccountID>
    regularKey() const
    {
        return at(~sfRegularKey);
    }

    void
    setRegularKey(AccountID const& newRegKey)
    {
        this->setOptional(sfRegularKey, newRegKey);
    }

    void
    clearRegularKey()
    {
        this->clearOptional(sfRegularKey);
    }

    [[nodiscard]] std::optional<uint128>
    emailHash() const
    {
        return at(~sfEmailHash);
    }

    void
    setEmailHash(uint128 const& newEmailHash)
    {
        this->setOrClearBaseUintIfZero(sfEmailHash, newEmailHash);
    }

    [[nodiscard]] std::optional<uint256>
    walletLocator() const
    {
        return at(~sfWalletLocator);
    }

    void
    setWalletLocator(uint256 const& newWalletLocator)
    {
        this->setOrClearBaseUintIfZero(sfWalletLocator, newWalletLocator);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    walletSize() const
    {
        return at(~sfWalletSize);
    }

    [[nodiscard]] Blob
    messageKey() const
    {
        return this->getOptionalVL(sfMessageKey);
    }

    void
    setMessageKey(Blob const& newMessageKey)
    {
        this->setOrClearVLIfEmpty(sfMessageKey, newMessageKey);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    transferRate() const
    {
        return at(~sfTransferRate);
    }

    void
    setTransferRate(std::uint32_t newTransferRate)
    {
        this->setOptional(sfTransferRate, newTransferRate);
    }

    void
    clearTransferRate()
    {
        this->clearOptional(sfTransferRate);
    }

    [[nodiscard]] Blob
    domain() const
    {
        return this->getOptionalVL(sfDomain);
    }

    void
    setDomain(Blob const& newDomain)
    {
        this->setOrClearVLIfEmpty(sfDomain, newDomain);
    }

    [[nodiscard]] std::optional<std::uint8_t>
    tickSize() const
    {
        return at(~sfTickSize);
    }

    void
    setTickSize(std::uint8_t newTickSize)
    {
        this->setOptional(sfTickSize, newTickSize);
    }

    void
    clearTickSize()
    {
        this->clearOptional(sfTickSize);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    ticketCount() const
    {
        return at(~sfTicketCount);
    }

    void
    setTicketCount(std::uint32_t newTicketCount)
    {
        this->setOptional(sfTicketCount, newTicketCount);
    }

    void
    clearTicketCount()
    {
        this->clearOptional(sfTicketCount);
    }

    [[nodiscard]] std::optional<AccountID>
    NFTokenMinter() const
    {
        return at(~sfNFTokenMinter);
    }

    void
    setNFTokenMinter(AccountID const& newMinter)
    {
        this->setOptional(sfNFTokenMinter, newMinter);
    }

    void
    clearNFTokenMinter()
    {
        this->clearOptional(sfNFTokenMinter);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    mintedNFTokens() const
    {
        return at(~sfMintedNFTokens);
    }

    void
    setMintedNFTokens(std::uint32_t newMintedCount)
    {
        this->setOptional(sfMintedNFTokens, newMintedCount);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    burnedNFTokens() const
    {
        return at(~sfBurnedNFTokens);
    }

    void
    setBurnedNFTokens(std::uint32_t newBurnedCount)
    {
        this->setOptional(sfBurnedNFTokens, newBurnedCount);
    }

    [[nodiscard]] std::optional<std::uint32_t>
    firstNFTokenSequence() const
    {
        return at(~sfFirstNFTokenSequence);
    }

    void
    setFirstNFTokenSequence(std::uint32_t newFirstNFTokenSeq)
    {
        this->setOptional(sfFirstNFTokenSequence, newFirstNFTokenSeq);
    }
};

// TODO: Rename `AcctRootImpl` to `AccountRoot` and eliminate these aliases.
using AcctRootRd = std::shared_ptr<AcctRootImpl const>;
using AcctRoot = std::shared_ptr<AcctRootImpl>;

// clang-format off
#ifndef __INTELLISENSE__
static_assert(std::is_default_constructible_v<AcctRootRd>);
static_assert(std::is_copy_constructible_v<AcctRootRd>);
static_assert(std::is_move_constructible_v<AcctRootRd>);
static_assert(std::is_copy_assignable_v<AcctRootRd>);
static_assert(std::is_move_assignable_v<AcctRootRd>);
static_assert(std::is_nothrow_destructible_v<AcctRootRd>);

static_assert(std::is_default_constructible_v<AcctRoot>);
static_assert(std::is_copy_constructible_v<AcctRoot>);
static_assert(std::is_move_constructible_v<AcctRoot>);
static_assert(std::is_copy_assignable_v<AcctRoot>);
static_assert(std::is_move_assignable_v<AcctRoot>);
static_assert(std::is_nothrow_destructible_v<AcctRoot>);
#endif  // __INTELLISENSE__
// clang-format on

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ACCT_ROOT_H_INCLUDED
