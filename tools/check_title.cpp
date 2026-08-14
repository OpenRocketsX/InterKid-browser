#include "network/http_client.h"
#include "network/text_decode.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <string>

namespace {

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <url>\n", argv[0]);
        return 1;
    }

    try {
        const auto response = FetchHttp(argv[1]);
        const std::string html = DecodeTextToUtf8(response.body, response.contentType);
        const std::string lowerHtml = ToLowerAscii(html);

        const auto titleStart = lowerHtml.find("<title");
        if (titleStart == std::string::npos) {
            std::printf("No title tag found\n");
            return 0;
        }

        const auto contentStart = lowerHtml.find('>', titleStart);
        const auto titleEnd = lowerHtml.find("</title>", contentStart);

        if (contentStart == std::string::npos || titleEnd == std::string::npos) {
            std::printf("Malformed title tag\n");
            return 0;
        }

        std::printf("RAW TITLE: %s\n",
                    html.substr(contentStart + 1, titleEnd - contentStart - 1).c_str());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Request failed: %s\n", error.what());
        return 2;
    }
}
