// SPDX-License-Identifier: MIT
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "support_report.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dbce::support {
namespace {
// Matches the managed helper's UTF-16 character limits. Redaction itself has
// a defensive 1 MiB input/output ceiling; no regex engine or unbounded scan.
constexpr std::size_t redaction_limit = 1024 * 1024;
[[noreturn]] void invalid(const char* message) { throw std::invalid_argument(message); }
[[noreturn]] void io_error(const char* message) { throw std::runtime_error(message); }
std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    if (text.size() > redaction_limit) invalid("Text exceeds report input limit.");
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count) invalid("Choose valid UTF-8 text.");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count))
        invalid("Choose valid UTF-8 text.");
    return result;
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    if (text.size() > redaction_limit) invalid("Text exceeds report output limit.");
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count) invalid("Choose valid UTF-16 text; a log may still be writing a partial character.");
    std::string result(static_cast<std::size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr))
        invalid("Choose valid UTF-16 text.");
    return result;
}
bool space(wchar_t c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
        (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
std::string required(const std::string& value, std::size_t limit) {
    auto text = wide(value);
    auto begin = std::find_if_not(text.begin(), text.end(), space);
    auto end = std::find_if_not(text.rbegin(), text.rend(), space).base();
    if (begin >= end || text.size() > limit) invalid("Enter nonempty text within the field's character limit.");
    return utf8(std::wstring(begin, end));
}
wchar_t lower(wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c; }
bool alpha(wchar_t c) { c = lower(c); return c >= L'a' && c <= L'z'; }
bool digit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool word(wchar_t c) { return alpha(c) || digit(c) || c == L'_'; }
bool at(const std::wstring& text, std::size_t pos, const std::wstring& literal) {
    if (literal.size() > text.size() - pos) return false;
    return CompareStringOrdinal(text.data() + pos, static_cast<int>(literal.size()), literal.data(),
                                static_cast<int>(literal.size()), TRUE) == CSTR_EQUAL;
}
std::wstring home_path() {
    DWORD needed = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (!needed || needed > 32768) return {};
    std::wstring result(needed, L'\0');
    DWORD count = GetEnvironmentVariableW(L"USERPROFILE", result.data(), needed);
    if (!count || count >= needed) return {};
    result.resize(count); return result;
}
void replace_all(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) { text.replace(pos, from.size(), to); pos += to.size(); }
}
std::wstring redact_w(std::wstring text, std::wstring home) {
    while (!home.empty() && (home.back() == L'\\' || home.back() == L'/')) home.pop_back();
    std::wstring out;
    for (std::size_t i = 0; i < text.size();) {
        if (!home.empty() && at(text, i, home)) { out += L"%USERPROFILE%"; i += home.size(); }
        else out += text[i++];
        if (out.size() > redaction_limit) invalid("Redacted report is too long.");
    }
    text = std::move(out); out.clear();
    for (std::size_t i = 0; i < text.size();) {
        std::size_t end = i;
        if (alpha(text[i]) && i + 9 <= text.size() && text[i+1] == L':' &&
            (text[i+2] == L'\\' || text[i+2] == L'/') && at(text, i+3, L"Users") &&
            (text[i+8] == L'\\' || text[i+8] == L'/')) {
            end = i + 9;
            while (end < text.size() && !space(text[end]) && text[end] != L'\\' && text[end] != L'/' &&
                   text[end] != L'"' && text[end] != L'<' && text[end] != L'>') ++end;
            if (end == i + 9) end = i;
        }
        if (end != i) { out += L"%USERPROFILE%"; i = end; continue; }
        std::size_t token = i;
        if (at(text, i, L"github_pat_")) {
            token = i + 11;
            while (token < text.size() && word(text[token])) ++token;
            if (token == i + 11) token = i;
        } else if (i + 4 <= text.size() && at(text, i, L"gh") &&
                   std::wstring(L"pousr").find(lower(text[i+2])) != std::wstring::npos && text[i+3] == L'_') {
            token = i + 4;
            while (token < text.size() && (alpha(text[token]) || digit(text[token]))) ++token;
            if (token - (i + 4) < 15) token = i;
        }
        if (token != i) { out += L"[redacted token]"; i = token; } else out += text[i++];
    }
    text = std::move(out); out.clear();
    // Like managed SupportReport.Redact, token also matches suffix labels such
    // as refresh_token and auth_token; keep the label and redact its line value.
    const std::array<std::wstring, 11> labels = {L"authorization",L"password",L"passwd",L"accesstoken",L"access_token",
        L"access-token",L"apikey",L"api_key",L"api-key",L"secret",L"token"};
    for (std::size_t i = 0; i < text.size();) {
        std::size_t value = i;
        for (const auto& label : labels) {
            if (!at(text, i, label)) continue;
            auto p = i + label.size();
            if (p < text.size() && (text[p] == L'"' || text[p] == L'\'')) ++p;
            while (p < text.size() && space(text[p])) ++p;
            if (p == text.size() || (text[p] != L':' && text[p] != L'=')) continue;
            ++p; while (p < text.size() && space(text[p])) ++p;
            if (p < text.size() && text[p] != L'\r' && text[p] != L'\n') { value = p; break; }
        }
        if (value != i) {
            out.append(text, i, value-i); out += L"[redacted]"; i = value;
            while (i < text.size() && text[i] != L'\r' && text[i] != L'\n') ++i;
        } else out += text[i++];
    }
    text = std::move(out); out.clear();
    auto local = [](wchar_t c) { return word(c) || c == L'.' || c == L'%' || c == L'+' || c == L'-'; };
    auto domain = [](wchar_t c) { return alpha(c) || digit(c) || c == L'.' || c == L'-'; };
    for (std::size_t i = 0; i < text.size();) {
        auto p = i;
        auto local_end = i;
        if (word(text[i]) && (i == 0 || !word(text[i-1]))) {
            while (p < text.size() && local(text[p])) ++p;
            local_end=p;
            if (p < text.size() && text[p] == L'@') {
                const auto start = ++p;
                while (p < text.size() && domain(text[p])) ++p;
                auto end = p;
                while (end > start && !alpha(text[end-1])) --end;
                auto letters = end;
                while (letters > start && alpha(text[letters-1])) --letters;
                if (end - letters >= 2 && letters > start && text[letters-1] == L'.' &&
                    (end == text.size() || !word(text[end]))) {
                    out += L"[redacted email]"; i = end; continue;
                }
            }
        }
        // Consume an unsuccessful local-part run once. Re-scanning every
        // dot-separated suffix would make a long non-email line quadratic.
        if (local_end > i) { out.append(text,i,local_end-i); i=local_end; } else out += text[i++];
    }
    if (out.size() > redaction_limit) invalid("Redacted report is too long.");
    return out;
}
std::wstring chosen_home(const std::optional<std::string>& home) { return home ? wide(*home) : home_path(); }
std::wstring shorten(std::wstring value, std::size_t limit) {
    if (value.size() <= limit) return value;
    if (limit && value[limit-1] >= 0xd800 && value[limit-1] <= 0xdbff) --limit;
    value.resize(limit); return value + L"...";
}
std::filesystem::path local_path(const std::filesystem::path& input) {
    auto name = input.native();
    auto slash=[](wchar_t c) { return c==L'\\' || c==L'/'; };
    if (name.empty() || name.find(L'\0') != std::wstring::npos ||
        (name.size() >= 2 && slash(name[0]) && slash(name[1])))
        invalid("Choose a regular local log or report folder, not a network/device path.");
    auto path = std::filesystem::absolute(input).lexically_normal();
    name=path.native();
    if (name.size()<3 || !alpha(name[0]) || name[1]!=L':' || name.find(L':',2)!=std::wstring::npos)
        invalid("Choose a local drive path, not a stream or device path.");
    for (const auto& component : path.relative_path()) {
        auto part=component.native();
        // Win32 DOS aliases remain devices with an extension (NUL.txt, COM1.log).
        // Trim the stem's trailing spaces too, rather than trust normalization.
        part.resize(part.find(L'.')==std::wstring::npos ? part.size() : part.find(L'.'));
        while (!part.empty() && part.back()==L' ') part.pop_back();
        std::transform(part.begin(),part.end(),part.begin(),lower);
        const bool numbered=part.size()==4 && (part.compare(0,3,L"com")==0 || part.compare(0,3,L"lpt")==0) &&
            ((part[3]>=L'1' && part[3]<=L'9') || part[3]==0xb9 || part[3]==0xb2 || part[3]==0xb3);
        if (part==L"con" || part==L"prn" || part==L"aux" || part==L"nul" || part==L"conin$" || part==L"conout$" || numbered)
            invalid("Choose a regular file path, not a reserved device name.");
    }
    (void)utf8(name);
    return path;
}
std::filesystem::path regular_local_path(const std::filesystem::path& input) {
    auto path=local_path(input);
    // Conservative refusal, like managed RefuseLinks; not a concurrent path-replacement sandbox.
    for (auto current = path; !current.empty();) {
        DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) io_error("Cannot inspect selected local path.");
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) invalid("Choose a regular local path, not a linked path.");
        auto parent = current.parent_path(); if (parent == current) break; current = std::move(parent);
    }
    return path;
}
class Handle {
public:
    explicit Handle(HANDLE value) : value_(value) { if (value == INVALID_HANDLE_VALUE) io_error("Cannot open selected local file."); }
    ~Handle() { CloseHandle(value_); }
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
private: HANDLE value_;
};
std::size_t read(HANDLE file, char* buffer, std::size_t size) {
    DWORD count = 0;
    if (!ReadFile(file, buffer, static_cast<DWORD>(size), &count, nullptr)) io_error("Cannot read selected text log.");
    return count;
}
std::string escaped(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
} // namespace

