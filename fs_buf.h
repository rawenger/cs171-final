//
// Created by ryan on 5/20/23.
//

#pragma once
#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <type_traits>
#include <iterator>

#include "cs171_cfg.h"

template <typename T>
concept FS_BUF_T = std::is_trivially_copyable_v<T>
                        && std::is_nothrow_default_constructible_v<T>;
/**
 * Filesystem-backed dynamically resizable storage.
 */
template <FS_BUF_T T>
class fs_buf {
    static constexpr const char *storage_path = "./ramfs";
public:
    explicit fs_buf(const char *file_label);
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
    T &operator[](size_t pos);

    T &operator*()
    { return *buf; }

    const T &operator*() const
    { return *buf; }

    T *operator->()
    { return buf; }

    const T *operator->() const
    { return buf; }

    const T *cbegin() const
    { return buf; }

    const T *cend() const
    { return buf + (bufsize / T_size); }

//    struct iterator {
//        // see https://stackoverflow.com/a/72405619
//        using iterator_concept [[maybe_unused]] = std::contiguous_iterator_tag;
//        using difference_type = std::ptrdiff_t;
//        using element_type = T;
//        using pointer = element_type *;
//        using reference = element_type &;
//
//
//    private:
//        pointer ptr;
//    };
//    static_assert(std::contiguous_iterator<iterator>);

private:
    static constexpr char dummydata = '\0';
    static constexpr size_t T_size = sizeof(T);

    size_t bufsize {0}; // actual size of backing file in bytes

    T *buf {nullptr};
    int backing_fd {-1};

    /// assumes that caller has already verified newsize > bufsize.
    /// grows backing memory *without* default-constructing anything.
    void grow_to(size_t newsize);

    /// grows backing file on disk using lseek() + write()
    bool grow_file(off_t newsize);
};

#include "fs_buf.ipp"