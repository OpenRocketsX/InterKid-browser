#include "html/parser.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

int main() {
    constexpr const char* html =
        "<!DOCTYPE html><html><head><title>Hello World</title></head>"
        "<body><p>Test</p></body></html>";

    const auto dom = ParseHtml(html);
    if (!dom) {
        std::fprintf(stderr, "ParseHtml returned no document\n");
        return 1;
    }

    std::vector<std::pair<const Node*, std::size_t>> stack;
    stack.emplace_back(dom.get(), 0);

    while (!stack.empty()) {
        const auto [node, depth] = stack.back();
        stack.pop_back();

        const int indent = static_cast<int>(depth * 2);

        switch (node->type) {
        case NodeType::Element:
            std::printf("%*sELEMENT <%s> children=%zu\n",
                        indent, "", node->tagName.c_str(), node->children.size());
            break;

        case NodeType::Text:
            std::printf("%*sTEXT \"%s\"\n",
                        indent, "", node->text.c_str());
            break;

        default:
            std::printf("%*sNODE type=%d\n",
                        indent, "", static_cast<int>(node->type));
            break;
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it) {
                stack.emplace_back(it->get(), depth + 1);
            }
        }
    }

    return 0;
}
