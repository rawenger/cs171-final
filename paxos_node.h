//
// Created by Ryan Wenger on 5/12/23.
//

#pragma once
#include <map>
#include <memory>
#include <future>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/queue.hpp>

//#include "peer_connection.h"
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

    ~peer_connection() { shutdown(sock, SHUT_RDWR); close(sock); }

//    void send(timestamp_t time, decltype(peer_msg::type) type) const;

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
    std::map<socket_t, peer_ptr> peers;
    std::atomic_flag update_pfds{false};
    size_t n_peers;

    node_id_t my_id;
    std::string my_hostname;
    int my_port;

    peer_connection *leader{nullptr};

    NODE_STATE my_state;

    paxos_msg::ballot_num balnum;
    std::vector<paxos_msg::V> log;
    fs_buf<paxos_msg::ballot_num> accept_bals;
//    std::map<size_t, paxos_msg::ballot_num> accept_bals {};
    fs_buf<std::optional<paxos_msg::V>> accept_vals;

    sema_q<std::tuple<cs171_cfg::socket_t, paxos_msg::promise_msg>> prom_q {};
    sema_q<std::tuple<cs171_cfg::socket_t, paxos_msg::accepted_msg>> acc_q {};
    boost::lockfree::queue<paxos_msg::V> request_q {64};
    std::mutex propose_mut;

    std::jthread polling_thread {};

    [[noreturn]] void listen_connections();
    void send_peer_list(socket_t sock);
    void connect_to(node_id_t id, int peer_port, const std::string &peer_hostname);

    peer_connection *new_peer(socket_t sock, node_id_t id);

    void handle_msg(socket_t sender, paxos_msg::msg &&m);

    void set_leader(peer_connection *new_leader)
      { leader = new_leader; }

    void receive_prepare(socket_t proposer, const paxos_msg::prepare_msg &proposal);

    void receive_accept(socket_t proposer, const paxos_msg::accept_msg &accept);

    void receive_decide(const paxos_msg::decide_msg &decision);

    void receive_promises(TimePoint timeout_time, paxos_msg::V propval,
                          std::promise<promise_promise> retval);

    void receive_accepteds(TimePoint timeout_time,
                           std::promise<bool> retval);

    std::future<promise_promise> broadcast_prepare(paxos_msg::V value);
    std::future<bool> broadcast_accept(paxos_msg::V value, const std::vector<cs171_cfg::socket_t> &targets);
    void broadcast_decision(paxos_msg::V value);

    auto say(const std::string &something) -> void;

public:
    paxos_node(const cs171_cfg::system_cfg &config, node_id_t my_id, std::string node_hostname);
    void propose(paxos_msg::V value);
    cs171_cfg::node_id_t peer_id_of(cs171_cfg::socket_t peer);
};
