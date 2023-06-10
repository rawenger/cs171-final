#include <charconv>
#include <cstring>
#include <string>
#include <vector>

#include "cli_interface.h"

static auto parse_list(std::string_view text) -> input::arguments;
static auto parse_call(std::string_view text) -> std::optional<input::call>;
static auto parse_name(std::string_view text) -> std::optional<input::KIND>;
static auto parse_pid(std::string_view text) -> std::optional<input::pid>;

auto parse_input(const std::string_view text) -> std::optional<input>
{
    std::optional<input> maybe_input;
    const auto maybe_call = parse_call(text);
    if (not maybe_call.has_value()) {
        return maybe_input;
    }
    const auto &[name, args] = maybe_call.value();

    const auto maybe_kind = parse_name(name);
    if (not maybe_kind.has_value()) {
        return maybe_input;
    }
    auto kind = maybe_kind.value();

    input input {.tag = kind};
    switch (kind) {
        case input::KIND::CRASH:
        case input::KIND::BLOCKCHAIN:
        case input::KIND::QUEUE:
        case input::KIND::LOG:
        case input::KIND::BLOG: {
            break;
        }

        case input::KIND::FAIL_LINK: {
            if (args.size() != 1) {
                return maybe_input;
            }
            auto pid = parse_pid(args[0]);
            if (not pid.has_value()) {
                return maybe_input;
            }
            input.fail_link = {
                .dest = pid.value(),
            };
            break;
        }

        case input::KIND::FIX_LINK: {
            if (args.size() != 1) {
                return maybe_input;
            }
            auto pid = parse_pid(args[0]);
            if (not pid.has_value()) {
                return maybe_input;
            }
            input.fix_link = {
                .dest = pid.value(),
            };
            break;
        }

        case input::KIND::TRANSACTION: {
            // example use: 'transfer(P1, 3)' to transfer P1 $3
            if (args.size() != 2) {
                    return maybe_input;
            }
            // @jackson: don't need bounds checking ::at() since we just checked in the `if` above
            auto dest = parse_pid(args[0]);
            if (!dest) {
                    return maybe_input;
            }
            // obviously atoi() is only here since we won't be using this in the final product
            uint16_t value;
            auto amt = std::from_chars(args[1].data(), args[1].data() + args[1].size(), value);

            if (amt.ec == std::errc::invalid_argument) {
                return maybe_input;
            }

            input.transaction = {
                    .dest = *dest,
                    .amt = value
            };
            break;
        }

        case input::KIND::POST: {
            if (args.size() != 3) {
                return maybe_input;
            }

            input.post.author = args[0];
            input.post.title = args[1];
            input.post.body = args[2];
            break;
        }

        case input::KIND::COMMENT: {
            if (args.size() != 3) {
                return maybe_input;
            }

            input.comment.commenter = args[0];
            input.comment.title = args[1];
            input.comment.comment = args[2];
            break;
        }

        case input::KIND::VIEW: {
            if (args.size() != 1) {
                return maybe_input;
            }

            input.view.author = args[0];
            break;
        }

        case input::KIND::READ: {
            if (args.size() != 1) {
                return maybe_input;
            }

            input.read.title = args[0];
            break;
        }
    }

    return input;
}

static auto parse_list(const std::string_view text) -> input::arguments
{
    const std::string sep = ", ";
    std::vector<std::string_view> args { };

    if (text.empty()) {
        return args;
    }

    size_t start_of_arg = 0;
    size_t end_of_arg = text.find(sep);

    while (end_of_arg != std::string_view::npos) {
        const auto arg = text.substr(start_of_arg, end_of_arg - start_of_arg);
        args.push_back(arg);
        start_of_arg = end_of_arg + sep.size();
        end_of_arg = text.find(sep, start_of_arg);
    }

    const auto arg = text.substr(start_of_arg);
    args.push_back(arg);

    return args;
}

static auto parse_call(const std::string_view text) -> std::optional<input::call>
{
    std::optional<input::call> call;

    size_t args_start = text.find('(');
    if (args_start++ == std::string::npos) {
        return call;
    }

    auto name = text.substr(0, args_start - 1);
    if (name.empty()) {
        return call;
    }

    size_t args_end = text.rfind(')');
    if (args_end == std::string::npos) {
        return call;
    }

    auto args = parse_list(text.substr(args_start, args_end - args_start));

    call = std::make_pair(name, args);
    return call;
}

static auto parse_name(const std::string_view text) -> std::optional<input::KIND>
{
    std::optional<input::KIND> kind {};

    if (text == "crash") {
        kind = input::KIND::CRASH;
    } else if (text == "failLink") {
        kind = input::KIND::FAIL_LINK;
    } else if (text == "fixLink") {
        kind = input::KIND::FIX_LINK;
    } else if (text == "blockchain") {
        kind = input::KIND::BLOCKCHAIN;
    } else if (text == "queue") {
        kind = input::KIND::QUEUE;
    } else if (text == "log") {
        kind = input::KIND::LOG;
    } else if (text == "transact") {
        kind = input::KIND::TRANSACTION;
    } else if (text == "post") {
        kind = input::KIND::POST;
    } else if (text == "comment") {
        kind = input::KIND::COMMENT;
    } else if (text == "blog") {
        kind = input::KIND::BLOG;
    } else if (text == "view") {
        kind = input::KIND::VIEW;
    } else if (text == "read") {
        kind = input::KIND::READ;
    }

    return kind;
}

static auto parse_pid(const std::string_view text) -> std::optional<input::pid>
{
    std::optional<input::pid> maybe_pid;

    if (text.at(0) != 'P') {
        return maybe_pid;
    }

    auto no_prefix = text.substr(1);
    input::pid value;
    auto pid = std::from_chars(no_prefix.data(), no_prefix.data() + no_prefix.size(), value);

    if (pid.ec == std::errc::invalid_argument) {
        return maybe_pid;
    }

    maybe_pid = value;
    return maybe_pid;
}

/*
    usage:

    auto text = "fixLink(P3)";
    const auto maybe_input = parse_input(text);
    if (maybe_input.has_value()) {
        const auto input = maybe_input.value();
        std::cout << "got it: '" << (int) input.fix_link.dest << "'" << std::endl;
    } else {
        std::cout << "bad parse" << std::endl;
    }
*/
