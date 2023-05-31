//
// Created by ryan on 4/8/23.
//

#pragma once

#include <cstdint>
#include <mutex>
#include <array>

#include <fmt/core.h>

using u256 = std::array<uint8_t, 32>;

/* This was designed before we had to do all the string conversions
 * to hash, but still works just fine.
 */
struct transaction {
    uint16_t amt;
    uint16_t sender;
    uint16_t receiver;

    template<class Archive>
    void serialize(Archive &ar) { ar(amt, sender, receiver); }
};

// blockchain.cpp uses its own custom transaction formatter
#ifndef DISABLE_TRANSACTION_FORMAT_AS
inline std::string format_as(transaction tr)
{
        return fmt::format("P{} -${}-> P{}", tr.sender, tr.receiver, tr.amt);
}
#endif

class blockchain {
    struct block {
        explicit block(transaction t, block *prev=nullptr);
        ~block() { delete P; }
        void compute_nonce();
        void hash(u256 &out);

        u256 H{0}; // hash of previous
        block *P;
        uint64_t N;
        transaction T;
    };
    friend struct fmt::formatter<blockchain::block>;

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

    bool transfer(transaction t);

    int balance(int client);

    std::string get_history();
    std::string get_balances();
};
