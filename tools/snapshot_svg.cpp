// snapshot_svg.cpp - URL/file visual snapshot from the real layout tree.
//
// Writes an SVG approximation containing backgrounds, borders, text fragments,
// and replaced-element placeholders. This tool intentionally does not render
// actual image content or execute JavaScript.
#include "css/stylesheet.h"
#include "html/parser.h"
#include "layout/layout_engine.h"
#include "network/resource_cache.h"
#include "network/text_decode.h"
#include "network/url.h"
#include "platform/browser_core.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct SvgMeasure final : ITextMeasure {
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

    const std::string metadata = ToLowerAscii(
        href.substr(dataPrefix.size(), comma - dataPrefix.size()));

    const size_t semicolon = metadata.find(';');
    const std::string_view mediaType = std::string_view(metadata).substr(
        0,
        semicolon == std::string::npos ? metadata.size() : semicolon);

    if (mediaType != cssMimeType) {
        return false;
    }

    if (metadata.find(";base64") != std::string::npos) {
        std::fprintf(stderr,
                     "snapshot_svg: base64 data stylesheets are not supported\n");
        return false;
    }

    if (!UrlDecode(href.substr(comma + 1), css)) {
        std::fprintf(stderr,
                     "snapshot_svg: ignoring malformed percent-encoded stylesheet URL\n");
        return false;
    }

    return true;
}

void AppendStylesheet(Stylesheet& destination, Stylesheet source) {
    if (source.rootRemBaseSet) {
        destination.rootRemBase = source.rootRemBase;
        destination.rootRemBaseSet = true;
    }

    for (auto& rule : source.rules) {
        destination.rules.push_back(std::move(rule));
    }
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

void AppendXmlCodePoint(std::string& output, unsigned int codePoint) {
    if (codePoint == '&') {
        output += "&amp;";
    } else if (codePoint == '<') {
        output += "&lt;";
    } else if (codePoint == '>') {
        output += "&gt;";
    } else if (codePoint == '"') {
        output += "&quot;";
    } else if (codePoint >= 32 &&
               codePoint != 127 &&
               codePoint <= 0x10FFFF) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "&#%u;", codePoint);
        output += buffer;
    }
}

std::string EscapeXml(const std::wstring& text) {
    std::string output;

    for (size_t i = 0; i < text.size(); ++i) {
        unsigned int codePoint = static_cast<unsigned int>(text[i]);

        if constexpr (sizeof(wchar_t) == 2) {
            const bool isHighSurrogate =
                codePoint >= 0xD800 && codePoint <= 0xDBFF;

            if (isHighSurrogate && i + 1 < text.size()) {
                const unsigned int low =
                    static_cast<unsigned int>(text[i + 1]);

                if (low >= 0xDC00 && low <= 0xDFFF) {
                    codePoint = 0x10000 +
                                ((codePoint - 0xD800) << 10) +
                                (low - 0xDC00);
                    ++i;
                } else {
                    codePoint = 0xFFFD;
                }
            } else if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
                codePoint = 0xFFFD;
            }
        }

        AppendXmlCodePoint(output, codePoint);
    }

    return output;
}

std::string Color(const CssColor& color) {
    const int red = std::clamp(
        static_cast<int>(color.r * 255.0f + 0.5f), 0, 255);
    const int green = std::clamp(
        static_cast<int>(color.g * 255.0f + 0.5f), 0, 255);
    const int blue = std::clamp(
        static_cast<int>(color.b * 255.0f + 0.5f), 0, 255);

    char buffer[64];

    if (color.a < 0.999f) {
        std::snprintf(buffer, sizeof(buffer),
                      "rgba(%d,%d,%d,%.3f)",
                      red, green, blue,
                      std::clamp(color.a, 0.0f, 1.0f));
    } else {
        std::snprintf(buffer, sizeof(buffer),
                      "#%02x%02x%02x",
                      red, green, blue);
    }

    return buffer;
}

bool ValidSvgNumber(float value) {
    return std::isfinite(value);
}

void Rect(std::ostream& output,
          float x,
          float y,
          float width,
          float height,
          const std::string& fill,
          const std::string& stroke = {},
          float strokeWidth = 0.0f) {
    if (width <= 0.0f || height <= 0.0f ||
        !ValidSvgNumber(x) || !ValidSvgNumber(y) ||
        !ValidSvgNumber(width) || !ValidSvgNumber(height)) {
        return;
    }

    output << "<rect x=\"" << x
           << "\" y=\"" << y
           << "\" width=\"" << width
           << "\" height=\"" << height
           << "\" fill=\"" << fill << "\"";

    if (!stroke.empty() && strokeWidth > 0.0f &&
        ValidSvgNumber(strokeWidth)) {
        output << " stroke=\"" << stroke
               << "\" stroke-width=\"" << strokeWidth << "\"";
    }

    output << "/>\n";
}

