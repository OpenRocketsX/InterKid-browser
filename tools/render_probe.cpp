// render_probe.cpp - deterministic layout/paint diagnostics for fixtures.
//
// This intentionally renderer-free tool uses the real HTML/CSS/layout engine,
// then emits stable text for metrics, layout boxes, and conceptual paint order.
#include "css/stylesheet.h"
#include "html/parser.h"
#include "layout/layout_engine.h"

#include <algorithm>
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

struct ProbeMeasure final : ITextMeasure {
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
        0, semicolon == std::string::npos ? metadata.size() : semicolon);

    if (mediaType != cssMimeType) {
        return false;
    }

    if (metadata.find(";base64") != std::string::npos) {
        std::fprintf(stderr,
                     "render_probe: base64 data stylesheets are not supported\n");
        return false;
    }

    if (!UrlDecode(href.substr(comma + 1), css)) {
        std::fprintf(stderr,
                     "render_probe: ignoring malformed percent-encoded stylesheet URL\n");
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
        return "replaced";
    case BoxKind::Text:
        return "text";
    case BoxKind::ListItem:
        return "list-item";
    case BoxKind::Table:
        return "table";
    case BoxKind::TableRow:
        return "table-row";
    case BoxKind::TableCell:
        return "table-cell";
    case BoxKind::Break:
        return "break";
    }

    return "unknown";
}

std::string Label(const LayoutBox& box) {
    if (box.kind == BoxKind::Text) {
        return "#text";
    }

    if (!box.node) {
        return box.anonymous ? "(anonymous)" : "(box)";
    }

    std::string label = box.node->tagName;

    const std::string id = box.node->attr("id");
    if (!id.empty()) {
        label += "#" + id;
    }

    const std::string className = box.node->attr("class");
    if (!className.empty()) {
        label += ".";

        for (const char ch : className) {
            label += ch == ' ' ? '.' : ch;
        }
    }

    return label;
}

bool IsRealCssBox(const LayoutBox& box) {
    return box.kind != BoxKind::Text && box.kind != BoxKind::Break;
}

int EffectivePosition(const LayoutBox& box) {
    return IsRealCssBox(box) ? box.style.positionMode : 0;
}

std::string EffectiveZ(const LayoutBox& box) {
    if (!IsRealCssBox(box) || !box.style.zIndexSet) {
        return "auto";
    }

    return std::to_string(box.style.zIndex);
}

struct Metrics {
    int boxes = 0;
    int positioned = 0;
    int floats = 0;
    int links = 0;
    int lineBoxes = 0;
    int maxDepth = 0;
};

void CollectMetrics(const LayoutBox& root, Metrics& metrics) {
    std::vector<std::pair<const LayoutBox*, int>> stack;
    stack.emplace_back(&root, 0);

    while (!stack.empty()) {
        const auto [box, depth] = stack.back();
        stack.pop_back();

        if (!box) {
            continue;
        }

        ++metrics.boxes;
        metrics.maxDepth = std::max(metrics.maxDepth, depth);

        if (EffectivePosition(*box)) {
            ++metrics.positioned;
        }
        if (box->isFloat()) {
            ++metrics.floats;
        }
        if (!box->href.empty()) {
            ++metrics.links;
        }

        metrics.lineBoxes += static_cast<int>(box->lines.size());

        for (auto it = box->kids.rbegin(); it != box->kids.rend(); ++it) {
            if (*it) {
                stack.emplace_back(it->get(), depth + 1);
            }
        }
    }
}

void PrintLayoutBox(const LayoutBox& box, int depth) {
    const std::string indent(static_cast<size_t>(depth) * 2, ' ');

    std::printf("%slayout %s %s x=%.0f y=%.0f w=%.0f h=%.0f pos=%d z=%s\n",
                indent.c_str(),
                KindName(box.kind),
                Label(box).c_str(),
                box.x,
                box.y,
                box.borderBoxW(),
                box.borderBoxH(),
                EffectivePosition(box),
                EffectiveZ(box).c_str());
}

void DumpLayout(const LayoutBox& root) {
    std::vector<std::pair<const LayoutBox*, int>> stack;
    stack.emplace_back(&root, 0);

    while (!stack.empty()) {
        const auto [box, depth] = stack.back();
        stack.pop_back();

        if (!box) {
            continue;
        }

        PrintLayoutBox(*box, depth);

        for (auto it = box->kids.rbegin(); it != box->kids.rend(); ++it) {
            if (*it) {
                stack.emplace_back(it->get(), depth + 1);
            }
        }
    }
}

