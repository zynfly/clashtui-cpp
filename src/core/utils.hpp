#pragma once

#include <string>

/// Shell-escape a string by wrapping in single quotes and escaping embedded quotes
inline std::string shell_quote(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

/// Validate a service name: only allow alphanumeric, dash, underscore, dot
inline bool is_valid_service_name(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

/// Parsed URL components
struct UrlParts {
    std::string scheme;
    std::string host;
    std::string path;
    int port = 443;
};

/// Parse a URL into scheme, host, path, and port
inline UrlParts parse_url(const std::string& url) {
    UrlParts parts;
    auto pos = url.find("://");
    if (pos != std::string::npos) {
        parts.scheme = url.substr(0, pos);
        auto rest = url.substr(pos + 3);
        auto path_pos = rest.find('/');
        if (path_pos != std::string::npos) {
            parts.host = rest.substr(0, path_pos);
            parts.path = rest.substr(path_pos);
        } else {
            parts.host = rest;
            parts.path = "/";
        }
    }
    auto colon = parts.host.find(':');
    if (colon != std::string::npos) {
        try {
            parts.port = std::stoi(parts.host.substr(colon + 1));
        } catch (...) {}
        parts.host = parts.host.substr(0, colon);
    } else {
        parts.port = (parts.scheme == "https") ? 443 : 80;
    }
    return parts;
}
