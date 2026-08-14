// dump_layout.cpp — offline layout diagnostic. Parses an HTML file or URL,
// runs the real layout engine with a stub text measurer, and prints each box's
// geometry and key style properties without launching the GUI.
#include "css/stylesheet.h"
#include "html/parser.h"
#include "layout/layout_engine.h"
#include "network/resource_cache.h"
#include "network/text_decode.h"
#include "network/url.h"
#include "platform/browser_core.h"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct StubMeasure final : ITextMeasure {
    float MeasureText(const std::wstring& text, const FontKey& font) override {
        return static_cast<float>(text.size()) * font.size * 0.5f;
    }

    float SpaceWidth(const FontKey& font) override {
        return font.size * 0.3f;
    }

    bool ImageIntrinsic(const std::string&, float& width, float& height) override {
        width = 0.0f;
        height = 0.0f;
        return false;
    }

    void RequestImage(const std::string&) override {}
};

int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool UrlDecode(std::string_view input, std::string& output) {
    output.clear();
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '%') {
            output += input[i];
            continue;
        }

        if (i + 2 >= input.size()) {
            return false;
        }

        const int high = HexValue(input[i + 1]);
        const int low = HexValue(input[i + 2]);
        if (high < 0 || low < 0) {
            return false;
        }

        output += static_cast<char>((high << 4) | low);
        i += 2;
    }

    return true;
}

std::string ToLowerAscii(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    for (const unsigned char ch : input) {
        output += static_cast<char>(std::tolower(ch));
    }

    return output;
}

bool ExtractDataCss(std::string_view href, std::string& css) {
    constexpr std::string_view dataPrefix = "data:";
    constexpr std::string_view cssMimeType = "text/css";

    if (href.size() < dataPrefix.size() ||
        ToLowerAscii(href.substr(0, dataPrefix.size())) != dataPrefix) {
        return false;
    }

    const size_t comma = href.find(',');
    if (comma == std::string_view::npos) {
        return false;
    }

    const std::string metadata = ToLowerAscii(href.substr(dataPrefix.size(),
                                                          comma - dataPrefix.size()));
    const size_t semicolon = metadata.find(';');
    const std::string_view mediaType = std::string_view(metadata).substr(
        0, semicolon == std::string::npos ? metadata.size() : semicolon);

    if (mediaType != cssMimeType) {
        return false;
    }

    if (metadata.find(";base64") != std::string::npos) {
        std::fprintf(stderr,
                     "Warning: base64 data stylesheets are not supported by dump_layout\n");
        return false;
    }

    if (!UrlDecode(href.substr(comma + 1), css)) {
        std::fprintf(stderr, "Warning: ignoring malformed percent-encoded stylesheet URL\n");
        return false;
    }

    return true;
}

void AppendStylesheet(Stylesheet& destination, const Stylesheet& source) {
    if (source.rootRemBaseSet) {
        destination.rootRemBase = source.rootRemBase;
        destination.rootRemBaseSet = true;
    }

    destination.rules.insert(destination.rules.end(),
                             source.rules.begin(),
                             source.rules.end());
}

Stylesheet CollectCss(const Node* root) {
    Stylesheet sheet;
    if (!root) {
        return sheet;
    }

    std::vector<const Node*> stack{root};

    while (!stack.empty()) {
        const Node* node = stack.back();
        stack.pop_back();

        if (!node) {
            continue;
        }

        if (node->type == NodeType::Element && node->tagName == "style") {
            std::string css;

            for (const auto& child : node->children) {
                if (child && child->type == NodeType::Text) {
                    css += child->text;
                }
            }

            AppendStylesheet(sheet, ParseStylesheet(css));
        } else if (node->type == NodeType::Element && node->tagName == "link") {
            const std::string rel = ToLowerAscii(node->attr("rel"));

            if (rel.find("stylesheet") != std::string::npos) {
                std::string css;

                if (ExtractDataCss(node->attr("href"), css)) {
                    AppendStylesheet(sheet, ParseStylesheet(css));
                }
            }
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it) {
                stack.push_back(it->get());
            }
        }
    }

    sheet.rebuildRuleBuckets();
    return sheet;
}

const char* KindName(BoxKind kind) {
    switch (kind) {
    case BoxKind::Block:
        return "block";
    case BoxKind::Inline:
        return "inline";
    case BoxKind::InlineBlock:
        return "iblock";
    case BoxKind::Replaced:
        return "img";
    case BoxKind::Text:
        return "text";
    case BoxKind::ListItem:
        return "li";
    case BoxKind::Table:
        return "table";
    case BoxKind::TableRow:
        return "tr";
    case BoxKind::TableCell:
        return "td";
    case BoxKind::Break:
        return "br";
    }

    return "?";
}

