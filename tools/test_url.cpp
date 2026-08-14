// test_url.cpp — unit tests for LooksLikeUrl and URL recognition helpers.
#include "platform/browser_core.h"

#include <cstdio>

namespace {

int g_passed = 0;
int g_failed = 0;

void Check(bool condition,
           const char* expression,
           const char* file,
           int line) {
    if (condition) {
        ++g_passed;
        return;
    }

    ++g_failed;
    std::fprintf(stderr, "FAIL: %s (%s:%d)\n", expression, file, line);
}

#define CHECK(expression) Check((expression), #expression, __FILE__, __LINE__)

} // namespace

int main() {
    // URLs with explicit schemes.
    CHECK(LooksLikeUrl("https://example.com"));
    CHECK(LooksLikeUrl("http://example.com"));
    CHECK(LooksLikeUrl("ftp://files.example.com"));
    CHECK(LooksLikeUrl("vertex://home"));
    CHECK(LooksLikeUrl("vertex://bookmarks"));
    CHECK(LooksLikeUrl("about:blank"));
    CHECK(LooksLikeUrl("about:vertex"));

    // Domain names.
    CHECK(LooksLikeUrl("example.com"));
    CHECK(LooksLikeUrl("en.wikipedia.org"));
    CHECK(LooksLikeUrl("sub.domain.example.com"));

    // localhost hosts.
    CHECK(LooksLikeUrl("localhost"));
    CHECK(LooksLikeUrl("localhost:3000"));
    CHECK(LooksLikeUrl("localhost:8080"));

    // IPv4 addresses.
    CHECK(LooksLikeUrl("127.0.0.1"));
    CHECK(LooksLikeUrl("127.0.0.1:8080"));
    CHECK(LooksLikeUrl("192.168.1.1"));
    CHECK(LooksLikeUrl("10.0.0.1:3000"));

    // IPv6 addresses.
    CHECK(LooksLikeUrl("[::1]"));
    CHECK(LooksLikeUrl("[::1]:8080"));

    // Protocol-relative URLs.
    CHECK(LooksLikeUrl("//cdn.example.com/file.js"));

    // Search queries and malformed hostnames.
    CHECK(!LooksLikeUrl("hello world"));
    CHECK(!LooksLikeUrl("what is CSS"));
    CHECK(!LooksLikeUrl("how to build a browser"));
    CHECK(!LooksLikeUrl(""));
    CHECK(!LooksLikeUrl("singleword"));
    CHECK(!LooksLikeUrl(".hidden"));
    CHECK(!LooksLikeUrl("trailing."));

    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
