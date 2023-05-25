//
// Created by Ryan Wenger on 5/12/23.
//

#pragma once
#include <map>
#include <memory>

//#include "peer_connection.h"
#include "paxos_msg.h"
#include "blockchain.h"
#include "request.h"
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

    // Every node is always an acceptor.
    enum NODE_STATE {
            PREPARER, // Consists of PREPARE and PROMISE messages. (Phase  I)
            PROPOSER, // Consits of ACCEPT and ACCEPTED messages.  (Phase II)
            LISTENER, // Consists of DECIDE messages.
            FOLLOWER, // Not a proposer.
    };

    static constexpr int MAX_PEERS = 100;
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
    fs_buf<std::optional<paxos_msg::V>> accept_vals;

    std::vector<std::tuple<cs171_cfg::socket_t, paxos_msg::promise_msg>> promises;
    std::vector<paxos_msg::promise_msg> promises_with_value;
    std::vector<paxos_msg::accepted_msg> accepteds;

    std::optional<paxos_msg::V> proposed_val; // What value has the client proposed?

    [[noreturn]] void listen_connections();
    void send_peer_list(socket_t sock);
    void connect_to(node_id_t id, int peer_port, const std::string &peer_hostname);

    peer_connection *new_peer(socket_t sock, node_id_t id);

    void handle_msg(socket_t sender, paxos_msg::msg &&m);

    void set_leader(peer_connection *new_leader)
      { leader = new_leader; }

    void start_election()
      { leader = nullptr; }

    void receive_prepare(socket_t proposer, const paxos_msg::prepare_msg &proposal);
    void receive_promise(cs171_cfg::socket_t sender, const paxos_msg::promise_msg &promise);
    void receive_accept(socket_t proposer, const paxos_msg::accept_msg &accept);
    void receive_accepted(cs171_cfg::socket_t sender, const paxos_msg::accepted_msg &accepted);
    void receive_decide(const paxos_msg::decide_msg &decision);

public:

    paxos_node(const cs171_cfg::system_cfg &config, node_id_t my_id, std::string node_hostname);
};

