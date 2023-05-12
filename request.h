//
// Created by ryan on 4/8/23.
//

#pragma once
#include <cstdint>

struct balance_req_t { uint16_t client; uint16_t unused; };
struct transfer_req_t { uint16_t recv; uint16_t amt; };

/** a.k.a. The thing that actually gets sent over the network */
struct request_t {
    enum : uint8_t {
        INVALID_REQUEST,
        BAL_REQUEST,
        TR_REQUEST
    } type;

    union {
        balance_req_t bal;
        transfer_req_t tr;
        uint32_t bits;
    };
}
__attribute__((packed));

static_assert(sizeof(request_t) == 5);

using server_reply_t = int;

#ifndef NDEBUG
#include "debug.h"
template <> struct fmt::formatter<request_t> {
  // Presentation format: 'e': bits, 'f': individual fields
  char presentation = 'f';

  // Parses format specifications of the form ['f' | 'e'].
  constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
    // [ctx.begin(), ctx.end()) is a character range that contains a part of
    // the format string starting from the format specifications to be parsed,
    // e.g. in
    //
    //   fmt::format("{:f} - point of interest", point{1, 2});
    //
    // the range will contain "f} - point of interest". The formatter should
    // parse specifiers until '}' or the end of the range. In this example
    // the formatter should parse the 'f' specifier and return an iterator
    // pointing to '}'.

    // Please also note that this character range may be empty, in case of
    // the "{}" format string, so therefore you should check ctx.begin()
    // for equality with ctx.end().

    // Parse the presentation format and store it in the formatter:
    auto it = ctx.begin(), end = ctx.end();
    if (it != end && (*it == 'f' || *it == 'e')) presentation = *it++;

    // Check if reached the end of the range:
    if (it != end && *it != '}') throw format_error("invalid format");

    // Return an iterator past the end of the parsed range:
    return it;
  }

  // Formats the point p using the parsed format specification (presentation)
  // stored in this formatter.
  template <typename FormatContext>
  auto format(const request_t& req, FormatContext& ctx) const -> decltype(ctx.out()) {
    // ctx.out() is an output iterator to write to.
    return presentation == 'e'
              ? fmt::format_to(ctx.out(), "bits: ({})", req.bits)
              : (req.type == request_t::BAL_REQUEST
                 ? fmt::format_to(ctx.out(), "Balance {{.client = {}}}", req.bal.client)
                 : fmt::format_to(ctx.out(), "Transfer {{.recv = {}, .amt = {}}}", req.tr.recv, req.tr.amt)
                 );
  }
};
#endif
