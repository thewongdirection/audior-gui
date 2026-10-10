#include "ring_buffer.h"

#include <algorithm>
#include <cstring>

namespace audiorgui {

RingBuffer::RingBuffer(std::size_t capacity) : data_(std::max<std::size_t>(capacity, 1)) {}

std::size_t RingBuffer::Write(const void *data, std::size_t count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    count = std::min(count, data_.size() - used_);
    const std::uint8_t *src = static_cast<const std::uint8_t *>(data);
    const std::size_t tail = (head_ + used_) % data_.size();
    const std::size_t first = std::min(count, data_.size() - tail);        /* up to the end of the buffer */
    if (count) {
        std::memcpy(data_.data() + tail, src, first);
        std::memcpy(data_.data(), src + first, count - first);               /* the wrapped part */
    }
    used_ += count;
    return count;
}

std::size_t RingBuffer::Read(void *out, std::size_t count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    count = std::min(count, used_);
    std::uint8_t *dst = static_cast<std::uint8_t *>(out);
    const std::size_t first = std::min(count, data_.size() - head_);
    if (count) {
        std::memcpy(dst, data_.data() + head_, first);
        std::memcpy(dst + first, data_.data(), count - first);
    }
    head_ = (head_ + count) % data_.size();
    used_ -= count;
    return count;
}

std::size_t RingBuffer::Used() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return used_;
}

}  // namespace audiorgui
