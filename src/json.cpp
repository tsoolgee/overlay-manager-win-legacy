#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace js {
namespace {

// Appends `cp` to `out` as UTF-8. Used when un-escaping \uXXXX.
void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

struct Parser {
    const char* p;
    const char* end;

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    bool lit(const char* s) {
        const size_t n = strlen(s);
        if ((size_t)(end - p) < n || memcmp(p, s, n) != 0) return false;
        p += n;
        return true;
    }

    unsigned hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4 && p < end; ++i, ++p) {
            const char c = *p;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        }
        return v;
    }

    std::string str() {
        std::string out;
        if (p >= end || *p != '"') return out;
        ++p;
        while (p < end && *p != '"') {
            if (*p != '\\') {
                out += *p++;
                continue;
            }
            if (++p >= end) break;
            const char c = *p++;
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = hex4();
                    // Re-join a UTF-16 surrogate pair into a single code point.
                    if (cp >= 0xD800 && cp <= 0xDBFF && p + 1 < end &&
                        p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        const unsigned lo = hex4();
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            // Unpaired high surrogate: emit both as-is rather
                            // than dropping characters.
                            appendUtf8(out, cp);
                            cp = lo;
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: out += c; break;  // covers \" \\ and \/
            }
        }
        if (p < end) ++p;  // closing quote
        return out;
    }

    Value value() {
        ws();
        if (p >= end) return Value();
        switch (*p) {
            case '"':
                return Value(str());

            case '{': {
                ++p;
                Object o;
                ws();
                if (p < end && *p == '}') {
                    ++p;
                    return Value(std::move(o));
                }
                while (p < end) {
                    ws();
                    std::string k = str();
                    ws();
                    if (p < end && *p == ':') ++p;
                    o[k] = value();
                    ws();
                    if (p < end && *p == ',') {
                        ++p;
                        continue;
                    }
                    break;
                }
                if (p < end && *p == '}') ++p;
                return Value(std::move(o));
            }

            case '[': {
                ++p;
                Array a;
                ws();
                if (p < end && *p == ']') {
                    ++p;
                    return Value(std::move(a));
                }
                while (p < end) {
                    a.push_back(value());
                    ws();
                    if (p < end && *p == ',') {
                        ++p;
                        continue;
                    }
                    break;
                }
                if (p < end && *p == ']') ++p;
                return Value(std::move(a));
            }

            case 't': return lit("true") ? Value(true) : Value();
            case 'f': return lit("false") ? Value(false) : Value();
            case 'n': lit("null"); return Value();

            default: {
                char* stop = nullptr;
                const double d = strtod(p, &stop);
                if (stop == p) {
                    ++p;  // not a number at all; skip it so parsing terminates
                    return Value();
                }
                p = stop;
                return Value(d);
            }
        }
    }
};

}  // namespace

void Value::dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    // UTF-8 bytes pass through untouched: the WebView reads UTF-8.
                    out += (char)c;
                }
        }
    }
    out += '"';
}

std::string Value::dump() const {
    std::string out;
    switch (type_) {
        case Type::Null:
            out = "null";
            break;

        case Type::Bool:
            out = num_ != 0 ? "true" : "false";
            break;

        case Type::Number: {
            char buf[40];
            // Print whole numbers without a decimal point, so ids and pixel
            // counts read as integers on the JavaScript side.
            if (num_ == (double)(long long)num_ && std::fabs(num_) < 1e15)
                snprintf(buf, sizeof buf, "%lld", (long long)num_);
            else
                snprintf(buf, sizeof buf, "%.6g", num_);
            out = buf;
            break;
        }

        case Type::String:
            dumpString(str_, out);
            break;

        case Type::Array: {
            out = "[";
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out += ',';
                out += arr_[i].dump();
            }
            out += ']';
            break;
        }

        case Type::Object: {
            out = "{";
            bool first = true;
            for (const auto& kv : obj_) {
                if (!first) out += ',';
                first = false;
                dumpString(kv.first, out);
                out += ':';
                out += kv.second.dump();
            }
            out += '}';
            break;
        }
    }
    return out;
}

Value Value::parse(const std::string& text) {
    Parser ps{text.data(), text.data() + text.size()};
    return ps.value();
}

}  // namespace js
