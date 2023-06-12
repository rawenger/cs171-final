#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cs171_cfg.h"

struct input {
    using pid = cs171_cfg::node_id_t;
    using arguments = std::vector<std::string_view>;
    using call = std::pair<std::string, arguments>;

    enum class KIND {
        CRASH,      // crash()
        FAIL_LINK,  // failLink(dest)
        FIX_LINK,   // fixLink(dest)
        TRANSACTION,
        BLOCKCHAIN, // blockchain()
        QUEUE,      // queue()
        LOG,        // log()
        POST,       // post(username, title, content)
        COMMENT,    // comment(username, title, content)
        BLOG,       // blog()
        VIEW,       // view(username)
        READ,       // read(title)
        BAL,        // bal()
    } tag;

    // See: C++'s union-like classes. https://en.cppreference.com/w/cpp/language/union
    union {
        struct {} crash;
        struct {pid dest;} fail_link;
        struct {pid dest;} fix_link;
        struct {pid dest; uint16_t amt;} transaction;
        struct {} blockchain;
        struct {} queue;
        struct {} log;
        struct {std::string_view author; std::string_view title; std::string_view body;} post;
        struct {std::string_view commenter; std::string_view title; std::string_view comment;} comment;
        struct {} blog;
        struct {std::string_view author;} view;
        struct {std::string_view title;} read;
        struct {} bal;
    };
};

/**
 * @param text Input text to parse, straight from the user
 * @return `None` if syntax error, else tagged union `input` type
 */
auto parse_input(const std::string_view text) -> std::optional<input>;
