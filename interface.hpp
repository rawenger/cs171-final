#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sys/socket.h>

#include "cs171_cfg.h"

using pid = cs171_cfg::node_id_t;
using arguments = std::vector<std::string>;
using call = std::pair<std::string, arguments>;

struct input {
    enum class kind_t {
        crash,      // crash()
        fail_link,  // failLink(dest)
        fix_link,   // fixLink(dest)
        blockchain, // blockchain()
        queue,      // queue()
        log         // log()
    };

    // See: C++'s union-like classes. https://en.cppreference.com/w/cpp/language/union
    union {
        struct { kind_t kind; } crash;
        struct { kind_t kind; pid dest; } fail_link;
        struct { kind_t kind; pid dest; } fix_link;
        struct { kind_t kind; } blockchain;
        struct { kind_t kind; } queue;
        struct { kind_t kind; } log;
    };
};

auto parse_list(const std::string &text) -> std::vector<std::string>;
auto parse_call(const std::string &text) -> std::optional<call>;
auto parse_input(const std::string &text) -> std::optional<input>;
auto parse_name(const std::string &text) -> std::optional<input::kind_t>;
auto parse_pid(const std::string &text) -> std::optional<pid>;
