//
// Created by Ryan Wenger on 5/12/23.
//

#pragma once
#include <map>
#include <memory>

//#include "peer_connection.h"
#include "blockchain.h"
#include "request.h"
#include "sema_q.h"
#include "pa2_cfg.h"

using pa2_cfg::socket_t, pa2_cfg::client_id_t;

std::unique_ptr<sockaddr> hostname_lookup(const std::string &hostname, int port);

struct peer_connection {
    peer_connection(socket_t sock, client_id_t id)
        : sock(sock), client_id(id)
        { }

    peer_connection(peer_connection &&other) = default;

    peer_connection(const peer_connection &other) = delete;
    peer_connection &operator=(const peer_connection &other) = delete;
    peer_connection &operator=(peer_connection &&other) = delete;

//    ~peer_connection() { shutdown(sock, SHUT_RDWR); close(sock); }

//    void send(timestamp_t time, decltype(peer_msg::type) type) const;

    socket_t sock {};
    client_id_t client_id {};
};

class paxos_node {
    enum MSG_TYPE : uint8_t {
            IM_NEW, // ask for a listing of other nodes in the network
            HANDSHAKE_COMPLETE,
            /* other paxos message types here */
    };

    enum NODE_STATE {
            /* acceptor, leader, etc */
    };

    static constexpr int MAX_PEERS = 100;
    static void polling_loop(std::stop_token stoken, paxos_node *me);

    std::recursive_mutex pmut;
    std::map<socket_t, peer_connection> peers;
    client_id_t my_id;
    client_id_t connection_arbitrator;
    std::string my_hostname;
    int my_port;
    std::atomic_flag update_pfds{true}; // no atomic we die like men (but actually we don't need one)

    [[noreturn]] void listen_connections();
    void send_peer_list(socket_t sock);
    void connect_to(client_id_t id, int peer_port, const std::string &peer_hostname);

public:

    paxos_node(const pa2_cfg::system_cfg &config, client_id_t my_id, std::string node_hostname);
};

