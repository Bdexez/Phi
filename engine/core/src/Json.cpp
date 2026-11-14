#include <phi/core/Json.hpp>

#include <charconv>
#include <cmath>
#include <format>

namespace phi::json {

namespace {

const Value kNull;
const Array kEmptyArray;
const Object kEmptyObject;

class Parser {
public:
    Parser(std::string_view text, u32 maxDepth) : text_(text), maxDepth_(maxDepth) {}

    Result<Value> document() {
        if (text_.starts_with("\xEF\xBB\xBF")) {
            at_ = 3;
        }
        auto value = parseValue(0);
        if (!value) {
            return value;
        }
        skipSpace();
        if (at_ != text_.size()) {
            return error("unexpected text after the document");
        }
        return value;
    }

private:
    std::unexpected<Error> error(std::string_view what) const {
        // Line and column of the current position, both from 1.
        usize line = 1;
        usize column = 1;
        for (usize i = 0; i < at_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        return std::unexpected(Error{std::format("json: {} at line {}, column {}", what, line, column)});
    }

    void skipSpace() {
        while (at_ < text_.size() &&
               (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
            ++at_;
        }
    }

    bool consume(std::string_view word) {
        if (text_.substr(at_).starts_with(word)) {
            at_ += word.size();
            return true;
        }
        return false;
    }

    Result<Value> parseValue(u32 depth) {
        skipSpace();
        if (at_ >= text_.size()) {
            return error("unexpected end of text");
        }
        switch (text_[at_]) {
            case '{': return parseObject(depth + 1);
            case '[': return parseArray(depth + 1);
            case '"': {
                auto text = parseString();
                if (!text) {
                    return std::unexpected(text.error());
                }
                return Value(std::move(*text));
            }
            case 't':
                if (consume("true")) return Value(true);
                break;
            case 'f':
                if (consume("false")) return Value(false);
                break;
            case 'n':
                if (consume("null")) return Value();
                break;
            default: return parseNumber();
        }
        return error("unknown literal");
    }

    Result<Value> parseNumber() {
        // The JSON grammar, checked by hand: from_chars alone would take
        // "+1", "01", ".5" or "1." too.
        const usize start = at_;
        if (at_ < text_.size() && text_[at_] == '-') ++at_;
        const auto digits = [&]() {
            const usize first = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            return at_ - first;
        };
        const usize integer = at_;
        if (digits() == 0) {
            return error("expected a value");
        }
        if (text_[integer] == '0' && at_ - integer > 1) {
            return error("leading zeros in a number");
        }
        if (at_ < text_.size() && text_[at_] == '.') {
            ++at_;
            if (digits() == 0) return error("expected digits after '.'");
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) ++at_;
            if (digits() == 0) return error("expected digits in the exponent");
        }
        f64 value = 0.0;
        const auto [end, failure] = std::from_chars(text_.data() + start, text_.data() + at_, value);
        if (failure == std::errc::result_out_of_range) {
            // Beyond a double, as JavaScript reads it: too small (a negative
            // exponent) is 0, too large is infinity.
            const std::string_view literal = text_.substr(start, at_ - start);
            const bool tiny = literal.find("e-") != std::string_view::npos || literal.find("E-") != std::string_view::npos;
            const bool negative = text_[start] == '-';
            value = tiny ? (negative ? -0.0 : 0.0) : (negative ? -HUGE_VAL : HUGE_VAL);
        } else if (failure != std::errc{} || end != text_.data() + at_) {
            return error("malformed number");
        }
        return Value(value);
    }

    static void appendUtf8(std::string& out, u32 code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    Result<u32> hex4() {
        if (at_ + 4 > text_.size()) {
            return std::unexpected(error("truncated \\u escape").error());
        }
        u32 code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[at_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<u32>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<u32>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<u32>(c - 'A' + 10);
            else return std::unexpected(error("bad \\u escape").error());
        }
        return code;
    }

    Result<std::string> parseString() {
        ++at_;  // opening quote
        std::string out;
        for (;;) {
            if (at_ >= text_.size()) {
                return std::unexpected(error("unterminated string").error());
            }
            const char c = text_[at_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return std::unexpected(error("control character in a string").error());
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) {
                return std::unexpected(error("unterminated escape").error());
            }
            switch (const char escape = text_[at_++]) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto code = hex4();
                    if (!code) return std::unexpected(code.error());
                    u32 point = *code;
                    if (point >= 0xD800 && point <= 0xDBFF) {
                        // A high surrogate pairs with the low one after it.
                        if (!consume("\\u")) return std::unexpected(error("lone surrogate").error());
                        auto low = hex4();
                        if (!low) return std::unexpected(low.error());
                        if (*low < 0xDC00 || *low > 0xDFFF) return std::unexpected(error("lone surrogate").error());
                        point = 0x10000 + ((point - 0xD800) << 10) + (*low - 0xDC00);
                    } else if (point >= 0xDC00 && point <= 0xDFFF) {
                        return std::unexpected(error("lone surrogate").error());
                    }
                    appendUtf8(out, point);
                    break;
                }
                default:
                    (void)escape;
                    return std::unexpected(error("unknown escape").error());
            }
        }
    }

