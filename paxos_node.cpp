//
// Created by Ryan Wenger on 5/12/23.
//

#ifdef linux
#define _GNU_SOURCE
#define SOCK_EVENT_CLOSE        POLLRDHUP
#define SOCK_REVENT_CLOSE       POLLRDHUP
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
#include <cereal/archives/portable_binary.hpp>

#include "paxos_node.h"
#include "peer_connection.h"
#include "cs171_cfg.h"
#include "paxos_msg.h"
#include "sema_q.h"

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
                                    me->peers.at(pfd.fd)->client_id);
                                close(pfd.fd);
                                me->pmut.lock();
                                me->peers.erase(pfd.fd);
                                me->pmut.unlock();
                                if (me->leader && me->leader->sock == pfd.fd) {
                                        DBG("Leader is down!");
                                        std::thread{&paxos_node::start_election, me}.detach();
                                }
                        }
                        return pfd.revents & SOCK_REVENT_CLOSE;
                });

                for (auto pfd : client_fds) {
                        DBG("checking fd {}: events: {}\n", pfd.fd, pfd.revents);

                        if (!(pfd.revents & POLLIN))
                                continue;

                        uint16_t msg_size;
                        recv(pfd.fd, &msg_size, 2, 0);
                        msg_size = ntohs(msg_size);

                        std::string contents;
                        contents.resize(msg_size);
                        recv(pfd.fd, contents.data(), msg_size, 0);
                        me->handle_msg(pfd.fd, paxos_msg::decode_msg(contents));
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
paxos_node::paxos_node(const cs171_cfg::system_cfg &config, node_id_t my_id, std::string node_hostname)
:       my_id(my_id),
        my_hostname(std::move(node_hostname)),
        my_port(config.my_port)
{
        auto origin = std::make_tuple(0, my_id, 0);
        my_ballot_num = origin;
        latest_accepted_ballot = origin;

        auto listener = new std::thread{&paxos_node::listen_connections, this};
        listener->detach();

        if (my_id != config.arbitrator) {
                const auto &[id, port, hostname] = config.peers.front();
                assert(id == config.arbitrator);
                auto arb_addr = hostname_lookup(hostname, port);
                socket_t sock = socket(AF_INET, SOCK_STREAM, 0);

                if (connect(sock, arb_addr.get(), sizeof(sockaddr_in)) < 0) {
                        perror("Unable to connect to arbitrator node");
                        exit(EXIT_FAILURE);
                }
                DBG("Connected to peer PID {}", config.arbitrator);

                if (cs171_cfg::send_with_delay<false>(sock, &my_id, sizeof my_id, 0) < 0) {
                        perror("Unable to send PID to connection arbitrator");
                        exit(EXIT_FAILURE);
                }

                paxos_msg::MSG_TYPE im_new = paxos_msg::IM_NEW;
                cs171_cfg::send_with_delay<false>(sock, &im_new, sizeof im_new, 0);

                uint8_t n_peers_up;
                if (recv(sock, &n_peers_up, sizeof n_peers_up, 0) < 0) {
                        perror("Unable to recv peer network info");
                        exit(EXIT_FAILURE);
                }

                set_leader(new_peer(sock, config.arbitrator));
                my_state = ACCEPTOR;

                if (n_peers_up > 0) {
                        auto *peers_up = new uint8_t[n_peers_up];
                        if (recv(sock, peers_up, n_peers_up, 0) < 0)
                                perror("recv() peers_up");

                        // start listening for new connections from other nodes
                        // sorting allows us to do this in O(n log n) instead of O(n^2)
                        std::sort(peers_up, peers_up + n_peers_up);

                        int peer = 0;
                        for (const auto &[pid, pport, phostname] : config.peers) {
                                if (peers_up[peer] != pid)
                                        continue;

                                // TODO: check that the PID isn't already in peers map
                                //  not sure if this could be a problem, but make sure not
                                //  to check this->peers.contains(pid) since that will lookup
                                //  the socket file descriptor. Checking for pid is O(n).
                                connect_to(pid, pport, phostname);

                                if (++peer == n_peers_up)
                                        break;
                        }
                        delete[] peers_up;
                }
        } else {
                my_state = LEADER;
        }

        auto poller = new std::jthread{polling_loop, this};
        poller->detach();
}

void paxos_node::receive_prepare(socket_t proposer, const paxos_msg::prepare_msg &proposal)
{
       /*
        * if (balnum && m.balnum > balnum) {
        *      balnum = m.balnum;
        *      send(PROMISE, {balnum, accept_bal, accept_val})
        * else {
        *      send(PROMISE, None)
        * }
        */

        // If the proposal's ballot number is later than the one we've most recently accepted,
        // promise the proposer we will accept no earlier ballot's than theirs by fast-forwarding
        // our ballot number.

        if (proposal > my_ballot_num) {
                const auto time_of_proposal = std::get<0>(proposal);
                std::get<0>(my_ballot_num) = time_of_proposal;

                paxos_msg::promise_msg promise = {
                        .balnum = proposal,
                        .acceptnum = latest_accepted_ballot,
                        .acceptval = latest_accepted_value,
                };

                paxos_msg::msg msg = {
                        .type = paxos_msg::MSG_TYPE::PROMISE,
                        .prom = promise,
                };

                auto payload = paxos_msg::encode_msg(msg);

                cs171_cfg::send_with_delay(
                        proposer,
                        payload.c_str(), payload.size(),
                        0,
                        "Choked on a promise."
                );
        }
}

void paxos_node::receive_promises(const TimePoint &timeout_time)
{
        /* TODO: don't forget to timeout
         * ++responses;
         * promises.push_back(promise)
         * if (responses > NPEERS_IN_CONFIG_FILE / 2) {
         *      if (p.acceptval is None for all p in promises) {
         *              val2send = our_value;
         *      else {
         *              val2send = max_balnum(promises).value
         *      }
         *      schedule_accept_msg(balnum, val2send);
         * }
         */

        size_t n_responses = 0;
        constexpr size_t majority = 3 / 2 + 1; // TODO
        std::vector<paxos_msg::promise_msg> promises {};

        while (n_responses < majority) {
                auto maybe_prom = prom_q.try_pop_until(timeout_time);

                if (!maybe_prom) {
                        fmt::print("Timed out waiting for promises");
                        return;
                }

                // Only if the node is promising to join our most recent ballot. Otherwise, it's
                // an old promise.
                if (my_ballot_num == maybe_prom->balnum) {
                        promises.push_back(std::move(*maybe_prom));
                        ++n_responses;
                }
        }

        bool all_bottom = true;
        std::vector<paxos_msg::promise_msg> promises_with_value;

        for (const auto &promise : promises) {
                if (promise.acceptval) {
                        all_bottom = false;
                        promises_with_value.push_back(promise);
                }
        }

        paxos_msg::V chosen_value = proposed_val;

        if (!all_bottom) {
                // We have received at least one promise that is not bottom.
                assert(promises_with_value.size() > 0);

                auto maybe_accept_val = std::max_element(
                        promises_with_value.cbegin(), promises_with_value.cend(),
                        [](const paxos_msg::promise_msg &p1, const paxos_msg::promise_msg &p2) -> bool
                                { return p1.balnum > p2.balnum; }
                )->acceptval;

                // This promise should not be bottom.
                assert(maybe_accept_val.has_value());

                chosen_value = *maybe_accept_val;
        }

        // TODO
        // schedule_accept_msg(balnum, val2send);
}

void paxos_node::handle_msg(socket_t sender, paxos_msg::msg &&m)
{
        pmut.lock();
        DBG("Received {} message from peer P{}\n",
            paxos_msg::msg_types[m.type],
            peers.at(sender)->client_id);
        pmut.unlock();

        switch (m.type) {
                // TODO:  start everyone's ballot number at 1
            case paxos_msg::PREPARE: {
                receive_prepare(sender, m.prep);
                break;
            }

            case paxos_msg::PROMISE: {
                assert(my_state == LEADER);
                const TimePoint timeout_time = Clock::now() + std::chrono::seconds{5};
                // only call this once
                prom_q.push(m.prom);
                //receive_promises(timeout_time);
                break;
            }

            case paxos_msg::ACCEPT: {
                /*
                 * if (m.balnum > balnum) {
                 *      acceptnum = m.balnum;
                 *      acceptval = m.value;
                 *      write(backupfile, "accepting m.value");
                 *      send(ACCEPTED, balnum);
                 * }
                 */
                break;
            }

            case paxos_msg::ACCEPTED: {
                /*
                 * ++accepts;
                 * if (accepts > NPEERS_IN_CONFIG_FILE / 2) {
                 *      send(DECIDE, our_value);
                 *  }
                 */
                break;
            }

            case paxos_msg::DECIDE: {
                bool success = blockchain::BLOCKCHAIN.transfer(m.dec);
                if (success) {
                        fmt::print("Success!");
                } else {
                        fmt::print("Insufficient balance");
                }

                /*
                 * acceptval = None
                 * balnum.depth += success;
                 */
                break;
            }

            case paxos_msg::IM_NEW:
            case paxos_msg::HANDSHAKE_COMPLETE: {
                assert(false);
                break;
            }
        }
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

                node_id_t newid;
                recv(newsock, &newid, sizeof newid, 0);

                DBG("Accepted incoming connection from PID {}\n", newid);

                paxos_msg::MSG_TYPE action;
                recv(newsock, &action, sizeof action, 0);

                if (action == paxos_msg::IM_NEW) {
                        send_peer_list(newsock);
                } else {
                        assert(action == paxos_msg::HANDSHAKE_COMPLETE);
                }

                new_peer(newsock, newid);
        }
}

