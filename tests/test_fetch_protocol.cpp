/* Tests of the lines the download program prints and how the recorder reads them. */
#include <string>
#include <vector>

#include "../src/core/fetch_protocol.h"
#include "test_support.h"

using namespace audiorgui;
using namespace audiorgui::fetch;

namespace {

/* Everything the reader reports for `chunks`, fed one after the other. */
std::vector<Line> Read(const std::vector<std::string> &chunks, LineReader *reader = nullptr)
{
    LineReader local;
    LineReader &r = reader ? *reader : local;
    std::vector<Line> lines;
    for (const std::string &chunk : chunks)
        r.Append(chunk.data(), chunk.size(), [&](const Line &l) { lines.push_back(l); });
    return lines;
}

}  // namespace

TEST(Fetch_LinesRoundTrip)
{
    const std::vector<Line> lines = Read({ProgressLine(0) + ProgressLine(190098681) + DoneLine() + ErrorLine("no route")});
    CHECK_EQ(lines.size(), std::size_t(4));
    CHECK(lines[0].kind == Line::Kind::Progress && lines[0].bytes == 0);
    CHECK(lines[1].kind == Line::Kind::Progress && lines[1].bytes == 190098681);
    CHECK(lines[2].kind == Line::Kind::Done);
    CHECK(lines[3].kind == Line::Kind::Error);
    CHECK_EQ(lines[3].text, std::wstring(L"no route"));
}

TEST(Fetch_ALineMayArriveInPieces)
{
    const std::vector<Line> lines = Read({"P 12", "3", "4\nD", "\n"});
    CHECK_EQ(lines.size(), std::size_t(2));
    CHECK_EQ(lines[0].bytes, std::uint64_t(1234));
    CHECK(lines[1].kind == Line::Kind::Done);

    std::vector<std::string> bytes;                                     /* one byte at a time, as a pipe may deliver it */
    for (char c : ProgressLine(77))
        bytes.push_back(std::string(1, c));
    CHECK_EQ(Read(bytes).size(), std::size_t(1));
}

TEST(Fetch_ALineThatDoesNotParseIsSkipped)
{
    const std::vector<Line> lines =
        Read({"hello\nP\nP \nP x\nP 12x\nX 5\nPP 5\n\nP 9\nDD\nD \nE\n"});    /* only "P 9" is good */
    CHECK_EQ(lines.size(), std::size_t(1));
    CHECK_EQ(lines[0].bytes, std::uint64_t(9));
}

TEST(Fetch_ANumberThatDoesNotFitIsRefused)
{
    CHECK(Read({"P 99999999999999999999999\n"}).empty());               /* no wrapping round to a small number */
    CHECK_EQ(Read({"P 9999999999999999999\n"}).size(), std::size_t(1)); /* 19 digits fit in 64 bits */
}

TEST(Fetch_ACarriageReturnBeforeTheNewLineIsIgnored)
{
    const std::vector<Line> lines = Read({"P 5\r\nD\r\n"});
    CHECK_EQ(lines.size(), std::size_t(2));
    CHECK_EQ(lines[0].bytes, std::uint64_t(5));
}

TEST(Fetch_ALineThatIsTooLongIsDroppedAndTheNextOneStillRead)
{
    const std::string huge = "E " + std::string(kMaxLine * 4, 'x') + "\n";
    const std::vector<Line> lines = Read({huge.substr(0, 700), huge.substr(700), "P 3\n"});
    CHECK_EQ(lines.size(), std::size_t(1));
    CHECK_EQ(lines[0].bytes, std::uint64_t(3));

    const std::string fits = "E " + std::string(kMaxLine - 2, 'y') + "\n";        /* exactly kMaxLine characters */
    CHECK_EQ(Read({fits}).size(), std::size_t(1));
}

TEST(Fetch_AReaderNeverHoldsMoreThanOneLine)
{
    LineReader reader;
    for (int i = 0; i < 200; ++i)                                       /* a long run with no line end at all */
        Read({std::string(1000, 'z')}, &reader);
    CHECK_EQ(Read({"D\n"}, &reader).size(), std::size_t(0));            /* its end: it is not taken for a "D" line */
    CHECK_EQ(Read({"D\n"}, &reader).size(), std::size_t(1));            /* and the next line reads normally */
}

TEST(Fetch_ErrorTextIsPrintableAsciiWhateverIsSent)
{
    const std::vector<Line> lines = Read({std::string("E bad \x01\xC3\xA9 text\n")});
    CHECK_EQ(lines.size(), std::size_t(1));
    CHECK_EQ(lines[0].text, std::wstring(L"bad ??? text"));

    const std::string line = ErrorLine(std::string("a\nb\x80") + std::string(2000, 'q'));      /* what is printed is clean, too */
    CHECK(line.size() <= kMaxLine);
    CHECK_EQ(line.find('\n'), line.size() - 1);                         /* one line, ended once */
    CHECK_EQ(line.substr(0, 7), std::string("E a?b?q"));
}
