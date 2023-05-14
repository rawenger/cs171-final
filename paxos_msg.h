//
// Created by ryan on 5/13/23.
//

#pragma once

#include <cstdint>
#include <tuple>

#include "cs171_cfg.h"
#include "blockchain.h"

namespace paxos_msg {
    using V = transaction;

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

    using ballot_num = std::tuple<size_t, cs171_cfg::node_id_t, size_t>;

    using prepare_msg = ballot_num;
    using accept_msg = ballot_num;
    using accepted_msg = ballot_num;
    using decide_msg = V;

    struct promise_msg {
        ballot_num balnum;
        ballot_num acceptnum;
        V acceptval;
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
                case ACCEPT: { ar(acc); break; }
                case ACCEPTED: { ar(accd); break; }
                case DECIDE: { ar(dec); break; }
                default: break;
            }
        }
    };

    std::string create_msg(msg m);

    msg read_msg(const std::string &data);
};