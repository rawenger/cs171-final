//
// Created by ryan on 5/20/23.
//

#pragma once
#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <vector>
#include <type_traits>

template <typename T>
concept FS_BUF_T = std::is_trivially_copyable_v<T>;
/**
 * Filesystem-backed dynamically resizable storage.
 */
template <FS_BUF_T T>
class fs_buf {
public:
    explicit fs_buf(uint8_t my_id, bool restore=false);
    fs_buf(const fs_buf &other) = delete;
    fs_buf(fs_buf &&other) noexcept;
    ~fs_buf();

    fs_buf &operator=(const fs_buf &other) = delete;
    fs_buf &operator=(fs_buf &&other) = delete;

    /// Grows the backing container to at least `n_entries` elements.
    /// Takes no action if container size is already larger than
    /// `n_entries * sizeof(T)`.
    void reserve(size_t n_entries);

    /// 1-indexed!!!!! grows container to `pos` if necessary
    T& operator[](size_t pos);

private:
    static constexpr char dummydata = '\0';
    static constexpr size_t T_size = sizeof(T);

    off_t bufsize {0}; // actual size of file/number of elements in vector

    T *buf {nullptr};
    int backing_fd {-1};

    void grow_to(size_t newsize);
};

#include "fs_buf.ipp"