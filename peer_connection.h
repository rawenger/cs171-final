//
// Created by ryan on 4/10/23.
//

#pragma once
#include <thread>
#include <mutex>
#include <semaphore>
#include <unordered_map>
#include <memory>
#include <queue>
#include <cstdint>
#include <latch>
#include <set>
#include <barrier>

#include "blockchain.h"
#include "request.h"
#include "sema_q.h"

using client_id_t = uint16_t;
using socket_t = int;

std::unique_ptr<sockaddr> hostname_lookup(const std::string &hostname, int port);

class lamport_mutex {
    using timestamp_t = uint64_t;

    /// type that gets sent between peers over the network
    struct peer_msg {
        timestamp_t time;
        enum : uint8_t {
            REQUEST,
            REPLY,
            RELEASE,
        } type;
    } __attribute__((packed));

    struct request_event_t {
        void issue();

        request_t req;
        timestamp_t time;
        client_id_t issuer;
        std::unique_ptr<std::barrier<>> n_responses;

        request_event_t(request_t req,
                        timestamp_t time,
                        client_id_t issuer,
                        size_t responses)
                : req(req), time(time), issuer(issuer),
                  n_responses(std::make_unique<decltype(n_responses)::element_type>(responses+1))
        { }

        bool operator<(const request_event_t &other) const
          { return (time != other.time) ? (time < other.time) : (issuer < other.issuer); }
    };

    struct peer_connection {
        peer_connection(socket_t sock, uint16_t id)
            : sock(sock), client_id(id)
            { }

        peer_connection(peer_connection &&other) = default;

        peer_connection(const peer_connection &other) = delete;
        peer_connection &operator=(const peer_connection &other) = delete;
        peer_connection &operator=(peer_connection &&other) = delete;

        ~peer_connection() { shutdown(sock, SHUT_RDWR); close(sock); }

        void send(timestamp_t time, decltype(peer_msg::type) type) const;

        socket_t sock {};
        uint16_t client_id {};
        std::queue<const request_event_t *> waiting_response {}; // TODO: switch to lock-free queue impl
    };

    socket_t accept_sock {-1};
    std::unique_ptr<sockaddr> my_addr {nullptr};
    std::unordered_map<socket_t, peer_connection> peers;
    std::string my_hostname;
    timestamp_t my_time {0}; // lamport local time
    std::mutex clockmut;
    client_id_t my_id {}; // client ID
//    std::priority_queue<std::pair<timestamp_t, client_id_t>> lock_queue;
    std::set<request_event_t> request_q;

    void handle_msg(lamport_mutex::peer_msg msg, socket_t insock);
    void queue_worker();
    static void socket_worker(std::stop_token stoken, lamport_mutex *me);
    static void server_worker(std::stop_token stoken, lamport_mutex *me);

public:
    lamport_mutex(std::string my_hostname, int my_port, int n_connections, uint16_t client_id);
    ~lamport_mutex();

    void connect_to(client_id_t id, int peer_port, const std::string &peer_hostname);
    void accept_from(client_id_t id);

    server_reply_t issue_request(request_t req);
};
