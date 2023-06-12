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


namespace fsbuf::detail {
    template<typename R>
    concept FS_BUF_T = std::is_trivially_copyable_v<R>
                       && std::is_nothrow_default_constructible_v<R>;

    /**
     * Filesystem-backed dynamically resizable storage.
     * Don't instantiate this class directly; use fs_buf instead!!
     */
    template<typename T, FS_BUF_T R>
    class _fs_buf_base
    {
    protected:
        static constexpr const char *storage_path = "./ramfs";


        explicit _fs_buf_base(const char *file_label);

    public:
        _fs_buf_base() = delete;

        _fs_buf_base(const _fs_buf_base &) = delete;

        _fs_buf_base(_fs_buf_base &&other) noexcept;

        ~_fs_buf_base();

        _fs_buf_base &operator=(const _fs_buf_base &) = delete;

        _fs_buf_base &operator=(_fs_buf_base &&) = delete;

        /// Grows the backing container to at least `n_entries` elements.
        /// Takes no action if container size is already larger than
        /// `n_entries * sizeof(T)`.
        void reserve(size_t n_entries);

        /// 1-indexed!!!!! grows container to `pos` if necessary
        T &operator[](size_t pos);

        T &operator*() { return *buf; }

        const T &operator*() const { return *buf; }

        T *operator->() { return buf; }

        const T *operator->() const { return buf; }

        const T *cbegin() const { return buf; }

        const T *cend() const { return buf + (bufsize / R_size); }

    protected:
        static constexpr char dummydata = '\0';
        static constexpr size_t R_size = sizeof(R);

        size_t bufsize{0}; // actual size of backing file in bytes

        R *buf{nullptr};
        int backing_fd{-1};

        /// assumes that caller has already verified newsize > bufsize.
        /// grows backing memory *without* default-constructing anything.
        void grow_to(size_t newsize);

        /// grows backing file on disk using lseek() + write()
        bool grow_file(off_t newsize);

        bool idx2high(size_t idx)
        { return idx * R_size > bufsize; }
    };

    template <typename T, FS_BUF_T R>
        requires std::is_constructible_v<T, R>
                && std::is_constructible_v<R, T>
    struct _R_proxy //NOLINT(bugprone-reserved-identifier)
    {
        _R_proxy() = default;

        explicit _R_proxy(T &&arg)
        : val(std::forward<T>(arg))
        { }

        // TODO: this will perform an unnecessary copy if std::is_same_v<T, R>
        // do NOT mark this explicit; it breaks many things!
        operator T() //NOLINT(google-explicit-constructor)
        { return T(val); }

    private:
        R val;
    };
}

/*
 * Goal: instantiate as either fs_buf<trivial_type>
 * or fs_buf<nontrivial_type, serialized_type>
 *
 * For the general/nontrivially copyable (first) case,
 * we want to store a trivially copyable type in the
 * actual buffer, but have our write operations be able
 * to accept the value_type. Read operations should return
 * the value_type, but should not grow the backing array unless needed.
 * We just require that the type T has some constructor that can
 * accept a value of type R.
 *
 * For the trivially copyable (second) template case, we don't want
 * to change the semantics at all from how they were originally.
 */

template <typename T, fsbuf::detail::FS_BUF_T R=T>
        requires std::is_convertible_v<fsbuf::detail::_R_proxy<T, R>, T>
class fs_buf : public fsbuf::detail::_fs_buf_base<T, fsbuf::detail::_R_proxy<T, R>>
{
    using _wrap_proxy = fsbuf::detail::_R_proxy<T, R>;
    using fs_buf_base = fsbuf::detail::_fs_buf_base<T, _wrap_proxy>;

public:
    explicit fs_buf(const char *file_label)
            : fs_buf_base(file_label)
    { }

    fs_buf(fs_buf &&other) noexcept
    : fs_buf_base(std::forward<fs_buf>(other))
    { }

    fs_buf() = delete;
    fs_buf(const fs_buf &other) = delete;

    fs_buf &operator=(const fs_buf &) = delete;
    fs_buf &operator=(fs_buf &&) = delete;

//    T &operator[](size_t n)
//    {  }
};

template <fsbuf::detail::FS_BUF_T T>
class fs_buf<T, T> : public fsbuf::detail::_fs_buf_base<T, T>
{
    using fs_buf_base = fsbuf::detail::_fs_buf_base<T, T>;
public:
    explicit fs_buf(const char *file_label)
            : fs_buf_base(file_label)
    { }

    fs_buf(fs_buf &&other) noexcept
    : fs_buf_base(std::forward(other))
    { }

    fs_buf() = delete;
    fs_buf(const fs_buf &other) = delete;

    fs_buf &operator=(const fs_buf &) = delete;
    fs_buf &operator=(fs_buf &&) = delete;
};

#include "fs_buf.ipp"