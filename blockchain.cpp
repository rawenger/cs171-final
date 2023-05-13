//
// Created by ryan on 4/8/23.
//

#include <openssl/evp.h>
#include <cassert>
#include <iterator>
#include <vector>
#include <map>

#include "debug.h"
#include "blockchain.h"

blockchain blockchain::BLOCKCHAIN;
static EVP_MD_CTX *sha256_ctx;

template<>
struct fmt::formatter<transaction> {
    char presentation = 'f'; // 'f' means tuple form (for printing); 'e' is raw concat (for hashing)
    // Parses format specifications of the form ['f' | 'e'].
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
            auto it = ctx.begin(), end = ctx.end();
            if (it != end && (*it == 'f' || *it == 'e')) presentation = *it++;

            // Check if reached the end of the range:
            if (it != end && *it != '}') throw format_error("invalid format");

            // Return an iterator past the end of the parsed range:
            return it;
    }

    // Formats the point p using the parsed format specification (presentation)
    // stored in this formatter.
    template<typename FormatContext>
    auto format(const transaction &tr, FormatContext &ctx) const -> decltype(ctx.out()) {
            // ctx.out() is an output iterator to write to.
            return presentation == 'e'
                        ? fmt::format_to(ctx.out(), "P{}P{}${}", tr.sender, tr.receiver, tr.amt)
                        : fmt::format_to(ctx.out(), "P{}, P{}, ${}", tr.sender, tr.receiver, tr.amt);
    }
};

template<>
struct fmt::formatter<blockchain::block> {
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
            return ctx.end();
    }

    template <typename FormatContext>
    auto format(const blockchain::block &blk, FormatContext &ctx) const -> decltype(ctx.out()) {
            return fmt::format_to(ctx.out(), "({:f}, {})", blk.T, blk.H);
    }
};

template<>
struct fmt::formatter<u256> {
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) {
            return ctx.end();
    }

    template<typename FormatContext>
    auto format(const u256 &h, FormatContext &ctx) const -> decltype(ctx.out()) {
            // ctx.out() is an output iterator to write to.
            return fmt::format_to(ctx.out(), "{:02x}", fmt::join(h.cbegin(), h.cend(), ""));
    }
};


blockchain::block::block(transaction t, block *prev)
 :      P(prev),
        N(0),
        T(t)
{
        if (prev)
                prev->hash(H); // H = prev->hash();

        compute_nonce();
}

void blockchain::block::compute_nonce()
{
        assert(sha256_ctx != nullptr);
        u256 sha_out {0};

        this->N = 0;
        hash(sha_out);

        while((sha_out[0] & (3 << 6)) != 0) {
                this->N = (N + 1) & -1L;
                hash(sha_out);
        }
}

void blockchain::block::hash(u256 &out)
{
        /* OK, so let's get this straight:
         * - we have perfectly good numbers sitting around on our blockchain
         * - we convert those numbers to c-like strings / byte arrays
         * - we convert *those* byte sequences *back* into strings--specifically the
         *      hex representation of each byte in the string, converted to characters
         * - we then encode *that* string in ASCII and *finally* feed its raw bytes
         *      into the SHA-256 algorithm
         */
        uint32_t sha_size;
        std::string sha_in = fmt::format("{}{:e}{}", H, T, N);
        EVP_DigestInit_ex2(sha256_ctx, nullptr, nullptr);
//        DBG("H: {}, ", H);
//        DBG("T: {:e}, ", T);
//        DBG("N: {}\n", N);
//        DBG("SHA-256 input: {}\n", sha_in);
        EVP_DigestUpdate(sha256_ctx, sha_in.c_str(), sha_in.length());
        EVP_DigestFinal_ex(sha256_ctx, out.data(), &sha_size);
        assert(sha_size == 32);
}

blockchain::blockchain()
{
        sha256_ctx = EVP_MD_CTX_create();
        EVP_DigestInit_ex(sha256_ctx, EVP_sha256(), nullptr);
}

blockchain::~blockchain()
{
        EVP_MD_CTX_destroy(sha256_ctx);
        delete tail;
}

bool blockchain::transfer(transaction t)
{
//        lock_gd lk{mut};

        if (balance(t.sender) < t.amt) {
                return false;
        }

        tail = new block(t, tail);

        return true;
}

int blockchain::balance(int client)
{
//        lock_gd lk{mut};

        int bal = 10;

        for (block *cur = this->tail; cur; cur = cur->P) {
                if (cur->T.sender == client)
                        bal -= cur->T.amt;
                if (cur->T.receiver == client)
                        bal += cur->T.amt;
        }
        return bal;
}

std::string blockchain::get_history()
{
        /* Not many options for optimized prepending to strings/buffers in C++...
         * We'll just have to do it *suboptimally*.
         */
//        lock_gd lk{mut};

        if (!tail)
                return "[]";

        std::string out{"]"};
        for (const block *cur = tail; cur; cur = cur->P) {
                out = fmt::format(", {}", *cur).append(out);
        }

        out.erase(0, 1); // erase leading comma
        out[0] = '['; // replace leading space

        return out;

}

std::string blockchain::get_balances() {
//        lock_gd lk{mut};
        std::map<uint16_t, int> bals; // ID : balance
        constexpr int starting_balance = 10;

        for (block *cur = this->tail; cur; cur = cur->P) {
                uint16_t send = cur->T.sender, recv = cur->T.receiver, amt = cur->T.amt;
                if (!bals.contains(send))
                        bals[send] = starting_balance - amt;
                else
                        bals[send] -= amt;

                if (!bals.contains(recv))
                        bals[recv] = starting_balance + amt;
                else
                        bals[recv] += amt;
        }

        std::vector<std::string> res;
        for (auto client : bals)
                res.push_back(fmt::format("P{}: ${}", client.first, client.second));

        return fmt::format("{}", fmt::join(res, ", "));
}
