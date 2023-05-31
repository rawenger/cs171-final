#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sys/socket.h>

#include "cs171_cfg.h"

struct input {
    using pid = cs171_cfg::node_id_t;
    using arguments = std::vector<std::string>;
    using call = std::pair<std::string, arguments>;

    enum class INPUT_KIND {
        CRASH,      // crash()
        FAIL_LINK,  // failLink(dest)
        FIX_LINK,   // fixLink(dest)
        BLOCKCHAIN, // blockchain()
        QUEUE,      // queue()
        LOG         // log()
    };

    // See: C++'s union-like classes. https://en.cppreference.com/w/cpp/language/union
    union {
        struct { INPUT_KIND kind; } crash;
        struct { INPUT_KIND kind; pid dest; } fail_link;
        struct { INPUT_KIND kind; pid dest; } fix_link;
        struct { INPUT_KIND kind; } blockchain;
        struct { INPUT_KIND kind; } queue;
        struct { INPUT_KIND kind; } log;
    };
};

auto parse_input(const std::string &text) -> std::optional<input>;
