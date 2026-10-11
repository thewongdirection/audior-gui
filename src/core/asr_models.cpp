#include "asr_models.h"

#include <algorithm>

namespace audiorgui {

namespace {

bool IsHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

/* File names here are ASCII, and Windows does not tell "A.dll" from "a.dll". */
bool SameName(const std::wstring &a, const std::wstring &b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const wchar_t x = a[i] >= L'A' && a[i] <= L'Z' ? a[i] - L'A' + L'a' : a[i];
        const wchar_t y = b[i] >= L'A' && b[i] <= L'Z' ? b[i] - L'A' + L'a' : b[i];
        if (x != y)
            return false;
    }
    return true;
}

}  // namespace

const AsrModel *FindAsrModel(const std::wstring &file)
{
    for (const AsrModel &m : kAsrModels) {
        if (SameName(file, m.file))
            return &m;
    }
    return nullptr;
}

std::wstring ModelUrl(const AsrModel &model) { return std::wstring(model.source) + L"/resolve/main/" + model.file; }

std::vector<const AsrModel *> SmallerModels(const AsrModel *table, const AsrModel *chosen,
                                            const std::vector<const AsrModel *> &installed, bool keepLanguages)
{
    std::vector<const AsrModel *> smaller;
    for (std::ptrdiff_t i = chosen - table - 1; i >= 0; --i) {
        const AsrModel *m = table + i;
        const bool present = std::find(installed.begin(), installed.end(), m) != installed.end();
        if (present && (m->multilingual || !keepLanguages || !chosen->multilingual))
            smaller.push_back(m);
    }
    return smaller;
}

namespace {

bool IsPlainFileName(const std::string &name)
{
    if (name.empty() || name == "." || name == "..")
        return false;
    for (char c : name) {
        if (c < 0x21 || c > 0x7E || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            return false;
    }
    return true;
}

}  // namespace

bool ParseManifest(const std::string &text, std::vector<ManifestEntry> *entries, std::wstring *problem)
{
    entries->clear();
    std::size_t at = 0;
    int number = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        ++number;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;

        const std::wstring where = L"The helper's manifest, line " + std::to_wstring(number) + L": ";
        if (line.size() < 66 || line[64] != ' ') {
            *problem = where + L"expected a SHA-256 and a file name.";
            return false;
        }
        std::string hash = line.substr(0, 64);
        for (char &c : hash) {
            if (!IsHex(c)) {
                *problem = where + L"the SHA-256 is not 64 hexadecimal characters.";
                return false;
            }
            c = static_cast<char>(c >= 'A' && c <= 'F' ? c - 'A' + 'a' : c);
        }
        std::size_t nameAt = 65;
        while (nameAt < line.size() && (line[nameAt] == ' ' || line[nameAt] == '*'))
            ++nameAt;
        const std::string name = line.substr(nameAt);
        if (!IsPlainFileName(name)) {
            *problem = where + L"the file name must be a plain name, with no folder.";
            return false;
        }

        ManifestEntry entry;
        entry.sha256 = hash;
        entry.file.assign(name.begin(), name.end());
        for (const ManifestEntry &other : *entries) {
            if (SameName(other.file, entry.file)) {
                *problem = where + L"\"" + entry.file + L"\" is listed twice.";
                return false;
            }
        }
        entries->push_back(entry);
    }
    if (entries->empty()) {
        *problem = L"The helper's manifest lists no files.";
        return false;
    }
    return true;
}

}  // namespace audiorgui
