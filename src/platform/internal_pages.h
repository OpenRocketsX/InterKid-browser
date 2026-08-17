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
    margin: 0; padding: 24px 0 40px; background: #eef0f3; color: #20242a;
    font-family: system-ui, sans-serif; font-size: 14px;
}
.shell { width: 88%; max-width: 820px; margin-left: auto; margin-right: auto; }
.panel {
    background: #ffffff; border: 1px solid #c9ced6; border-radius: 6px;
    padding: 20px; margin-bottom: 14px;
}
.section-label {
    color: #596273; font-size: 11px; font-weight: 700; letter-spacing: 1px;
    text-transform: uppercase; margin-bottom: 9px;
}
.status {
    display: inline-block; padding: 4px 8px; margin-bottom: 12px;
    border: 1px solid #b8c0cc; border-radius: 4px; background: #f3f5f8;
    color: #424b59; font-size: 12px; font-weight: 700;
}
h1 { margin: 0 0 6px; color: #171a1f; font-size: 26px; line-height: 1.2; }
.summary { margin: 0; color: #596273; line-height: 1.5; }
.details { margin-top: 15px; padding-top: 9px; border-top: 1px solid #e0e3e8; }
.detail-row { padding: 5px 0; }
.detail-row strong { display: inline-block; width: 70px; color: #596273; font-size: 12px; }
.detail-row span { color: #303640; }
.detail-row code { color: #294b8f; font-family: monospace; font-size: 12px; }
.local-note { margin: 14px 0 0; color: #596273; font-size: 12px; }
.actions { margin-top: 16px; }
.button {
    display: inline-block; min-width: 92px; margin-right: 8px; padding: 8px 13px;
    border: 1px solid #aeb6c2; border-radius: 4px; background: #ffffff;
    color: #273140; font-size: 13px; font-weight: 700; text-align: center;
    text-decoration: none;
}
.primary-button { background: #315cf6; border-color: #315cf6; color: #ffffff; }
.game-header { margin-bottom: 10px; }
.game-title { display: inline-block; margin: 0 14px 0 0; font-size: 16px; }
.score {
    display: inline-block; padding: 4px 7px; border: 1px solid #d3d7de;
    border-radius: 3px; background: #f5f6f8; color: #404958;
    font-family: monospace; font-size: 12px; font-weight: 700;
}
.game {
    position: relative; height: 190px; overflow: hidden; border: 1px solid #aeb6c2;
    border-radius: 4px; background: #edf1f6;
}
.guide { position: absolute; left: 0; right: 0; top: 55px; border-top: 1px solid #d1d6de; }
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
.game-message {
    position: absolute; left: 20px; right: 20px; top: 73px; padding: 9px;
    border: 1px solid #c3c9d1; border-radius: 3px; background: #ffffff;
    color: #353c46; text-align: center; font-size: 12px;
}
.game-actions { margin-top: 10px; }
.control-button {
    margin-right: 7px; padding: 7px 12px; border: 1px solid #aeb6c2;
    border-radius: 4px; background: #ffffff; color: #273140; font-weight: 700;
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
<div class="section-label">While you wait</div>
<div class="game-header"><h2 class="game-title">Rocket Runner</h2><span id="score" class="score">Score 0000</span></div>
<div id="game" class="game" tabindex="0" aria-label="Rocket Runner game">
<div class="guide"></div>
<div id="obstacles"></div>
<div id="rocket" class="rocket" aria-label="Rocket">
<span class="rocket-flame"></span><span class="rocket-body"></span>
<span class="rocket-nose"></span><span class="rocket-fin"></span>
</div>
<div class="ground"></div>
<div id="game-message" class="game-message">Use Jump or press Space to start.</div>
</div>
<div class="game-actions">
<button id="jump-game" class="control-button" type="button">Jump</button>
<button id="restart-game" class="control-button" type="button">Restart</button>
</div>
<p class="hint">Space / ↑ / W to thrust · R to restart</p>
<noscript><p class="no-script">Rocket Runner needs JavaScript. Retry and Open Home remain available.</p></noscript>
</section>
</main>
<script>
(function () {
    var stage = document.getElementById('game');
    var rocket = document.getElementById('rocket');
    var obstacleLayer = document.getElementById('obstacles');
    var scoreLabel = document.getElementById('score');
    var message = document.getElementById('game-message');
    var jumpButton = document.getElementById('jump-game');
    var restartButton = document.getElementById('restart-game');
    if (!stage || !rocket || !obstacleLayer || !scoreLabel || !message) return;

    var rocketY = 0;
    var velocity = 0;
    var score = 0;
    var ticksUntilObstacle = 38;
    var running = false;
    var crashed = false;
    var obstacles = [];

    function setRocketPosition() {
        rocket.style.bottom = (24 + Math.floor(rocketY)) + 'px';
    }

    function clearObstacles() {
        while (obstacles.length) {
            var obstacle = obstacles.pop();
            if (obstacle.element.parentNode) {
                obstacle.element.parentNode.removeChild(obstacle.element);
            }
        }
    }

    function resetGame() {
        clearObstacles();
        rocketY = 0;
        velocity = 0;
        score = 0;
        ticksUntilObstacle = 38;
        running = false;
        crashed = false;
        setRocketPosition();
        scoreLabel.textContent = 'Score 0000';
        message.textContent = 'Use Jump or press Space to start.';
        message.style.display = 'block';
    }

    function jump() {
        if (crashed) return;
        running = true;
        velocity = 12;
        message.style.display = 'none';
    }

    function addObstacle() {
        var element = document.createElement('div');
        var height = 32 + Math.floor(Math.random() * 34);
        var width = stage.clientWidth;
        if (!width || width < 200) width = 640;
        element.className = 'obstacle';
        element.style.height = height + 'px';
        element.style.left = width + 'px';
        obstacleLayer.appendChild(element);
        obstacles.push({ element: element, x: width, height: height });
    }

    function crash() {
        crashed = true;
        running = false;
        message.textContent = 'Flight ended. Press R or Restart.';
        message.style.display = 'block';
    }

    function updateGame() {
        if (!running || crashed) return;

        velocity -= 1;
        rocketY += velocity;
        if (rocketY < 0) {
            rocketY = 0;
            velocity = 0;
        }
        if (rocketY > 135) {
            rocketY = 135;
            velocity = 0;
        }
        setRocketPosition();

        ticksUntilObstacle -= 1;
        if (ticksUntilObstacle <= 0) {
            addObstacle();
            ticksUntilObstacle = 34 + Math.floor(Math.random() * 14);
        }

        for (var i = obstacles.length - 1; i >= 0; i -= 1) {
            var obstacle = obstacles[i];
            obstacle.x -= 7;
            obstacle.element.style.left = obstacle.x + 'px';
            if (obstacle.x < -28) {
                if (obstacle.element.parentNode) {
                    obstacle.element.parentNode.removeChild(obstacle.element);
                }
                obstacles.splice(i, 1);
            } else if (obstacle.x < 80 && obstacle.x + 22 > 48 && rocketY < obstacle.height - 3) {
                crash();
                return;
            }
        }

        score += 1;
        var shown = String(score);
        while (shown.length < 4) shown = '0' + shown;
        scoreLabel.textContent = 'Score ' + shown;
    }

    function onKey(event) {
        var key = event.key;
        if (key === ' ' || key === 'Space' || key === 'Spacebar' || key === 'ArrowUp' || key === 'w' || key === 'W') {
            if (event.preventDefault) event.preventDefault();
            jump();
        } else if (key === 'r' || key === 'R') {
            resetGame();
        }
    }

    window.addEventListener('keydown', onKey);
    stage.addEventListener('click', jump);
    if (jumpButton) jumpButton.addEventListener('click', jump);
    if (restartButton) restartButton.addEventListener('click', resetGame);
    resetGame();
    if (typeof setInterval === 'function') setInterval(updateGame, 40);
})();
</script>
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
