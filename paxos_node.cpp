//
// Created by Ryan Wenger on 5/12/23.
//

#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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

extern cs171_cfg::system_cfg *config;

template<>
struct fmt::formatter<paxos_msg::ballot_num> {
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
            return ctx.end();
    }

    template<typename FormatContext>
    auto format(const paxos_msg::ballot_num &ballot, FormatContext &ctx) const -> decltype(ctx.out()) {
            // ctx.out() is an output iterator to write to.
            return fmt::format_to(ctx.out(), "({}, {}, {})",
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

                // Do not handle poll events if we've "crashed."
                if (stoken.stop_requested()) {
                        break;
                }

                if (n_events < 0) {
                        perror("Unable to poll client sockets");
                        return;
                }

                if (n_events == 0)
                        continue;

                // remove peers who've closed their connection
                std::erase_if(client_fds, [me] (const pollfd &pfd) {
                        if (pfd.revents & SOCK_REVENT_CLOSE
                            || pfd.revents & POLLNVAL)
                        {
                                me->pmut.lock();
                                DBG("Peer P{} has been disconnected\n",
                                    me->peers.at(pfd.fd)->client_id);
                                auto lead = me->leader.load();
                                if (lead && lead->sock == pfd.fd) {
                                        DBG("Leader is down!\n");
                                        me->set_leader(nullptr);
                                }
                                close(pfd.fd);
                                me->peers.erase(pfd.fd);
                                me->update_pfds.test_and_set();
                                me->pmut.unlock();
                        }
                        return pfd.revents & SOCK_REVENT_CLOSE;
                });

                for (auto pfd : client_fds) {
//                        DBG("checking fd {}: events: {}\n", pfd.fd, pfd.revents);

                        if (!(pfd.revents & POLLIN))
                                continue;

                        paxos_msg::msg_size_t msg_size;
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
paxos_node::paxos_node(node_id_t my_id, std::string node_hostname)
:       my_id(my_id),
        my_hostname(std::move(node_hostname)),
        my_port(config->my_port),
        n_peers(config->n_peers),
        balnum("num"),
        accept_bals("bals"),
        accept_vals("vals"),
        log("log")
{
        accept_bals[1] = *balnum;

        std::thread{&paxos_node::listen_connections, this}.detach();

        for (const auto &peer : config->peers) {
                connect_to(peer);
        }

        // reconstruct the blockchain; we can assume
        //  that we don't have any holes in the log
        size_t i;
        for (i = 1; log[i].has_value(); i++) {
                commit(*log[i]);
        }
        first_uncommitted_slot = i;

        my_state = PREPARER;

        polling_thread = std::jthread{polling_loop, this};
        std::thread{&paxos_node::request_worker, this}.detach();
}

void paxos_node::propose(paxos_msg::V &&value)
{
        DBG("Proposing {}\n", value);
        request_q.push(std::forward<paxos_msg::V>(value));
}

bool paxos_node::fail_link(cs171_cfg::node_id_t peer_id)
{
        std::lock_guard<decltype(pmut)> lk{pmut};

        const peer_connection *lead = leader.load();

        if (lead && peer_id == lead->client_id)
                set_leader(nullptr);

        for (auto &&peer : peers) {
                if (peer.second->client_id == peer_id) {
                        peer.second->disconnect();
                        return true;
                }
        }

        return false;
}

bool paxos_node::fix_link(cs171_cfg::node_id_t peer_id)
{
        auto target =
                std::find_if(config->peers.cbegin(), config->peers.cend(),
                             [peer_id] (const auto &p) {return std::get<node_id_t>(p) == peer_id;});

        if (target == config->peers.cend())
                return false;

        return connect_to(*target);
}

std::string paxos_node::dump_op_queue()
{
        return fmt::format("queue: {}\n", request_q.format());
}

std::string paxos_node::dump_log() const
{
        std::string result;

        // NOTE: below doesn't work because we also want to include the slot #
        // return fmt::format("#{:2}: {}\n", fmt::join(log.cbegin(), log.cend(), ""));

        size_t slot = 1;
        for (auto it = log.cbegin();
             slot < balnum->slot_num && it != log.cend();
             ++slot, ++it)
        {
                result += fmt::format("#{:3}: {}\n", slot, *it);
        }

	return result;
}

/************************************************************************************
 *                              PRIVATE MEMBER FUNCTIONS
 ************************************************************************************/


void paxos_node::request_worker()
{
        while (true) {
                std::vector<cs171_cfg::socket_t> accept_targets{};
                paxos_msg::V value = request_q.front();

                auto lead = leader.load();
                if (lead) {
                        forward_msg(lead, value);
                        request_q.pop();
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
                        request_q.pop();
                        broadcast_decision(value);
                }
        }
}

std::vector<cs171_cfg::socket_t> paxos_node::broadcast_prepare(paxos_msg::V &value)
{
        // Increment the sequence number of our ballot. This is a fresh proposal.
        balnum->seq_num += 1;
        balnum->node_pid = my_id;

        paxos_msg::prepare_msg msg {*balnum};

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
                        "Choked on a PREPARE message"
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
                .balnum = *balnum,
                .value = value,
        };

        accept.balnum.node_pid = my_id;

        auto payload = paxos_msg::encode_msg(accept);

        pmut.lock();
        for (const auto &peer : targets) {
                if (!peers.contains(peer))
                        continue;

                // I don't care don't have time to learn how to implement the {fmt} API. Don't @ me.
                say(fmt::format("Broadcasting ACCEPT to P_{} with ballot {}.",
                                peer_id_of(peer),
                                accept.balnum));

                cs171_cfg::send_with_delay(
                        peer,
                        payload.c_str(), payload.size(),
                        0,
                        "Choked on an ACCEPT message"
                );
        }
        pmut.unlock();

        const TimePoint timeout_time = Clock::now() + timeout_interval;

        return receive_accepteds(timeout_time);
}

void paxos_node::broadcast_decision(const paxos_msg::V &value)
{
        paxos_msg::decide_msg msg {value, balnum->slot_num};

        std::string payload = paxos_msg::encode_msg(msg);

        log[balnum->slot_num] = value;
        commit(value);

        pmut.lock();

        // TODO: Need a function to inspect all slots of the log.

        for (const auto &[peer, _] : peers) {
                say(fmt::format("Sending DECIDE to {} with value {}.",
                        peer_id_of(peer), value));

                cs171_cfg::send_with_delay(
                        peer,
                        payload.c_str(), payload.size(),
                        0,
                        "Choked on a DECIDE message"
                );
        }

        pmut.unlock();

        if (first_uncommitted_slot == balnum->slot_num)
                ++first_uncommitted_slot;

        balnum->slot_num += 1;
}

void paxos_node::receive_prepare(socket_t proposer, const paxos_msg::prepare_msg &proposal)
{
        paxos_msg::ballot_num bal = proposal.balnum;

        say(fmt::format("Received PREPARE from P_{} with ballot {}",
                        peer_id_of(proposer),
                        bal));

        // If the proposal's ballot number is later than the one we've most recently accepted,
        // promise the proposer we will accept no earlier ballot's than theirs by fast-forwarding
        // our ballot number to theirs.

        // TODO: Not sure when it would be the case that the two are equal, even though we're
        //  defining the relation on less than or equal to.
        if (*balnum <= bal) {
                *balnum = bal;

                paxos_msg::promise_msg promise = {
                        .balnum = bal,
                        .acceptnum = accept_bals[bal.slot_num],
                        .acceptval = accept_vals[bal.slot_num],
                };

                paxos_msg::promise_msg msg{promise};

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
                        "Choked on a PROMISE message"
                );
//        } else if (my_state == LEARNER) {
        // NOTE: this second way is much less efficient BUT it fixes the case
        //  where we hang forever when we aren't connected to any leader
        } else if (first_uncommitted_slot > bal.slot_num) {
                issue_logresp(proposer, bal.slot_num);
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

        std::vector<cs171_cfg::socket_t> retvec {};

        size_t peers_for_majority = (n_peers + 1) / 2;

        while (n_responses < peers_for_majority) {
                auto maybe_prom = prom_q.try_pop_until(timeout_time);

                if (not maybe_prom.has_value()) {
                        fmt::print("Timed out waiting for promises\n");
                        return {};
                }

                const auto &[sender, promise] = maybe_prom.value();

                // Only if the node is promising to join our most recent ballot. Otherwise, it's
                // an old promise.
                if (*balnum != promise.balnum)
                        continue;

                say(fmt::format("Received PROMISE from P_{} to ballot {}. "
                                "Last accepted ballot is {} with value '{}'.",
                                peer_id_of(sender), promise.balnum,
                                promise.acceptnum, promise.acceptval));

                n_responses += 1;
                retvec.push_back(sender);
                if (promise.acceptval.has_value()) {
                        promises_with_value.push_back(promise);
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

                // We only care about this if the node is responding to our most recent ballot.
                // Otherwise, it's an old message.
                if (*balnum != msg.balnum)
                        continue;

                say(fmt::format("Received ACCEPTED from P_{} to ballot {}.",
                                peer_id_of(sender),
                                msg.balnum));

                n_responses++;
        }

        return true;
}

void paxos_node::receive_accept(socket_t proposer, const paxos_msg::accept_msg &accept)
{
        // I don't care don't have time to learn how to implement the {fmt} API. Don't @ me.
        say(fmt::format("Received ACCEPT from P_{} with ballot{}.",
                        peer_id_of(proposer),
                        accept.balnum));

        if (*balnum <= accept.balnum) {
                accept_bals[accept.balnum.slot_num] = accept.balnum;
                accept_vals[accept.balnum.slot_num] = accept.value;

                paxos_msg::accepted_msg msg {accept.balnum};

                auto payload = paxos_msg::encode_msg(msg);

                say(fmt::format("Sending ACCEPTED to P_{} with ballot {}.",
                                peer_id_of(proposer), accept.balnum));

                cs171_cfg::send_with_delay(
                        proposer,
                        payload.c_str(), payload.size(),
                        0,
                        "Choked on an ACCEPTED message"
                );
//        } else if (my_state == LEARNER) {
        // NOTE: this second way is much less efficient BUT it fixes the case
        //  where we hang forever when we aren't connected to any leader
        } else if (first_uncommitted_slot > accept.balnum.slot_num) {
                issue_logresp(proposer, accept.balnum.slot_num);
        }
}

void paxos_node::receive_decide(socket_t sender, const paxos_msg::decide_msg &decision)
{
        say(fmt::format("Received DECIDE from P{} for slot {} with value {}.",
                        peer_id_of(sender), decision.slotnum, decision.val));

        set_leader(peers[sender].get());

//        if (decision.slotnum > last_committed_slot)
//                last_committed_slot = decision.slotnum;

        // TODO: Don't commit immediately, write the value to a log -- there may be gaps we need to
        //  recover from. I don't believe this is part of the spec though.

        // Increase the slot number of our ballot number, which corresponds to the depth of
        // our blockchain. We have decided this slot number, and so our next proposal should
        // try to gain consensus on the next slot.
        if (decision.slotnum > first_uncommitted_slot) {
                // need to recover missing gaps in log
                if (!awaiting_logresp)
                        issue_logreq(sender, first_uncommitted_slot);
                // also need to handle case where we get another DECIDE while waiting
                // on a response to a LOGREQ: we don't want to issue another request
                // that would contain redundant data; we just set a flag here or
                // something that says "don't issue another LOGREQ if this flag is true,"
                // and write the new decision(s) to the appropriate slots.
        } else {
                assert(decision.slotnum == first_uncommitted_slot);

                commit(decision.val);
                first_uncommitted_slot++;
                balnum->slot_num++;
        }

        log[decision.slotnum] = decision.val;
}

void paxos_node::issue_logreq(cs171_cfg::socket_t dest, size_t slotnum)
{
        awaiting_logresp = true;

        say(fmt::format("Issuing LOGREQ to P_{} starting from slot {}.",
                        peer_id_of(dest), slotnum));

        paxos_msg::logreq_msg msg {slotnum};

        std::string payload = paxos_msg::encode_msg(msg);

        cs171_cfg::send_with_delay(
                dest,
                payload.c_str(), payload.size(),
                0,
                "Choked on a LOGREQ message"
        );
}

void paxos_node::issue_logresp(cs171_cfg::socket_t dest, size_t slotnum)
{
        std::forward_list<paxos_msg::V> vals {};
        auto tail = vals.before_begin();
        for (size_t i = slotnum; i < first_uncommitted_slot; i++)
                tail = vals.insert_after(tail, *log[i]);

        paxos_msg::logresp_msg response {
                .slot = slotnum,
                .vals = std::move(vals),
        };

        std::string payload = paxos_msg::encode_msg(response);

        say(fmt::format("Sending LOGRESP to P_{} for slots {} to {}.",
                                peer_id_of(dest), slotnum, balnum->slot_num-1));

        cs171_cfg::send_with_delay(
                dest,
                payload.c_str(), payload.size(),
                0,
                "Choked on a LOGRESP message"
        );
}

void paxos_node::receive_logreq(cs171_cfg::socket_t sender, const paxos_msg::logreq_msg &m)
{
        /*
         * We assume we are the leader here, since nodes will *only* send log requests
         * after receiving a decision from us.
         */

        say(fmt::format("Received LOGREQ from P_{} starting from slot {}.",
                                peer_id_of(sender), m.slot));

        assert(m.slot < balnum->slot_num); // YIKES

        issue_logresp(sender, m.slot);
}

void paxos_node::receive_logresp(const paxos_msg::logresp_msg &m)
{
        // TODO: how do we know our slot number is caught up if we receive a DECIDE while
        //  waiting for this response (after sending the request)?
        //  We probably can't assume the leader won't have sent another DECIDE while
        //  it was busy answering us since another node may have sent the DECIDE.
        //  Added the `latest_slot` field to the node class--maybe that will be helpful?
        //  ***
        //  NO WAIT: we don't need to know this; we just set our first uncommitted slot to the
        //  latest slot delivered in this message, and if any decisions have been issued in
        //  the intervening time, we don't process them until *after* we receive this response!

        say(fmt::format("Received LOGRESP starting from slot {}.",
                                m.slot));

        // commented out since we can also receive one of these in response to a proposal
//        assert(awaiting_logresp);


        // don't want to duplicate commit anything
        // note: we can't just put a `continue` in the for loop
        //  cause we may have actual log entries for future slots.
        //  we just
        size_t i = m.slot;
        auto start = m.vals.cbegin();
        while (log[i] && start != m.vals.cend()) {
                ++i;
                ++start;
        }

        for (; start != m.vals.cend(); ++start) {
                log[i++] = *start;
                commit(*start);
        }

        // TODO: double check with Jackson if this is safe
        balnum->slot_num = first_uncommitted_slot = i;
        awaiting_logresp = false;
}

// see https://en.cppreference.com/w/cpp/utility/variant/visit
template<class... Ts>
struct msg_handler : Ts... { using Ts::operator()...; };
template<class... Ts>
msg_handler(Ts...) -> msg_handler<Ts...>;

void paxos_node::handle_msg(socket_t sender, paxos_msg::msg &&m)
{
        pmut_guard lk {pmut};

        DBG("Received {} message from peer P{}\n",
            paxos_msg::msg_types[m.index()],
            peers.at(sender)->client_id);

        std::visit(msg_handler{
                [sender, this] (const paxos_msg::prepare_msg &prep) {
                        receive_prepare(sender, prep);
                },
                [sender, this] (const paxos_msg::promise_msg &prom) {
                        prom_q.push({sender, prom});
                },
                [sender, this] (const paxos_msg::accept_msg &acc) {
                        receive_accept(sender, acc);
                },
                [sender, this] (const paxos_msg::accepted_msg &accd) {
                        acc_q.push({sender, accd});
                },
                [sender, this] (const paxos_msg::decide_msg &dec) {
                        receive_decide(sender, dec);
                },
                [this] (const paxos_msg::fwd_msg &fwd) {
                        propose(std::move(fwd.val));
                },
                [sender, this] (const paxos_msg::logreq_msg &recreq) {
                    receive_logreq(sender, recreq);
                },
                [this] (const paxos_msg::logresp_msg &recresp) {
                    receive_logresp(recresp);
                }
        }, m);
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
                                 my_id, balnum->seq_num, balnum->node_pid, balnum->slot_num, readable_state, message)
                << std::endl;
}

cs171_cfg::node_id_t paxos_node::peer_id_of(cs171_cfg::socket_t peer)
{
        std::lock_guard<decltype(pmut)> lk{pmut};
        return peers.at(peer)->client_id;
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

        paxos_msg::msg m = paxos_msg::fwd_msg {val};
        std::string payload = paxos_msg::encode_msg(m);

        cs171_cfg::send_with_delay(dest->sock,
                                   payload.c_str(),
                                   payload.length(),
                                   0,
                                   "Unable to forward message to leader");
}

bool paxos_node::connect_to(const decltype(cs171_cfg::system_cfg::peers)::value_type &peer)
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
