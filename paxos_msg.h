//
// Created by ryan on 5/13/23.
//

#pragma once

#include <cstdint>
#include <tuple>
#include <optional>

//#include "cs171_cfg.h"
#include "blockchain.h"

namespace paxos_msg {
    using V = transaction;

    static constexpr const char *msg_types[] = {
            "IM_NEW",
            "HANDSHAKE_COMPLETE",
            "PREPARE", // propose
            "PROMISE",
            "ACCEPT",
            "ACCEPTED",
            "DECIDE",
    };

    enum MSG_TYPE : uint8_t {
        IM_NEW, // ask for a listing of other nodes in the network
        HANDSHAKE_COMPLETE,

        /* types that include additional data */
        PREPARE, // propose
        PROMISE,
        ACCEPT,
        ACCEPTED,
        DECIDE,

        /* no additional data needs to be read */
    };

    using ballot_num = std::tuple<size_t, uint8_t, size_t>;

    using prepare_msg = ballot_num;
//    using accept_msg = ballot_num; // also needs value
    using accepted_msg = ballot_num;
    using decide_msg = V;

    struct promise_msg {
        /* Ballot number
         *
         */
        ballot_num balnum;

        /* The proposal with the highest number less than n (balnum) that it has
         * accepted, if any.
         */
        std::optional<ballot_num> acceptnum;

        std::optional<V> acceptval; // only if we accepted something already (this is the value that we accepted)
    };

    struct accept_msg {
        ballot_num balnum;
        V value;
    };

    struct msg {
        MSG_TYPE type;

        union {
            prepare_msg prep;
            promise_msg prom;
            accept_msg acc;
            accepted_msg accd;
            decide_msg dec;
        };

        template<class Archive>
        void serialize(Archive &ar) {
            ar(type);
            switch (type) {
                case PREPARE: { ar(prep); break; }
                case PROMISE: { ar(prom.balnum, prom.acceptnum, prom.acceptval); break; }
                case ACCEPT: { ar(acc.balnum, acc.value); break; }
                case ACCEPTED: { ar(accd); break; }
                case DECIDE: { ar(dec); break; }
                default: break;
            }
        }
    };

//    template <MSG_TYPE Type>
//    msg new_msg()
        // note: use forwarding to make sure we take in the parameters that correspond to the appropriate constructor

    std::string encode_msg(msg m);
    msg decode_msg(const std::string &data);
};