bool valid_repository(const std::string& repository) {
    if (repository.empty() || repository.size() > 201) return false;
    auto slash = repository.find('/');
    if (slash == std::string::npos || repository.find('/', slash+1) != std::string::npos) return false;
    for (const auto& part : {repository.substr(0,slash),repository.substr(slash+1)}) {
        if (part.empty() || part.size() > 100 || !word(static_cast<unsigned char>(part[0]))) return false;
        for (unsigned char c : part) if (!word(c) && c != '.' && c != '-') return false;
    }
    return true;
}
ReportProduct::ReportProduct(std::string game, std::string version, std::string repository,
                            std::optional<std::string> client, std::string wheelkit_repository)
    : game_(required(game,120)), version_(required(version,120)), repository_(std::move(repository)),
      wheelkit_repository_(std::move(wheelkit_repository)), client_(std::move(client)) {
    if (!valid_repository(repository_) || !valid_repository(wheelkit_repository_)) invalid("Choose a valid packaged GitHub repository.");
    if (client_) *client_ = required(*client_,120);
}
std::string redact(const std::string& text, const std::optional<std::string>& home) {
    return utf8(redact_w(wide(text),chosen_home(home)));
}
std::string log_tail(const std::filesystem::path& input, std::size_t max_bytes) {
    if (max_bytes < 256 || max_bytes > max_log_bytes) invalid("Invalid log byte budget.");
    auto path = regular_local_path(input);
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
                            OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(),&info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
        invalid("Choose a regular text log.");
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(),&length) || length.QuadPart < 0) io_error("Cannot determine log length.");
    std::array<unsigned char,4> prefix{};
    auto prefix_count = read(file.get(),reinterpret_cast<char*>(prefix.data()),prefix.size());
    if (prefix_count == 4 && ((prefix[0]==255 && prefix[1]==254 && prefix[2]==0 && prefix[3]==0) ||
        (prefix[0]==0 && prefix[1]==0 && prefix[2]==254 && prefix[3]==255))) invalid("Choose UTF-8 or BOM-marked UTF-16 text.");
    bool le = prefix_count >= 2 && prefix[0]==255 && prefix[1]==254;
    bool be = prefix_count >= 2 && prefix[0]==254 && prefix[1]==255;
    bool u16 = le || be;
    const auto size = static_cast<unsigned long long>(length.QuadPart);
    auto start = size > max_bytes ? size - max_bytes : 0;
    bool truncated = start != 0;
    if (u16 && start % 2) ++start;
    LARGE_INTEGER seek{}; seek.QuadPart = static_cast<LONGLONG>(start);
    if (!SetFilePointerEx(file.get(),seek,nullptr,FILE_BEGIN)) io_error("Cannot seek selected log.");
    std::string bytes(static_cast<std::size_t>(std::min<unsigned long long>(max_bytes,size-start)), '\0');
    std::size_t count = 0;
    while (count < bytes.size()) { auto got = read(file.get(),bytes.data()+count,bytes.size()-count); if (!got) break; count += got; }
    bytes.resize(count);
    std::size_t offset = 0;
    if (truncated) {
        offset = count;
        for (std::size_t i=0; i<count; i += u16 ? 2 : 1) {
            if ((!u16 && bytes[i]=='\n') || (u16 && i+1<count &&
                ((le && bytes[i]=='\n' && bytes[i+1]==0) || (be && bytes[i]==0 && bytes[i+1]=='\n')))) {
                offset = i + (u16 ? 2 : 1); break;
            }
        }
    }
    std::wstring text;
    if (u16) {
        if ((count-offset)%2) invalid("UTF-16 log ends in a partial character.");
        for (auto i=offset; i<count; i+=2) {
            auto a=static_cast<unsigned char>(bytes[i]), b=static_cast<unsigned char>(bytes[i+1]);
            text += static_cast<wchar_t>(le ? a|(b<<8) : (a<<8)|b);
        }
        (void)utf8(text); // Reject unpaired UTF-16 surrogates before processing.
    } else text = wide(bytes.substr(offset));
    while (!text.empty() && text.front()==0xfeff) text.erase(text.begin());
    for (auto c : text) if ((c<32 || (c>=0x7f && c<=0x9f)) && c!=L'\n' && c!=L'\r' && c!=L'\t')
        invalid("Select a text log, not a binary file.");
    return (truncated ? "[Last "+std::to_string(max_bytes)+" bytes only; partial first line omitted]\n" : "") + utf8(text);
}
std::string selected_logs(const std::vector<std::filesystem::path>& paths) {
    if (paths.empty() || paths.size()>max_logs) invalid("Choose between one and five known text logs.");
    std::vector<std::wstring> seen;
    std::string result;
    for (const auto& path : paths) {
        auto extension=path.extension().native(); std::transform(extension.begin(),extension.end(),extension.begin(),lower);
        const std::array<std::wstring,7> allowed={L".log",L".txt",L".json",L".jsonl",L".csv",L".tsv",L".xml"};
        if (std::find(allowed.begin(),allowed.end(),extension)==allowed.end()) invalid("Choose a known text log format.");
        auto absolute=regular_local_path(path).native();
        for (const auto& prior : seen) if (prior.size()==absolute.size() && at(absolute,0,prior)) invalid("Select each log only once.");
        seen.push_back(absolute);
        auto name=shorten(path.filename().native(),200);
        std::replace(name.begin(),name.end(),L'\r',L' '); std::replace(name.begin(),name.end(),L'\n',L' ');
        if (!result.empty()) result += "\n\n";
        result += "--- "+utf8(name)+" ---\n"+log_tail(path,max_log_bytes/paths.size());
    }
    return redact(result);
}
PreparedReport::PreparedReport(std::string repository,std::string title,std::string body,std::string summary,std::size_t count)
    : repository_(std::move(repository)),title_(std::move(title)),body_(std::move(body)),summary_(std::move(summary)),log_count_(count) {}
