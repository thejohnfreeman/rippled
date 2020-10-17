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

#include <ripple/app/ledger/BuildLedger.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerReplay.h>
#include <ripple/app/ledger/LedgerReplayer.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

struct LedgerReplay_test : public beast::unit_test::suite
{
    void
    run() override
    {
        testcase("Replay ledger");

        using namespace jtx;

        // Build a ledger normally
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env(*this);
        env.fund(XRP(100000), alice, bob);
        env.close();

        LedgerMaster& ledgerMaster = env.app().getLedgerMaster();
        auto const lastClosed = ledgerMaster.getClosedLedger();
        auto const lastClosedParent =
            ledgerMaster.getLedgerByHash(lastClosed->info().parentHash);

        auto const replayed = buildLedger(
            LedgerReplay(lastClosedParent, lastClosed),
            tapNONE,
            env.app(),
            env.journal);

        BEAST_EXPECT(replayed->info().hash == lastClosed->info().hash);
    }
};

class ReplayTestPeer : public Peer
{
public:
    void
    send(std::shared_ptr<Message> const& m) override
    {
    }
    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return {};
    }
    void
    charge(Resource::Charge const& fee) override
    {
    }
    id_t
    id() const override
    {
        return 1234;
    }
    bool
    cluster() const override
    {
        return false;
    }
    bool
    isHighLatency() const override
    {
        return false;
    }
    int
    getScore(bool) const override
    {
        return 0;
    }
    PublicKey const&
    getNodePublic() const override
    {
        static PublicKey key{};
        return key;
    }
    Json::Value
    json() override
    {
        return {};
    }
    bool
    supportsFeature(ProtocolFeature f) const override
    {
        return false;
    }
    boost::optional<std::size_t>
    publisherListSequence(PublicKey const&) const override
    {
        return {};
    }
    void
    setPublisherListSequence(PublicKey const&, std::size_t const) override
    {
    }
    uint256 const&
    getClosedLedgerHash() const override
    {
        static uint256 hash{};
        return hash;
    }
    bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const override
    {
        return true;
    }
    void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const override
    {
    }
    bool
    hasShard(std::uint32_t shardIndex) const override
    {
        return false;
    }
    bool
    hasTxSet(uint256 const& hash) const override
    {
        return false;
    }
    void
    cycleStatus() override
    {
    }
    bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) override
    {
        return false;
    }
    bool
    compressionEnabled() const override
    {
        return false;
    }
};

struct ReplayPeerSet : public PeerSet
{
    ReplayPeerSet(
        LedgerReplayMsgHandler& me,
        LedgerReplayMsgHandler& other,
        int drop)
        : local(me)
        , remote(other)
        , dummyPeer(std::make_shared<ReplayTestPeer>())
        , dropRate(drop)
    {
    }

    void
    addPeers(
        std::size_t limit,
        std::function<bool(std::shared_ptr<Peer> const&)> hasItem,
        std::function<void(std::shared_ptr<Peer> const&)> onPeerAdded) override
    {
        hasItem(dummyPeer);
        onPeerAdded(dummyPeer);
    }

    void
    sendRequest(
        ::google::protobuf::Message const& msg,
        protocol::MessageType type,
        std::shared_ptr<Peer> const& peer) override
    {
        if ((rand() % 100 + 1) <= dropRate)
            return;

        switch (type)
        {
            case protocol::mtProofPathRequest: {
                auto request = std::make_shared<protocol::TMProofPathRequest>(
                    dynamic_cast<protocol::TMProofPathRequest const&>(msg));
                auto reply = std::make_shared<protocol::TMProofPathResponse>(
                    remote.processProofPathRequest(request));
                local.processProofPathResponse(reply);
                break;
            }
            case protocol::mtReplayDeltaRequest: {
                auto request = std::make_shared<protocol::TMReplayDeltaRequest>(
                    dynamic_cast<protocol::TMReplayDeltaRequest const&>(msg));
                auto reply = std::make_shared<protocol::TMReplayDeltaResponse>(
                    remote.processReplayDeltaRequest(request));
                local.processReplayDeltaResponse(reply);
                break;
            }
            default:
                return;
        }
    }

    void
    visitAddedPeers(std::function<void(Peer::id_t)> onPeer) override
    {
        onPeer(dummyPeer->id());
    }

    std::size_t
    countAddedPeers() override
    {
        return 1;
    }

