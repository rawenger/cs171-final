//
// Created by ryan on 4/13/23.
//

#pragma once
#include <semaphore>
#include <cstdint>

/** Semaphore-controlled FIFO queue with exactly one producer and one consumer.
 * Use a size large enough that the queue will *never* become full!
 * The design assumes we will NEVER become full!
 * BAAAD things will happen if we become full!
 * Also, the size has to be a power of 2.
 */
template <typename T, size_t bufsize=128>
        requires std::is_trivially_copyable_v<T>
                && ((bufsize & (bufsize - 1)) == 0)
class sema_q {
    size_t head{0};
    size_t tail{0};

    std::counting_semaphore<bufsize> size{0};
    T buf[bufsize] {};

public:
    sema_q() = default;

    void push(T i)
    {
        buf[tail] = i;
        tail = (tail + 1) & (bufsize - 1);
        size.release();
    }

    T pop()
    {
        size.acquire();
        size_t hd = head;
        head = (head + 1) & (bufsize - 1);
        return buf[hd];
    }
};

