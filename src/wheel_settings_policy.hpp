#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

// Presentation and lossless INI edits; deliberately independent of game/device APIs.
namespace WheelSettingsPolicy
{
inline bool IsAdvanced(std::string_view value) { return value == "Advanced"; }

inline std::string Trim(std::string value)
{
    auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
    return value;
}

inline bool Equal(std::string a, std::string b)
{
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::transform(a.begin(), a.end(), a.begin(), lower);
    std::transform(b.begin(), b.end(), b.begin(), lower);
    return a == b;
}

// Update only the requested key. Preserve unknown sections, comments, ordering,
// newline convention and every unrelated byte instead of reserializing a tune.
inline std::string SetIniValue(const std::string& source, const std::string& section,
    const std::string& key, const std::string& value)
{
    const std::string newline = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    bool inSection = false, foundSection = false;
    size_t insertAt = source.size();
    for (size_t pos = 0; pos < source.size();)
    {
        const auto end = source.find('\n', pos);
        const auto next = end == std::string::npos ? source.size() : end + 1;
        auto line = Trim(source.substr(pos, next - pos));
        if (pos == 0 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        if (!line.empty() && line.front() == '[')
        {
            if (inSection) insertAt = pos;
            const auto close = line.find(']');
            inSection = close != std::string::npos && Equal(Trim(line.substr(1, close - 1)), section);
            foundSection |= inSection;
        }
        else if (inSection && !line.empty() && line.front() != ';' && line.front() != '#')
        {
            const auto equals = line.find('=');
            if (equals != std::string::npos && Equal(Trim(line.substr(0, equals)), key))
            {
                const auto raw = source.substr(pos, next - pos);
                const auto rawEquals = raw.find('=');
                auto comment = raw.find(';', rawEquals + 1);
                if (comment != std::string::npos && (comment == 0 || !std::isspace(static_cast<unsigned char>(raw[comment - 1]))))
                    comment = std::string::npos;
                std::string suffix;
                if (comment != std::string::npos) suffix = " " + Trim(raw.substr(comment));
                return source.substr(0, pos) + raw.substr(0, rawEquals + 1) + " " + value + suffix
                    + (end == std::string::npos ? "" : newline) + source.substr(next);
            }
        }
        pos = next;
    }
    if (inSection) insertAt = source.size();
    auto before = source.substr(0, insertAt);
    if (!before.empty() && before.back() != '\n') before += newline;
    if (!foundSection) before += "[" + section + "]" + newline;
    return before + key + " = " + value + newline + source.substr(insertAt);
}
}