void PrintBox(const LayoutBox& box, int depth) {
    const std::string indent(static_cast<size_t>(depth) * 2, ' ');
    const std::string tag =
        box.node && box.node->type == NodeType::Element
            ? box.node->tagName
            : (box.anonymous ? "(anon)" : "");

    const std::string className = box.node ? box.node->attr("class") : "";

    char background[64] = "";
    if (box.style.bgColor.valid) {
        std::snprintf(background, sizeof(background),
                      " bg=%.1f,%.1f,%.1f,%.1f",
                      box.style.bgColor.r,
                      box.style.bgColor.g,
                      box.style.bgColor.b,
                      box.style.bgColor.a);
    }

    char position[24] = "";
    if (box.style.positionMode) {
        std::snprintf(position, sizeof(position),
                      " pos=%d", box.style.positionMode);
    }

    char floatMode[16] = "";
    if (box.style.floatMode) {
        std::snprintf(floatMode, sizeof(floatMode),
                      " float=%d", box.style.floatMode);
    }

    std::printf("%s%s<%s%s%s> x=%.0f y=%.0f w=%.0f h=%.0f%s%s%s\n",
                indent.c_str(),
                KindName(box.kind),
                tag.c_str(),
                className.empty() ? "" : ".",
                className.c_str(),
                box.x,
                box.y,
                box.borderBoxW(),
                box.borderBoxH(),
                background,
                position,
                floatMode);
}

void DumpLayoutTree(const LayoutBox& root) {
    std::vector<std::pair<const LayoutBox*, int>> stack;
    stack.emplace_back(&root, 0);

    while (!stack.empty()) {
        const auto [box, depth] = stack.back();
        stack.pop_back();

        if (!box) {
            continue;
        }

        PrintBox(*box, depth);

        for (auto it = box->kids.rbegin(); it != box->kids.rend(); ++it) {
            if (*it) {
                stack.emplace_back(it->get(), depth + 1);
            }
        }
    }
}

bool ParseViewportWidth(const char* text, float& width) {
    char* end = nullptr;
    errno = 0;

    const float value = std::strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0' || value <= 0.0f) {
        return false;
    }

    width = value;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "Usage: %s <file.html|url> [viewport_width]\n", argv[0]);
        return 1;
    }

    float viewportWidth = 1200.0f;
    if (argc == 3 && !ParseViewportWidth(argv[2], viewportWidth)) {
        std::fprintf(stderr, "Invalid viewport width: %s\n", argv[2]);
        return 1;
    }

    const std::string source = argv[1];
    std::string html;
    std::string baseUrl;

    if (HasUrlScheme(source)) {
        const FetchResult result = FetchResourceCached(
            source, 12 * 1024 * 1024, ResourceKind::Document);

        if (!result.success) {
            std::fprintf(stderr, "Fetch failed: %s\n", result.error.c_str());
            return 2;
        }

        html = DecodeTextToUtf8(result.body, result.contentType);
        baseUrl = result.finalUrl.empty() ? source : result.finalUrl;
    } else {
        std::ifstream file(source, std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "Unable to open: %s\n", source.c_str());
            return 2;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();

        if (file.bad()) {
            std::fprintf(stderr, "Failed while reading: %s\n", source.c_str());
            return 2;
        }

        html = buffer.str();
    }

    std::printf("Parsing %zu bytes at viewport %.0fx800...\n",
                html.size(), viewportWidth);

    const auto dom = ParseHtml(html);
    if (!dom) {
        std::fprintf(stderr, "HTML parsing produced no document\n");
        return 3;
    }

    if (!baseUrl.empty()) {
        LoadExternalStylesheets(dom, baseUrl);
    }

    Stylesheet sheet = CollectCss(dom.get());
    sheet.setViewport(viewportWidth, 800.0f);

    StubMeasure measure;
    LayoutInput input;
    input.document = dom.get();
    input.sheet = &sheet;
    input.measure = &measure;
    input.viewportW = viewportWidth;
    input.viewportH = 800.0f;
    input.zoom = 1.0f;
    input.baseUrl = baseUrl;

    const auto root = LayoutDocument(input);
    if (!root) {
        std::fprintf(stderr, "LayoutDocument returned no layout tree\n");
        return 4;
    }

    DumpLayoutTree(*root);
    return 0;
}
