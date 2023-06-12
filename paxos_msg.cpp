//
// Created by ryan on 5/13/23.
//

#include <netinet/in.h>

#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/forward_list.hpp>
#include <cereal/types/memory.hpp>

#include <sstream>

#include <fmt/core.h>

#include "paxos_msg.h"

std::string paxos_msg::encode_msg(const msg &m)
{
        std::stringstream ss;
        {
                cereal::PortableBinaryOutputArchive serial{ss};
                serial(CEREAL_NVP(m));
        }

        std::string res, str;
        str = ss.str();

        auto len = htons(static_cast<msg_size_t>(str.size()));
        res.append(reinterpret_cast<char *>(&len), sizeof(msg_size_t));

        return std::move(res) + std::move(str);
}

paxos_msg::msg paxos_msg::decode_msg(const std::string &data)
{
        std::stringstream ss{data};
        paxos_msg::msg res {};
        {
                cereal::PortableBinaryInputArchive deserial{ss};
                deserial(res);
        }

        return res;
}

//std::string format_as(paxos_msg::ballot_num ballot)
//{
//        return fmt::format("({}, {}, {})",
//                           ballot.seq_num,
//                           ballot.node_pid,
//                           ballot.slot_num);
//}

std::string format_as(std::optional<paxos_msg::V> optval)
{
        return optval ? fmt::format("{}", (*optval)->formatter()) : "bottom";
}

std::string format_as(const paxos_msg::V &val)
{
        return val ? fmt::format("{}", val->formatter()) : "bottom";
}