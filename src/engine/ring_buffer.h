/*
 * ring_buffer.h - A fixed-size, thread-safe byte queue.
 *
 * It carries audio from a capture thread to the writer thread.  Its size is
 * the whole of the memory a stream may use, however long the recording runs:
 * a writer that cannot keep up fills it, and the session then ends the
 * recording rather than grow without bound.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace audiorgui {

class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity);

    /* Appends up to `count` bytes and returns how many fit. */
    std::size_t Write(const void *data, std::size_t count);

    /* Removes up to `count` bytes (oldest first) into `out`; returns how many. */
    std::size_t Read(void *out, std::size_t count);

    std::size_t Used() const;

private:
    mutable std::mutex        mutex_;
    std::vector<std::uint8_t> data_;
    std::size_t               head_ = 0;     /* where the oldest byte is */
    std::size_t               used_ = 0;
};

}  // namespace audiorgui
