// =============================================================================
//  json.hpp — мінімальний JSON (читання та запис) без зовнішніх бібліотек.
//  Потрібен для обміну завданнями/статусом з Lua-драйвером у GMod
//  (у Lua є util.TableToJSON / util.JSONToTable).
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gmdr::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value boolean(bool b)            { Value v; v.type_ = Type::Bool; v.b_ = b; return v; }
    static Value number(double d)           { Value v; v.type_ = Type::Number; v.n_ = d; return v; }
    static Value string(std::string s)      { Value v; v.type_ = Type::String; v.s_ = std::move(s); return v; }
    static Value array()                    { Value v; v.type_ = Type::Array; return v; }
    static Value object()                   { Value v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool is_null() const   { return type_ == Type::Null; }
    bool is_object() const { return type_ == Type::Object; }
    bool is_array() const  { return type_ == Type::Array; }

    // Безпечні геттери з типовими значеннями.
    bool        as_bool(bool def = false) const;
    double      as_number(double def = 0.0) const;
    int64_t     as_int(int64_t def = 0) const { return static_cast<int64_t>(as_number(static_cast<double>(def))); }
    std::string as_string(const std::string& def = {}) const;

    const Value& operator[](const std::string& key) const;   // для об'єктів (Null якщо нема)
    const Value& operator[](size_t index) const;             // для масивів
    bool has(const std::string& key) const;

    Value& set(const std::string& key, Value v);   // для об'єктів
    Value& push(Value v);                          // для масивів

    const std::vector<Value>&                  items() const { return arr_; }
    const std::map<std::string, Value>&        members() const { return obj_; }

    std::string dump() const;   // компактний JSON

private:
    Type                          type_ = Type::Null;
    bool                          b_ = false;
    double                        n_ = 0.0;
    std::string                   s_;
    std::vector<Value>            arr_;
    std::map<std::string, Value>  obj_;
};

// Повертає nullopt при синтаксичній помилці (текст помилки — у error).
std::optional<Value> parse(std::string_view text, std::string* error = nullptr);

std::string escape_string(std::string_view s);   // з лапками

} // namespace gmdr::json
