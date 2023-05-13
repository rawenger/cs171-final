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
#include <sys/socket.h>
#include <sys/poll.h>
#include <arpa/inet.h>

#include <iostream>
#include <thread>
#include <forward_list>
#include <vector>
#include <map>
#include <charconv>
#include <string>
#include <fstream>
#include <cassert>

#include "blockchain.h"
#include "request.h"
#include "debug.h"
#include "cs171_cfg.h"

static constexpr int MAX_CLIENT_CONNECTIONS = 100;

static std::map<int, uint16_t> clients; /* socket fd : ID */

struct server_cfg {
        std::string hostname;
        int port;
        int num_clients;

        server_cfg() : port(0), num_clients(0)
        {
                std::ifstream in {pa2_cfg::SERVER_CFG};
                std::string portstr;
                std::getline(in, hostname, ',');
                if (in.peek() == ' ')
                        in.ignore();
                std::getline(in, portstr);
                auto portview = std::string_view {portstr};
                std::from_chars(portview.cbegin(), portview.cend(), port);
                in.close();

                // now parse the clients
                in.open(pa2_cfg::CLIENT_CFG);
                while (!in.eof()) {
                        if (in.peek() == '#') {
                                in.ignore(256, '\n');
                                continue;
                        }
                        in.ignore();
                        if (in.eof())
                                break;
                        in.ignore(256, '\n');
                        ++num_clients;
                }
                in.close();
                DBG("num_clients: {}\n", num_clients);
        }
};

static void respond(int socket, request_t req)
{
        switch (req.type) {
                case request_t::TR_REQUEST: {
                        transaction t{req.tr.amt, clients.at(socket), req.tr.recv};
                        uint8_t response = blockchain::BLOCKCHAIN.transfer(t);
                        pa2_cfg::send_with_delay<false>(socket, &response, 1, MSG_NOSIGNAL);
                        break;
                }
                case request_t::BAL_REQUEST: {
                        int bal = blockchain::BLOCKCHAIN.balance(req.bal.client);
                        pa2_cfg::send_with_delay<false>(socket, &bal, sizeof bal, MSG_NOSIGNAL);
                        break;
                }
                default: {
                        DBG_ERR("Invalid issue_request. (how did we get here?)\n");
                }
        }
}

/* main server thread */
static void responder(std::stop_token stoken, std::vector<pollfd> *const client_fds)
{
        while (!stoken.stop_requested()) {
                int n_events = poll(client_fds->data(), client_fds->size(), 5000);

                if (n_events < 0) {
                        perror("Unable to poll client sockets");
                        return;
                }

                if (n_events == 0)
                        continue;

                // remove clients who've closed their connections
                std::erase_if(*client_fds, [] (const pollfd &pfd) {
                        if (pfd.revents & SOCK_REVENT_CLOSE) {
                                DBG("Client P{} has been disconnected\n", clients.at(pfd.fd));
                                fflush(stdout);
                                close(pfd.fd);
                        }
                        return pfd.revents & SOCK_REVENT_CLOSE;
                });

                bool did_fork = false;

                for (auto pfd : *client_fds) {
                        DBG("checking fd {}: events: {}\n", pfd.fd, pfd.revents);

                        if (!(pfd.revents & POLLIN))
                                continue;

                        request_t req {};
                        ssize_t status = recv(pfd.fd, &req, sizeof req, 0);
                        if (status < 0) {
                                perror("Unable to receive data from socket");
                                break;
                        }

                        assert(status == sizeof req);

                        std::thread(respond, pfd.fd, req).detach();
                        did_fork = true;
                }

                // give some time to respond to any messages if necessary
                if (did_fork)
                        std::this_thread::yield();
        }
}