void PaintBox(const LayoutBox& box, std::ostream& output) {
    if (box.style.bgColor.valid && box.style.bgColor.a > 0.001f) {
        Rect(output,
             box.x,
             box.y,
             box.borderBoxW(),
             box.borderBoxH(),
             Color(box.style.bgColor));
    }

    const float borderWidth = std::max(
        std::max(box.borderTop, box.borderRight),
        std::max(box.borderBottom, box.borderLeft));

    if (borderWidth > 0.0f) {
        const CssColor borderColor =
            box.style.borderColor.valid
                ? box.style.borderColor
                : CssColor{true, 0.0f, 0.0f, 0.0f, 1.0f};

        Rect(output,
             box.x,
             box.y,
             box.borderBoxW(),
             box.borderBoxH(),
             "none",
             Color(borderColor),
             borderWidth);
    }

    if (box.kind == BoxKind::Replaced ||
        (!box.replacedUrl.empty() && box.kids.empty())) {
        Rect(output,
             box.x,
             box.y,
             box.borderBoxW(),
             box.borderBoxH(),
             "#f3f4f6",
             "#cbd5e1",
             1.0f);
    }

    for (const auto& line : box.lines) {
        for (const auto& fragment : line.frags) {
            if (fragment.text.empty()) {
                continue;
            }

            CssColor textColor =
                fragment.src ? fragment.src->style.color : box.style.color;

            if (!textColor.valid) {
                textColor = {true, 0.0f, 0.0f, 0.0f, 1.0f};
            }

            const float fontSize =
                fragment.src && fragment.src->style.fontSize > 0.0f
                    ? fragment.src->style.fontSize
                    : 16.0f;

            const float textY = fragment.y + fragment.baseline;

            if (!ValidSvgNumber(fragment.x) ||
                !ValidSvgNumber(textY) ||
                !ValidSvgNumber(fontSize)) {
                continue;
            }

            output << "<text x=\"" << fragment.x
                   << "\" y=\"" << textY
                   << "\" font-size=\"" << fontSize
                   << "\" fill=\"" << Color(textColor)
                   << "\">"
                   << EscapeXml(fragment.text)
                   << "</text>\n";
        }
    }
}

void PaintSvg(const LayoutBox& root, std::ostream& output) {
    std::vector<const LayoutBox*> stack{&root};

    while (!stack.empty()) {
        const LayoutBox* box = stack.back();
        stack.pop_back();

        if (!box || box->style.visibilityHidden) {
            continue;
        }

        PaintBox(*box, output);

        for (auto it = box->kids.rbegin(); it != box->kids.rend(); ++it) {
            if (*it) {
                stack.push_back(it->get());
            }
        }
    }
}

bool ParseDimension(const char* text, float& output) {
    char* end = nullptr;
    errno = 0;

    const float value = std::strtof(text, &end);

    if (errno != 0 || end == text || *end != '\0' ||
        !std::isfinite(value) || value <= 0.0f) {
        return false;
    }

    output = value;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        std::fprintf(stderr,
                     "Usage: %s <file.html|url> <out.svg> [width] [height]\n",
                     argv[0]);
        return 1;
    }

    float width = 1200.0f;
    float height = 800.0f;

    if (argc >= 4 && !ParseDimension(argv[3], width)) {
        std::fprintf(stderr, "Invalid width: %s\n", argv[3]);
        return 1;
    }

    if (argc == 5 && !ParseDimension(argv[4], height)) {
        std::fprintf(stderr, "Invalid height: %s\n", argv[4]);
        return 1;
    }

    const std::string source = argv[1];
    const std::string outputPath = argv[2];

    std::string html;
    std::string baseUrl;

    if (HasUrlScheme(source)) {
        const FetchResult result = FetchResourceCached(
            source,
            12 * 1024 * 1024,
            ResourceKind::Document);

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

    const auto dom = ParseHtml(html);

    if (!dom) {
        std::fprintf(stderr, "HTML parsing produced no document\n");
        return 3;
    }

    if (!baseUrl.empty()) {
        LoadExternalStylesheets(dom, baseUrl);
    }

    Stylesheet sheet = CollectCss(dom.get());
    sheet.setViewport(width, height);

    SvgMeasure measure;

    LayoutInput input;
    input.document = dom.get();
    input.sheet = &sheet;
    input.measure = &measure;
    input.viewportW = width;
    input.viewportH = height;
    input.zoom = 1.0f;
    input.baseUrl = baseUrl;

    const auto root = LayoutDocument(input);

    if (!root) {
        std::fprintf(stderr, "LayoutDocument returned no layout tree\n");
        return 4;
    }

    std::ofstream output(outputPath, std::ios::binary);

    if (!output) {
        std::fprintf(stderr, "Unable to write: %s\n", outputPath.c_str());
        return 5;
    }

    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    output << "<svg xmlns=\"http://www.w3.org/2000/svg\""
           << " width=\"" << width << "\""
           << " height=\"" << height << "\""
           << " viewBox=\"0 0 " << width << " " << height << "\">\n";

    output << "<defs><clipPath id=\"viewport-clip\">"
           << "<rect x=\"0\" y=\"0\" width=\"" << width
           << "\" height=\"" << height << "\"/>"
           << "</clipPath></defs>\n";

    output << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    output << "<g clip-path=\"url(#viewport-clip)\">\n";

    PaintSvg(*root, output);

    output << "</g>\n";
    output << "</svg>\n";

    if (!output) {
        std::fprintf(stderr, "Write failed: %s\n", outputPath.c_str());
        return 5;
    }

    std::printf("Wrote %s\n", outputPath.c_str());
    return 0;
}
