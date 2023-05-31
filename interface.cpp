#include <cstring>
#include <string>
#include <vector>

#include "interface.h"

static auto parse_list(const std::string &text) -> input::arguments;
static auto parse_call(const std::string &text) -> std::optional<input::call>;
static auto parse_name(const std::string &text) -> std::optional<input::INPUT_KIND>;
static auto parse_pid(const std::string &text) -> std::optional<input::pid>;

auto parse_input(const std::string &text) -> std::optional<input>
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

    input input{};
    switch (kind) {
        case input::INPUT_KIND::CRASH: {
            input.crash = {
                .kind = kind,
            };
            break;
        }
        case input::INPUT_KIND::FAIL_LINK: {
            if (args.size() != 1) {
                return maybe_input;
            }
            auto pid = parse_pid(args.at(0));
            if (not pid.has_value()) {
                return maybe_input;
            }
            input.fail_link = {
                .kind = kind,
                .dest = pid.value(),
            };
            break;
        }
        case input::INPUT_KIND::FIX_LINK: {
            if (args.size() != 1) {
                return maybe_input;
            }
            auto pid = parse_pid(args.at(0));
            if (not pid.has_value()) {
                return maybe_input;
            }
            input.fix_link = {
                .kind = kind,
                .dest = pid.value(),
            };
            break;
        }
        case input::INPUT_KIND::BLOCKCHAIN: {
            input.blockchain = {
                .kind = kind,
            };
            break;
        }
        case input::INPUT_KIND::QUEUE: {
            input.queue = {
                .kind = kind,
            };
            break;
        }
        case input::INPUT_KIND::LOG: {
            input.log = {
                .kind = kind,
            };
            break;
        }
    }

    return input;
}

static auto parse_list(const std::string &text) -> input::arguments
{
    const std::string sep = ", ";
    std::vector<std::string> args { };

    if (text.empty()) {
        return args;
    }

    size_t start_of_arg = 0;
    size_t end_of_arg = text.find(sep);

    while (end_of_arg != std::string::npos) {
        const auto arg = text.substr(start_of_arg, end_of_arg - start_of_arg);
        args.push_back(arg);
        start_of_arg = end_of_arg + sep.size();
        end_of_arg = text.find(sep, start_of_arg);
    }

    const auto arg = text.substr(start_of_arg);
    args.push_back(arg);

    return args;
}

static auto parse_call(const std::string &text) -> std::optional<input::call>
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

static auto parse_name(const std::string &text) -> std::optional<input::INPUT_KIND>
{
    std::optional<input::INPUT_KIND> kind;

    if (text == "crash") {
        kind = input::INPUT_KIND::CRASH;
    } else if (text == "failLink") {
        kind = input::INPUT_KIND::FAIL_LINK;
    } else if (text == "fixLink") {
        kind = input::INPUT_KIND::FIX_LINK;
    } else if (text == "blockchain") {
        kind = input::INPUT_KIND::BLOCKCHAIN;
    } else if (text == "queue") {
        kind = input::INPUT_KIND::QUEUE;
    } else if (text == "log") {
        kind = input::INPUT_KIND::LOG;
    }

    return kind;
}

static auto parse_pid(const std::string &text) -> std::optional<input::pid>
{
    std::optional<input::pid> maybe_pid;

    if (text.at(0) != 'P') {
        return maybe_pid;
    }

    try {
        int pid = std::stoi(text.substr(1));
        maybe_pid = pid;
    } catch (std::invalid_argument &) { }

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
