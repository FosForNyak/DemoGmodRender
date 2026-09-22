#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace gmdr::json {

namespace {
const Value& null_value() {
    static const Value v;
    return v;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {}

    std::optional<Value> run(std::string* error) {
        skip_ws();
        auto v = parse_value();
        skip_ws();
        if (v && pos_ != t_.size()) {
            fail("зайві символи після JSON");
            v.reset();
        }
        if (!v && error) *error = std::format("JSON: {} (позиція {})", err_, pos_);
        return v;
    }

private:
    std::optional<Value> parse_value() {
        if (++depth_ > 64) { fail("надто глибока вкладеність"); return std::nullopt; }
        struct DepthGuard { int& d; ~DepthGuard() { --d; } } guard{depth_};
        if (pos_ >= t_.size()) { fail("неочікуваний кінець"); return std::nullopt; }
        const char c = t_[pos_];
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') {
            auto s = parse_string();
            if (!s) return std::nullopt;
            return Value::string(std::move(*s));
        }
        if (c == 't' && t_.substr(pos_, 4) == "true")  { pos_ += 4; return Value::boolean(true); }
        if (c == 'f' && t_.substr(pos_, 5) == "false") { pos_ += 5; return Value::boolean(false); }
        if (c == 'n' && t_.substr(pos_, 4) == "null")  { pos_ += 4; return Value(); }
        return parse_number();
    }

    std::optional<Value> parse_object() {
        Value obj = Value::object();
        ++pos_;   // {
        skip_ws();
        if (peek() == '}') { ++pos_; return obj; }
        for (;;) {
            skip_ws();
            if (peek() != '"') { fail("очікувався ключ"); return std::nullopt; }
            auto key = parse_string();
            if (!key) return std::nullopt;
            skip_ws();
            if (peek() != ':') { fail("очікувалась ':'"); return std::nullopt; }
            ++pos_;
            skip_ws();
            auto v = parse_value();
            if (!v) return std::nullopt;
            obj.set(*key, std::move(*v));
            skip_ws();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; return obj; }
            fail("очікувалась ',' або '}'");
            return std::nullopt;
        }
    }

    std::optional<Value> parse_array() {
        Value arr = Value::array();
        ++pos_;   // [
        skip_ws();
        if (peek() == ']') { ++pos_; return arr; }
        for (;;) {
            skip_ws();
            auto v = parse_value();
            if (!v) return std::nullopt;
            arr.push(std::move(*v));
            skip_ws();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; return arr; }
            fail("очікувалась ',' або ']'");
            return std::nullopt;
        }
    }

    std::optional<std::string> parse_string() {
        ++pos_;   // "
        std::string out;
        while (pos_ < t_.size()) {
            const char c = t_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= t_.size()) break;
            const char e = t_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!read_hex4(cp)) { fail("погана \\u послідовність"); return std::nullopt; }
                if (cp >= 0xD800 && cp <= 0xDBFF && t_.substr(pos_, 2) == "\\u") {
                    pos_ += 2;
                    uint32_t lo = 0;
                    if (!read_hex4(lo)) { fail("погана сурогатна пара"); return std::nullopt; }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                append_utf8(out, cp);
                break;
            }
            default: out += e; break;
            }
        }
        fail("незакритий рядок");
        return std::nullopt;
    }

    bool read_hex4(uint32_t& v) {
        if (pos_ + 4 > t_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    std::optional<Value> parse_number() {
        const size_t start = pos_;
        while (pos_ < t_.size()) {
            const char c = t_[pos_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') ++pos_;
            else break;
        }
        if (start == pos_) { fail("неочікуваний символ"); return std::nullopt; }
        const std::string num(t_.substr(start, pos_ - start));
        char* end = nullptr;
        const double d = std::strtod(num.c_str(), &end);
        if (end != num.c_str() + num.size()) { fail("погане число"); return std::nullopt; }
        return Value::number(d);
    }

    char peek() const { return pos_ < t_.size() ? t_[pos_] : '\0'; }
    void skip_ws() {
        while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r'))
            ++pos_;
    }
    void fail(const char* msg) { if (err_.empty()) err_ = msg; }

    std::string_view t_;
    size_t           pos_ = 0;
    int              depth_ = 0;
    std::string      err_;
};
} // namespace

bool Value::as_bool(bool def) const {
    if (type_ == Type::Bool) return b_;
    if (type_ == Type::Number) return n_ != 0.0;
    return def;
}

double Value::as_number(double def) const {
    if (type_ == Type::Number) return n_;
    if (type_ == Type::Bool) return b_ ? 1.0 : 0.0;
    if (type_ == Type::String) {
        char* end = nullptr;
        const double d = std::strtod(s_.c_str(), &end);
        if (end && end != s_.c_str()) return d;
    }
    return def;
}

std::string Value::as_string(const std::string& def) const {
    if (type_ == Type::String) return s_;
    if (type_ == Type::Number) {
        if (std::abs(n_ - std::round(n_)) < 1e-9 && std::abs(n_) < 9e15) return std::to_string(static_cast<int64_t>(n_));
        return std::format("{}", n_);
    }
    if (type_ == Type::Bool) return b_ ? "true" : "false";
    return def;
}

const Value& Value::operator[](const std::string& key) const {
    if (type_ != Type::Object) return null_value();
    auto it = obj_.find(key);
    return it == obj_.end() ? null_value() : it->second;
}

const Value& Value::operator[](size_t index) const {
    if (type_ != Type::Array || index >= arr_.size()) return null_value();
    return arr_[index];
}

bool Value::has(const std::string& key) const { return type_ == Type::Object && obj_.count(key) > 0; }

Value& Value::set(const std::string& key, Value v) {
    if (type_ != Type::Object) { *this = object(); }
    return obj_[key] = std::move(v);
}

Value& Value::push(Value v) {
    if (type_ != Type::Array) { *this = array(); }
    arr_.push_back(std::move(v));
    return arr_.back();
}

std::string escape_string(std::string_view s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) out += std::format("\\u{:04x}", c);
            else out += static_cast<char>(c);
        }
    }
    out += '"';
    return out;
}

std::string Value::dump() const {
    switch (type_) {
    case Type::Null: return "null";
    case Type::Bool: return b_ ? "true" : "false";
    case Type::Number: {
        if (std::abs(n_ - std::round(n_)) < 1e-9 && std::abs(n_) < 9e15)
            return std::to_string(static_cast<int64_t>(std::llround(n_)));
        return std::format("{}", n_);
    }
    case Type::String: return escape_string(s_);
    case Type::Array: {
        std::string out = "[";
        for (size_t i = 0; i < arr_.size(); ++i) {
            if (i) out += ',';
            out += arr_[i].dump();
        }
        return out + "]";
    }
    case Type::Object: {
        std::string out = "{";
        bool first = true;
        for (const auto& [k, v] : obj_) {
            if (!first) out += ',';
            first = false;
            out += escape_string(k) + ":" + v.dump();
        }
        return out + "}";
    }
    }
    return "null";
}

std::optional<Value> parse(std::string_view text, std::string* error) {
    return Parser(text).run(error);
}

} // namespace gmdr::json
