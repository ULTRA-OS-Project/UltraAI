// MiniJson.h — tiny JSON value, parser, and serializer for UltraAI adapters.
// Internal to adapters; not installed. Covers the subset of JSON the local
// and (future) cloud provider protocols need — no comments, no NaN/Inf.
// Part of ULTRA OS · MIT license · Cloverleaf UG
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraAI {
namespace minijson {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    using Array  = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() = default;
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int n) : type_(Type::Number), number_(n) {}
    Value(double n) : type_(Type::Number), number_(n) {}
    Value(std::uint64_t n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    Value(const char* s) : type_(Type::String), string_(s) {}
    Value(std::string s) : type_(Type::String), string_(std::move(s)) {}

    static Value MakeArray()  { Value v; v.type_ = Type::Array;  return v; }
    static Value MakeObject() { Value v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool IsNull()   const { return type_ == Type::Null; }
    bool IsBool()   const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray()  const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool          AsBool(bool fallback = false) const { return IsBool() ? bool_ : fallback; }
    double        AsDouble(double fallback = 0.0) const { return IsNumber() ? number_ : fallback; }
    int           AsInt(int fallback = 0) const { return IsNumber() ? static_cast<int>(number_) : fallback; }
    std::uint64_t AsUInt64(std::uint64_t fallback = 0) const {
        return IsNumber() && number_ >= 0 ? static_cast<std::uint64_t>(number_) : fallback;
    }
    const std::string& AsString() const { return string_; } // "" unless Type::String
    const Array&  AsArray()  const { return array_; }
    const Object& AsObject() const { return object_; }

    // Object lookup; returns a Null sentinel when absent or not an object.
    const Value& Get(const std::string& key) const;
    bool Has(const std::string& key) const { return IsObject() && object_.count(key) != 0; }

    // Builders — coerce this value into an object / array as needed.
    Value& Set(const std::string& key, Value v);
    Value& Push(Value v);

    std::string Dump() const;

    // Returns false and fills *error on malformed input.
    static bool Parse(const std::string& text, Value* out, std::string* error = nullptr);

private:
    Type        type_   = Type::Null;
    bool        bool_   = false;
    double      number_ = 0.0;
    std::string string_;
    Array       array_;
    Object      object_;
};

} // namespace minijson
} // namespace UltraAI
