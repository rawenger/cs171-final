//
// Created by ryan on 4/8/23.
//

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <iostream>
#include <cstring>
#include <vector>
#include <string_view>
#include <charconv>
#include <thread>
#include <variant>
#include <fstream>
#include <cassert>
#include <forward_list>

#include "debug.h"
#include "sema_q.h"
#include "cs171_cfg.h"
#include "paxos_node.h"
#include "cli_interface.h"

node_id_t my_id;
static std::string my_hostname;

cs171_cfg::system_cfg *config {nullptr};

#include "blag.h"

int main(int argc, char **argv)
{
        if (argc < 2) {
                fmt::print(stderr, "Usage: {} <node-id>\n", argv[0]);
                exit(EXIT_FAILURE);
        }

        if (argv[1][0] == 'P')
                my_id = atoi(argv[1]+1); //NOLINT(cert-err34-c)
        else
                my_id = atoi(argv[1]); //NOLINT(cert-err34-c)

        /* Constructing this object will open and parse the config.csv file */
        config = new cs171_cfg::system_cfg {};

        paxos_node node{my_id, my_hostname};

        while (true) {
                std::string in;
                std::cout << "> ";
                std::getline(std::cin, in);

                if (in.empty())
                        continue;

                // so we don't have to type crash() every time lmao
                if (in.starts_with("exit"))
                        break;

                std::optional<input> cmd = parse_input(in);

                if (!cmd) {
                        std::cerr << "Invalid input" << std::endl;
                        continue;
                }

                switch (cmd->tag) {
                    case input::KIND::CRASH:
                        goto exit;
                        break;
                    case input::KIND::FAIL_LINK: {
                        cs171_cfg::node_id_t dest = cmd->fail_link.dest;
                        if (node.fail_link(dest)) {
                                fmt::print("Successfully severed link with P{}\n", dest);
                        } else {
                                fmt::print(stderr, "Failed to sever link with P{}\n", dest);
                        }
                        break;
                    }
                    case input::KIND::FIX_LINK: {
                        cs171_cfg::node_id_t dest = cmd->fix_link.dest;
                        if (node.fix_link(dest)) {
                                fmt::print("Repaired link with P{}\n", dest);
                        } else {
                                fmt::print(stderr, "Unable to repair link to P{}\n", dest);
                        }
                        break;
                    }
                    case input::KIND::TRANSACTION: {
                        auto [dest, amt] = cmd->transaction;
                        cs171_cfg::node_id_t sender = node.id();

                        node.propose({amt, sender, dest});
                        break;
                    }
                    case input::KIND::BLOCKCHAIN:
                        fmt::print("{}\n", blockchain::BLOCKCHAIN.get_history());
                        break;
                    case input::KIND::QUEUE:
                        fmt::print("{}\n", node.dump_op_queue());
                        break;
                    case input::KIND::LOG:
                        fmt::print("{}\n", node.dump_log());
                        break;
                }
        }
exit:

        return 0;
}

cs171_cfg::system_cfg::system_cfg()
{
        /* line format is 'ID, hostname, port'
         * lines beginning with '#' are ignored
         */
        std::ifstream in {cs171_cfg::CLIENT_CFG};
        if (!in) {
                fmt::print(stderr, "Unable to open file '{}'\n", cs171_cfg::CLIENT_CFG);
                return;
        }

        n_peers = 0;

        while (!in.eof()) {
                if (in.peek() == '#') {
                        in.ignore(256, '\n');
                        continue;
                }

                in.ignore(); // skip the leading 'P' character of the client ID

                if (in.eof()) // handle newline at end of file
                        break;

                std::string hostname;
                int id, port;
                in >> id;
                in.ignore(256, ' ');
                std::getline(in, hostname, ',');
                in.ignore(256, ' ');
                in >> port;

                if (id == my_id) {
                        my_port = port;
                        my_hostname = hostname;
                        DBG("[ME] ");
                } else {
                        ++n_peers;
                }

                DBG("{{id: {}, host: {}, port: {}}}\n",
                    id, hostname, port);
                in.ignore(256, '\n');

                auto pos = std::lower_bound(peers.begin(), peers.end(), id,
                                            [] (const client_tuple &other, int id1) -> bool
                                            {
                                                return get<0>(other) < id1;
                                            });

                peers.emplace(pos, id, port, std::move(hostname));
        }
        in.close();

//        DBG("connect_to: [{}]\n", fmt::join(connect_to, ", "));
//        DBG("accept_from: [{}]\n", fmt::join(accept_from, ", "));
}

#if 0
/* This is disgusting. I hate string parsing.
 * This whole thing is way more repetitive than any of it has any right
 * to be; terrible spaghetti code.
 * Don't care, it is the bane of my existence.
 * If I think about it more I'll start having ideas for how to clean
 * it up, and I really don't want to spend my time doing that.
 */
static cmd_t parse_cmd(std::string_view &&msg)
{
        size_t wordlen, pos = 0;
        std::vector<std::string_view> words;
        do {
                wordlen = msg.find_first_of(' ', pos) - pos;
                words.push_back(msg.substr(pos, wordlen));
                pos += wordlen + 1;
        } while (wordlen < msg.length());
        DBG("words: {}\n", fmt::join(words, ", "));

        auto word = words.begin();
        request_t res = {.type = request_t::INVALID_REQUEST};

        if (*word == "Transfer") {
                res.type = request_t::TR_REQUEST;
        } else if (*word == "Balance") {
                res.type = request_t::BAL_REQUEST;
        } else if (*word == "exit") {
                return exit_t{};
        } else if (*word == "wait") {
                int wait;
                ++word;
                auto result = std::from_chars(word->data(), word->data() + word->size(), wait);
                if (result.ec == std::errc::invalid_argument) {
                        fmt::print(stderr, "Malformed input.\n");
                        return res;
                }
                return wait_t{wait};
        } else {
                fmt::print(stderr, "Malformed input.\n");
                return res;
        }

        ++word;
        if (word->starts_with('P')) {
                word->remove_prefix(1);
        } else {
                fmt::print(stderr, "Malformed input.\n");
                res.type = request_t::INVALID_REQUEST;
                return res;
        }
        uint16_t dst;
        auto result = std::from_chars(word->data(), word->data() + word->size(), dst);
        if (result.ec == std::errc::invalid_argument) {
                fmt::print(stderr, "Malformed input.\n");
                res.type = request_t::INVALID_REQUEST;
                return res;
        }
        res.bal.client = dst; // equivalent to setting res.bal.client = dst
        res.bal.unused = 0xFFFF;
        if (res.type == request_t::BAL_REQUEST)
                return res;

        ++word;
        if (word->starts_with('$')) {
                word->remove_prefix(1);
        } else {
                fmt::print(stderr, "Malformed input.\n");
                res.type = request_t::INVALID_REQUEST;
                return res;
        }
        uint16_t amt;
        result = std::from_chars(word->data(), word->data() + word->size(), amt);
        if (result.ec == std::errc::invalid_argument) {
                fmt::print(stderr, "Malformed input.\n");
                res.type = request_t::INVALID_REQUEST;
                return res;
        }
        res.tr = {
                .amt = amt,
                .sender = my_id,
                .receiver = dst,
        };
        return res;
}
#endif
