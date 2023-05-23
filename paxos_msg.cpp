//
// Created by ryan on 5/13/23.
//

#include <netinet/in.h>

#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/tuple.hpp>
#include <sstream>

#include "paxos_msg.h"

std::string paxos_msg::encode_msg(paxos_msg::msg m)
{
        std::stringstream ss;
        {
                cereal::PortableBinaryOutputArchive serial{ss};
                serial(CEREAL_NVP(m));
        }

        std::string res, str;
        str = ss.str();

        auto len = htons(static_cast<uint16_t>(str.size()));
        res.append(reinterpret_cast<char *>(&len), 2);

        return std::move(res) + std::move(str);
}

paxos_msg::msg paxos_msg::decode_msg(const std::string &data)
{
        std::stringstream ss{data};
        paxos_msg::msg res{};
        {
                cereal::PortableBinaryInputArchive deserial{ss};
                deserial(res);
        }

        return res;
}