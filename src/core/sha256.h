/*
 * sha256.h - SHA-256, for checking that the transcription helper and a model
 * are the files they should be.
 *
 * It is a plain implementation of FIPS 180-4 (a few dozen lines, checked
 * against the standard's test vectors), so it needs no library and adds no DLL
 * to the program.  A check like this finds a damaged, truncated or swapped
 * file; it does not prove who made one.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace audiorgui {

class Sha256 {
public:
    Sha256();

    /* Adds data; any number of calls, of any sizes. */
    void Update(const void *data, std::size_t count);

    /* The digest as 64 lower-case hex characters.  The hasher is finished
     * after this: start a new one for more data. */
    std::string Finish();

private:
    void Block(const std::uint8_t *block);

    std::uint32_t state_[8];
    std::uint8_t  pending_[64];
    std::size_t   pendingBytes_ = 0;
    std::uint64_t totalBytes_ = 0;
};

/* The digest of `count` bytes in one go. */
std::string Sha256Hex(const void *data, std::size_t count);

}  // namespace audiorgui