void paxos_node::send_peer_list(socket_t sock)
{
        // send a list of all the clients who are currently up and connected (listening)
        std::vector<uint8_t> peers_up{};
        pmut.lock();
        for (const auto &[_, peer] : peers) {
                peers_up.push_back(peer->client_id);
        }
        pmut.unlock();

        auto npeers = static_cast<uint8_t>(peers_up.size());

        cs171_cfg::send_with_delay(sock, &npeers, sizeof npeers, 0);

        if (npeers > 0)
                cs171_cfg::send_with_delay(sock, peers_up.data(), peers_up.size(), 0);
}

void paxos_node::connect_to(node_id_t id, int peer_port, const std::string &peer_hostname)
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

        cs171_cfg::send_with_delay(sock, &my_id, sizeof my_id, 0, "Unable to send ID to peer");
        paxos_msg::MSG_TYPE handshake = paxos_msg::HANDSHAKE_COMPLETE;
        cs171_cfg::send_with_delay(sock, &handshake, sizeof handshake, 0,
                                   "Unable to send handshake to peer");

        new_peer(sock, id);

        DBG("Connected to peer PID {}\n", id);
//        say(this, "Connected to peer ID 'P{}' on fd #{}\n", id, sock);
}

void paxos_node::broadcast(transaction t)
{
        paxos_msg::msg msg{.type = paxos_msg::PROMISE,
                        .prom = {
                                {1, my_id, 2},
                                {3, my_id, 4},
                                t
                        }
        };

        auto bytes = paxos_msg::encode_msg(msg);

        for (const auto &[sock, _] : this->peers) {
                cs171_cfg::send_with_delay(sock, bytes.c_str(), bytes.size(), 0,
                                           "Failed point-to-point of broadcast");
        }
}

peer_connection *paxos_node::new_peer(socket_t sock, node_id_t id)
{
        auto connection = std::make_unique<peer_connection>(sock, id);
        auto res = connection.get();
        pmut.lock();
        peers.emplace(sock, std::move(connection));
        update_pfds.test_and_set();
        pmut.unlock();

        return res; //NOLINT
}
