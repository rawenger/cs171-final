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
#include "fs_buf.h"

struct exit_t {};
using wait_t = std::chrono::seconds;

// TODO: Horrible legacy parse sum type.
struct balance_req_t { uint16_t client; uint16_t unused; };
struct request_t {
    enum : uint8_t {
        INVALID_REQUEST,
        BAL_REQUEST,
        TR_REQUEST
    } type;

    union {
        balance_req_t bal;
        transaction tr;
    };
};

using cmd_t = std::variant<request_t, exit_t, wait_t>;

static node_id_t my_id;
static std::string my_hostname;

static cmd_t parse_cmd(std::string_view &&msg);

// TODO: requires fmtlib 10.0, which has not been packaged by homebrew yet.
//  but this speeds up compilation times by a bit since less templates.
//std::string format_as(request_t req)
//{
//        return (req.type == request_t::BAL_REQUEST
//                 ? fmt::format("BALANCE {{.client = {}}}",
//                               (uint16_t) req.bal.client)
//                 : fmt::format("TRANSFER {{.recv = {}, .amt = {}}}",
//                               (uint16_t) req.tr.recv, (uint16_t) req.tr.amt));
//}

//struct test_serial {
//        size_t sn;
//        size_t pn;
//        uint8_t plbl;
//};
//
//std::string format_as(test_serial req)
//{
//        return fmt::format("{{{}, {}, {}}}", req.sn, req.pn, req.plbl);
//}

int main(int argc, char **argv)
{
        if (argc < 2) {
                fmt::print(stderr, "Usage: {} <node-id>\n", argv[0]);
                exit(EXIT_FAILURE);
        }

        if (argv[1][0] == 'P')
                my_id = atoi(argv[1]+1);
        else
                my_id = atoi(argv[1]);

        // fs_buf tests
//        if (my_id == 1) {
//                fs_buf<std::optional<test_serial>> test1 {1, "test1"};
//                test1[4] = {1,2,3};
//                test1[5] = {0,0,0};
//                test1[100] = {7,8,9};
//                test1[2] = {4,5,6};
//                test1[75] = {};
//                test1[5000] = {34, 35, 36};
//                std::this_thread::sleep_for(std::chrono::seconds{5}); // data will still be saved even if we ctrl-C here!
//        } else {
//                fs_buf<std::optional<test_serial>> test1 {1, "test1"}; // initialize data from disk backup
//                fmt::print("[2]: {}, [4]: {}, [5]: {}, [75]: {}, [100]: {}, [5000]: {}\n",
//                           *test1[2], *test1[4], *test1[5], !!test1[75], *test1[100], *test1[5000]);
//        }
//        return 0;

        #if 1
                /* Constructing this object will open and parse the config.csv file */
                cs171_cfg::system_cfg config{};

                paxos_node node{config, my_id, my_hostname};

                while (1) {
                        std::string in;
                        std::cout << "> ";
                        std::getline(std::cin, in);
                        if (in.empty())
                                continue;

                        cmd_t cmd = parse_cmd(in);

                        if (std::holds_alternative<exit_t>(cmd)) {
                                break;
                        }

                        else if (std::holds_alternative<wait_t>(cmd)) {
                                std::this_thread::sleep_for(std::get<wait_t>(cmd));
                                continue;
                        }

                        request_t req = std::get<request_t>(cmd);
                        if (req.type == request_t::INVALID_REQUEST) {
                                continue;
                        }

                        switch (req.type) {
                                case request_t::TR_REQUEST:
                                        node.propose(req.tr);
                                        break;
                        }
                }
        #endif
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
        arbitrator = -1;

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

                if (id < arbitrator)
                        arbitrator = id;

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
