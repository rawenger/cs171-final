//
// Created by Ryan Wenger on 5/12/23.
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

#include <cassert>

#include "paxos_node.h"
#include "peer_connection.h"
#include "pa2_cfg.h"

std::unique_ptr<sockaddr> hostname_lookup(const std::string &hostname, int port)
{
         addrinfo addr_hint = {
                .ai_family = AF_INET,
                .ai_socktype = SOCK_STREAM,
        };

        addrinfo *m_addrinfo;

        if (getaddrinfo(hostname.c_str(), nullptr, &addr_hint, &m_addrinfo) < 0
            || m_addrinfo == nullptr)
        {
                perror("getaddrinfo");
                exit(EXIT_FAILURE);
        }

        reinterpret_cast<sockaddr_in *>(m_addrinfo->ai_addr)->sin_port = htons(port);

        auto res = std::make_unique<sockaddr>(*m_addrinfo->ai_addr);

        freeaddrinfo(m_addrinfo);

        return res;
}

void paxos_node::polling_loop(std::stop_token stoken, paxos_node *me) //NOLINT
{
        std::vector<pollfd> client_fds {};

        while (!stoken.stop_requested()) {
                if (me->update_pfds.test()) {
                        client_fds.clear();
                        me->pmut.lock();
                        client_fds.reserve(me->peers.size());
                        for (auto &[sock, _] : me->peers)
                                client_fds.push_back(pollfd{.fd = sock,
                                                            .events = POLLIN | SOCK_EVENT_CLOSE});
                        me->update_pfds.clear();
                        me->pmut.unlock();
                }

                int n_events = poll(client_fds.data(), client_fds.size(), 5000);

                if (n_events < 0) {
                        perror("Unable to poll client sockets");
                        return;
                }

                if (n_events == 0)
                        continue;

                // remove peers who've closed their connection
                std::erase_if(client_fds, [me] (const pollfd &pfd) {
                        if (pfd.revents & SOCK_REVENT_CLOSE) {
                                DBG("Peer P{} has been disconnected\n",
                                    me->peers.at(pfd.fd).client_id);
                                close(pfd.fd);
                                me->pmut.lock();
                                me->peers.erase(pfd.fd);
                                me->pmut.unlock();
                        }
                        return pfd.revents & SOCK_REVENT_CLOSE;
                });

                for (auto pfd : client_fds) {
                        DBG("checking fd {}: events: {}\n", pfd.fd, pfd.revents);

                        if (!(pfd.revents & POLLIN))
                                continue;

                        /*
                        peer_msg msg {};
                        ssize_t status = recv(pfd.fd, &msg, sizeof msg, 0);
                        if (status < 0) {
                                perror("Unable to receive data from socket");
                                break;
                        }
                        assert(status == sizeof msg);

                        me->handle_msg(msg, pfd.fd);
                        */
                }
        }
}

/*
 * The way this works is one of the nodes (typically the one with the lowest PID)
 * will be delegated as the "connection arbitrator"--the one whom everyone else
 * queries to determine who is up already and who isn't yet. When a node comes
 * online, the first thing it does is connect to this arbitrator and send its
 * PID; the arbitrator then responds with a list of the PID's of all the other
 * nodes in the system who are currently up, and the new arrival connects to all of
 * these and sends its own PID to each one.
 *
 *  [P2 connects to P1, the arbitrator]:
 *      - P2 sends PID to P1
 *      - P2 says IM_NEW to P1
 *      - P2 recieves num_peers from P1
 *      - P2 receives peers_up from P1
 */
