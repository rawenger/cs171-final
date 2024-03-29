//
// Created by Ryan Wenger on 5/5/23.
//

#pragma once
#include <thread>
#include <vector>
#include <sys/socket.h>

namespace cs171_cfg {
    using namespace std::chrono_literals;
    constexpr const char *CLIENT_CFG = "clients.csv";
    constexpr auto NETWORK_DELAY = 100ms;

    /** `err_msg` must be a string literal! */
    template <bool Async=true>
    ssize_t send_with_delay(int socket,
                            const void *buffer,
                            size_t length,
                            int flags,
                            const char *err_msg="")
    {
            constexpr bool execute_async = Async && (NETWORK_DELAY > 0s);
            auto wake_time = std::chrono::system_clock::now() + NETWORK_DELAY;

            const uint8_t *bufcpy;

            if constexpr (execute_async) {
                    auto tmp = new uint8_t[length];
                    memcpy(tmp, buffer, length);
                    bufcpy = tmp;
            } else {
                    bufcpy = static_cast<const uint8_t *>(buffer);
            }

            auto send_worker = [=]() -> ssize_t {
                    std::this_thread::sleep_until(wake_time);
                    ssize_t result = send(socket, bufcpy, length, flags);
                    if (result < 0)
                            perror(err_msg);

                    if constexpr (execute_async)
                        delete[] bufcpy;

                    return result;
            };

            if constexpr (execute_async)
                    std::thread{send_worker}.detach();
            else
                    return send_worker();

            return static_cast<ssize_t>(length);
    }

    using node_id_t = uint8_t;
    using socket_t = int;
    struct system_cfg {
        // <PID, port #, hostname>
        using client_tuple = std::tuple<node_id_t, int, std::string>;
        std::vector<client_tuple> peers;
        size_t n_peers;
        int my_port;

        system_cfg();
        system_cfg(const system_cfg& other) = default;
    };
};
