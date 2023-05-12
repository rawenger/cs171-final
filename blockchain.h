//
// Created by ryan on 4/8/23.
//

#pragma once

#include <cstdint>
#include <mutex>
#include <array>

#include "request.h"

using u256 = std::array<uint8_t, 32>;

/* This was designed before we had to do all the string conversions
 * to hash, but still works just fine.
 */
union transaction {
    struct {
        uint16_t amt;
        uint16_t sender;
        uint16_t receiver;
    } __attribute__((packed));
    uint64_t bits : 48;
} __attribute__((packed));
static_assert(sizeof(transaction) == 6);

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

class blockchain {
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