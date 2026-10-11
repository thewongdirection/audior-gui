/*
 * fetch.cpp - audior_fetch.exe: downloads one speech model file over HTTPS.
 *
 *     audior_fetch.exe <https address> <file to create> <size in bytes>
 *
 * This is the one program of the project that touches the network, so the
 * recorder and the transcription helper contain no networking code (the import
 * check fails the build if either imports a networking DLL).  The recorder starts
 * it only after the user has said yes, and checks what it brings; it is held to
 * the smallest job it can do:
 *
 *   - Windows' own WinHTTP, with the system's proxy settings and certificate
 *     checks, and nothing else: no third-party library;
 *   - only an https address on huggingface.co (where the models of the table in
 *     core/asr_models.h are), with TLS 1.2 or later, and no cookies.  Redirects are
 *     followed, to any https host but never to plain http: a model's file is served
 *     from a CDN whose name changes, so it is not the host that vouches for the file
 *     but the SHA-256 the recorder checks, so a redirect cannot make a wrong file
 *     get used;
 *   - it creates a new file, which must be called "*.part", and writes nothing
 *     else, never more than the size it was told to expect;
 *   - 64 KiB of buffer, whatever the size of the file.
 *
 * What it says on its standard output is described in core/fetch_protocol.h.  It
 * does not check what it got (the recorder does, against a SHA-256) and it removes
 * its file if it fails.
 */
#include <windows.h>

#include <winhttp.h>

#include <cwctype>
#include <string>

#include "../core/fetch_protocol.h"
#include "../core/settings.h"

namespace {

using audiorgui::fetch::DoneLine;
using audiorgui::fetch::ErrorLine;
using audiorgui::fetch::ProgressLine;

constexpr wchar_t kAllowedHost[]       = L"huggingface.co";
constexpr wchar_t kPartSuffix[]        = L".part";
constexpr unsigned long long kMaxBytes = 4ull << 30;        /* no model is larger than this */
constexpr DWORD kBufferBytes           = 64 * 1024;
constexpr DWORD kReportMs              = 250;               /* progress is told this often (and at the end) */
constexpr DWORD kTimeoutMs             = 30000;
constexpr DWORD kTls12                 = 0x00000800;        /* WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 and _TLS1_3 (newer headers only) */
constexpr DWORD kTls13                 = 0x00002000;

void Say(const std::string &line)
{
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

/* Closes a WinHTTP handle when it goes out of scope. */
class Internet {
public:
    explicit Internet(HINTERNET h = nullptr) : h_(h) {}
    ~Internet()
    {
        if (h_)
            WinHttpCloseHandle(h_);
    }
    Internet(const Internet &) = delete;
    Internet &operator=(const Internet &) = delete;
    HINTERNET Get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }

private:
    HINTERNET h_;
};

/* What a WinHTTP error means to a person, in ASCII (it is sent over a pipe). */
std::string Describe(DWORD code)
{
    const char *what = "network error";
    switch (code) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:    what = "the address could not be found (is there an internet connection?)"; break;
    case ERROR_WINHTTP_CANNOT_CONNECT:       what = "could not connect"; break;
    case ERROR_WINHTTP_TIMEOUT:              what = "the connection timed out"; break;
    case ERROR_WINHTTP_CONNECTION_ERROR:     what = "the connection was lost"; break;
    case ERROR_WINHTTP_SECURE_FAILURE:       what = "the secure connection could not be made"; break;
    case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED: what = "a certificate was asked for"; break;
    default: break;
    }
    return std::string(what) + " (error " + std::to_string(code) + ")";
}

bool SameText(const std::wstring &a, const wchar_t *b) { return _wcsicmp(a.c_str(), b) == 0; }

/* The host of `url` if it is an https address on the allowed host with nothing odd about it; else false. */
bool ParseAddress(const std::wstring &url, std::wstring *host, std::wstring *path)
{
    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS ||
        parts.nPort != INTERNET_DEFAULT_HTTPS_PORT || parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0)
        return false;
    *host = std::wstring(parts.lpszHostName, parts.dwHostNameLength);
    *path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength + parts.dwExtraInfoLength);
    return SameText(*host, kAllowedHost) && !path->empty();
}

bool EndsWith(const std::wstring &text, const wchar_t *suffix)
{
    const std::wstring s = suffix;
    return text.size() > s.size() && text.compare(text.size() - s.size(), s.size(), s) == 0;
}

