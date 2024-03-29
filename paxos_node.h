//
// Created by Ryan Wenger on 5/12/23.
//

#pragma once
#include <map>
#include <memory>
#include <future>
#include <queue>

#include "paxos_msg.h"
#include "blockchain.h"
#include "sema_q.h"
#include "fs_buf.h"
#include "cs171_cfg.h"

using cs171_cfg::socket_t, cs171_cfg::node_id_t;

std::unique_ptr<sockaddr> hostname_lookup(const std::string &hostname, int port);

struct peer_connection {
    peer_connection(socket_t sock, node_id_t id)
        : sock(sock), client_id(id)
        { }

    peer_connection(peer_connection &&other) = delete;
    peer_connection &operator=(peer_connection &&other) = delete;

    peer_connection(const peer_connection &other) = delete;
    peer_connection &operator=(const peer_connection &other) = delete;

    void disconnect() const
    { shutdown(sock, SHUT_RDWR); close(sock); }

    ~peer_connection() { disconnect(); }

    socket_t sock {};
    node_id_t client_id {};
};

class paxos_node {
    using peer_ptr = std::unique_ptr<peer_connection>;

    // type that needs to be returned by receive_promises
    using promise_promise = std::tuple<std::vector<cs171_cfg::socket_t>, paxos_msg::V>;

    // Every node is always an acceptor.
    enum NODE_STATE {
            PREPARER, // Consists of PREPARE and PROMISE messages. (Phase  I)
            LEARNER, // Consists of ACCEPT and ACCEPTED messages.  (Phase II)
            FOLLOWER, // Not a proposer.
    };

    static constexpr int MAX_PEERS = 100;
    static constexpr std::chrono::seconds timeout_interval {20};
    static void polling_loop(std::stop_token stoken, paxos_node *me);

    std::recursive_mutex pmut;
    using pmut_guard = std::lock_guard<decltype(pmut)>;

    std::map<socket_t, peer_ptr> peers;
    std::atomic_flag update_pfds {false};
    size_t n_peers;

    node_id_t my_id;
    std::string my_hostname;
    int my_port;

    // only modified in polling thread
    std::atomic<const peer_connection *> leader {nullptr};

    std::atomic<NODE_STATE> my_state;

    fs_buf<paxos_msg::ballot_num> balnum;
    fs_buf<std::optional<paxos_msg::V>> log;
    fs_buf<paxos_msg::ballot_num> accept_bals;
    fs_buf<std::optional<paxos_msg::V>> accept_vals;
    size_t first_uncommitted_slot {1}; // latest slot num we have received a DECISION for
    bool awaiting_logresp{false};

    sema_q<std::tuple<cs171_cfg::socket_t, paxos_msg::promise_msg>> prom_q {};
    sema_q<std::tuple<cs171_cfg::socket_t, paxos_msg::accepted_msg>> acc_q {};
    sema_q<paxos_msg::V, true> request_q;

    std::jthread polling_thread {};

    [[noreturn]] void listen_connections();

    peer_connection *new_peer(socket_t sock, node_id_t id);

    void request_worker();
    void handle_msg(socket_t sender, paxos_msg::msg &&m);

    // only called from polling thread
    void set_leader(const peer_connection *new_leader)
      { leader.store(new_leader); }

    // needs to take leader parameter since another thread might set it to NULL
    void forward_msg(const peer_connection *dest, const paxos_msg::V &val);

    void receive_prepare(socket_t proposer, const paxos_msg::prepare_msg &proposal);

    void receive_accept(socket_t proposer, const paxos_msg::accept_msg &accept);

    void receive_decide(socket_t sender, const paxos_msg::decide_msg &decision);

    std::vector<cs171_cfg::socket_t>
    receive_promises(TimePoint timeout_time, paxos_msg::V &propval);

    bool receive_accepteds(TimePoint timeout_time);

    std::vector<cs171_cfg::socket_t> broadcast_prepare(paxos_msg::V &value);
    bool broadcast_accept(const paxos_msg::V &value, const std::vector<cs171_cfg::socket_t> &targets);
    void broadcast_decision(const paxos_msg::V &value);

    void issue_logreq(cs171_cfg::socket_t dest, size_t slotnum);
    void issue_logresp(cs171_cfg::socket_t dest, size_t slotnum);
    void receive_logreq(cs171_cfg::socket_t sender, const paxos_msg::logreq_msg &m);
    void receive_logresp(const paxos_msg::logresp_msg &m);

    void say(std::string &&something) const;

    cs171_cfg::node_id_t peer_id_of(cs171_cfg::socket_t peer);

    bool has_connection_to(cs171_cfg::node_id_t id);
    bool connect_to(const decltype(cs171_cfg::system_cfg::peers)::value_type &peer);

    void commit(const paxos_msg::V &val) //NOLINT(readability-convert-member-functions-to-static)
    { if (blag::BLAG.transact(val)) blockchain::BLOCKCHAIN.transact(val); }

public:
    paxos_node(node_id_t my_id, std::string node_hostname);
    void propose(paxos_msg::V value);

    cs171_cfg::node_id_t id() const { return my_id; } //NOLINT(modernize-use-nodiscard)

    bool fail_link(cs171_cfg::node_id_t peer_id);
    bool fix_link(cs171_cfg::node_id_t peer_id);
    std::string dump_op_queue();
    std::string dump_log() const; //NOLINT(modernize-use-nodiscard)
    std::string dump_ballot() const;
};
