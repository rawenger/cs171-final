//
// Created by ryan on 4/10/23.
//

#ifdef linux
#define _GNU_SOURCE
#define SOCK_REVENT_CLOSE       POLLRDHUP
#define SOCK_EVENT_CLOSE        POLLRDHUP
#else
#define SOCK_EVENT_CLOSE        0
#define SOCK_REVENT_CLOSE       POLLHUP
#endif

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <fcntl.h>

#include <forward_list>
#include <cstring>
#include <memory>
#include <cassert>
#include <vector>
#include <stdatomic.h>

#include "peer_connection.h"
#include "debug.h"
#include "cs171_cfg.h"

#define say(lpm__, fmt__, args__...)    fmt::print("[P{} @ t_{}] " fmt__,\
                                        lpm__->my_id,                    \
                                        lpm__->my_time, ##args__)

lamport_mutex::lamport_mutex(std::string my_hostname,
                             int my_port,
                             int n_connections,
                             client_id_t client_id)
:   my_hostname(std::move(my_hostname)), my_id(client_id)
{
        if (n_connections == 0)
                return;

        peers.reserve(n_connections);

        socket_t in_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (in_sock < 0) {
                perror("Unable to create in-socket");
                exit(EXIT_FAILURE);
        }

        int opt = 1;
        if (setsockopt(in_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt) < 0) {
                perror("Unable to set socket options");
                exit(EXIT_FAILURE);
        }

        my_addr = hostname_lookup(this->my_hostname, my_port);

        if (bind(in_sock, my_addr.get(), sizeof(sockaddr_in)) < 0) {
                perror("Unable to bind socket to port");
                exit(EXIT_FAILURE);
        }

        if (listen(in_sock, n_connections) < 0) {
                perror("Unable to listen on incoming socket");
                exit(EXIT_FAILURE);
        }

        accept_sock = in_sock;
}

lamport_mutex::~lamport_mutex()
{
        if (accept_sock == -1)
                return;

        shutdown(accept_sock, SHUT_RDWR);
        close(accept_sock);
}

void lamport_mutex::connect_to(uint16_t id, int peer_port, const std::string &peer_hostname)
{
        /* connect to client P<id> at <hostname>:<port> */
        socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
                perror("Unable to create peer socket");
                exit(EXIT_FAILURE);
        }

        auto peer_addr = hostname_lookup(peer_hostname, peer_port);

        if (connect(sock, peer_addr.get(), sizeof(sockaddr_in)) < 0) {
                perror("Unable to connect to peer");
                exit(EXIT_FAILURE);
        }

        assert(!peers.contains(sock));
        peers.emplace(sock, peer_connection{sock, id});
        say(this, "Connected to peer ID 'P{}' on fd #{}\n", id, sock);
}

void lamport_mutex::accept_from(uint16_t id)
{
        /* accept connection from client P<id> into port */
        assert(accept_sock != -1);

        socklen_t addrlen = sizeof(sockaddr_in);

        socket_t sock = accept(accept_sock, my_addr.get(), &addrlen);
        if (sock < 0) {
                perror("Unable to accept incoming peer connection");
                exit(EXIT_FAILURE);
        }

        assert(!peers.contains(sock));
        peers.emplace(sock, peer_connection{sock, id});
        say(this, "Accepted connection from 'P{}' on fd #{}\n", id, sock);
}

/*void lamport_mutex::queue_worker()
{

}*/

/*
void lamport_mutex::socket_worker(std::stop_token stoken, lamport_mutex *me)
{
        std::vector<pollfd> client_fds {me->peers.size()};
        for (auto &[sock, _] : me->peers)
                client_fds.push_back(pollfd{.fd = sock,
                                            .events = POLLIN | SOCK_EVENT_CLOSE});

        while (!stoken.stop_requested()) {
                int n_events = poll(client_fds.data(), client_fds.size(), 5000);

                if (n_events < 0) {
                        perror("Unable to poll client sockets");
                        return;
                }

                if (n_events == 0)
                        continue;

                // remove peers who've closed their connection
                */
/*
                std::erase_if(client_fds, [me] (const pollfd &pfd) {
                        if (pfd.revents & SOCK_REVENT_CLOSE) {
                                say(me, "Peer P{} has been disconnected\n",
                                    me->peers.at(pfd.fd).client_id);
                                close(pfd.fd);
                        }
                        return pfd.revents & SOCK_REVENT_CLOSE;
                });
                *//*


                for (auto pfd : client_fds) {
                        DBG("checking fd {}: events: {}\n", pfd.fd, pfd.revents);

                        if (!(pfd.revents & POLLIN))
                                continue;

                        peer_msg msg {};
                        ssize_t status = recv(pfd.fd, &msg, sizeof msg, 0);
                        if (status < 0) {
                                perror("Unable to receive data from socket");
                                break;
                        }
                        assert(status == sizeof msg);

                        me->handle_msg(msg, pfd.fd);
                }
        }
}
*/

/*void lamport_mutex::server_worker(std::stop_token stoken, lamport_mutex *me)
{

}*/

/*
void lamport_mutex::handle_msg(lamport_mutex::peer_msg msg, socket_t insock)
{
        clockmut.lock();
        my_time = std::max(my_time, msg.time) + 1;
        peer_connection &sender = peers.at(insock);
        switch (msg.type) {
            case peer_msg::REQUEST: {
                say(this, "Received REQUEST from P{} sent at t_{}\n", sender.client_id, msg.time);
                ++my_time;
                sender.send(my_time, peer_msg::REPLY);
                break;
            }
            case peer_msg::REPLY: {
                say(this, "Received REPLY from P{} sent at t_{}\n", sender.client_id, msg.time);
                assert(!sender.waiting_response.empty());
                sender.waiting_response.front()->n_responses->arrive();
                sender.waiting_response.pop();
                break;
            }
            case peer_msg::RELEASE: {
                say(this, "Received RELEASE from P{} sent at t_{}\n", sender.client_id, msg.time);
                request_q.erase(request_q.begin());
                // check if front of request_q is from us and decrement the counter if so
                if (request_q.begin()->issuer == my_id)
                        request_q.begin()->n_responses->arrive();
                break;
            }
        }
        clockmut.unlock();

//        // fiixme: this works but fails to preserve the "happens before" relationship
//        ++my_time;
//        timestamp_t time, newtime;
//        do {
//                time = my_time.load(std::memory_order_relaxed);
//                newtime = std::max(time, msg.time) + 1;
//        } while (!my_time.compare_exchange_strong(time, newtime));

}
*/

/*server_reply_t lamport_mutex::issue_request(request_t req)
{
        clockmut.lock();

        auto time = ++my_time;
        auto [request, success] = request_q.emplace(req, time, my_id, peers.size());
        assert(success);

        say(this, "Broadcasting REQUEST to all peers\n");
        for (auto &[_, peer] : peers) {
                peer.send(my_time, peer_msg::REQUEST);
                 // TODO: make sure this actually gives the address of the object in the container
                 //  (and that this address won't change)
                peer.waiting_response.push(&*request);
        }

        clockmut.unlock();
}*/

void lamport_mutex::peer_connection::send(timestamp_t time, decltype(peer_msg::type) type) const
{
        peer_msg msg {time, type};
        pa2_cfg::send_with_delay(sock, &msg, sizeof msg, 0,
                                 "Unable to send message to peer");
}

/*void lamport_mutex::request_event_t::issue()
{

}*/
