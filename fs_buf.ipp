//
// Created by ryan on 5/20/23.
//

#include <unistd.h>

#include <filesystem>
#include <fcntl.h>
#include <sys/mman.h>

#include "debug.h"

static size_t round_to_pagesize(size_t s)
{
        // round newsize up to the nearest multiple of pagesize
        // math from https://stackoverflow.com/a/22971450
        auto pagesize = sysconf(_SC_PAGESIZE);

        return ((pagesize - 1) & s)
                ? ((s + pagesize) & ~(pagesize - 1))
                : s;
}

template<FS_BUF_T T>
fs_buf<T>::fs_buf(cs171_cfg::node_id_t my_id, const char *file_label)
{
        namespace fs = std::filesystem;
        auto filepath = fs::path{"./ramfs"} / (std::to_string(my_id) + file_label);
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
                lseek(backing_fd, min_target_size, SEEK_SET);
                write(backing_fd, &dummydata, sizeof dummydata);
                bufsize = min_target_size;
        } else {
                bufsize = round_to_pagesize(bufsize);
        }

        buf = static_cast<T *>( mmap(nullptr,
                                bufsize,
                                PROT_READ | PROT_WRITE,
                                MAP_FILE | MAP_SHARED,
                                backing_fd, 0) );

        if (buf == MAP_FAILED) {
                perror("unable to mmap from disk");
                exit(EXIT_FAILURE);
        }

        if (!do_restore) {
                // IMPORTANT: we don't know that bufsize will be a multiple of
                // sizeof(T)
                for (size_t i = 0; i < bufsize / T_size; i++) {
                        std::construct_at(buf + i);
                }
        }
}

template<FS_BUF_T T>
fs_buf<T>::~fs_buf()
{
        if (buf) {
                msync(buf, bufsize, MS_ASYNC);
                munmap(buf, bufsize);
        }

        if (backing_fd >= 0)
                close(backing_fd);
}

template<FS_BUF_T T>
fs_buf<T>::fs_buf(fs_buf &&other) noexcept
: bufsize(other.bufsize), buf(other.buf), backing_fd(other.backing_fd)
{
        other.bufsize = 0;
        other.buf = nullptr;
        other.backing_fd = -1;
}

// TODO: default construct newly added elements
template<FS_BUF_T T>
void fs_buf<T>::grow_to(size_t newsize)
{
        lseek(backing_fd, newsize, SEEK_SET);
        // write() required after lseek() in order for file to grow
        write(backing_fd, &dummydata, sizeof dummydata);

        if (buf)
#if !(defined(__linux__) && defined(_GNU_SOURCE))
        {
                msync(buf, bufsize, MS_ASYNC);
                munmap(buf, bufsize);
        }
#else
                buf = static_cast<T *>( mremap(buf, bufsize, newsize, MREMAP_MAYMOVE) );
        else // notice this is here but not in the other #if branch lol
#endif
                buf = static_cast<T *>( mmap(nullptr,
                                        newsize,
                                        PROT_READ | PROT_WRITE,
                                        MAP_FILE | MAP_SHARED,
                                        backing_fd, 0) );

        if (buf == MAP_FAILED) {
                perror("Unable to grow disk buffer");
                exit(EXIT_FAILURE); // LOL get rekt
        }

        bufsize = newsize;
}

template<FS_BUF_T T>
T &fs_buf<T>::operator[](size_t pos)
{
        reserve(pos);

        return buf[pos-1];
}

template<FS_BUF_T T>
void fs_buf<T>::reserve(size_t n_entries) {
        size_t newsize = n_entries * T_size;

        if (newsize <= bufsize)
                return;

        newsize = round_to_pagesize(newsize);

        this->grow_to(newsize);

        // TODO: default construct new elements
}