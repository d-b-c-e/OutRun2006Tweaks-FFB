// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Windows C++17; strict UTF-8 strings, native filesystem paths. No UI/network/CLR.
namespace dbce::support {
inline constexpr std::size_t max_logs = 5, max_log_bytes = 24000, browser_url_limit = 4096;
bool valid_repository(const std::string& repository);
std::string redact(const std::string& text, const std::optional<std::string>& home = std::nullopt);
std::string log_tail(const std::filesystem::path& path, std::size_t max_bytes = max_log_bytes);
std::string selected_logs(const std::vector<std::filesystem::path>& paths);

class ReportProduct {
public:
    ReportProduct(std::string game, std::string version, std::string repository,
                  std::optional<std::string> client = std::nullopt,
                  std::string wheelkit_repository = "d-b-c-e/dbce-wheelkit");
    const std::string& game() const noexcept { return game_; }
    const std::string& version() const noexcept { return version_; }
    const std::string& repository() const noexcept { return repository_; }
    const std::optional<std::string>& client() const noexcept { return client_; }
    std::string mod_page() const { return "https://github.com/" + repository_; }
    std::string wheelkit_page() const { return "https://github.com/" + wheelkit_repository_; }
private:
    std::string game_, version_, repository_, wheelkit_repository_;
    std::optional<std::string> client_;
};

struct BrowserSubmission {
    const std::string url;
    const bool needs_attachment;
    const std::string issue_body;
    const std::string instructions;
};

class PreparedReport {
public:
    const std::string& repository() const noexcept { return repository_; }
    const std::string& title() const noexcept { return title_; }
    const std::string& body() const noexcept { return body_; }
    std::size_t selected_log_count() const noexcept { return log_count_; }
    std::string preview() const { return "# " + title_ + "\n\n" + body_; }
    BrowserSubmission create_browser_submission(bool reviewed) const;
    void write_review_file(const std::filesystem::path& new_txt_path) const;
private:
    PreparedReport(std::string repository, std::string title, std::string body,
                   std::string summary, std::size_t count);
    std::string repository_, title_, body_, summary_;
    std::size_t log_count_;
    friend PreparedReport prepare(const ReportProduct&, const std::string&, const std::string&,
                                  const std::vector<std::filesystem::path>&, const std::optional<std::string>&);
};

// Review creates one snapshot; any host input/selection change must invalidate it.
// Exceptions report failure; callers must not retain an old preview after failure.
PreparedReport prepare(const ReportProduct& product, const std::string& title, const std::string& description,
                       const std::vector<std::filesystem::path>& selected_log_paths = {},
                       const std::optional<std::string>& home = std::nullopt);
} // namespace dbce::support
