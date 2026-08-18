#pragma once

#include <string>

namespace vertex::internal_pages {

struct PageContent {
    std::string url;
    std::string title;
    std::string html;
};

inline std::string EscapeHtml(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char c : input) {
        switch (c) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&#39;"; break;
        default: output += c; break;
        }
    }
    return output;
}

inline bool IsRetryableUrl(const std::string& url) {
    return url.rfind("http://", 0) == 0
        || url.rfind("https://", 0) == 0
        || url.rfind("file://", 0) == 0;
}

inline std::string OfflinePageHtml(const std::string& failedUrl = {},
                                   const std::string& error = {},
                                   int httpStatus = 0) {
    const bool hasFailure = !failedUrl.empty() || !error.empty() || httpStatus > 0;
    const std::string statusLabel = httpStatus > 0
        ? "HTTP " + std::to_string(httpStatus)
        : (hasFailure ? "Network error" : "Offline");
    const std::string retryUrl = IsRetryableUrl(failedUrl)
        ? failedUrl : "vertex://offline-game";

    std::string details;
    if (!failedUrl.empty()) {
        details += "<div class=\"detail-row\"><strong>Address</strong><code>"
            + EscapeHtml(failedUrl) + "</code></div>";
    }
    if (!error.empty()) {
        details += "<div class=\"detail-row\"><strong>Reason</strong><span>"
            + EscapeHtml(error) + "</span></div>";
    }
    if (!hasFailure) {
        details = "<p class=\"local-note\">This recovery page is stored inside Vertex and remains available without a connection.</p>";
    }

    return R"html(<!DOCTYPE html>
<html>
<head>
<title>Page unavailable</title>
<style>
body {
    margin: 0; padding: 24px 0 36px; background: #f4f6f8; color: #20242a;
    font-family: 'Segoe UI', system-ui, sans-serif; font-size: 14px;
}
.shell { width: 88%; max-width: 820px; margin-left: auto; margin-right: auto; }
.panel {
    background: #ffffff; border: 1px solid #d7dce3; border-radius: 6px;
    padding: 18px 20px; margin-bottom: 12px;
}
.section-label {
    color: #5f6977; font-size: 11px; font-weight: 800; letter-spacing: 1px;
    text-transform: uppercase; margin-bottom: 8px;
}
.status {
    display: inline-block; padding: 4px 8px; margin-bottom: 10px;
    border: 1px solid #c9d3e8; border-radius: 4px; background: #f1f5ff;
    color: #294f9c; font-size: 12px; font-weight: 700;
}
h1 { margin: 0 0 5px; color: #171a1f; font-size: 25px; line-height: 1.2; }
.summary { margin: 0; color: #596273; line-height: 1.5; }
.details { margin-top: 13px; padding-top: 8px; border-top: 1px solid #e2e6eb; }
.detail-row { padding: 5px 0; }
.detail-row strong { display: inline-block; width: 70px; color: #596273; font-size: 12px; }
.detail-row span { color: #303640; }
.detail-row code { color: #294b8f; font-family: monospace; font-size: 12px; }
.local-note { margin: 14px 0 0; color: #596273; font-size: 12px; }
.actions { margin-top: 16px; }
.button {
    display: inline-block; min-width: 92px; margin-right: 8px; padding: 8px 13px;
    border: 1px solid #b9c1cc; border-radius: 4px; background: #ffffff;
    color: #273140; font-size: 13px; font-weight: 700; text-align: center;
    text-decoration: none;
}
.primary-button { background: #315cf6; border-color: #315cf6; color: #ffffff; }
.game-header { margin-bottom: 9px; }
.game-title { display: inline-block; margin: 0 14px 0 0; font-size: 16px; }
.preview-label {
    display: inline-block; padding: 4px 7px; border: 1px solid #d3d7de;
    border-radius: 3px; background: #f5f6f8; color: #404958;
    font-size: 11px; font-weight: 700;
}
.game {
    position: relative; height: 170px; overflow: hidden; border: 1px solid #b9c1cc;
    border-radius: 4px; background: #eef2f6;
}
.guide { position: absolute; left: 0; right: 0; top: 48px; border-top: 1px solid #d4dae2; }
.ground {
    position: absolute; left: 0; right: 0; bottom: 0; height: 22px;
    background: #d9dee6; border-top: 2px solid #667181;
}
.rocket { position: absolute; left: 48px; bottom: 24px; width: 34px; height: 23px; }
.rocket-body {
    position: absolute; left: 7px; top: 4px; width: 23px; height: 14px;
    background: #315cf6; border: 2px solid #2448b8; border-radius: 3px;
}
.rocket-nose { position: absolute; left: 29px; top: 7px; width: 6px; height: 9px; background: #2448b8; }
.rocket-fin { position: absolute; left: 10px; top: 17px; width: 9px; height: 5px; background: #626d7b; }
.rocket-flame { position: absolute; left: 0; top: 9px; width: 8px; height: 6px; background: #c86d16; }
.obstacle {
    position: absolute; bottom: 22px; width: 22px; background: #606b79;
    border: 2px solid #46505c; border-radius: 2px;
}
.obstacle-one { right: 118px; height: 34px; }
.obstacle-two { right: 42px; height: 52px; }
.game-message {
    position: absolute; left: 20px; right: 20px; top: 68px; padding: 8px;
    border: 1px solid #c3c9d1; border-radius: 3px; background: #ffffff;
    color: #353c46; text-align: center; font-size: 12px;
}
.hint { margin: 10px 0 0; color: #657080; font-family: monospace; font-size: 12px; }
.no-script { color: #657080; font-size: 12px; }
</style>
</head>
<body>
<main class="shell">
<section class="panel">
<div class="section-label">Vertex internal recovery</div>
<div class="status">)html" + EscapeHtml(statusLabel) + R"html(</div>
<h1>Page unavailable</h1>
<p class="summary">Vertex could not finish loading this page.</p>
<div class="details">)html" + details + R"html(</div>
<div class="actions">
<a id="retry" class="button primary-button" href=")html" + EscapeHtml(retryUrl) + R"html(">Retry</a>
<a id="home" class="button" href="vertex://home">Open Home</a>
</div>
</section>
<section class="panel">
<div class="section-label">Recovery panel</div>
<div class="game-header"><h2 class="game-title">Rocket Runner</h2><span class="preview-label">Static preview</span></div>
<div id="game" class="game" tabindex="0" aria-label="Rocket Runner game">
<div class="guide"></div>
<div id="obstacles"><span class="obstacle obstacle-one"></span><span class="obstacle obstacle-two"></span></div>
<div id="rocket" class="rocket" aria-label="Rocket">
<span class="rocket-flame"></span><span class="rocket-body"></span>
<span class="rocket-nose"></span><span class="rocket-fin"></span>
</div>
<div class="ground"></div>
<div id="game-message" class="game-message">Rocket Runner is paused in this stability build.</div>
</div>
<p class="hint">Interactive controls are disabled on recovery pages for stability.</p>
</section>
</main>
</body>
</html>)html";
}

inline bool Resolve(const std::string& url, PageContent& page) {
    if (url != "vertex://offline-game" && url != "vertex://404") return false;
    page.url = url;
    page.title = "Page unavailable";
    page.html = OfflinePageHtml({}, {}, url == "vertex://404" ? 404 : 0);
    return true;
}

} // namespace vertex::internal_pages
