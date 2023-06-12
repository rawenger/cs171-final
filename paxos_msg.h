//
// Created by ryan on 5/13/23.
//

#pragma once

#include <cstdint>
#include <tuple>
#include <optional>
#include <cassert>
#include <forward_list>
#include <memory>

#include "cs171_cfg.h"
#include "blag.h"

extern cs171_cfg::node_id_t my_id;

namespace paxos_msg {
    struct V {
        using ptr_type = blag::transaction;
        std::shared_ptr<ptr_type> ptr{nullptr};

        V() = default;

        explicit V(ptr_type &&tr)
        : ptr(tr.allocate())
        { }

        operator bool() const
        { return static_cast<bool>(ptr); }

        auto &operator*() const
        { return *ptr; }

        auto operator->() const
        { return ptr; }

        template <class Archive>
        void serialize(Archive &ar)
        { ar(ptr); }

//        V &operator=(V &&other) noexcept
//        { ptr.swap(other.ptr); other.ptr.reset(nullptr); return *this; }
    };

    using msg_size_t = uint16_t;

    static constexpr const char *msg_types[] = {
            "PREPARE", // propose
            "PROMISE",
            "ACCEPT",
            "ACCEPTED",
            "DECIDE",
            "FWD_VAL",
            "RECOVER_REQ",
            "RECOVER_RESP",
            // NOTE: No entries after these.
            "DUPLICATE",
            "HANDSHAKE_COMPLETE",
    };

    enum MSG_TYPE : uint8_t {
        /* types that include additional data */
        PREPARE = 0, // propose
        PROMISE,
        ACCEPT,
        ACCEPTED,
        DECIDE,

        FWD_VAL, // value forwarded to "known leader"
        RECOVER_REQ,
        RECOVER_RESP,

        /* no additional data needs to be read */
        // NOTE: Please don't put any enumerations after these. I beg you.
        // messages sent during initial connection handshake
        DUPLICATE, // a connection between these 2 nodes already exists--we can safely close the new one
        HANDSHAKE_COMPLETE,
    };

    struct ballot_num {
        size_t seq_num {0}; // sequence number
        uint8_t node_pid {my_id};
        size_t slot_num {1};

        ballot_num(size_t number, uint8_t nodePid, size_t slotNumber)
        : seq_num(number), node_pid(nodePid), slot_num(slotNumber)
        { }

        ballot_num() = default;
        ballot_num(const ballot_num &other) = default;

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

    struct prepare_msg {
            ballot_num balnum;

            template<class Archive>
            void serialize(Archive &ar)
            { ar(balnum); }
    };

    struct accepted_msg {
            ballot_num balnum;

            template<class Archive>
            void serialize(Archive &ar)
            { ar(balnum); }
    };

    struct fwd_msg {
            V val;

            template<class Archive>
            void serialize(Archive &ar)
            { ar(*val); }
    };

    struct decide_msg {
        V val;
        size_t slotnum;

        template<class Archive>
        void serialize(Archive &ar)
        { ar(val, slotnum); }
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

        template<class Archive>
        void serialize(Archive &ar)
        { ar(balnum, acceptnum, acceptval); }
    };

    struct accept_msg {
        ballot_num balnum;
        V value;

        template<class Archive>
        void serialize(Archive &ar)
        { ar(balnum, value); }
    };

    // recover request message
    struct logreq_msg {
        size_t slot; // first slot # for which decision is unknown

        template<class Archive>
        void serialize(Archive &ar)
        { ar(slot); }
    };

    // recover response message
    struct logresp_msg {
        size_t slot;
        std::forward_list<V> vals;

        template<class Archive>
        void serialize(Archive &ar)
        { ar(slot, vals); }
    };

    // NOTE: has to match order above in msg_types array
    using msg = std::variant< prepare_msg,
                              promise_msg,
                              accept_msg,
                              accepted_msg,
                              decide_msg,
                              fwd_msg,
                              logreq_msg,
                              logresp_msg
                            >;

    std::string encode_msg(const msg &m);
    msg decode_msg(const std::string &data);
}

//std::string format_as(paxos_msg::ballot_num ballot);

std::string format_as(std::optional<paxos_msg::V> optval);
std::string format_as(const paxos_msg::V &val);