/* The number in the header `what` of the response; false if there is none. */
bool NumberHeader(HINTERNET request, DWORD what, unsigned long long *value)
{
    wchar_t text[32];
    DWORD size = sizeof(text);
    if (!WinHttpQueryHeaders(request, what, WINHTTP_HEADER_NAME_BY_INDEX, text, &size, WINHTTP_NO_HEADER_INDEX))
        return false;
    wchar_t *end = nullptr;
    *value = _wcstoui64(text, &end, 10);
    return end != text;
}

int Fail(const std::string &why)
{
    Say(ErrorLine(why));
    return 1;
}

/* Reads the response body into `file`, saying how far it has got.  Returns an error text, empty if all `expected` bytes arrived. */
std::string Receive(HINTERNET request, HANDLE file, unsigned long long expected)
{
    static char buffer[kBufferBytes];
    unsigned long long total = 0;
    DWORD reportedAt = GetTickCount();
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(request, buffer, kBufferBytes, &got))
            return Describe(GetLastError());
        if (got == 0)
            break;
        if (total + got > expected)
            return "the server sent more than the file is meant to hold";
        DWORD written = 0;
        if (!WriteFile(file, buffer, got, &written, nullptr) || written != got)
            return "the file could not be written (is the disk full?)";
        total += got;
        if (GetTickCount() - reportedAt >= kReportMs) {
            Say(ProgressLine(total));
            reportedAt = GetTickCount();
        }
    }
    if (total != expected)
        return "the download ended early";
    Say(ProgressLine(total));
    return std::string();
}

int Download(const std::wstring &url, const std::wstring &path, unsigned long long expected)
{
    std::wstring host, resource;
    if (!ParseAddress(url, &host, &resource))
        return Fail("only https addresses on huggingface.co are accepted");
    if (!audiorgui::IsAbsolutePath(path) || !EndsWith(path, kPartSuffix))
        return Fail("the file must be given by a full path and end in .part");
    if (expected == 0 || expected > kMaxBytes)
        return Fail("the size is not one a model can have");

    Internet session(WinHttpOpen(L"Audior-GUI model download", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
        return Fail(Describe(GetLastError()));
    WinHttpSetTimeouts(session.Get(), kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
    DWORD protocols = kTls12 | kTls13;
    if (!WinHttpSetOption(session.Get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = kTls12;                                     /* a Windows 10 that does not know TLS 1.3 yet */
        WinHttpSetOption(session.Get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP, redirects = 5;
    WinHttpSetOption(session.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    WinHttpSetOption(session.Get(), WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects, sizeof(redirects));

    Internet connection(WinHttpConnect(session.Get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    Internet request(connection ? WinHttpOpenRequest(connection.Get(), L"GET", resource.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr);
    if (request) {
        DWORD features = WINHTTP_DISABLE_COOKIES;               /* nothing here needs a cookie, and none is kept or sent */
        WinHttpSetOption(request.Get(), WINHTTP_OPTION_DISABLE_FEATURE, &features, sizeof(features));
    }
    if (!request || !WinHttpSendRequest(request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.Get(), nullptr))
        return Fail(Describe(GetLastError()));

    unsigned long long status = 0, length = 0;
    if (!NumberHeader(request.Get(), WINHTTP_QUERY_STATUS_CODE, &status) || status != 200)
        return Fail("the server answered with HTTP status " + std::to_string(status));
    if (NumberHeader(request.Get(), WINHTTP_QUERY_CONTENT_LENGTH, &length) && length != expected)
        return Fail("the file on the server is not the size it should be");

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return Fail("the file could not be created (" + std::to_string(GetLastError()) + ")");
    const std::string problem = Receive(request.Get(), file, expected);
    CloseHandle(file);                                          /* before "done" is said, so that it can be read */
    if (!problem.empty()) {
        DeleteFileW(path.c_str());
        return Fail(problem);
    }
    Say(DoneLine());
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4)
        return Fail("usage: audior_fetch <https address> <file to create> <size in bytes>");
    wchar_t *end = nullptr;
    const unsigned long long expected = _wcstoui64(argv[3], &end, 10);
    if (!std::iswdigit(argv[3][0]) || *end != L'\0')           /* digits only: not " 5", "+5" or "-5" either */
        return Fail("the size is not a number");
    return Download(argv[1], argv[2], expected);
}
