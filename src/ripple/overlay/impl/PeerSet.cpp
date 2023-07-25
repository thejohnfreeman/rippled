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

#include <ripple/app/main/Application.h>
#include <ripple/overlay/PeerSet.h>

namespace ripple {

class PeerSetImpl : public PeerSet
{
public:
    PeerSetImpl(Overlay const& overlay);

    void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
        std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded) override;

    /** Send a message to one or all peers. */
    void
    sendRequest(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        std::shared_ptr<Peer> const& peer) override;

    const std::set<Peer::id_t>&
    getPeerIds() const override;

private:
    Overlay const& overlay_;

    /** The identifiers of the peers we are tracking. */
    std::set<Peer::id_t> peers_;
};

PeerSetImpl::PeerSetImpl(Overlay const& overlay) : overlay_(overlay)
{
}

void
PeerSetImpl::addPeers(
    std::size_t limit,
    std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
    std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded)
{
    using ScoredPeer = std::pair<int, std::shared_ptr<Peer>>;

    std::vector<ScoredPeer> pairs;
    pairs.reserve(overlay_.size());

    overlay_.foreach([&](auto const& peer) {
        auto const score = peer->getScore(hasItem(peer));
        pairs.emplace_back(score, std::move(peer));
    });

    std::sort(
        pairs.begin(),
        pairs.end(),
        [](ScoredPeer const& lhs, ScoredPeer const& rhs) {
            return lhs.first > rhs.first;
        });

    std::size_t accepted = 0;
    for (auto const& pair : pairs)
    {
        auto const peer = pair.second;
        if (!peers_.insert(peer->id()).second)
            continue;
        onPeerAdded(peer);
        if (++accepted >= limit)
            break;
    }
}

void
PeerSetImpl::sendRequest(
    ::google::protobuf::Message const& message,
    protocol::MessageType type,
    std::shared_ptr<Peer> const& peer)
{
    auto packet = std::make_shared<Message>(message, type);
    if (peer)
    {
        peer->send(packet);
        return;
    }

    for (auto id : peers_)
    {
        if (auto p = overlay_.findPeerByShortID(id))
            p->send(packet);
    }
}

const std::set<Peer::id_t>&
PeerSetImpl::getPeerIds() const
{
    return peers_;
}

class PeerSetBuilderImpl : public PeerSetBuilder
{
public:
    PeerSetBuilderImpl(Application& app) : app_(app)
    {
    }

    virtual std::unique_ptr<PeerSet>
    build() override
    {
        return std::make_unique<PeerSetImpl>(app_.overlay());
    }

private:
    Application& app_;
};

std::unique_ptr<PeerSetBuilder>
make_PeerSetBuilder(Application& app)
{
    return std::make_unique<PeerSetBuilderImpl>(app);
}

class DummyPeerSet : public PeerSet
{
public:
    DummyPeerSet(beast::Journal const& journal) : journal_(journal)
    {
    }

    void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
        std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded) override
    {
        JLOG(journal_.error()) << "DummyPeerSet addPeers should not be called";
    }

    void
    sendRequest(
        ::google::protobuf::Message const& message,
        protocol::MessageType type,
        std::shared_ptr<Peer> const& peer) override
    {
        JLOG(journal_.error())
            << "DummyPeerSet sendRequest should not be called";
    }

    const std::set<Peer::id_t>&
    getPeerIds() const override
    {
        static std::set<Peer::id_t> emptyPeers;
        JLOG(journal_.error())
            << "DummyPeerSet getPeerIds should not be called";
        return emptyPeers;
    }

private:
    beast::Journal journal_;
};

std::unique_ptr<PeerSet>
make_DummyPeerSet(Application& app)
{
    return std::make_unique<DummyPeerSet>(app.journal("DummyPeerSet"));
}

}  // namespace ripple
