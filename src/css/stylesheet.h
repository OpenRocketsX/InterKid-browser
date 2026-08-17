#pragma once
#include "css/style.h"
#include "html/dom.h"
#include <vector>
#include <string>
#include <cstddef>
#include <unordered_map>

enum class CssAttrMatch { Exists, Exact, Includes, DashPrefix, Prefix, Suffix, Substring };

struct CssSelectorPart {
    std::string tag;
    std::vector<std::string> classes;
    std::string id;
    std::string attrName;
    std::string attrValue;
    bool attrHasValue = false;
    bool attrCaseInsensitive = false;
    CssAttrMatch attrMatch = CssAttrMatch::Exists;
    std::vector<std::string> pseudos;
    std::vector<std::vector<std::string>> notSelectorLists;
    std::vector<std::vector<std::string>> matchSelectorLists;
    std::vector<std::vector<std::string>> hasSelectorLists;
    int functionalSpecificity = 0;
    std::string pseudoElement;
    bool neverMatch = false;
    char combinator = 0;
};

struct CssMediaCondition {
    float minWidth = -1.f;
    float maxWidth = -1.f;
    float minHeight = -1.f;
    float maxHeight = -1.f;
    int colorScheme = 0;
    bool supported = true;

    bool matches(float width, float height, bool prefersDark) const {
        return supported
            && (minWidth < 0.f || width >= minWidth)
            && (maxWidth < 0.f || width <= maxWidth)
            && (minHeight < 0.f || height >= minHeight)
            && (maxHeight < 0.f || height <= maxHeight)
            && (colorScheme == 0 || (colorScheme == 2) == prefersDark);
    }
};

struct CssRule {
    std::string tag;
    std::string cls;
    std::string id;
    std::vector<CssSelectorPart> selector;
    ComputedStyle style;
    ComputedStyle importantStyle;
    std::vector<CssMediaCondition> media;
    int specificity() const;
    bool matches(const Node* node) const;
};

struct FontFace { std::string family; std::string srcUrl; };

struct KeyframeStop {
    float percent = 0.f;
    ComputedStyle style;
};

struct Stylesheet {
    std::vector<CssRule> rules;
    std::vector<FontFace> fontFaces;
    std::unordered_map<std::string, std::vector<KeyframeStop>> keyframes;
    std::unordered_map<std::string, std::vector<size_t>> idRuleBuckets;
    std::unordered_map<std::string, std::vector<size_t>> classRuleBuckets;
    std::unordered_map<std::string, std::vector<size_t>> tagRuleBuckets;
    std::vector<size_t> universalRuleBucket;
    mutable std::unordered_map<std::string, std::vector<size_t>> candidateRuleIndexCache;
    float viewportWidth = 800.f;
    float viewportHeight = 600.f;
    float rootRemBase = 16.f;
    bool rootRemBaseSet = false;
    bool prefersDarkScheme = false;
    void setViewport(float width, float height) {
        viewportWidth = width; viewportHeight = height; candidateRuleIndexCache.clear();
    }
    void setPreferredColorScheme(bool dark) {
        prefersDarkScheme = dark; candidateRuleIndexCache.clear();
    }
    void rebuildRuleBuckets();
    ComputedStyle resolve(const Node* node) const;
};

Stylesheet ParseStylesheet(const std::string& css);
ComputedStyle ParseInlineStyle(const std::string& style);
void ResolveStyleVariables(ComputedStyle& style);
void SetCssHoverNode(const Node* node);
void SetCssFocusNode(const Node* node);
void SetCssViewport(float w, float h);
std::string SerializeStylesheet(const Stylesheet& sheet);
std::string SerializeComputedStyle(const ComputedStyle& style);