    LedgerReplayMsgHandler& local;
    LedgerReplayMsgHandler& remote;
    std::shared_ptr<ReplayTestPeer> dummyPeer;
    int dropRate;
};

class ReplayPeerSetBuilder : public PeerSetBuilder
{
public:
    ReplayPeerSetBuilder(
        LedgerReplayMsgHandler& me,
        LedgerReplayMsgHandler& other,
        int drop)
        : local(me), remote(other), dropRate(drop)
    {
    }

    std::unique_ptr<PeerSet>
    build() override
    {
        return std::make_unique<ReplayPeerSet>(local, remote, dropRate);
    }

private:
    LedgerReplayMsgHandler& local;
    LedgerReplayMsgHandler& remote;
    int dropRate;
};

/**
 * Utility class for creating ledger history
 */
struct LedgerServer
{
    struct Parameter
    {
        int initLedgers;
        int initAccounts = 10;
        int initAmount = 1'000'000;
        int numTxPerLedger = 10;
        int txAmount = 10;
    };

    LedgerServer(beast::unit_test::suite& suite, Parameter const& p)
        : env(suite)
        , app(env.app())
        , ledgerMaster(env.app().getLedgerMaster())
        , msgHandler(env.app())
        , param(p)
    {
        assert(param.initLedgers > 0);
        createAccounts(param.initAccounts);
        createLedgerHistory();
        app.logs().threshold(beast::severities::Severity::kWarning);
    }

    /**
     * @note close a ledger
     */
    void
    createAccounts(int newAccounts)
    {
        auto fundedAccounts = accounts.size();
        for (int i = 0; i < newAccounts; ++i)
        {
            accounts.emplace_back(
                "alice_" + std::to_string(fundedAccounts + i));
            env.fund(jtx::XRP(param.initAmount), accounts.back());
        }
        env.close();
    }

    /**
     * @note close a ledger
     */
    void
    sendPayments(int newTxes)
    {
        int fundedAccounts = accounts.size();
        assert(fundedAccounts >= newTxes);
        std::unordered_set<int> senders;

        // somewhat random but reproducible
        int r = ledgerMaster.getClosedLedger()->seq() * 7;
        int fromIdx = 0;
        int toIdx = 0;
        auto updateIdx = [&]() {
            assert(fundedAccounts > senders.size());
            fromIdx = (fromIdx + r) % fundedAccounts;
            while (senders.count(fromIdx) != 0)
                fromIdx = (fromIdx + 1) % fundedAccounts;
            senders.insert(fromIdx);
            toIdx = (toIdx + r * 2) % fundedAccounts;
            if (toIdx == fromIdx)
                toIdx = (toIdx + 1) % fundedAccounts;
        };

        for (int i = 0; i < newTxes; ++i)
        {
            updateIdx();
            env.apply(
                pay(accounts[fromIdx],
                    accounts[toIdx],
                    jtx::drops(ledgerMaster.getClosedLedger()->fees().base) +
                        jtx::XRP(param.txAmount)),
                jtx::seq(jtx::autofill),
                jtx::fee(jtx::autofill),
                jtx::sig(jtx::autofill));
        }
        env.close();
    }

    /**
     * create ledger history
     * TODO consider added different kinds of Tx
     */
    void
    createLedgerHistory()
    {
        for (int i = 0; i < param.initLedgers - 1; ++i)
        {
            sendPayments(param.numTxPerLedger);
        }
    }

    jtx::Env env;
    Application& app;
    LedgerMaster& ledgerMaster;
    LedgerReplayMsgHandler msgHandler;
    Parameter param;
    std::vector<jtx::Account> accounts;
};

extern void
incPorts();

struct LedgerReplayClient
{
    LedgerReplayClient(
        beast::unit_test::suite& suite,
        LedgerServer& server,
        int drop = 0)
        : env(suite)
        , app(env.app())
        , ledgerMaster(env.app().getLedgerMaster())
        , replayer(env.app().getLedgerReplayer())
        , serverMsgHandler(server.app)
        , clientMsgHandler(env.app())
    {
        replayer.peerSetBuilder_ = std::make_unique<ReplayPeerSetBuilder>(
            clientMsgHandler, serverMsgHandler, drop);
    }

    void
    addLedger(std::shared_ptr<Ledger const> const& l)
    {
        ledgerMaster.storeLedger(l);
    }

