// site_probe.cpp — real-site compatibility harness.
// Fetches a page, parses DOM, loads bounded external scripts, runs JS, lays out,
// and prints enough stats/errors to drive crash-first compatibility work.

#include "css/stylesheet.h"
#include "html/parser.h"
#include "html/resources.h"
#include "js/engine.h"
#include "layout/layout_engine.h"
#include "network/resource_cache.h"
#include "network/text_decode.h"
#include "network/url.h"
#include "platform/browser_core.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>

#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif
#endif

#ifdef _WIN32
static LONG WINAPI SehHandler(EXCEPTION_POINTERS* exceptionPointers) {
    const DWORD code = exceptionPointers->ExceptionRecord->ExceptionCode;
    void* address = exceptionPointers->ExceptionRecord->ExceptionAddress;

    std::fprintf(stderr, "CRASH: exception=0x%08lX addr=%p\n", code, address);

    HANDLE process = GetCurrentProcess();
    const bool symbolsReady = SymInitialize(process, nullptr, TRUE) == TRUE;

    if (symbolsReady) {
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    }

    void* stack[64];
    const USHORT frames = CaptureStackBackTrace(0, 64, stack, nullptr);

    for (USHORT i = 0; i < frames; ++i) {
        const DWORD64 frameAddress = reinterpret_cast<DWORD64>(stack[i]);

        if (!symbolsReady) {
            std::fprintf(stderr, "  [%u] 0x%llX\n", i, frameAddress);
            continue;
        }

        char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisplacement = 0;

        if (SymFromAddr(process, frameAddress, &displacement, symbol)) {
            std::fprintf(stderr, "  [%u] %s+0x%llX",
                         i, symbol->Name, displacement);
        } else {
            std::fprintf(stderr, "  [%u] 0x%llX", i, frameAddress);
        }

        if (SymGetLineFromAddr64(process, frameAddress, &lineDisplacement, &line)) {
            std::fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
        }

        std::fprintf(stderr, "\n");
    }

    if (symbolsReady) {
        SymCleanup(process);
    }

    std::fflush(stderr);
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

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

static void Log(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stdout, format, arguments);
    va_end(arguments);
    std::fflush(stdout);
}

static std::string Lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

static std::string Truncate(const std::string& value, size_t limit) {
    if (value.size() <= limit) {
        return value;
    }
    return value.substr(0, limit) + "...[" + std::to_string(value.size()) + "]";
}

static std::string NormalizeUrl(std::string url) {
    if (HasUrlScheme(url)) {
        return url;
    }
    return "https://" + url;
}

static bool ParseNonNegativeInt(const char* text, int& output) {
    char* end = nullptr;
    errno = 0;

    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value < 0 || value > 1000000) {
        return false;
    }

    output = static_cast<int>(value);
    return true;
}

static std::string TextContent(const Node* root) {
    if (!root) {
        return {};
    }

    std::string output;
    std::vector<const Node*> stack{root};

    while (!stack.empty()) {
        const Node* node = stack.back();
        stack.pop_back();

        if (!node) {
            continue;
        }

        if (node->type == NodeType::Text) {
            output += node->text;
            continue;
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it) {
                stack.push_back(it->get());
            }
        }
    }

    return output;
}

static std::string PageTitle(const Node* root) {
    if (!root) {
        return {};
    }

    std::vector<const Node*> stack{root};

    while (!stack.empty()) {
        const Node* node = stack.back();
        stack.pop_back();

        if (!node) {
            continue;
        }

        if (node->type == NodeType::Element && Lower(node->tagName) == "title") {
            return TextContent(node);
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it) {
                stack.push_back(it->get());
            }
        }
    }

    return {};
}

static void AppendStylesheet(Stylesheet& destination, Stylesheet source) {
    if (source.rootRemBaseSet) {
        destination.rootRemBase = source.rootRemBase;
        destination.rootRemBaseSet = true;
    }

    for (auto& rule : source.rules) {
        destination.rules.push_back(std::move(rule));
    }
}

