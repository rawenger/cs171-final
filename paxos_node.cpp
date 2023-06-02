//
// Created by Ryan Wenger on 5/12/23.
//

#ifdef __linux__
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

#include <iostream>
#include <cassert>
#include <cereal/archives/portable_binary.hpp>

#include <fmt/core.h>

#include "paxos_msg.h"
#include "paxos_node.h"
#include "cs171_cfg.h"
#include "sema_q.h"
#include "debug.h"

template<>
struct fmt::formatter<paxos_msg::ballot_num> {
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
            return ctx.end();
    }

    template<typename FormatContext>
    auto format(const paxos_msg::ballot_num &ballot, FormatContext &ctx) const -> decltype(ctx.out()) {
            // ctx.out() is an output iterator to write to.
            return fmt::format_to(ctx.out(), "({})",
                                  ballot.seq_num, ballot.node_pid, ballot.slot_num);
    }
};

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
        DBG("Node P{} up and ready for paxos!\n", me->my_id);

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
                                auto lead = me->leader.load();
                                if (lead && lead->sock == pfd.fd) {
                                        DBG("Leader is down!\n");
                                        me->set_leader(nullptr);
                                }
                                close(pfd.fd);
                                me->pmut.lock();
                                me->peers.erase(pfd.fd);
                                me->pmut.unlock();
                        }
                        return pfd.revents & SOCK_REVENT_CLOSE;
                });

                for (auto pfd : client_fds) {
//                        DBG("checking fd {}: events: {}\n", pfd.fd, pfd.revents);

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
        config(config),
        my_port(config.my_port),
        n_peers(config.n_peers),
        balnum(0, my_id, 1),
        accept_bals(my_id, "bals"),
        accept_vals(my_id, "vals")
{
        accept_bals[1] = balnum;

        std::thread{&paxos_node::listen_connections, this}.detach();

        for (const auto &peer : config.peers) {
                connect_to(peer);
        }

        my_state = PREPARER;

        polling_thread = std::jthread{polling_loop, this};
}

void paxos_node::propose(paxos_msg::V value)
{
        DBG("Proposing {}\n", value);
        if (!request_q.bounded_push(value)) {
                DBG("uh-oh, push failed oopsie\n");
                exit(EXIT_FAILURE);
        }

        // TODO: do we need to put the operation back on the queue if we don't reach a DECIDE on it?
        std::thread{[this] () -> void {
                // wait till the previous operation is done
                std::lock_guard<decltype(propose_mut)> lk1{propose_mut};

                std::vector<cs171_cfg::socket_t> accept_targets{};
                paxos_msg::V value;
                if (!request_q.pop(value)) {
                        DBG("ub-oh #2, pop failed\n");
                        exit(EXIT_FAILURE);
                }

                // TODO: jackson pls double check this logic
                auto lead = leader.load();
                if (lead) {
                        forward_msg(lead, value);
                        return;
                }

                if (my_state == LEARNER) { // I am the leader
                        std::lock_guard<decltype(pmut)> lk2{pmut};
                        for (const auto &[sock, _] : peers) {
                                accept_targets.push_back(sock);
                        }
                } else { // I am not the leader, but I don't know who is
                        accept_targets = broadcast_prepare(value); // treats `value` as out pointer
                        if (!accept_targets.empty()) my_state = LEARNER;
                }

                if (!accept_targets.empty() && broadcast_accept(value, accept_targets)) {
                        broadcast_decision(value);
                }
        }}.detach();
}

bool paxos_node::fail_link(cs171_cfg::node_id_t peer_id)
{
        std::lock_guard<decltype(pmut)> lk{pmut};
        return std::erase_if(peers, [peer_id] (auto &&peer) {
                return peer.second->client_id == peer_id;
        }) > 0;
}

bool paxos_node::fix_link(cs171_cfg::node_id_t peer_id)
{
        auto target =
                std::find_if(config.peers.cbegin(), config.peers.cend(),
                             [peer_id] (const auto &p) {return std::get<1>(p) == peer_id;});

        if (target == config.peers.cend())
                return false;

        return connect_to(*target);
}

std::string paxos_node::dump_op_queue() /* const */
{
        // TODO: Seriously the only way I can think of implementing this is to derive the lock-free
        //  queue class and add a (thread-unsafe) iterator over the nodes.
        //  Or add a copy constructor. ugh.
        return "dump_op_queue() STUB";
}

std::string paxos_node::dump_log() const
{
        return "dump_log() STUB";
}

/************************************************************************************
 *                              PRIVATE MEMBER FUNCTIONS
 ************************************************************************************/

std::vector<cs171_cfg::socket_t> paxos_node::broadcast_prepare(paxos_msg::V &value)
{
        // Increment the sequence number of our ballot. This is a fresh proposal.
        balnum.seq_num += 1;
        balnum.node_pid = my_id;

        paxos_msg::prepare_msg prepare = balnum;

        paxos_msg::msg msg = {
                .type = paxos_msg::MSG_TYPE::PREPARE,
                .prep = prepare,
        };

        auto payload = paxos_msg::encode_msg(msg);
        pmut.lock();
        for (const auto &[peer, _] : peers) {
                say(fmt::format("Broadcasting PREPARE with value {} to P_{}.",
                        value,
                        peer_id_of(peer)));

                cs171_cfg::send_with_delay(
                        peer,
                        payload.c_str(), payload.size(),
                        0,
                        "Choked on broadcasting a PREPARE message."
                );
        }
        pmut.unlock();

        const TimePoint timeout_time = Clock::now() + timeout_interval;

        return receive_promises(timeout_time, value);
}

bool paxos_node::broadcast_accept(
        const paxos_msg::V &value,
        const std::vector<cs171_cfg::socket_t> &targets)
{
        paxos_msg::accept_msg accept = {
                .balnum = balnum,
                .value = value,
        };

        accept.balnum.node_pid = my_id;

        paxos_msg::msg msg = {
                .type = paxos_msg::MSG_TYPE::ACCEPT,
                .acc = accept,
        };

        auto payload = paxos_msg::encode_msg(msg);

        pmut.lock();
        for (const auto &peer : targets) {
                // I don't care don't have time to learn how to implement the {fmt} API. Don't @ me.
                say(fmt::format("Broadcasting ACCEPT to P_{} with ballot {}.",
                                peer_id_of(peer),
                                accept.balnum));

                cs171_cfg::send_with_delay(
                        peer,
                        payload.c_str(), payload.size(),
                        0,
                        "Choked on broadcasting a ACCEPT message."
                );
        }
        pmut.unlock();

        const TimePoint timeout_time = Clock::now() + timeout_interval;

        return receive_accepteds(timeout_time);
}

void paxos_node::broadcast_decision(const paxos_msg::V &value)
{
        paxos_msg::msg msg = {
                .type = paxos_msg::MSG_TYPE::DECIDE,
                .dec = value,
        };

        auto payload = paxos_msg::encode_msg(msg);

        pmut.lock();

        // TODO: Need a function to inspect all slots of the log.

        for (const auto &[peer, _] : peers) {
                say(fmt::format("Sending DECIDE to {} with value {}.",
                        peer_id_of(peer), value));

                cs171_cfg::send_with_delay(
                        peer,
                        payload.c_str(), payload.size(),
                        0,
                        "Choked on a DECIDE message."
                );
        }

        pmut.unlock();

        // TODO: Proposer must write to log.
        blockchain::BLOCKCHAIN.transfer(value);

        balnum.slot_num += 1;
}

void paxos_node::receive_prepare(socket_t proposer, const paxos_msg::prepare_msg &proposal)
{
        say(fmt::format("Received PREPARE from P_{} with ballot {}",
                        peer_id_of(proposer),
                        proposal));

        // If the proposal's ballot number is later than the one we've most recently accepted,
        // promise the proposer we will accept no earlier ballot's than theirs by fast-forwarding
        // our ballot number to theirs.

        // TODO: Not sure when it would be the case that the two are equal, even though we're
        //  defining the relation on less than or equal to.
        if (balnum <= proposal) {
                balnum = proposal;

                paxos_msg::promise_msg promise = {
                        .balnum = proposal,
                        .acceptnum = accept_bals[proposal.slot_num],
                        .acceptval = accept_vals[proposal.slot_num],
                };

                paxos_msg::msg msg = {
                        .type = paxos_msg::MSG_TYPE::PROMISE,
                        .prom = promise,
                };

                auto payload = paxos_msg::encode_msg(msg);

                // I don't care don't have time to learn how to implement the {fmt} API. Don't @ me.
                say(fmt::format("Sending PROMISE to P_{} with ballot {}. Last accepted ballot is {} with value '{}'.",
                                peer_id_of(proposer),
                                promise.balnum,
                                promise.acceptnum,
                                promise.acceptval));

                cs171_cfg::send_with_delay(
                        proposer,
                        payload.c_str(), payload.size(),
                        0,
                        "Choked on a PROMISE message."
                );
        }
}

std::vector<cs171_cfg::socket_t>
paxos_node::receive_promises(const TimePoint timeout_time, paxos_msg::V &propval)
{
        /*
         * Return a) We timed out
         *        b) We are keeping the same value that we initially proposed
         *        c) We are using a value that was sent to us in a promise message
         */
        size_t n_responses = 0;

        std::vector<paxos_msg::promise_msg> promises_with_value {};

//        promise_promise returnval {{}, {propval}};

        std::vector<cs171_cfg::socket_t> retvec {};

        size_t peers_for_majority = (n_peers + 1) / 2;

        while (n_responses < peers_for_majority) {
                auto maybe_prom = prom_q.try_pop_until(timeout_time);

                if (not maybe_prom.has_value()) {
                        fmt::print("Timed out waiting for promises\n");
                        // set our future to None
//                        std::get<0>(returnval).clear();
//                        retval.set_value_at_thread_exit(std::move(returnval));
                        return {};
                }

                const auto &[sender, promise] = maybe_prom.value();

                say(fmt::format("Received PROMISE from P_{} to ballot {}. "
                                "Last accepted ballot is {} with value '{}'.",
                                peer_id_of(sender), promise.balnum,
                                promise.acceptnum, promise.acceptval));

                // Only if the node is promising to join our most recent ballot. Otherwise, it's
                // an old promise.
                if (balnum == promise.balnum) {
                        n_responses += 1;
                        retvec.push_back(sender);
                        if (promise.acceptval.has_value()) {
                                promises_with_value.push_back(promise);
                        }
                }
        }

        // We have received at least one promise that is not bottom.
        if (!promises_with_value.empty()) {
                propval =
                        std::max_element(promises_with_value.cbegin(), promises_with_value.cend(),
                        [](const auto &p1, const auto &p2) -> bool
                                { return p1.balnum > p2.balnum; }
                )->acceptval.value();
        }

//        retval.set_value_at_thread_exit(std::move(returnval));
        return retvec;
}

bool paxos_node::receive_accepteds(const TimePoint timeout_time)
{
        size_t n_responses = 0;

        std::vector<paxos_msg::accepted_msg> accepted {};

        const size_t peers_for_majority = (n_peers + 1) / 2;

        while (n_responses < peers_for_majority) {
                auto response = acc_q.try_pop_until(timeout_time);
                if (!response) {
                        fmt::print("Timed out waiting for accepteds\n");
                        return false;
                }

                const auto &[sender, msg] = response.value();

                say(fmt::format("Received ACCEPTED from P_{} to ballot {}.",
                                peer_id_of(sender),
                                msg));

                n_responses += (balnum == msg);
        }

        return true;
}

void paxos_node::receive_accept(socket_t proposer, const paxos_msg::accept_msg &accept)
{
        // I don't care don't have time to learn how to implement the {fmt} API. Don't @ me.
        say(fmt::format("Received ACCEPT from P_{} with ballot{}.",
                        peer_id_of(proposer),
                        accept.balnum));

        if (balnum <= accept.balnum) {
                accept_bals[accept.balnum.slot_num] = accept.balnum;
                accept_vals[accept.balnum.slot_num] = accept.value;

                paxos_msg::accepted_msg accepted = accept.balnum;

                paxos_msg::msg msg = {
                        .type = paxos_msg::MSG_TYPE::ACCEPTED,
                        .accd = accepted,
                };

                auto payload = paxos_msg::encode_msg(msg);

                // I don't care don't have time to learn how to implement the {fmt} API. Don't @ me.
                say(fmt::format("Sending ACCEPTED to P_{} with ballot {}.",
                                peer_id_of(proposer), accepted));

                cs171_cfg::send_with_delay(
                        proposer,
                        payload.c_str(), payload.size(),
                        0,
                        "Choked on an ACCEPTED message."
                );
        }
}

// TODO: also print who sent the decide
void paxos_node::receive_decide(const paxos_msg::decide_msg &decision)
{
        // I don't care don't have time to learn how to implement the {fmt} API. Don't @ me.
        say(fmt::format("Received DECIDE with value {}.", decision));

        // TODO: Don't commit immediately, write the value to a log -- there may be gaps we need to
        //  recover from. I don't believe this is part of the spec though.

        bool success = blockchain::BLOCKCHAIN.transfer(decision);

        if (success) {
                fmt::print("Success!\n");
        } else {
                fmt::print("Insufficient balance.\n");
        }

        // Increase the slot number of our ballot number, which corresponds to the depth of
        // our blockchain. We have decided this slot number, and so our next proposal should
        // try to gain concensus on the next slot.
        ++balnum.slot_num;
        accept_bals[balnum.slot_num] = {0, my_id, balnum.slot_num};
}

void paxos_node::handle_msg(socket_t sender, paxos_msg::msg &&m)
{
        pmut.lock();
        DBG("Received {} message from peer P{}\n",
            paxos_msg::msg_types[m.type],
            peers.at(sender)->client_id);
        pmut.unlock();

        switch (m.type) {
            case paxos_msg::PREPARE: {
                receive_prepare(sender, m.prep);
                break;
            }

            case paxos_msg::PROMISE: {
                prom_q.push({sender, m.prom});
                break;
            }

            case paxos_msg::ACCEPT: {
                receive_accept(sender, m.acc);
                break;
            }

            case paxos_msg::ACCEPTED: {
                acc_q.push({sender, m.accd});
                break;
            }

            case paxos_msg::DECIDE: {
                pmut.lock();
                set_leader(peers[sender].get());
                pmut.unlock();
                receive_decide(m.dec);
                break;
            }

            case paxos_msg::FWD_VAL: {
                propose(m.fwd);
                break;
            }

            case paxos_msg::DUPLICATE:
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

                pmut.lock(); // block any more outgoing connections from happening

                node_id_t newid;
                recv(newsock, &newid, sizeof newid, 0);

                paxos_msg::MSG_TYPE action =
                        has_connection_to(newid) ? paxos_msg::DUPLICATE : paxos_msg::HANDSHAKE_COMPLETE;

                DBG("Accepted incoming {}connection from PID {}\n",
                    action == paxos_msg::DUPLICATE ? "(duplicate) " : "",
                    newid);

                cs171_cfg::send_with_delay<false>(newsock, &action, sizeof action, 0);

                if (action == paxos_msg::DUPLICATE)
                        close(newsock);
                else
                        new_peer(newsock, newid);

                pmut.unlock(); // MUST be dropped AFTER we add the new peer to our peerlist
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

void paxos_node::say(std::string &&message) const
{
        std::string readable_state;
        switch (my_state) {
                case PREPARER:
                        readable_state = "PREPARER";
                        break;
                case LEARNER:
                        readable_state = "LEARNER";
                        break;
                case FOLLOWER:
                        readable_state = "FOLLOWER";
                        break;
        }

        std::cout << fmt::format("P_{} ({}, {}, {}) <{}>: {}",
                                 my_id, balnum.seq_num, balnum.node_pid, balnum.slot_num, readable_state, message)
                << std::endl;
}


cs171_cfg::node_id_t paxos_node::peer_id_of(cs171_cfg::socket_t peer)
{
        std::lock_guard<decltype(pmut)> lk{pmut};
        cs171_cfg::socket_t id = peers.at(peer)->client_id;

        return id;
}

bool paxos_node::has_connection_to(cs171_cfg::node_id_t id)
{
        std::lock_guard<decltype(pmut)> lk{pmut};

        for (const auto &[_, peer] : peers)
                if (id == peer->client_id)
                        return true;
        return false;
}

void paxos_node::forward_msg(const peer_connection *dest, const paxos_msg::V &val)
{
        DBG("Forwarding message '{}' to P{}\n", val, dest->client_id);

        std::string payload = paxos_msg::encode_msg({.type = paxos_msg::FWD_VAL, .fwd = val});

        cs171_cfg::send_with_delay(dest->sock,
                                   payload.c_str(),
                                   payload.length(),
                                   0,
                                   "Unable to forward message to leader");
}

bool paxos_node::connect_to(const decltype(config.peers)::value_type &peer)
{
        const auto &[id, port, hostname] = peer;
        if (id == my_id || has_connection_to(id))
                return false;

        auto addr = hostname_lookup(hostname, port);
        socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
        pmut.lock(); // make sure that we are allowed to make a new outgoing connection
        if (connect(sock, addr.get(), sizeof(sockaddr_in)) < 0) {
                close(sock);
                pmut.unlock();
                return false;
        }
        new_peer(sock, id); // need this up here so listener thread can learn of duplicates
        pmut.unlock();

        fmt::print("Connected to peer P{}\n", id);

        if (cs171_cfg::send_with_delay<false>(sock, &my_id, sizeof my_id, 0) < 0) {
                perror("Unable to send PID to new peer");
                exit(EXIT_FAILURE);
        }

        paxos_msg::MSG_TYPE handshake;
        if (recv(sock, &handshake, sizeof handshake, 0) < 0) {
                perror("Unable to recv connection handshake status");
                exit(EXIT_FAILURE);
        }

        // the peer we've just connected to informs us we already have an existing connection to them
        if (handshake == paxos_msg::DUPLICATE) {
                pmut.lock();
                peers.erase(sock);
                pmut.unlock();
                return false;
        }

        return true;
}
