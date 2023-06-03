//
// Created by ryan on 5/13/23.
//

#pragma once

#include <cstdint>
#include <tuple>
#include <optional>
#include <cassert>


#include "cs171_cfg.h"
#include "blockchain.h"

extern cs171_cfg::node_id_t my_id;

namespace paxos_msg {
    using V = transaction;

    static constexpr const char *msg_types[] = {
            "DUPLICATE",
            "HANDSHAKE_COMPLETE",
            "PREPARE", // propose
            "PROMISE",
            "ACCEPT",
            "ACCEPTED",
            "DECIDE",
            "FWD_VAL",
    };

    enum MSG_TYPE : uint8_t {
        // messages sent during initial connection handshake
        DUPLICATE = 0, // a connection between these 2 nodes already exists--we can safely close the new one
        HANDSHAKE_COMPLETE,

        /* types that include additional data */
        PREPARE, // propose
        PROMISE,
        ACCEPT,
        ACCEPTED,
        DECIDE,

        FWD_VAL, // value forwarded to "known leader"

        /* no additional data needs to be read */
    };

    struct ballot_num {
        size_t seq_num {0}; // sequence number
        uint8_t node_pid {my_id};
        size_t slot_num {1};

        ballot_num(size_t number, uint8_t nodePid, size_t slotNumber)
        : seq_num(number), node_pid(nodePid), slot_num(slotNumber)
        { }

        ballot_num() = default;

        template <class Archive>
        void serialize(Archive &ar)
        { ar(seq_num), ar(node_pid), ar(slot_num); }

        // TODO: relational operators IDE-generated....look over these later
        bool operator==(const ballot_num &rhs) const {
                return seq_num == rhs.seq_num &&
                       node_pid == rhs.node_pid &&
                       slot_num == rhs.slot_num;
        }

        bool operator<=(const ballot_num &rhs) const {
                if (slot_num < rhs.slot_num)
                        return true;
                if (slot_num > rhs.slot_num)
                        return false;
                if (seq_num < rhs.seq_num)
                        return true;
                if (seq_num > rhs.seq_num)
                        return false;
                if (node_pid < rhs.node_pid)
                        return true;
                if (node_pid > rhs.node_pid)
                        return false;

                assert(*this == rhs);
                return true; // *this == rhs
        }

        bool operator<(const ballot_num &rhs) const {
                return !(rhs <= *this);
        }

        bool operator>(const ballot_num &rhs) const {
                return !(*this <= rhs);
        }

        bool operator>=(const ballot_num &rhs) const {
                return rhs <= *this;
        }
    };

    using prepare_msg = ballot_num;
//    using accept_msg = ballot_num; // also needs value
    using accepted_msg = ballot_num;
    using fwd_msg = V;

    struct decide_msg {
        V val;
        size_t slotnum;
    };

    struct promise_msg {
        /* Ballot number
         *
         */
        ballot_num balnum;

        /* The proposal with the highest number less than n (balnum) that it has
         * accepted, if any.
         */
        ballot_num acceptnum;

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
            fwd_msg fwd;
        };

        template<class Archive>
        void serialize(Archive &ar) {
            ar(type);
            switch (type) {
                case PREPARE: ar(prep); break;
                case PROMISE: ar(prom.balnum, prom.acceptnum, prom.acceptval); break;
                case ACCEPT: ar(acc.balnum, acc.value); break;
                case ACCEPTED: ar(accd); break;
                case DECIDE: ar(dec.val); ar(dec.slotnum); break;
                case FWD_VAL: ar(fwd); break;
                default: break;
            }
        }
    };

    std::string encode_msg(msg m);
    msg decode_msg(const std::string &data);
}

//std::string format_as(paxos_msg::ballot_num ballot);

std::string format_as(std::optional<paxos_msg::V> optval);
