#pragma once

#include <string>
#include <functional>

class Subscription {
public:
    struct DownloadResult {
        bool success = false;
        std::string error;
        std::string content;
    };

    // Download subscription content from URL
    static DownloadResult download(const std::string& url);

    // Save subscription content to mihomo config path
    static bool save_to_file(const std::string& content, const std::string& path);
};
