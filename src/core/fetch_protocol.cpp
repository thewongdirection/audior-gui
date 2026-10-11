#include "fetch_protocol.h"

namespace audiorgui::fetch {

namespace {

/* The digits of `text` (a non-empty run of them that fits 64 bits). */
bool ParseNumber(const std::string &text, std::size_t from, std::uint64_t *value)
{
    if (from >= text.size() || text.size() - from > 19)
        return false;
    std::uint64_t v = 0;
    for (std::size_t i = from; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9')
            return false;
        v = v * 10 + static_cast<std::uint64_t>(text[i] - '0');
    }
    *value = v;
    return true;
}

bool Parse(const std::string &text, Line *line)
{
    if (text == "D") {
        line->kind = Line::Kind::Done;
        return true;
    }
    if (text.size() > 2 && text[1] == ' ' && text[0] == 'P') {
        line->kind = Line::Kind::Progress;
        return ParseNumber(text, 2, &line->bytes);
    }
    if (text.size() >= 2 && text[1] == ' ' && text[0] == 'E') {
        line->kind = Line::Kind::Error;
        line->text.clear();
        for (std::size_t i = 2; i < text.size(); ++i)
            line->text += text[i] >= 0x20 && text[i] < 0x7F ? static_cast<wchar_t>(text[i]) : L'?';
        return true;
    }
    return false;
}

}  // namespace

std::string ProgressLine(std::uint64_t bytes) { return "P " + std::to_string(bytes) + "\n"; }

std::string DoneLine() { return "D\n"; }

std::string ErrorLine(const std::string &text)
{
    std::string line = "E ";
    for (char c : text)
        line += c >= 0x20 && c < 0x7F ? c : '?';
    if (line.size() > kMaxLine - 1)
        line.resize(kMaxLine - 1);
    return line + "\n";
}

void LineReader::Append(const void *data, std::size_t count, const OnLine &onLine)
{
    const char *bytes = static_cast<const char *>(data);
    for (std::size_t i = 0; i < count; ++i) {
        const char c = bytes[i];
        if (c == '\n') {
            Line line;
            if (!tooLong_ && !line_.empty() && line_.back() == '\r')
                line_.pop_back();
            if (!tooLong_ && Parse(line_, &line))
                onLine(line);
            line_.clear();
            tooLong_ = false;
        } else if (!tooLong_) {
            if (line_.size() >= kMaxLine) {
                tooLong_ = true;
                line_.clear();
            } else {
                line_ += c;
            }
        }
    }
}

}  // namespace audiorgui::fetch
