//
// Created by ryan on 4/8/23.
//

#ifndef CSIL
#include <openssl/evp.h>
#endif

#include <cassert>
#include <iterator>
#include <vector>
#include <map>

#include <fmt/core.h>

#include "debug.h"

// we use a different transaction formatter here
#define DISABLE_TRANSACTION_FORMAT_AS
#include "blockchain.h"

blockchain blockchain::BLOCKCHAIN;
#ifndef CSIL
static EVP_MD_CTX *sha256_ctx;
#endif

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

std::string format_as(const blockchain::block &blk)
{
        return fmt::format("({}, {})", blk.T, blk.H);
}

blockchain::block::block(const transaction &t, block *prev)
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
#ifndef CSIL
        assert(sha256_ctx != nullptr);
#endif
        u256 sha_out {0};

        this->N = 0;
        hash(sha_out);

        while((sha_out[0] & (7 << 5)) != 0) {
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
        uint32_t sha_size = 32;
//        std::string sha_in = fmt::format("{}{:e}{}", H, T, N);
        std::string sha_in = fmt::format("{}{}{}", H, T, N);
#ifdef CSIL
        out[0] = 0;
#else
        EVP_DigestInit_ex2(sha256_ctx, nullptr, nullptr);
//        DBG("H: {}, ", H);
//        DBG("T: {:e}, ", T);
//        DBG("N: {}\n", N);
//        DBG("SHA-256 input: {}\n", sha_in);
        EVP_DigestUpdate(sha256_ctx, sha_in.c_str(), sha_in.length());
        EVP_DigestFinal_ex(sha256_ctx, out.data(), &sha_size);
#endif
        assert(sha_size == 32);
}

blockchain::blockchain()
{
#ifndef CSIL
        sha256_ctx = EVP_MD_CTX_create();
        EVP_DigestInit_ex(sha256_ctx, EVP_sha256(), nullptr);
#endif
}

blockchain::~blockchain()
{
#ifndef CSIL
        EVP_MD_CTX_destroy(sha256_ctx);
#endif
        delete tail;
}

bool blockchain::transact(const transaction &t)
{
//        lock_gd lk{mut};

//        if (balance(t.sender) < t.amt) {
//                return false;
//        }

        tail = new block(t, tail);

        return true;
}

std::string blockchain::get_history()
{
        /* Not many options for optimized prepending to strings/buffers in C++...
         * We'll just have to do it *suboptimally*.
         */
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

