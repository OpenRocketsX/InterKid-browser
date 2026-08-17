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
    const std::string title = httpStatus == 404 ? "Page unavailable" : "Connection lost";
    const std::string retryUrl = IsRetryableUrl(failedUrl)
        ? failedUrl : "vertex://offline-game";

    std::string details;
    if (hasFailure) {
        details = "<dl class=\"details\">";
        if (!failedUrl.empty()) {
            details += "<dt>Address</dt><dd class=\"address\">"
                + EscapeHtml(failedUrl) + "</dd>";
        }
        if (httpStatus > 0) {
            details += "<dt>Status</dt><dd>HTTP " + std::to_string(httpStatus) + "</dd>";
        }
        if (!error.empty()) {
            details += "<dt>Reason</dt><dd>" + EscapeHtml(error) + "</dd>";
        }
        details += "</dl>";
    } else {
        details = "<p class=\"direct-note\">This built-in page is always available, "
            "even when the network is not.</p>";
    }

    return R"html(<!DOCTYPE html>
<html>
<head>
<title>)html" + title + R"html(</title>
<style>
* { box-sizing: border-box; }
body {
    margin: 0; padding: 30px 0 48px;
    background: #f3f4f6; color: #20242b;
    font-family: 'Segoe UI', system-ui, sans-serif;
}
.shell { width: 90%; max-width: 920px; margin-left: auto; margin-right: auto; }
.masthead {
    background: #ffffff; border: 1px solid #cfd4dc; border-left: 5px solid #315cf6;
    border-radius: 7px; padding: 22px 24px; margin-bottom: 16px;
}
.eyebrow {
    color: #52606f; font-size: 11px; font-weight: 800;
    letter-spacing: 1px; text-transform: uppercase; margin-bottom: 8px;
}
h1 { margin: 0 0 7px; font-size: 30px; line-height: 1.15; color: #171a1f; }
.subtitle { margin: 0; color: #58616d; font-size: 14px; line-height: 1.5; }
.details {
    margin: 18px 0 0; padding-top: 14px; border-top: 1px solid #e1e4e8;
    font-size: 13px;
}
.details dt { color: #6a737e; font-weight: 700; margin-top: 7px; }
.details dd { margin: 2px 0 0; color: #252a31; }
.details .address { color: #3154bb; overflow-wrap: anywhere; }
.direct-note { margin: 16px 0 0; color: #58616d; font-size: 13px; }
.actions { margin-top: 18px; }
.button {
    display: inline-block; min-width: 96px; margin: 0 8px 0 0; padding: 9px 15px;
    border: 1px solid #aeb6c2; border-radius: 5px; background: #ffffff;
    color: #26303d; font-size: 13px; font-weight: 700; text-decoration: none;
    text-align: center;
}
.button.primary { background: #315cf6; border-color: #315cf6; color: #ffffff; }
.button:hover { border-color: #315cf6; }
.workspace { display: flex; gap: 16px; align-items: stretch; }
.panel {
    background: #ffffff; border: 1px solid #cfd4dc; border-radius: 7px;
    padding: 18px; min-width: 0;
}
.game-panel { flex: 3; }
.help-panel { flex: 2; }
.panel-head { display: flex; align-items: center; margin-bottom: 12px; }
.panel-head h2 { flex: 1; margin: 0; font-size: 16px; color: #20242b; }
.score {
    border: 1px solid #d8dce2; border-radius: 4px; background: #f7f8fa;
    padding: 5px 8px; color: #3f4854; font: 700 12px monospace;
}
.game {
    position: relative; height: 220px; overflow: hidden;
    border: 1px solid #aeb6c2; border-radius: 5px;
    background: #eef2f7; cursor: pointer;
}
.sky-line { position: absolute; left: 0; right: 0; top: 58px; border-top: 1px dashed #c5ccd5; }
.ground { position: absolute; left: 0; right: 0; bottom: 0; height: 24px; background: #d9dee6; border-top: 2px solid #657181; }
.rocket { position: absolute; left: 48px; bottom: 24px; width: 34px; height: 25px; z-index: 2; }
.rocket-body { position: absolute; left: 7px; top: 5px; width: 22px; height: 14px; background: #315cf6; border: 2px solid #1f3f9e; border-radius: 4px; }
.rocket-nose { position: absolute; left: 27px; top: 7px; width: 0; height: 0; border-left: 8px solid #1f3f9e; border-top: 5px solid transparent; border-bottom: 5px solid transparent; }
.rocket-fin { position: absolute; left: 10px; top: 17px; width: 10px; height: 6px; background: #5e6875; }
.rocket-flame { position: absolute; left: 0; top: 9px; width: 8px; height: 7px; background: #d97706; }
.obstacle { position: absolute; bottom: 24px; width: 22px; background: #596473; border: 2px solid #3f4854; border-radius: 3px 3px 0 0; }
.game-message {
    position: absolute; left: 18px; right: 18px; top: 78px; z-index: 4;
    padding: 10px; border: 1px solid #bcc3cc; border-radius: 4px;
    background: #ffffff; color: #2d343d; text-align: center; font-size: 13px;
}
.game-controls { margin: 10px 0 0; color: #69727e; font-size: 12px; }
.restart {
    margin-top: 10px; padding: 7px 11px; border: 1px solid #aeb6c2;
    border-radius: 4px; background: #ffffff; color: #26303d; font-weight: 700;
}
.help-panel h2 { margin: 0 0 12px; font-size: 16px; }
.key-row { padding: 9px 0; border-top: 1px solid #e2e5e9; color: #555f6c; font-size: 12px; }
.key-row:first-of-type { border-top: 0; }
kbd {
    display: inline-block; min-width: 34px; margin-right: 7px; padding: 3px 6px;
    border: 1px solid #aeb6c2; border-bottom-width: 2px; border-radius: 4px;
    background: #f8f9fa; color: #252a31; font: 700 11px monospace; text-align: center;
}
.tip { margin: 16px 0 0; color: #737d89; font-size: 12px; line-height: 1.5; }
@media (max-width: 680px) {
    body { padding-top: 16px; }
    .workspace { display: block; }
    .help-panel { margin-top: 16px; }
    .masthead { padding: 18px; }
}
</style>
</head>
<body>
<main class="shell">
<section class="masthead">
<div class="eyebrow">Vertex network</div>
<h1>)html" + title + R"html(</h1>
<p class="subtitle">The page could not be loaded. Check the connection, then try again.</p>
)html" + details + R"html(
<div class="actions">
<a id="retry" class="button primary" href=")html" + EscapeHtml(retryUrl) + R"html(">Retry</a>
<a id="home" class="button" href="vertex://home">Home</a>
</div>
</section>
<div class="workspace">
<section class="panel game-panel">
<div class="panel-head"><h2>Rocket Runner</h2><div id="score" class="score">Score 0000</div></div>
<div id="game" class="game" tabindex="0" role="application" aria-label="Rocket Runner game">
<div class="sky-line"></div>
<div id="obstacles"></div>
<div id="rocket" class="rocket" aria-label="Rocket">
<span class="rocket-flame"></span><span class="rocket-body"></span>
<span class="rocket-nose"></span><span class="rocket-fin"></span>
</div>
<div class="ground"></div>
<div id="game-message" class="game-message">Press Space, Arrow Up, or W to launch.</div>
</div>
<p class="game-controls">The game is local to Vertex and does not use the network.</p>
<button id="restart-game" class="restart" type="button">Restart game</button>
<noscript><p>Rocket Runner needs JavaScript. Retry and Home still work without it.</p></noscript>
</section>
<aside class="panel help-panel">
<h2>Keyboard</h2>
<div class="key-row"><kbd>Space</kbd> Thrust</div>
<div class="key-row"><kbd>Up</kbd> Thrust</div>
<div class="key-row"><kbd>W</kbd> Thrust</div>
<div class="key-row"><kbd>R</kbd> Restart after a crash</div>
<p class="tip">Tip: short, measured thrusts keep the rocket low and make obstacles easier to read.</p>
</aside>
</div>
</main>
<script>
(function () {
    var stage = document.getElementById('game');
    var rocket = document.getElementById('rocket');
    var obstacleLayer = document.getElementById('obstacles');
    var scoreLabel = document.getElementById('score');
    var message = document.getElementById('game-message');
    var restartButton = document.getElementById('restart-game');
    if (!stage || !rocket || !obstacleLayer || !scoreLabel || !message) return;

    var rocketY = 0;
    var velocity = 0;
    var score = 0;
    var running = false;
    var crashed = false;
    var lastTime = 0;
    var spawnTimer = 0;
    var obstacles = [];

    function setRocketPosition() {
        rocket.style.bottom = (24 + Math.round(rocketY)) + 'px';
    }

    function clearObstacles() {
        while (obstacles.length) {
            var old = obstacles.pop();
            if (old.element.parentNode) old.element.parentNode.removeChild(old.element);
        }
    }

    function resetGame() {
        clearObstacles();
        rocketY = 0;
        velocity = 0;
        score = 0;
        spawnTimer = 0.9;
        crashed = false;
        running = false;
        lastTime = 0;
        setRocketPosition();
        scoreLabel.textContent = 'Score 0000';
        message.textContent = 'Press Space, Arrow Up, or W to launch.';
        message.style.display = 'block';
    }

    function thrust() {
        if (crashed) return;
        running = true;
        velocity = 330;
        message.style.display = 'none';
    }

    function addObstacle() {
        var element = document.createElement('div');
        var height = 32 + Math.floor(Math.random() * 44);
        element.className = 'obstacle';
        element.style.height = height + 'px';
        obstacleLayer.appendChild(element);
        obstacles.push({ element: element, x: stage.clientWidth + 12, height: height });
    }

    function crash() {
        crashed = true;
        running = false;
        message.textContent = 'Flight ended - press R or Restart game.';
        message.style.display = 'block';
    }

    function update(dt) {
        velocity -= 780 * dt;
        rocketY += velocity * dt;
        if (rocketY < 0) {
            rocketY = 0;
            velocity = 0;
        }
        if (rocketY > 158) {
            rocketY = 158;
            velocity = 0;
        }
        setRocketPosition();

        spawnTimer -= dt;
        if (spawnTimer <= 0) {
            addObstacle();
            spawnTimer = 1.25 + Math.random() * 0.55;
        }

        var speed = 190 + Math.min(score * 0.7, 90);
        for (var i = obstacles.length - 1; i >= 0; i--) {
            var obstacle = obstacles[i];
            obstacle.x -= speed * dt;
            obstacle.element.style.left = Math.round(obstacle.x) + 'px';
            if (obstacle.x < -28) {
                if (obstacle.element.parentNode) obstacle.element.parentNode.removeChild(obstacle.element);
                obstacles.splice(i, 1);
            } else if (obstacle.x < 80 && obstacle.x + 22 > 48 && rocketY < obstacle.height - 3) {
                crash();
                return;
            }
        }
        score += dt * 10;
        var shown = String(Math.floor(score));
        while (shown.length < 4) shown = '0' + shown;
        scoreLabel.textContent = 'Score ' + shown;
    }

    function frame(time) {
        if (lastTime === 0) lastTime = time;
        var dt = Math.min((time - lastTime) / 1000, 0.04);
        lastTime = time;
        if (running) update(dt);
        requestAnimationFrame(frame);
    }

    function onKey(event) {
        var key = event.key;
        if (key === ' ' || key === 'ArrowUp' || key === 'w' || key === 'W') {
            if (event.preventDefault) event.preventDefault();
            thrust();
        } else if ((key === 'r' || key === 'R') && crashed) {
            resetGame();
        }
    }

    window.addEventListener('keydown', onKey);
    stage.addEventListener('click', thrust);
    if (restartButton) restartButton.addEventListener('click', resetGame);
    resetGame();
    requestAnimationFrame(frame);
})();
</script>
</body>
</html>)html";
}

inline bool Resolve(const std::string& url, PageContent& page) {
    if (url != "vertex://offline-game" && url != "vertex://404") return false;
    page.url = url;
    page.title = url == "vertex://404" ? "Page unavailable" : "Connection lost";
    page.html = OfflinePageHtml();
    return true;
}

} // namespace vertex::internal_pages
