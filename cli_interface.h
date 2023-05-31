#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cs171_cfg.h"

struct input {
    using pid = cs171_cfg::node_id_t;
    using arguments = std::vector<std::string>;
    using call = std::pair<std::string, arguments>;

    enum class KIND {
        CRASH,      // crash()
        FAIL_LINK,  // failLink(dest)
        FIX_LINK,   // fixLink(dest)
        TRANSACTION,
        BLOCKCHAIN, // blockchain()
        QUEUE,      // queue()
        LOG         // log()
    } tag;

    // See: C++'s union-like classes. https://en.cppreference.com/w/cpp/language/union
    union {
        struct {} crash;
        struct {pid dest;} fail_link;
        struct {pid dest;} fix_link;
        struct {} blockchain;
        struct {} queue;
        struct {} log;
    };
};

/**
 * @param text Input text to parse, straight from the user
 * @return `None` if syntax error, else tagged union `input` type
 */
auto parse_input(const std::string &text) -> std::optional<input>;