// dump_js.cpp — offline inline-JavaScript reproducer.
#include "html/parser.h"
#include "js/engine.h"

#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <file.html>\n", argv[0]);
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "Unable to open: %s\n", argv[1]);
        return 1;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    if (file.bad()) {
        std::fprintf(stderr, "Failed while reading: %s\n", argv[1]);
        return 1;
    }

    const std::string html = buffer.str();

    try {
        std::printf("Parsing %zu bytes...\n", html.size());
        std::fflush(stdout);

        auto dom = ParseHtml(html);
        if (!dom) {
            std::fprintf(stderr, "ParseHtml returned no document\n");
            return 2;
        }

        std::printf("Parsed. Attaching document to JS engine...\n");
        std::fflush(stdout);

        JsEngine js;
        js.setDocument(dom, [] {});

        std::printf("setDocument OK. Running inline scripts...\n");
        std::fflush(stdout);

        std::vector<const Node*> stack{dom.get()};
        int scriptIndex = 0;

        while (!stack.empty()) {
            const Node* node = stack.back();
            stack.pop_back();

            if (node->type == NodeType::Element && node->tagName == "script") {
                std::string source;

                for (const auto& child : node->children) {
                    if (child && child->type == NodeType::Text) {
                        source += child->text;
                    }
                }

                if (source.empty()) {
                    std::printf("  Script #%d: empty; skipped\n", scriptIndex);
                } else {
                    std::printf("  Script #%d (%zu bytes)...\n",
                                scriptIndex, source.size());
                    std::fflush(stdout);

                    js.runScript(source, "inline-" + std::to_string(scriptIndex));

                    std::printf("  Script #%d: done\n", scriptIndex);
                }

                ++scriptIndex;
            }

            for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
                if (*it) {
                    stack.push_back(it->get());
                }
            }
        }

        std::printf("All %d inline script(s) complete. Running macrotasks...\n",
                    scriptIndex);
        std::fflush(stdout);

        js.runMacrotasks();

        std::printf("DONE — no crash\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "JS repro failed: %s\n", error.what());
        return 3;
    }
}