static Stylesheet CollectCss(const Node* root, const std::string& baseUrl) {
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
            AppendStylesheet(sheet, ParseStylesheet(TextContent(node)));
        } else if (node->type == NodeType::Element &&
                   node->tagName == "link" &&
                   Lower(node->attr("rel")).find("stylesheet") != std::string::npos) {
            const std::string href = node->attr("href");

            if (!href.empty()) {
                const std::string stylesheetUrl = ResolveUrlAgainstBase(href, baseUrl);
                const FetchResult result = FetchResourceCached(
                    stylesheetUrl,
                    1024 * 1024,
                    ResourceKind::Stylesheet);

                if (result.success && !result.body.empty()) {
                    AppendStylesheet(
                        sheet,
                        ParseStylesheet(
                            DecodeTextToUtf8(result.body, result.contentType)));
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

static bool IsScriptNode(const Node* node) {
    if (!node || node->type != NodeType::Element || node->tagName != "script") {
        return false;
    }

    const std::string type = Lower(node->attr("type"));

    return type.empty() ||
           type == "text/javascript" ||
           type == "application/javascript" ||
           type == "application/ecmascript" ||
           type == "text/ecmascript";
}

static std::vector<Node*> Scripts(Node* root) {
    std::vector<Node*> output;
    if (!root) {
        return output;
    }

    std::vector<Node*> stack{root};

    while (!stack.empty()) {
        Node* node = stack.back();
        stack.pop_back();

        if (!node) {
            continue;
        }

        if (IsScriptNode(node)) {
            output.push_back(node);
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it) {
                stack.push_back(it->get());
            }
        }
    }

    return output;
}

static Node* FindById(Node* root, const std::string& id) {
    if (!root) {
        return nullptr;
    }

    std::vector<Node*> stack{root};

    while (!stack.empty()) {
        Node* node = stack.back();
        stack.pop_back();

        if (!node) {
            continue;
        }

        if (node->type == NodeType::Element && node->attr("id") == id) {
            return node;
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it) {
                stack.push_back(it->get());
            }
        }
    }

    return nullptr;
}

static Node* FindByTagMutable(Node* root, const std::string& tag) {
    if (!root) {
        return nullptr;
    }

    std::vector<Node*> stack{root};

    while (!stack.empty()) {
        Node* node = stack.back();
        stack.pop_back();

        if (!node) {
            continue;
        }

        if (node->type == NodeType::Element && node->tagName == tag) {
            return node;
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it) {
                stack.push_back(it->get());
            }
        }
    }

    return nullptr;
}

static bool HasClass(const Node* node, const std::string& className) {
    if (!node) {
        return false;
    }

    std::istringstream input(node->attr("class"));
    std::string token;

    while (input >> token) {
        if (token == className) {
            return true;
        }
    }

    return false;
}

static Node* FindByClass(Node* root, const std::string& className) {
    if (!root) {
        return nullptr;
    }

    std::vector<Node*> stack{root};

    while (!stack.empty()) {
        Node* node = stack.back();
        stack.pop_back();

        if (!node) {
            continue;
        }

        if (HasClass(node, className)) {
            return node;
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it) {
                stack.push_back(it->get());
            }
        }
    }

    return nullptr;
}

static std::string FirstTags(Node* root, int limit) {
    std::string output;
    if (!root || limit <= 0) {
        return output;
    }

    std::vector<Node*> stack{root};

    while (!stack.empty() && limit > 0) {
        Node* node = stack.back();
        stack.pop_back();

        if (!node) {
            continue;
        }

        if (node->type == NodeType::Element) {
            if (!output.empty()) {
                output += ",";
            }

            output += node->tagName;

            const std::string id = node->attr("id");
            if (!id.empty()) {
                output += "#" + id;
            }

            const std::string className = node->attr("class");
            if (!className.empty()) {
                output += "." + className;
            }

            --limit;
        }

        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it) {
                stack.push_back(it->get());
            }
        }
    }

    return output;
}

static size_t CountDomNodes(const Node* root) {
    if (!root) {
        return 0;
    }

    size_t count = 0;
    std::vector<const Node*> stack{root};

    while (!stack.empty()) {
        const Node* node = stack.back();
        stack.pop_back();

        if (!node) {
            continue;
        }

        ++count;

        for (const auto& child : node->children) {
            if (child) {
                stack.push_back(child.get());
            }
        }
    }

    return count;
}