int PaintZ(const LayoutBox* box) {
    return IsRealCssBox(*box) && box->style.zIndexSet ? box->style.zIndex : 0;
}

void PaintOrder(const LayoutBox& box, std::vector<const LayoutBox*>& output) {
    output.push_back(&box);

    bool simple = true;
    for (const auto& child : box.kids) {
        if (!child) {
            continue;
        }

        if (child->isOutOfFlow() || child->isFloat() ||
            child->style.positionMode == 1 || child->style.zIndexSet) {
            simple = false;
            break;
        }
    }

    if (simple) {
        for (const auto& child : box.kids) {
            if (child) {
                PaintOrder(*child, output);
            }
        }
        return;
    }

    std::vector<const LayoutBox*> negativeZ;
    std::vector<const LayoutBox*> inFlow;
    std::vector<const LayoutBox*> floats;
    std::vector<const LayoutBox*> positioned;

    for (const auto& child : box.kids) {
        if (!child) {
            continue;
        }

        const LayoutBox* item = child.get();

        if (item->isOutOfFlow()) {
            if (item->style.zIndexSet && item->style.zIndex < 0) {
                negativeZ.push_back(item);
            } else {
                positioned.push_back(item);
            }
        } else if (item->isFloat()) {
            floats.push_back(item);
        } else if (item->style.positionMode == 1) {
            positioned.push_back(item);
        } else {
            inFlow.push_back(item);
        }
    }

    const auto byZ = [](const LayoutBox* left, const LayoutBox* right) {
        return PaintZ(left) < PaintZ(right);
    };

    std::stable_sort(negativeZ.begin(), negativeZ.end(), byZ);
    std::stable_sort(positioned.begin(), positioned.end(), byZ);

    for (const LayoutBox* item : negativeZ) {
        PaintOrder(*item, output);
    }
    for (const LayoutBox* item : inFlow) {
        PaintOrder(*item, output);
    }
    for (const LayoutBox* item : floats) {
        PaintOrder(*item, output);
    }
    for (const LayoutBox* item : positioned) {
        PaintOrder(*item, output);
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
        std::fprintf(stderr, "Usage: %s <file.html> [viewport-width]\n", argv[0]);
        return 1;
    }

    float viewportWidth = 1200.0f;
    if (argc == 3 && !ParseViewportWidth(argv[2], viewportWidth)) {
        std::fprintf(stderr, "Invalid viewport width: %s\n", argv[2]);
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "render_probe: could not open %s\n", argv[1]);
        return 2;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    if (file.bad()) {
        std::fprintf(stderr, "render_probe: failed while reading %s\n", argv[1]);
        return 2;
    }

    const auto dom = ParseHtml(buffer.str());
    if (!dom) {
        std::fprintf(stderr, "render_probe: HTML parsing produced no document\n");
        return 3;
    }

    Stylesheet sheet = CollectCss(dom.get());
    sheet.setViewport(viewportWidth, 800.0f);

    ProbeMeasure measure;

    LayoutInput input;
    input.document = dom.get();
    input.sheet = &sheet;
    input.measure = &measure;
    input.viewportW = viewportWidth;
    input.viewportH = 800.0f;
    input.zoom = 1.0f;

    const auto layout = LayoutDocument(input);
    if (!layout) {
        std::fprintf(stderr, "render_probe: LayoutDocument returned no layout tree\n");
        return 4;
    }

    Metrics metrics;
    CollectMetrics(*layout, metrics);

    std::printf(
        "metrics boxes=%d positioned=%d floats=%d links=%d lineBoxes=%d maxDepth=%d\n",
        metrics.boxes,
        metrics.positioned,
        metrics.floats,
        metrics.links,
        metrics.lineBoxes,
        metrics.maxDepth);

    DumpLayout(*layout);

    std::vector<const LayoutBox*> order;
    PaintOrder(*layout, order);

    for (size_t i = 0; i < order.size(); ++i) {
        const LayoutBox* box = order[i];

        std::printf("paint-order %zu %s %s z=%s\n",
                    i,
                    KindName(box->kind),
                    Label(*box).c_str(),
                    EffectiveZ(*box).c_str());
    }

    return 0;
}