    jtx::Env env;
    Application& app;
    LedgerMaster& ledgerMaster;
    LedgerReplayer& replayer;
    LedgerReplayMsgHandler serverMsgHandler;
    LedgerReplayMsgHandler clientMsgHandler;
};

using namespace beast::severities;
void
logAll(
    LedgerServer& server,
    LedgerReplayClient& client,
    beast::severities::Severity level = Severity::kWarning)
{
    server.app.logs().threshold(level);
    client.app.logs().threshold(level);
}

using namespace std::chrono;
void
shortenTimeouts()
{
    const_cast<milliseconds&>(LedgerReplayer::TASK_TIMEOUT) = 5ms;
    const_cast<milliseconds&>(LedgerReplayer::SUB_TASK_TIMEOUT) = 2ms;
    const_cast<int&>(LedgerReplayer::TASK_MAX_TIMEOUTS_MULTIPLIER) = 1;
    const_cast<int&>(LedgerReplayer::SUB_TASK_MAX_TIMEOUTS) = 3;
};//TODO UB?

struct LedgerForwardReplay_test : public beast::unit_test::suite
{
    void
    testSimple()
    {
        testcase("simple test");
        int totalReplay = 3;
        LedgerServer server(*this, {totalReplay + 1});
        incPorts();
        LedgerReplayClient client(*this, server);
        logAll(server, client);
        auto l = server.ledgerMaster.getClosedLedger();
        auto finalHash = l->info().hash;
        for (int i = 0; i < totalReplay - 1; ++i)
        {
            l = server.ledgerMaster.getLedgerByHash(l->info().parentHash);
        }
        client.ledgerMaster.storeLedger(l);

        client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);
        int totalWait = 10;
        while (!client.replayer.tasks_.empty() && totalWait-- > 0)
        {
            client.replayer.sweep();
            usleep(1000000);
        }
        l = client.ledgerMaster.getLedgerByHash(finalHash);
        BEAST_EXPECT(l);
        if (l)
        {
            for (int i = 0; i < totalReplay - 1; ++i)
            {
                l = client.ledgerMaster.getLedgerByHash(l->info().parentHash);
                BEAST_EXPECT(l);
                if (!l)
                    break;
            }
        }
        client.replayer.sweep();
    }

    void
    testMsgDrop(int dropRate)
    {
        testcase("drop msg test");
        int totalReplay = 5;
        LedgerServer server(*this, {totalReplay + 1});
        incPorts();
        LedgerReplayClient client(*this, server, dropRate);
        logAll(server, client);
        auto l = server.ledgerMaster.getClosedLedger();
        auto finalHash = l->info().hash;
        for (int i = 0; i < totalReplay - 1; ++i)
        {
            l = server.ledgerMaster.getLedgerByHash(l->info().parentHash);
        }
        client.ledgerMaster.storeLedger(l);
        // logAll(server, client);
        client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);
        int totalWait = 10;
        while (!client.replayer.tasks_.empty() && totalWait-- > 0)
        {
            client.replayer.sweep();
            usleep(1000000);
        }
        BEAST_EXPECT(client.ledgerMaster.getLedgerByHash(finalHash));
    }

    void
    testOverlap()
    {
        testcase("overlap test");
        int totalReplay = 5;
        LedgerServer server(*this, {totalReplay + 1});
        incPorts();
        LedgerReplayClient client(*this, server);
        logAll(server, client);
        auto l = server.ledgerMaster.getClosedLedger();
        auto finalHash = l->info().hash;
        auto prevHash = l->info().parentHash;
        for (int i = 0; i < totalReplay - 1; ++i)
        {
            l = server.ledgerMaster.getLedgerByHash(l->info().parentHash);
        }
        client.ledgerMaster.storeLedger(l);

        client.replayer.replay(
            InboundLedger::Reason::GENERIC, prevHash, totalReplay - 1);
        client.replayer.replay(
            InboundLedger::Reason::GENERIC, finalHash, totalReplay);

        int totalWait = 10;
        while (!client.replayer.tasks_.empty() && totalWait-- > 0)
        {
            client.replayer.sweep();
            usleep(1000000);
        }
        BEAST_EXPECT(client.ledgerMaster.getLedgerByHash(finalHash));
        BEAST_EXPECT(client.ledgerMaster.getLedgerByHash(prevHash));
    }

