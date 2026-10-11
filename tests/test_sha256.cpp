#include <algorithm>
#include <string>

#include "../src/core/sha256.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

std::string Hex(const std::string &text) { return Sha256Hex(text.data(), text.size()); }

}  // namespace

/* The vectors of FIPS 180-4 / NIST. */

TEST(Sha256_Empty)
{
    CHECK_EQ(Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST(Sha256_Abc)
{
    CHECK_EQ(Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

TEST(Sha256_ATwoBlockMessage)
{
    /* 56 bytes: the padding no longer fits in the first block. */
    CHECK_EQ(Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(Sha256_AMillionAs)
{
    Sha256 hasher;
    const std::string piece(1000, 'a');
    for (int i = 0; i < 1000; ++i)
        hasher.Update(piece.data(), piece.size());
    CHECK_EQ(hasher.Finish(), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST(Sha256_PiecesOfAnySizeGiveTheSameDigest)
{
    std::string data;
    for (int i = 0; i < 5000; ++i)
        data += static_cast<char>('A' + (i * 7) % 26);
    const std::string whole = Hex(data);

    for (std::size_t piece : {std::size_t(1), std::size_t(55), std::size_t(63), std::size_t(64), std::size_t(65),
                              std::size_t(1000)}) {
        Sha256 hasher;
        for (std::size_t at = 0; at < data.size(); at += piece)
            hasher.Update(data.data() + at, std::min(piece, data.size() - at));
        CHECK_EQ(hasher.Finish(), whole);
    }
}

TEST(Sha256_LengthsAroundTheBlockSizeDiffer)
{
    std::string previous;
    for (std::size_t length : {54, 55, 56, 57, 63, 64, 65, 119, 120, 128}) {
        const std::string digest = Hex(std::string(length, 'x'));
        CHECK_EQ(digest.size(), std::size_t(64));
        CHECK(digest != previous);
        previous = digest;
    }
}
