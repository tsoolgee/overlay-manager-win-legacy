// json.h — minimal UTF-8 JSON reader/writer.
//
// Only what the WebView bridge protocol needs: objects, arrays, strings,
// doubles, bools and null. Deliberately tiny so the repo stays dependency-free.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace js {

class Value;
using Object = std::map<std::string, Value>;
using Array  = std::vector<Value>;

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() : type_(Type::Null) {}
    Value(bool b) : type_(Type::Bool), num_(b ? 1 : 0) {}
    Value(int n) : type_(Type::Number), num_(n) {}
    Value(double n) : type_(Type::Number), num_(n) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), arr_(std::move(a)) {}
    Value(Object o) : type_(Type::Object), obj_(std::move(o)) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }

    // Lenient accessors: a missing or wrongly-typed value yields the fallback,
    // so a malformed message from the UI degrades instead of crashing.
    bool asBool(bool d = false) const {
        if (type_ == Type::Bool || type_ == Type::Number) return num_ != 0;
        return d;
    }
    double asNumber(double d = 0) const { return type_ == Type::Number ? num_ : d; }
    int asInt(int d = 0) const { return type_ == Type::Number ? (int)(num_ < 0 ? num_ - 0.5 : num_ + 0.5) : d; }
    const std::string& asString() const {
        static const std::string kEmpty;
        return type_ == Type::String ? str_ : kEmpty;
    }

    const Array& asArray() const {
        static const Array kEmpty;
        return type_ == Type::Array ? arr_ : kEmpty;
    }

    // obj["key"] on a non-object (or absent key) returns a shared null Value.
    const Value& operator[](const std::string& key) const {
        static const Value kNull;
        if (type_ != Type::Object) return kNull;
        auto it = obj_.find(key);
        return it == obj_.end() ? kNull : it->second;
    }
    const Value& operator[](size_t i) const {
        static const Value kNull;
        if (type_ != Type::Array || i >= arr_.size()) return kNull;
        return arr_[i];
    }
    bool has(const std::string& key) const {
        return type_ == Type::Object && obj_.count(key) != 0;
    }
    size_t size() const {
        return type_ == Type::Array ? arr_.size() : (type_ == Type::Object ? obj_.size() : 0);
    }

    std::string dump() const;
    static Value parse(const std::string& text);

private:
    static void dumpString(const std::string& s, std::string& out);

    Type type_;
    double num_ = 0;
    std::string str_;
    Array arr_;
    Object obj_;
};

}  // namespace js
