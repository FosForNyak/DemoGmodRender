#include "assets/vdf.h"

#include "core/text.h"

namespace gmdr::assets {

namespace {

constexpr std::size_t kMaxVdfBytes = 16u << 20;
constexpr int kMaxDepth = 64;
constexpr std::size_t kMaxNodes = 1u << 20;
constexpr std::size_t kMaxToken = 1u << 16;

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

class Lexer {
public:
    Lexer(std::string_view s, bool escapes) : s_(s), escapes_(escapes) {}

    enum class Kind { End, Open, Close, String, Error };
    struct Token {
        Kind kind = Kind::End;
        std::string text;
        bool quoted = false;
    };

    Token next() {
        skipSpaceAndComments();
        if (pos_ >= s_.size())
            return {Kind::End, {}, false};
        const char c = s_[pos_];
        if (c == '{') {
            ++pos_;
            return {Kind::Open, {}, false};
        }
        if (c == '}') {
            ++pos_;
            return {Kind::Close, {}, false};
        }
        if (c == '"')
            return quoted();
        return bare();
    }

    std::size_t line() const {
        std::size_t n = 1;
        for (std::size_t i = 0; i < pos_ && i < s_.size(); ++i)
            n += s_[i] == '\n';
        return n;
    }

private:
    void skipSpaceAndComments() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/') {
                while (pos_ < s_.size() && s_[pos_] != '\n')
                    ++pos_;
            } else if (c == '[') {
                // Conditional like [$WIN32]: ignored.
                while (pos_ < s_.size() && s_[pos_] != ']' && s_[pos_] != '\n')
                    ++pos_;
                if (pos_ < s_.size() && s_[pos_] == ']')
                    ++pos_;
            } else {
                break;
            }
        }
    }

    Token quoted() {
        ++pos_;
        std::string out;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"')
                return {Kind::String, std::move(out), true};
            if (escapes_ && c == '\\' && pos_ < s_.size()) {
                const char e = s_[pos_++];
                switch (e) {
                case 'n':
                    out += '\n';
                    break;
                case 't':
                    out += '\t';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '"':
                    out += '"';
                    break;
                default:
                    out += '\\';
                    out += e;
                }
            } else {
                out += c;
            }
            if (out.size() > kMaxToken)
                return {Kind::Error, "token too long", false};
        }
        return {Kind::Error, "unterminated string", false};
    }

    Token bare() {
        std::string out;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}' || c == '"')
                break;
            out += c;
            ++pos_;
            if (out.size() > kMaxToken)
                return {Kind::Error, "token too long", false};
        }
        return {Kind::String, std::move(out), false};
    }

    std::string_view s_;
    bool escapes_;
    std::size_t pos_ = 0;
};

Result<void> parseObject(Lexer& lex, VdfNode& into, int depth, std::size_t& nodes, bool topLevel) {
    if (depth > kMaxDepth)
        return makeError("vdf.too_deep", "VDF nesting is too deep");
    while (true) {
        auto key = lex.next();
        if (key.kind == Lexer::Kind::End) {
            if (topLevel)
                return {};
            return makeError("vdf.unexpected_end", "VDF ends inside an object",
                             "line " + std::to_string(lex.line()));
        }
        if (key.kind == Lexer::Kind::Close) {
            if (topLevel)
                return makeError("vdf.unexpected_close", "unexpected '}'",
                                 "line " + std::to_string(lex.line()));
            return {};
        }
        if (key.kind != Lexer::Kind::String)
            return makeError("vdf.syntax", "expected a key", "line " + std::to_string(lex.line()));
        if (++nodes > kMaxNodes)
            return makeError("vdf.too_large", "VDF has too many entries");
        VdfNode node;
        node.key = std::move(key.text);
        auto value = lex.next();
        if (value.kind == Lexer::Kind::Open) {
            node.object = true;
            GMDR_TRY(parseObject(lex, node, depth + 1, nodes, false));
        } else if (value.kind == Lexer::Kind::String) {
            node.value = std::move(value.text);
        } else {
            return makeError("vdf.syntax", "expected a value or '{'", "line " + std::to_string(lex.line()));
        }
        into.children.push_back(std::move(node));
    }
}

} // namespace

const VdfNode* VdfNode::child(std::string_view k) const {
    for (const auto& c : children)
        if (iequals(c.key, k))
            return &c;
    return nullptr;
}

std::string_view VdfNode::get(std::string_view k, std::string_view fallback) const {
    const VdfNode* c = child(k);
    return c && !c->object ? std::string_view(c->value) : fallback;
}

Result<VdfNode> parseVdf(std::string_view text, bool escapes) {
    if (text.size() > kMaxVdfBytes)
        return makeError("vdf.too_large", "VDF file is too large");
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    Lexer lex(text, escapes);
    VdfNode root;
    root.object = true;
    std::size_t nodes = 0;
    GMDR_TRY(parseObject(lex, root, 0, nodes, true));
    return root;
}

} // namespace gmdr::assets
