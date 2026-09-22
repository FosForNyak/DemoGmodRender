#include "vdf.hpp"

#include "strings.hpp"

#include <cctype>

namespace gmdr::vdf {

const Node* Node::child(std::string_view key) const {
    for (const auto& [k, v] : children)
        if (iequals(k, key)) return v.get();
    return nullptr;
}

std::string Node::get(std::string_view key, std::string def) const {
    const Node* c = child(key);
    return (c && !c->is_block) ? c->value : def;
}

namespace {
class Lexer {
public:
    explicit Lexer(std::string_view t) : t_(t) {}

    // Повертає токен: рядок, "{" або "}". nullopt — кінець.
    std::optional<std::string> next(bool& is_brace) {
        skip();
        if (pos_ >= t_.size()) return std::nullopt;
        const char c = t_[pos_];
        if (c == '{' || c == '}') {
            ++pos_;
            is_brace = true;
            return std::string(1, c);
        }
        is_brace = false;
        std::string out;
        if (c == '"') {
            ++pos_;
            while (pos_ < t_.size() && t_[pos_] != '"') {
                if (t_[pos_] == '\\' && pos_ + 1 < t_.size()) {
                    const char e = t_[pos_ + 1];
                    out += (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
                    pos_ += 2;
                    continue;
                }
                out += t_[pos_++];
            }
            ++pos_;   // закриваюча лапка
            return out;
        }
        while (pos_ < t_.size() && !std::isspace(static_cast<unsigned char>(t_[pos_])) && t_[pos_] != '{' &&
               t_[pos_] != '}' && t_[pos_] != '"')
            out += t_[pos_++];
        return out;
    }

private:
    void skip() {
        for (;;) {
            while (pos_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[pos_]))) ++pos_;
            if (pos_ + 1 < t_.size() && t_[pos_] == '/' && t_[pos_ + 1] == '/') {
                while (pos_ < t_.size() && t_[pos_] != '\n') ++pos_;
                continue;
            }
            // умовні блоки на кшталт [$WIN32] просто пропускаємо
            if (pos_ < t_.size() && t_[pos_] == '[') {
                while (pos_ < t_.size() && t_[pos_] != ']') ++pos_;
                if (pos_ < t_.size()) ++pos_;
                continue;
            }
            break;
        }
    }
    std::string_view t_;
    size_t           pos_ = 0;
};

bool parse_block(Lexer& lx, Node& parent, int depth) {
    if (depth > 32) return false;
    for (;;) {
        bool brace = false;
        auto key = lx.next(brace);
        if (!key) return true;                     // кінець файлу
        if (brace && *key == "}") return true;     // кінець блоку
        if (brace) return false;                   // "{" без ключа
        bool brace2 = false;
        auto val = lx.next(brace2);
        if (!val) return false;
        auto node = std::make_unique<Node>();
        if (brace2 && *val == "{") {
            node->is_block = true;
            if (!parse_block(lx, *node, depth + 1)) return false;
        } else if (brace2) {
            return false;
        } else {
            node->value = *val;
        }
        parent.children.emplace_back(*key, std::move(node));
    }
}
} // namespace

std::unique_ptr<Node> parse(std::string_view text) {
    auto root = std::make_unique<Node>();
    root->is_block = true;
    Lexer lx(text);
    if (!parse_block(lx, *root, 0)) return nullptr;
    return root;
}

} // namespace gmdr::vdf
