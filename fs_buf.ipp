//
// Created by ryan on 5/20/23.
//

#include <unistd.h>

#include <filesystem>
#include <fcntl.h>
#include <sys/mman.h>

#include "debug.h"

extern cs171_cfg::node_id_t my_id;

/// round `sz` up to the nearest multiple of the system's page size
static size_t round_to_pagesize(size_t sz)
{
        auto pagesize = sysconf(_SC_PAGESIZE);

        // math from https://stackoverflow.com/a/22971450
        return ((pagesize - 1) & sz)
                ? ((sz + pagesize) & ~(pagesize - 1))
                : sz;
}

namespace fsbuf::detail {
template<typename T, FS_BUF_T R>
bool _fs_buf_base<T, R>::grow_file(off_t newsize) {
        if (lseek(backing_fd, newsize, SEEK_SET) < 0
            || write(backing_fd, &dummydata, sizeof dummydata) < 0) {
                perror("Unable to grow backing file on disk");
                exit(EXIT_FAILURE);
        }

        return true;
}

template<typename T, FS_BUF_T R>
_fs_buf_base<T, R>::_fs_buf_base(const char *file_label) {
        assert(R_size < sysconf(_SC_PAGESIZE));

        namespace fs = std::filesystem;
        auto filepath = fs::path{storage_path} / (std::to_string(my_id) + file_label);
        bool do_restore = fs::exists(filepath);
        int oflags = O_RDWR | (do_restore ? 0 : O_CREAT);
        backing_fd = open(filepath.c_str(), oflags, S_IRUSR | S_IWUSR); // chmod 0600

        if (backing_fd < 0) {
                perror("unable to open disk backup");
                exit(EXIT_FAILURE);
        }

        DBG("Node P{} using backup {} file\n", my_id, filepath.c_str());

        /* ---------------NOT TRUE ANYMORE (but might need it later idk)----------------
         * The first `sizeof(n_elems)` bytes in the file are used to store the number of
         * elements in the buffer (yes, it's a waste of a disk block LOL oh well; that's
         * why we're using a RAM disk).
         *
         * This logic is awkward because ultimately we want the filesize variable to not
         * include those first 4/8/etc. bytes of the size_t, but we do need to take into
         * account here the fact that they're present.
         */
        off_t min_target_size = sysconf(_SC_PAGESIZE);
        bufsize = fs::file_size(filepath);

        if (bufsize < min_target_size) {
                grow_file(min_target_size);
                bufsize = min_target_size;
        } else {
                bufsize = round_to_pagesize(bufsize);
        }

        buf = static_cast<R *>( mmap(nullptr,
                                     bufsize,
                                     PROT_READ | PROT_WRITE,
                                     MAP_FILE | MAP_SHARED,
                                     backing_fd, 0));

        if (buf == MAP_FAILED) {
                perror("unable to mmap from disk");
                exit(EXIT_FAILURE);
        }

        if (!do_restore) {
                // IMPORTANT: we don't know that bufsize will be an integer
                // multiple of T_size
                for (size_t i = 0; i < bufsize / R_size; i++) {
                        std::construct_at(buf + i);
                }
        }
}

template<typename T, FS_BUF_T R>
_fs_buf_base<T, R>::~_fs_buf_base() {
        if (buf) {
                msync(buf, bufsize, MS_ASYNC);
                munmap(buf, bufsize);
        }

        if (backing_fd >= 0)
                close(backing_fd);
}

template<typename T, FS_BUF_T R>
_fs_buf_base<T, R>::_fs_buf_base(_fs_buf_base &&other) noexcept
        : bufsize(other.bufsize), buf(other.buf), backing_fd(other.backing_fd) {
        other.bufsize = 0;
        other.buf = nullptr;
        other.backing_fd = -1;
}

template<typename T, FS_BUF_T R>
void _fs_buf_base<T, R>::grow_to(size_t newsize) {
        grow_file(newsize);

        if (buf && buf != MAP_FAILED)
#if !(defined(__linux__) && defined(_GNU_SOURCE))
                {
                        msync(buf, bufsize, MS_ASYNC);
                        munmap(buf, bufsize);
                }
#else
                buf = static_cast<R *>( mremap(buf, bufsize, newsize, MREMAP_MAYMOVE));
        else // notice this is here but not in the other #if branch lol
#endif
                buf = static_cast<R *>( mmap(nullptr,
                                             newsize,
                                             PROT_READ | PROT_WRITE,
                                             MAP_FILE | MAP_SHARED,
                                             backing_fd, 0));

        if (buf == MAP_FAILED) {
                perror("Unable to grow disk buffer");
                exit(EXIT_FAILURE); // LOL get rekt
        }

        bufsize = newsize;
}

template<typename T, FS_BUF_T R>
T &_fs_buf_base<T, R>::operator[](size_t pos)
{
        reserve(pos);

        return buf[pos - 1];
}

template<typename T, FS_BUF_T R>
void _fs_buf_base<T, R>::reserve(size_t n_entries) {
        if (!idx2high(n_entries))
                return;

        size_t oldsize = bufsize;

        size_t newsize = round_to_pagesize(n_entries * R_size);

        this->grow_to(newsize);

        // default construct new elements
        for (size_t i = oldsize / R_size; i < newsize / R_size; i++)
                std::construct_at(buf + i);
}

} // end namespace fsbuf::detail