paxos_node::paxos_node(const pa2_cfg::system_cfg &config, client_id_t my_id, std::string node_hostname)
:       my_id(my_id),
        connection_arbitrator(config.arbitrator),
        my_hostname(std::move(node_hostname)),
        my_port(config.my_port)
{
        auto listener = new std::thread{&paxos_node::listen_connections, this};
        listener->detach();

        if (my_id != connection_arbitrator) {
                const auto &[id, port, hostname] = config.peers.front();
                assert(id == connection_arbitrator);
                auto arb_addr = hostname_lookup(hostname, port);
                socket_t sock = socket(AF_INET, SOCK_STREAM, 0);

                if (connect(sock, arb_addr.get(), sizeof(sockaddr_in)) < 0) {
                        perror("Unable to connect to arbitrator node");
                        exit(EXIT_FAILURE);
                }
                if (pa2_cfg::send_with_delay<false>(sock, &my_id, sizeof my_id, 0) < 0) {
                        perror("Unable to send PID to connection arbitrator");
                        exit(EXIT_FAILURE);
                }

                MSG_TYPE im_new = IM_NEW;
                pa2_cfg::send_with_delay<false>(sock, &im_new, sizeof im_new, 0);

                uint8_t n_peers_up;
                if (recv(sock, &n_peers_up, sizeof n_peers_up, 0) < 0) {
                        perror("Unable to read network info");
                        exit(EXIT_FAILURE);
                }

                auto *peers_up = new uint8_t[n_peers_up];
                recv(sock, peers_up, n_peers_up, 0);

                // start listening for new connections from other nodes
                // sorting allows us to do this in O(n log n) instead of O(n^2)
                std::sort(peers_up, peers_up + n_peers_up);

                int peer = 0;
                for (const auto &[pid, pport, phostname] : config.peers) {
                        if (peers_up[peer] != pid)
                                continue;

                        connect_to(pid, pport, phostname);

                        if (++peer == n_peers_up)
                                break;
                }

                delete[] peers_up;
        }

        auto poller = new std::jthread{polling_loop, this};
        poller->detach();
}

[[noreturn]]
void paxos_node::listen_connections()
{
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

        auto my_addr = hostname_lookup(my_hostname, my_port);
        socklen_t addrlen = sizeof(sockaddr_in);

        if (bind(in_sock, my_addr.get(), addrlen) < 0) {
                perror("Unable to bind socket to port");
                exit(EXIT_FAILURE);
        }

        if (listen(in_sock, MAX_PEERS) < 0) {
                perror("Unable to listen on incoming socket");
                exit(EXIT_FAILURE);
        }

        while (true) {
                socket_t newsock = accept(in_sock, my_addr.get(), &addrlen);
                if (newsock < 0) {
                        perror("Unable to accept incoming connection");
                        continue;
                }

                client_id_t newid;
                recv(newsock, &newid, sizeof newid, 0);

                DBG("Accepted incoming connection from PID {}\n", newid);

                MSG_TYPE action;
                recv(newsock, &action, sizeof action, 0);

                if (action == IM_NEW) {
                        send_peer_list(newsock);
                } else {
                        assert(action == HANDSHAKE_COMPLETE);
                }

                pmut.lock();
                peers.emplace(newsock, peer_connection{newsock, newid});
                update_pfds.test_and_set();
                pmut.unlock();
        }
}

void paxos_node::send_peer_list(socket_t sock)
{
        // send a list of all the clients who are currently up and connected (listening)
        std::vector<uint8_t> peers_up{};
        pmut.lock();
        for (const auto &[_, peer] : peers) {
                peers_up.push_back(peer.client_id);
        }
        pmut.unlock();

        auto npeers = static_cast<uint8_t>(peers_up.size());

        pa2_cfg::send_with_delay(sock, &npeers, sizeof npeers, 0);
        pa2_cfg::send_with_delay(sock, peers_up.data(), peers_up.size(), 0);
}

void paxos_node::connect_to(client_id_t id, int peer_port, const std::string &peer_hostname)
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

        pmut.lock();
        assert(!peers.contains(sock));
        peers.emplace(sock, peer_connection{sock, id});
        pmut.unlock();

        pa2_cfg::send_with_delay(sock, &my_id, sizeof my_id, 0, "Unable to send ID to peer");
        MSG_TYPE handshake = HANDSHAKE_COMPLETE;
        pa2_cfg::send_with_delay(sock, &handshake, sizeof handshake, 0,
                                 "Unable to send handshake to peer");

        DBG("Connected to peer PID {}\n", id);
//        say(this, "Connected to peer ID 'P{}' on fd #{}\n", id, sock);
}