    Result<Value> parseArray(u32 depth) {
        if (depth > maxDepth_) {
            return error("nested too deeply");
        }
        ++at_;  // [
        Array items;
        skipSpace();
        if (consume("]")) {
            return Value(std::move(items));
        }
        for (;;) {
            auto item = parseValue(depth);
            if (!item) return item;
            items.push_back(std::move(*item));
            skipSpace();
            if (consume(",")) continue;
            if (consume("]")) return Value(std::move(items));
            return error("expected ',' or ']'");
        }
    }

    Result<Value> parseObject(u32 depth) {
        if (depth > maxDepth_) {
            return error("nested too deeply");
        }
        ++at_;  // {
        Object members;
        skipSpace();
        if (consume("}")) {
            return Value(std::move(members));
        }
        for (;;) {
            skipSpace();
            if (at_ >= text_.size() || text_[at_] != '"') {
                return error("expected a member name");
            }
            auto key = parseString();
            if (!key) return std::unexpected(key.error());
            skipSpace();
            if (!consume(":")) return error("expected ':'");
            auto value = parseValue(depth);
            if (!value) return value;
            members.emplace_back(std::move(*key), std::move(*value));
            skipSpace();
            if (consume(",")) continue;
            if (consume("}")) return Value(std::move(members));
            return error("expected ',' or '}'");
        }
    }

    std::string_view text_;
    usize at_ = 0;
    u32 maxDepth_;
};

void writeString(std::string& out, std::string_view text) {
    out += '"';
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void writeValue(std::string& out, const Value& value) {
    if (value.isBool()) {
        out += value.asBool() ? "true" : "false";
    } else if (value.isNumber()) {
        const f64 number = value.asNumber();
        out += std::isfinite(number) ? std::format("{}", number) : "null";  // JSON has no infinity
    } else if (value.isString()) {
        writeString(out, value.asString());
    } else if (value.isArray()) {
        out += '[';
        bool first = true;
        for (const Value& item : value.asArray()) {
            out += first ? "" : ",";
            first = false;
            writeValue(out, item);
        }
        out += ']';
    } else if (value.isObject()) {
        out += '{';
        bool first = true;
        for (const auto& [key, member] : value.asObject()) {
            out += first ? "" : ",";
            first = false;
            writeString(out, key);
            out += ':';
            writeValue(out, member);
        }
        out += '}';
    } else {
        out += "null";
    }
}

}  // namespace

bool Value::asBool(bool fallback) const {
    const auto* value = std::get_if<bool>(&data_);
    return value != nullptr ? *value : fallback;
}

f64 Value::asNumber(f64 fallback) const {
    const auto* value = std::get_if<f64>(&data_);
    return value != nullptr ? *value : fallback;
}

std::string_view Value::asString(std::string_view fallback) const {
    const auto* value = std::get_if<std::string>(&data_);
    return value != nullptr ? std::string_view(*value) : fallback;
}

const Array& Value::asArray() const {
    const auto* value = std::get_if<std::shared_ptr<Array>>(&data_);
    return value != nullptr ? **value : kEmptyArray;
}

const Object& Value::asObject() const {
    const auto* value = std::get_if<std::shared_ptr<Object>>(&data_);
    return value != nullptr ? **value : kEmptyObject;
}

const Value* Value::find(std::string_view key) const {
    for (const auto& [name, member] : asObject()) {
        if (name == key) {
            return &member;
        }
    }
    return nullptr;
}

const Value& Value::operator[](std::string_view key) const {
    const Value* member = find(key);
    return member != nullptr ? *member : kNull;
}

const Value& Value::operator[](usize index) const {
    const Array& items = asArray();
    return index < items.size() ? items[index] : kNull;
}

usize Value::size() const {
    return isArray() ? asArray().size() : asObject().size();
}

Result<Value> parse(std::string_view text, u32 maxDepth) {
    return Parser(text, maxDepth).document();
}

std::string write(const Value& value) {
    std::string out;
    writeValue(out, value);
    return out;
}

}  // namespace phi::json