PreparedReport prepare(const ReportProduct& product,const std::string& title,const std::string& description,
                       const std::vector<std::filesystem::path>& paths,const std::optional<std::string>& home) {
    auto clean_title=required(redact(required(title,200),home),200);
    auto desc=redact(required(description,10000),home);
    std::string body="## What happened\n"+desc+"\n\n## Setup\n- Game: "+product.game()+"\n- Mod: "+product.version()+"\n";
    if (product.client()) body += "- Reporting app: "+*product.client()+"\n";
    if (paths.empty()) body += "\nNo logs included.\n";
    else {
        auto logs=wide(selected_logs(paths)); replace_all(logs,L"```",L"[code fence]");
        replace_all(logs,L"<",L"&lt;"); replace_all(logs,L">",L"&gt;");
        body += "\n## Selected mod logs (redacted)\n<details><summary>Log excerpts</summary>\n\n```text\n"+utf8(logs)+"\n```\n</details>\n";
    }
    body=redact(body,home);
    if (wide(body).size()>50000) invalid("Report is too long; shorten description or select fewer logs.");
    auto summary=redact("## What happened\n"+utf8(shorten(wide(desc),80))+"\n\n## Setup\n- Game: "+
        utf8(shorten(wide(product.game()),20))+"\n- Mod: "+utf8(shorten(wide(product.version()),20)),home);
    return PreparedReport(product.repository(),std::move(clean_title),std::move(body),std::move(summary),paths.size());
}
BrowserSubmission PreparedReport::create_browser_submission(bool reviewed) const {
    if (!reviewed) throw std::logic_error("Review the prepared report before opening GitHub.");
    auto prefix="https://github.com/"+repository_+"/issues/new?title="+escaped(title_)+"&body=";
    auto full=prefix+escaped(body_);
    if (full.size()<=browser_url_limit) return {full,false,body_,"GitHub opens with the reviewed report. Check it and submit the issue there."};
    auto fallback=summary_+"\n\nPlease see the attached support-report.txt for the complete reviewed report"+
        (log_count_ ? " and selected log excerpts." : ".");
    auto url=prefix+escaped(fallback);
    if (url.size()>browser_url_limit) throw std::logic_error("Title is too long for the browser draft; shorten it and review again.");
    return {url,true,fallback,"Save the reviewed report as support-report.txt, then attach that one file in GitHub's issue box. "
        "Attaching uploads the file immediately. Check the draft and submit the issue there."};
}
void PreparedReport::write_review_file(const std::filesystem::path& destination) const {
    auto extension=destination.extension().native(); std::transform(extension.begin(),extension.end(),extension.begin(),lower);
    if (extension!=L".txt") invalid("Choose a new .txt report file.");
    auto absolute=local_path(destination);
    (void)regular_local_path(absolute.parent_path());
    Handle file(CreateFileW(absolute.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(file.get())!=FILE_TYPE_DISK || !GetFileInformationByHandle(file.get(),&info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
        io_error("Report destination is not a regular disk file.");
    auto text=preview(); std::size_t written=0;
    while (written<text.size()) {
        DWORD count=0;
        if (!WriteFile(file.get(),text.data()+written,static_cast<DWORD>(text.size()-written),&count,nullptr) || !count)
            io_error("Cannot finish writing reviewed report; partial file retained.");
        written += count;
    }
    if (!FlushFileBuffers(file.get())) io_error("Cannot flush reviewed report; file retained.");
}
} // namespace dbce::support
