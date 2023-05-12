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
#include <latch>
#include <cassert>
#include <forward_list>
#include <set>

#include "peer_connection.h"
#include "request.h"
#include "debug.h"
#include "sema_q.h"
#include "pa2_cfg.h"

struct exit_t {};
using wait_t = std::chrono::seconds;

using cmd_t = std::variant<request_t, exit_t, wait_t>;

static client_id_t my_id;
static std::string my_hostname;
static std::unique_ptr<lamport_mutex> lpmut;

static cmd_t parse_cmd(std::string_view &&msg);

struct client_cfg {
        using client_tuple = std::tuple<client_id_t, int, std::string>;
        std::vector<client_id_t> accept_from;
        std::vector<client_tuple> connect_to;
        size_t n_peers;
        int my_port;

        client_cfg();
};

static void build_connections(int serv_sock);
static void connect_server(int serv_sock);

int main(int argc, char **argv)
{
        int serv_sock;
        std::string serv_hostname;

        if (argc < 2) {
                fmt::print(stderr, "Usage: {} <client-id>\n", argv[0]);
                exit(EXIT_FAILURE);
        }

        if (argv[1][0] == 'P')
                my_id = atoi(argv[1]+1);
        else
                my_id = atoi(argv[1]);

        serv_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (serv_sock < 0) {
                perror("Unable to create out-socket");
                exit(EXIT_FAILURE);
        }

        build_connections(serv_sock);


//        std::jthread socket_worker ([&](std::stop_token stoken)
//        {
//                char buf[1024];
//                while (1) {
//                        request_t req = reqs.pop();
//
//                        if (req.type == request_t::INVALID_REQUEST)
//                                break;
//
//                        DBG("Sending {:f}\n", req);
//
//                        send(out_sock, &req, sizeof req, 0);
//
//                        bzero(buf, sizeof buf);
//                        if (recv(out_sock, buf, sizeof buf, 0) < 0) {
//                                perror("Unable to read from socket");
//                                exit(EXIT_FAILURE);
//                        }
//
//                        fmt::print("{}\n", buf);
//                }
//
//                if (!stoken.stop_requested())
//                        DBG_ERR("Invalid request ended up in queue!\n");
//        });

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

                // LOCK LAMPORT MUTEX
                pa2_cfg::send_with_delay(serv_sock, &req, sizeof req, 0,
                                         "Unable to send issue_request to server");

                uint8_t response;
                recv(serv_sock, &response, sizeof response, 0);

                // UNLOCK LAMPORT MUTEX
                if (response)
                        fmt::print("[Server]: Success!\n");
                else
                        fmt::print("[Server]: Insufficient Balance\n");
//                reqs.push(req);
        }
//        socket_worker.request_stop();

//        reqs.push({.type = request_t::INVALID_REQUEST});

//        socket_worker.join();

        shutdown(serv_sock, SHUT_RDWR);
        close(serv_sock);

        return 0;
}

static void build_connections(int serv_sock)
{
        std::thread serv_connect{connect_server, serv_sock};

        /* Constructing this object will pen and parse the config.csv file */
        client_cfg config{};

        /*
         * Okay, we've parsed our config file. Now, we wait for the server to greenlight us,
         * then begin connecting to everybody.
         */
        lpmut = std::make_unique<lamport_mutex>(my_hostname,
                                                config.my_port,
                                                config.accept_from.size(),
                                                my_id);

        for (const auto &p : config.accept_from)
                lpmut->accept_from(p);

        // Wait for server to tell us that everyone's connected
        // if we are in charge of initiating the peer network.
        serv_connect.join();
        if (config.accept_from.empty()) {
                uint8_t greenlight;
                recv(serv_sock, &greenlight, sizeof greenlight, 0);
                assert(greenlight == 1);
        }

        for (const auto &[id, port, hostname] : config.connect_to)
                lpmut->connect_to(id, port, hostname);

        DBG("All peers connected successfully!\n");
}

client_cfg::client_cfg()
{
        /* line format is 'ID, hostname, port'
         * lines beginning with '#' are ignored
         */
        std::ifstream in {pa2_cfg::CLIENT_CFG};
        if (!in) {
                fmt::print(stderr, "Unable to open file '{}'\n", pa2_cfg::CLIENT_CFG);
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

                if (id < my_id) {
                        auto pos = std::lower_bound(accept_from.begin(), accept_from.end(), id);
                        accept_from.insert(pos, id);
                } else if (id > my_id) {
                        auto pos = std::lower_bound(connect_to.begin(), connect_to.end(), id,
                                                    [] (const client_tuple &other, int id1) -> bool
                                                    {
                                                        return get<0>(other) > id1;
                                                    });
                        connect_to.emplace(pos, id, port, std::move(hostname));
                }
        }
        in.close();

//        DBG("connect_to: [{}]\n", fmt::join(connect_to, ", "));
//        DBG("accept_from: [{}]\n", fmt::join(accept_from, ", "));
}

static void connect_server(int serv_sock)
{
        /* parse the server config */
        std::string serv_hostname;
        int server_port;
        {
                std::ifstream in{pa2_cfg::SERVER_CFG};
                std::string portstr;
                std::getline(in, serv_hostname, ',');
                if (in.peek() == ' ')
                        in.ignore();
                std::getline(in, portstr);
                auto portview = std::string_view{portstr};
                std::from_chars(portview.cbegin(), portview.cend(), server_port);
                in.close();
        }

        auto serv_addr = hostname_lookup(serv_hostname, server_port);

        if (connect(serv_sock, serv_addr.get(), sizeof(sockaddr_in)) < 0) {
                perror("Unable to connect to server");
                exit(EXIT_FAILURE);
        }

        /* Sending our ID tells the server we are up and waiting to make connections
         * to our peers.
         */
        pa2_cfg::send_with_delay(serv_sock, &my_id, 1, 0,
                                 "Unable to communicate with server");
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
        int dst;
        auto result = std::from_chars(word->data(), word->data() + word->size(), dst);
        if (result.ec == std::errc::invalid_argument) {
                fmt::print(stderr, "Malformed input.\n");
                res.type = request_t::INVALID_REQUEST;
                return res;
        }
        res.tr.recv = dst; // equivalent to setting res.bal.client = dst
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
        int amt;
        result = std::from_chars(word->data(), word->data() + word->size(), amt);
        if (result.ec == std::errc::invalid_argument) {
                fmt::print(stderr, "Malformed input.\n");
                res.type = request_t::INVALID_REQUEST;
                return res;
        }
        res.tr.amt = amt;
        return res;
}
