//
// Created by ryan on 4/13/23.
//

#pragma once

#include <chrono>
#include <cstdint>
#include <semaphore>
#include <optional>
#include <mutex>
#include <shared_mutex>

using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<Clock>;

/** Semaphore-controlled FIFO queue with exactly one producer and one consumer.
 * Use a size large enough that the queue will *never* become full!
 * The design assumes we will NEVER become full!
 * BAAAD things will happen if we become full!
 * Also, the size has to be a power of 2.
 *
 * Update: this has been bastardized a bit and now is weird.
 */
template <typename T, bool multiple_writers=false, size_t bufsize=128>
        //requires std::is_trivially_copyable_v<T>
        //        && ((bufsize & (bufsize - 1)) == 0)
class sema_q {
    size_t head{0};
    size_t tail{0};
    std::mutex write_lock;
    std::shared_mutex dump_lk;

    std::counting_semaphore<bufsize> size{0};
    T buf[bufsize] {};

public:
    sema_q() = default;

    void push(T i)
    {
        std::shared_lock<decltype(dump_lk)> sl {dump_lk};

        if constexpr (multiple_writers) {
            std::lock_guard<decltype(write_lock)> lk {write_lock};
            buf[tail] = i;
            tail = (tail + 1) & (bufsize - 1);
            size.release();
        } else {
            buf[tail] = i;
            tail = (tail + 1) & (bufsize - 1);
            size.release();
        }
    }

    T pop()
    {
        size.acquire();
        std::shared_lock<decltype(dump_lk)> sl {dump_lk};
        size_t hd = head;
        head = (head + 1) & (bufsize - 1);
        return buf[hd];
    }

    T front()
    {
            // need to block until we can acquire, but not actually
            // consume anything
            size.acquire();
            size.release();
            return buf[head];
    }

    // NOTE: don't expect to use this on the same queue object as format_as()!!!
    std::optional<T> try_pop_until(const TimePoint& abs_time)
    {
        if (size.try_acquire_until(abs_time)) {
            size_t hd = head;
            head = (head + 1) & (bufsize - 1);
            return {buf[hd]};
        } else {
            return {};
        }
    }

    std::string format()
    {
        std::unique_lock<decltype(dump_lk)> sl {dump_lk};
        std::string result = "[";

        for (size_t idx = head; idx != tail; idx = (idx + 1) & (bufsize - 1)) {
            result += fmt::format("{} ", buf[idx]);
        }

        return result + ']';
    }
};