static std::vector<pollfd>
accept_connections(int in_sock, const server_cfg &config)
{
        std::vector<pollfd> client_fds;

        addrinfo addr_hint = {
                .ai_family = AF_INET,
                .ai_socktype = SOCK_STREAM,
        };

        addrinfo *m_addrinfo;

        if (getaddrinfo(config.hostname.c_str(), nullptr, &addr_hint, &m_addrinfo) < 0
            || m_addrinfo == nullptr)
        {
                perror("Unable to perform DNS lookup on server hostname");
                exit(EXIT_FAILURE);
        }

        sockaddr *m_addr = m_addrinfo->ai_addr;
        reinterpret_cast<sockaddr_in *>(m_addr)->sin_port = htons(config.port);

        socklen_t addrlen = sizeof(sockaddr_in);

        if (bind(in_sock, m_addr, addrlen) < 0) {
                perror("Unable to bind socket to port");
                exit(EXIT_FAILURE);
        }

        if (listen(in_sock, MAX_CLIENT_CONNECTIONS) < 0) {
                perror("Unable to listen on incoming socket");
                exit(EXIT_FAILURE);
        }

        for (int i = 0; i < config.num_clients; i++) {
                int new_sock = accept(in_sock, m_addr, &addrlen);

                if (new_sock < 0) {
                        perror("Unable to accept incoming connection");
                        exit(EXIT_FAILURE);
                }

                DBG("Accepted new connection from socketfd {}\n", new_sock);

                client_fds.push_back(pollfd{.fd = new_sock, .events = POLLIN | SOCK_EVENT_CLOSE});
        }

        freeaddrinfo(m_addrinfo);

        // get everyone's ID
        for (auto &c : client_fds) {
                uint16_t id;
                if (recv(c.fd, &id, sizeof id, 0) < 0) {
                        perror("Unable to get all client ID's");
                        return client_fds;
                }
                DBG("Got client ID 'P{}'\n", id);
                clients.insert({c.fd, id});
        }

        DBG("All clients connected successfully\n");
        shutdown(in_sock, SHUT_RDWR);

        /* Broadcast 'READY' message to client with the lowest ID
         * (a.k.a: the one who will initiate the peer connection process)
         * To do this, we first need to identify which client this is.
         */
        using map_t = decltype(*clients.begin());
        auto min_id = std::min_element(clients.begin(), clients.end(),
                                       [] (map_t &&e1, map_t &&e2) {
                                           return e1.second < e2.second;
                                       });

        DBG("Broadcasting 'READY' message to client P{}\n", clients.at(min_id->first));
        constexpr uint8_t rdy = true;
        pa2_cfg::send_with_delay(min_id->first, &rdy, sizeof rdy, 0,
                                 "Unable to communicate with one or more clients");

        return client_fds;
}

static bool parse(std::string_view&& msg);

static void user_input()
{
        std::string inpt;

        do {
                std::cout << "> ";
                std::getline(std::cin, inpt);
        } while (parse(inpt));
}

int main()
{
        int in_sock, opt;

        server_cfg config{};

        in_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (in_sock < 0) {
                perror("Unable to create in-socket");
                exit(EXIT_FAILURE);
        }

        opt = 1;
        if (setsockopt(in_sock, SOL_SOCKET, /*SO_REUSEADDR |*/ SO_REUSEPORT, &opt, sizeof opt) < 0) {
                perror("Unable to set socket options");
                exit(EXIT_FAILURE);
        }

        auto fds = accept_connections(in_sock, config);
        std::jthread resp{responder, &fds};

        user_input();

        DBG("Requesting stop of socket polling thread\n");
        resp.request_stop();
        resp.join();
        return 0;
}

static bool parse(std::string_view&& msg)
{
        /* If this is bad you should see the parser in the client.
         */
        if (msg.empty())
                return true;

        size_t wordlen, pos = 0;
        std::vector<std::string_view> words;
        do {
                wordlen = msg.find_first_of(' ', pos) - pos;
                words.push_back(msg.substr(pos, wordlen));
                pos += wordlen + 1;
        } while (wordlen < msg.length());
        DBG("words: {}\n", fmt::join(words, ", "));

        auto word = words.begin();

        if (*word == "exit") {
                return false;
        } else if (*word == "wait") {
                int wait;
                ++word;
                auto result = std::from_chars(word->data(), word->data() + word->size(), wait);
                if (result.ec == std::errc::invalid_argument) {
                        fmt::print(stderr, "Malformed input.\n");
                        return true;
                }
                std::this_thread::sleep_for(std::chrono::seconds{wait});
                return true;
        } else if (*word == "Blockchain") {
                fmt::print("{}\n", blockchain::BLOCKCHAIN.get_history());
                return true;
        } else if (*word == "Balance") {
                fmt::print("{}\n", blockchain::BLOCKCHAIN.get_balances());
                return true;
        } else {
                fmt::print(stderr, "Malformed input.\n");
                return true;
        }
}