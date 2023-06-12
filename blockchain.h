//
// Created by ryan on 4/8/23.
//

#pragma once

#include <cstdint>
#include <mutex>
#include <array>

#include "blag.h"
#include "paxos_msg.h"

using u256 = std::array<uint8_t, 32>;

/* This was designed before we had to do all the string conversions
 * to hash, but still works just fine.
 */
//struct transaction {
//    uint16_t amt;
//    uint16_t sender;
//    uint16_t receiver;
//
//    template<class Archive>
//    void serialize(Archive &ar) { ar(amt, sender, receiver); }
//};

using transaction = paxos_msg::V;

class blockchain {
    struct block {
        block(const transaction &t, block *prev=nullptr);
        ~block() { delete P; }
        void compute_nonce();
        void hash(u256 &out);

        u256 H{0}; // hash of previous
        block *P;
        uint64_t N;
        transaction T;
    };

    friend std::string format_as(const blockchain::block &blk);

    block *tail{nullptr};
    std::recursive_mutex mut{};
    using lock_gd =
            std::lock_guard<decltype(blockchain::mut)>;

    blockchain();
    ~blockchain();

public:
    static blockchain BLOCKCHAIN;

    blockchain(blockchain& other) = delete;
    blockchain& operator=(blockchain& other) = delete;

    bool transact(const transaction &t);

    std::string get_history();
};
