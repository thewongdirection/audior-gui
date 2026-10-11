/*
 * fetch_protocol.h - What the download program (audior_fetch.exe) says on its
 * standard output, and how the recorder reads it.
 *
 * The program is a separate process so that the recorder itself has no networking
 * code.  It is started with the address, the file to write and the size to expect,
 * and reports one line at a time:
 *
 *     P <bytes>       that many bytes have been written so far
 *     D               the whole file has been written
 *     E <text>        it failed, and why (printable ASCII)
 *
 * No last line means it died.  What comes through the pipe is data from another
 * process: a line that does not parse is skipped, and none can be longer than
 * kMaxLine, so a confused or hostile program cannot make the reader grow.
 * Plain C++, no Windows.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace audiorgui::fetch {

inline constexpr std::size_t kMaxLine = 512;

std::string ProgressLine(std::uint64_t bytes);
std::string DoneLine();
std::string ErrorLine(const std::string &text);

struct Line {
    enum class Kind { Progress, Done, Error };

    Kind          kind  = Kind::Done;
    std::uint64_t bytes = 0;            /* Progress */
    std::wstring  text;                 /* Error: printable ASCII only */
};

class LineReader {
public:
    using OnLine = std::function<void(const Line &)>;

    /* Takes the next `count` bytes of the output and calls `onLine` for each whole line in them. */
    void Append(const void *data, std::size_t count, const OnLine &onLine);

private:
    std::string line_;
    bool        tooLong_ = false;       /* skipping the rest of a line that did not fit */
};

}  // namespace audiorgui::fetch