#if 0
    void
    testSkipList()
    {
        testcase("skip list with proof");
        LedgerServer history(*this, {3});
        auto const l = history.ledgerMaster.getClosedLedger();

        // create request
        auto request = std::make_shared<protocol::TMProofPathRequest>();
        request->set_ledgerhash(l->info().hash.data(), l->info().hash.size());
        request->set_key(keylet::skip().key.data(), keylet::skip().key.size());
        request->set_type(protocol::TMLedgerMapType::lmAS_NODE);

        // generate response
        auto reply = history.ledgerMaster.getProofPathResponse(request);
        BEAST_EXPECT(reply.has_ledgerheader());
        BEAST_EXPECT(!reply.has_error());
        if (!reply.has_ledgerheader() || reply.has_error())
            return;

        // verify the header
        auto info = deserializeHeader(
            {reply.ledgerheader().data(), reply.ledgerheader().size()});
        BEAST_EXPECT(calculateLedgerHash(info) == l->info().hash);

        // verify the path
        std::vector<Blob> path;
        path.reserve(reply.path_size());
        for (int i = 0; i < reply.path_size(); ++i)
        {
            path.emplace_back(reply.path(i).begin(), reply.path(i).end());
        }
        BEAST_EXPECT(SHAMap::verifyProofPath(
            info.accountHash, keylet::skip().key, path));

        // deserialize the skip list
        // TODO shorten the deserialize code
        auto node = SHAMapAbstractNode::makeFromWire(makeSlice(path.front()));
        BEAST_EXPECT(node);
        BEAST_EXPECT(node->isLeaf());
        if (!node || !node->isLeaf())
            return;
        auto item = static_cast<SHAMapTreeNode*>(node.get())->peekItem();
        BEAST_EXPECT(item);
        if (!item)
            return;
        auto sle = std::make_shared<SLE>(
            SerialIter{item->data(), item->size()}, item->key());
        BEAST_EXPECT(sle);
        if (!sle)
            return;
        auto const& skipList = sle->getFieldV256(sfHashes).value();

        // verify the skip list
        std::vector<uint256> hashes;
        int current = l->seq();
        for (int seq = std::max(1, current - 256); seq < current; ++seq)
        {
            auto ledger = history.ledgerMaster.getLedgerBySeq(seq);
            BEAST_EXPECT(ledger);
            if (ledger)
                hashes.push_back(ledger->info().hash);
        }
        BEAST_EXPECT(hashes == skipList);
    }

    void
    testLedgerReplayBuild()
    {
        testcase("ledger replay build");
        LedgerServer history(*this, {3});
        auto const l = history.ledgerMaster.getClosedLedger();
        auto const parent =
            history.ledgerMaster.getLedgerByHash(l->info().parentHash);

        // create LedgerReplay object
        auto l_replay_temp = std::make_shared<Ledger>(
            l->info(),
            history.env.app().config(),
            history.env.app().getNodeFamily());
        BEAST_EXPECT(l_replay_temp);
        if (!l_replay_temp)
            return;
        BEAST_EXPECT(l_replay_temp->info().hash == l->info().hash);

        std::map<std::uint32_t, std::shared_ptr<STTx const>> orderedTxns;
        for (auto const& item : l->txMap())
        {
            auto txPair = l->txRead(item.key());
            auto const txIndex = (*txPair.second)[sfTransactionIndex];
            orderedTxns.emplace(txIndex, std::move(txPair.first));
        }

        LedgerReplay replayData(parent, l_replay_temp, std::move(orderedTxns));

        // build ledger
        auto const l_replayed = buildLedger(
            replayData, tapNONE, history.env.app(), history.env.journal);
        BEAST_EXPECT(l_replayed);
        if (!l_replayed)
            return;

        // verify ledger, note that hash is calculated in Ledger::setImmutable()
        BEAST_EXPECT(l_replayed->info().hash == l->info().hash);
    }

    void
    testLedgerDeltaReplayBuild(int numTxPerLedger = 10)
    {
        testcase("ledger delta replay build");
        LedgerServer::Parameter p = {3};
        p.numTxPerLedger = numTxPerLedger;
        LedgerServer history(*this, p);

        auto const l = history.ledgerMaster.getClosedLedger();
        auto const parent =
            history.ledgerMaster.getLedgerByHash(l->info().parentHash);

        // create request
        auto request = std::make_shared<protocol::TMReplayDeltaRequest>();
        request->set_ledgerhash(l->info().hash.data(), l->info().hash.size());

        // generate response
        auto reply = history.ledgerMaster.getReplayDeltaResponse(request);
        BEAST_EXPECT(reply.has_ledgerheader());
        BEAST_EXPECT(!reply.has_error());
        if (!reply.has_ledgerheader() || reply.has_error())
            return;

        // verify the header
        auto info = deserializeHeader(
            {reply.ledgerheader().data(), reply.ledgerheader().size()});
        BEAST_EXPECT(calculateLedgerHash(info) == l->info().hash);

        // verify the transaction size
        auto numTxns = reply.transaction_size();
        BEAST_EXPECT(numTxns == history.param.numTxPerLedger);

        std::map<std::uint32_t, std::shared_ptr<STTx const>> orderedTxns;
        SHAMap txMap(
            SHAMapType::TRANSACTION, history.env.app().getNodeFamily());
        try
        {
            for (int i = 0; i < numTxns; ++i)
            {
                // deserialize:
                // -- TxShaMapItem for building a ShaMap for verification
                // -- Tx
                // -- TxMetaData for Tx ordering
                Serializer shaMapItemData(
                    reply.transaction(i).data(), reply.transaction(i).size());

                SerialIter txMetaSit(makeSlice(reply.transaction(i)));
                SerialIter txSit(
                    txMetaSit.getSlice(txMetaSit.getVLDataLength()));
                SerialIter metaSit(
                    txMetaSit.getSlice(txMetaSit.getVLDataLength()));

                auto tx = std::make_shared<STTx const>(txSit);
                BEAST_EXPECT(tx);
                if (!tx)
                    return;
                auto tid = tx->getTransactionID();
                STObject meta(metaSit, sfMetadata);
                orderedTxns.emplace(meta[sfTransactionIndex], std::move(tx));

                auto item = std::make_shared<SHAMapItem const>(
                    tid, std::move(shaMapItemData));
                BEAST_EXPECT(item);
                if (!item)
                    return;
                auto added = txMap.addGiveItem(std::move(item), true, true);
                BEAST_EXPECT(added);
                if (!added)
                    return;
            }
        }
        catch (std::exception const&)
        {
            BEAST_EXPECT(false);
            return;
        }

        BEAST_EXPECT(txMap.getHash().as_uint256() == info.txHash);
        if (txMap.getHash().as_uint256() != info.txHash)
            return;

        // create LedgerReplay object
        auto l_replay_temp = std::make_shared<Ledger>(
            info,
            history.env.app().config(),
            history.env.app().getNodeFamily());
        BEAST_EXPECT(l_replay_temp);
        if (!l_replay_temp)
            return;
        LedgerReplay replayData(parent, l_replay_temp, std::move(orderedTxns));

        // build ledger
        auto const l_replayed = buildLedger(
            replayData, tapNONE, history.env.app(), history.env.journal);
        BEAST_EXPECT(l_replayed);
        if (!l_replayed)
            return;

        // verify ledger, note that hash is calculated in Ledger::setImmutable()
        BEAST_EXPECT(l_replayed->info().hash == l->info().hash);
    }

    void
    testConfig()
    {
        testcase("ledger replay config test");
        {
            Config c;
            BEAST_EXPECT(c.LEDGER_REPLAY_ENABLE == false);
        }

        {
            Config c;
            std::string toLoad(R"rippleConfig(
[ledger_replay]
enable=1
)rippleConfig");
            c.loadFromString(toLoad);
            BEAST_EXPECT(c.LEDGER_REPLAY_ENABLE == true);
        }

        {
            Config c;
            std::string toLoad = (R"rippleConfig(
[ledger_replay]
enable=0
)rippleConfig");
            c.loadFromString(toLoad);
            BEAST_EXPECT(c.LEDGER_REPLAY_ENABLE == false);
        }
    }
#endif
    void
    run() override
    {
        testSimple();
        testMsgDrop(20);
        testOverlap();
        //        testLedgerReplayBuild();
        //        testLedgerDeltaReplayBuild();
        //        testLedgerDeltaReplayBuild(0);
        //        testConfig();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerReplay, app, ripple);
BEAST_DEFINE_TESTSUITE(LedgerForwardReplay, app, ripple);

}  // namespace test
}  // namespace ripple