static void Pump(JsEngine& js, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        js.runMacrotasks(64);
        DrainResourceCompletions(64);
    }
}

static bool TryPump(JsEngine& js, int ticks, const char* label) {
    try {
        Pump(js, ticks);
        return true;
    } catch (const std::exception& error) {
        Log("%s failed: %s\n", label, error.what());
    } catch (...) {
        Log("%s failed: unknown C++ exception\n", label);
    }

    return false;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(SehHandler);
#endif

    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: site_probe URL [max_scripts] [--verbose] [--websitething-flow]\n");
        return 2;
    }

    const std::string url = NormalizeUrl(argv[1]);

    int maxScripts = 64;
    bool verbose = false;
    bool websitethingFlow = false;

    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];

        if (argument == "--verbose" || argument == "-v") {
            verbose = true;
        } else if (argument == "--websitething-flow") {
            websitethingFlow = true;
        } else if (!ParseNonNegativeInt(argv[i], maxScripts)) {
            std::fprintf(stderr, "invalid argument: %s\n", argv[i]);
            return 2;
        }
    }

    Log("=== %s ===\n", url.c_str());

    const FetchResult documentResult = FetchResourceCached(
        url,
        12 * 1024 * 1024,
        ResourceKind::Document);

    Log("fetch ok=%d status=%d bytes=%zu type=%s final=%s\n",
        documentResult.success ? 1 : 0,
        documentResult.status,
        documentResult.body.size(),
        documentResult.contentType.c_str(),
        documentResult.finalUrl.c_str());

    if (!documentResult.success) {
        Log("FAIL fetch error=%s\n", documentResult.error.c_str());
        return 1;
    }

    const std::string finalUrl =
        documentResult.finalUrl.empty() ? url : documentResult.finalUrl;

    const std::string html =
        DecodeTextToUtf8(documentResult.body, documentResult.contentType);

    auto dom = ParseHtml(html);
    if (!dom) {
        Log("FAIL parse: parser returned no document\n");
        return 3;
    }

    const size_t domNodeCount = CountDomNodes(dom.get());

    Log("dom nodes=%zu html_bytes=%zu\n", domNodeCount, html.size());
    Log("title=%s\n", Truncate(PageTitle(dom.get()), 120).c_str());

    LoadExternalStylesheets(dom, finalUrl);
    LoadExternalScriptSources(dom, finalUrl);

    const std::vector<Node*> scripts = Scripts(dom.get());

    size_t inlineScripts = 0;
    size_t externalScripts = 0;
    size_t totalScriptBytes = 0;

    for (const Node* script : scripts) {
        const std::string source = TextContent(script);
        totalScriptBytes += source.size();

        if (script->attr("__vertex_script_filename").empty()) {
            ++inlineScripts;
        } else {
            ++externalScripts;
        }
    }

    Log("scripts total=%zu inline=%zu external=%zu script_bytes=%zu max=%d\n",
        scripts.size(),
        inlineScripts,
        externalScripts,
        totalScriptBytes,
        maxScripts);

    JsEngine js;
    JsScriptBudget budget;
    budget.maxScriptBytes = 256 * 1024;
    js.setScriptBudget(budget);
    js.setDocument(dom, []() {}, finalUrl);

    int attempted = 0;
    int failed = 0;
    int skipped = 0;

    for (Node* script : scripts) {
        if (attempted >= maxScripts) {
            break;
        }

        const std::string source = TextContent(script);
        if (source.empty()) {
            continue;
        }

        std::string filename = script->attr("__vertex_script_filename");
        if (filename.empty()) {
            filename = "inline:" + std::to_string(attempted);
        }

        if (source.size() > budget.maxScriptBytes) {
            if (verbose) {
                Log("  [%d] SKIP %zu bytes %s\n",
                    attempted,
                    source.size(),
                    Truncate(filename, 100).c_str());
            }

            ++skipped;
            ++attempted;
            continue;
        }

        if (verbose) {
            Log("  [%d] %zu bytes %s\n",
                attempted,
                source.size(),
                Truncate(filename, 100).c_str());
        }

        bool ok = false;

        try {
            ok = js.runScript(source, filename);
        } catch (const std::exception& error) {
            Log("  [%d] C++ exception: %s\n", attempted, error.what());
        } catch (...) {
            Log("  [%d] C++ exception: unknown\n", attempted);
        }

        if (!ok) {
            ++failed;
        }

        ++attempted;
    }

    js.dispatchDocumentEvent("DOMContentLoaded");
    js.dispatchWindowEvent("DOMContentLoaded");
    js.dispatchWindowEvent("load");
    TryPump(js, 32, "initial macrotasks");

    if (websitethingFlow) {
        TryPump(js, 130, "flow intro macrotasks");

        Node* counter = FindById(dom.get(), "counter");

        Log("websitething after_intro counter=%s title=%s\n",
            counter ? TextContent(counter).c_str() : "missing",
            Truncate(PageTitle(dom.get()), 120).c_str());

        Log("websitething tags=%s\n", FirstTags(dom.get(), 24).c_str());

        Node* body = FindByTagMutable(dom.get(), "body");

        for (int i = 0; i < 10; ++i) {
            if (body) {
                js.dispatchClick(body, 0, 0);
            }

            TryPump(js, 8, "flow click macrotasks");

            counter = FindById(dom.get(), "counter");

            Log("websitething click=%d counter=%s title=%s button=%d\n",
                i + 1,
                counter ? TextContent(counter).c_str() : "missing",
                Truncate(PageTitle(dom.get()), 120).c_str(),
                FindById(dom.get(), "button") ? 1 : 0);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        TryPump(js, 2500, "flow async macrotasks");

        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        TryPump(js, 64, "flow final macrotasks");

        counter = FindById(dom.get(), "counter");
        Node* button = FindById(dom.get(), "button");
        Node* app1 = FindByClass(dom.get(), "app1");

        if (button) {
            js.dispatchClick(button, 0, 0);
            TryPump(js, 8, "flow portfolio button macrotasks");
        }

        Node* prompt = FindById(dom.get(), "prompt");

        Log("websitething counter=%s title=%s portfolio_button=%d\n",
            counter ? TextContent(counter).c_str() : "missing",
            Truncate(PageTitle(dom.get()), 120).c_str(),
            button ? 1 : 0);

        Log("websitething portfolio_shaking=%d\n",
            HasClass(app1, "shaking") ? 1 : 0);

        Log("websitething prompt=%s\n",
            prompt ? TextContent(prompt).c_str() : "missing");

        Log("websitething pending_macrotasks=%d\n",
            js.hasPendingMacrotasks() ? 1 : 0);

        Log("websitething pending_resources=%d\n",
            HasPendingResourceCompletions() ? 1 : 0);

        Log("websitething final_tags=%s\n", FirstTags(dom.get(), 96).c_str());
    }

    Stylesheet sheet = CollectCss(dom.get(), finalUrl);
    sheet.setViewport(1366.0f, 768.0f);

    Log("css rules=%zu\n", sheet.rules.size());

    ProbeMeasure measure;

    LayoutInput input;
    input.document = dom.get();
    input.sheet = &sheet;
    input.measure = &measure;
    input.viewportW = 1366.0f;
    input.viewportH = 768.0f;
    input.zoom = 1.0f;
    input.baseUrl = finalUrl;

    const auto layout = LayoutDocument(input);

    if (layout) {
        Log("layout ok docW=%.0f docH=%.0f\n",
            layout->contentW,
            layout->contentH);
    } else {
        Log("layout FAIL (null)\n");
    }

    const JsScriptStats stats = js.scriptStats();

    Log("RESULT url=%s fetch=%d dom=%zu scripts=%d/%zu ok=%d fail=%d skip=%d "
        "parse=%d runtime=%d parse_ms=%.0f run_ms=%.0f layout=%d\n",
        url.c_str(),
        documentResult.success ? 1 : 0,
        domNodeCount,
        attempted,
        scripts.size(),
        attempted - failed - skipped,
        failed,
        skipped,
        static_cast<int>(stats.parseFailures),
        static_cast<int>(stats.runtimeFailures),
        stats.parseMs,
        stats.compileRunMs,
        layout ? 1 : 0);

    if (!layout) {
        return 4;
    }

    return failed == 0 ? 0 : 3;
}
