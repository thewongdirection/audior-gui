#include <set>
#include <string>

#include "../src/core/asr_models.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

const std::string kHashA(64, 'a'), kHashB(64, 'b');

bool Parses(const std::string &text, std::vector<ManifestEntry> *entries = nullptr)
{
    std::vector<ManifestEntry> local;
    std::wstring problem;
    const bool ok = ParseManifest(text, entries ? entries : &local, &problem);
    CHECK_EQ(ok, problem.empty());                      /* a reason if, and only if, it was refused */
    return ok;
}

}  // namespace

/* ---- the table: every model names where it comes from, its licence and its hash ---- */

TEST(Models_EveryEntryIsCompleteAndUnique)
{
    std::set<std::wstring> files;
    std::set<std::string> hashes;
    auto check = [&](const AsrModel &m) {
        const std::wstring file = m.file;
        CHECK(file.size() > 4 && file.compare(file.size() - 4, 4, L".bin") == 0);
        CHECK(file.find_first_of(L"\\/:") == std::wstring::npos);          /* a plain file name */

        const std::string hash = m.sha256;
        CHECK_EQ(hash.size(), std::size_t(64));
        for (char c : hash)
            CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));       /* lower-case hex */

        CHECK(m.bytes > 0);
        CHECK(m.label && *m.label);
        CHECK(m.licence && *m.licence);                                     /* no licence, no model */
        CHECK(m.source && std::wstring(m.source).compare(0, 8, L"https://") == 0);
        CHECK(files.insert(file).second);
        CHECK(hashes.insert(hash).second);
    };
    static_assert(kAsrModelCount > 0, "the recorder needs at least one model to offer");
    for (const AsrModel &model : kAsrModels)
        check(model);
    check(kVadModel);
}

/* ---- the manifest ---- */

TEST(Manifest_ReadsOneFilePerLine)
{
    std::vector<ManifestEntry> entries;
    CHECK(Parses(kHashA + "  audior_asr.exe\n" + kHashB + "  ggml-cpu.dll\n", &entries));
    CHECK_EQ(entries.size(), std::size_t(2));
    CHECK_EQ(entries[0].file, std::wstring(L"audior_asr.exe"));
    CHECK_EQ(entries[0].sha256, kHashA);
    CHECK_EQ(entries[1].file, std::wstring(L"ggml-cpu.dll"));
}

TEST(Manifest_AcceptsTheShaSumVariants)
{
    std::vector<ManifestEntry> entries;
    CHECK(Parses("# the pack\r\n\r\n" + kHashA + " *audior_asr.exe\r\n   \r\n" + std::string(64, 'B') + "  b.dll",
                 &entries));                            /* '*', comments, CRLF, no final newline */
    CHECK_EQ(entries.size(), std::size_t(2));
    CHECK_EQ(entries[0].file, std::wstring(L"audior_asr.exe"));
    CHECK_EQ(entries[1].sha256, kHashB);                /* upper case is read as lower case */
}

TEST(Manifest_RefusesAMalformedLine)
{
    CHECK(!Parses(""));                                 /* nothing listed */
    CHECK(!Parses("# only a comment\n"));
    CHECK(!Parses("abc  audior_asr.exe\n"));            /* a hash that is too short */
    CHECK(!Parses(kHashA + "\n"));                      /* no name */
    CHECK(!Parses(kHashA + "  \n"));
    CHECK(!Parses(std::string(63, 'a') + "g  x.exe\n"));            /* not hex */
    CHECK(!Parses(kHashA + "x.exe\n"));                 /* no separator */
    CHECK(!Parses(kHashA + "  ok.exe\n" + "oops\n"));   /* one bad line spoils the file */
}

TEST(Manifest_RefusesAnythingButAPlainFileName)
{
    for (const char *name : {"..\\evil.dll", "sub\\a.dll", "sub/a.dll", "..", ".", "C:a.dll", "\\\\server\\share\\a.dll",
                             "a?.dll", "a*.dll", "a|b.dll", "a\"b.dll", "a<b.dll", "a>b.dll"})
        CHECK(!Parses(kHashA + "  " + name + "\n"));
    CHECK(!Parses(kHashA + "  caf\xC3\xA9.dll\n"));     /* only printable ASCII */
    CHECK(!Parses(kHashA + "  with space.dll\n"));
}

TEST(Manifest_RefusesAFileListedTwice)
{
    CHECK(!Parses(kHashA + "  a.dll\n" + kHashB + "  a.dll\n"));
    CHECK(!Parses(kHashA + "  a.dll\n" + kHashB + "  A.DLL\n"));    /* Windows does not tell them apart */
}
