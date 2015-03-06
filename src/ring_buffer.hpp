#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include "util.hpp"

#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <atomic>

using std::atomic_int;

#ifndef ATOMIC_INT_LOCK_FREE
#error "require atomic int to be lock free"
#endif

class RingBuffer {
public:
    RingBuffer(int requested_capacity) {
        // round size up to the nearest power of two
        int pow2_size = powf(2, ceilf(log2(requested_capacity)));
        // at minimum must be page size
        int page_size = getpagesize();
        _capacity = max(pow2_size, page_size);

        _write_offset = 0;
        _read_offset = 0;

        char shm_path[] = "/dev/shm/ring-buffer-XXXXXX";
        int fd = mkstemp(shm_path);
        if (fd < 0)
            panic("unable to open shared memory");

        if (unlink(shm_path))
            panic("unable to unlink shared memory path");

        if (ftruncate(fd, _capacity))
            panic("unable to allocate shared memory");

        _address = (char*)mmap(NULL, _capacity * 2, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (_address == MAP_FAILED)
            panic("mmap init failed");

        char *other_address = (char*)mmap(_address, _capacity, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_SHARED, fd, 0);
        if (other_address != _address)
            panic("first mmap failed");

        other_address = (char*)mmap(_address + _capacity, _capacity, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_SHARED, fd, 0);
        if (other_address != _address + _capacity)
            panic("second mmap failed");

        if (close(fd))
            panic("unable to close fd");
    }

    ~RingBuffer() {
        if (munmap(_address, _capacity * 2))
            panic("munmap failed");
    }

    int capacity() const {
        return _capacity;
    }

    // don't write more than `size()`
    char *write_ptr() const {
        return _address + _write_offset;
    }

    void advance_write_ptr(int count) {
        _write_offset += count;
    }

    // don't read more than `size()`
    char *read_ptr() const {
        return _address + _read_offset;
    }

    void advance_read_ptr(int count) {
        _read_offset += count;

        if (_read_offset >= _capacity) {
            _read_offset -= _capacity;
            _write_offset -= _capacity;
        }
    }

    // how much of the buffer is used, ready for reading
    int fill_count() const {
        return _write_offset - _read_offset;
    }

    // how much is available, ready for writing
    int free_count() const {
        return _capacity - fill_count();
    }
 
    // set all bytes to 0 and reset read and write offset
    void clear() {
        _write_offset = 0;
        _read_offset = 0;
    }

private:
    char *_address;
    int _capacity;
    atomic_int _write_offset;
    atomic_int _read_offset;

    RingBuffer(const RingBuffer &copy) = delete;
    RingBuffer &operator=(const RingBuffer &copy) = delete;
};

#endif
