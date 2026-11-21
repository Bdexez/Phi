#pragma once

#include <phi/core/Result.hpp>
#include <phi/core/Types.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace phi::json {

class Value;
using Array = std::vector<Value>;
/// Members in file order (formats such as glTF never need a lookup table).
using Object = std::vector<std::pair<std::string, Value>>;

/// A JSON value: null, boolean, number (a double), string, array or object.
class Value {
public:
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : data_(value) {}
    Value(f64 value) : data_(value) {}
    Value(int value) : data_(static_cast<f64>(value)) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(const char* value) : data_(std::string(value)) {}
    Value(Array value) : data_(std::make_shared<Array>(std::move(value))) {}
    Value(Object value) : data_(std::make_shared<Object>(std::move(value))) {}

    [[nodiscard]] bool isNull() const { return std::holds_alternative<std::monostate>(data_); }
    [[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(data_); }
    [[nodiscard]] bool isNumber() const { return std::holds_alternative<f64>(data_); }
    [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(data_); }
    [[nodiscard]] bool isArray() const { return std::holds_alternative<std::shared_ptr<Array>>(data_); }
    [[nodiscard]] bool isObject() const { return std::holds_alternative<std::shared_ptr<Object>>(data_); }

    /// The value, or `fallback` if it is of another type.
    [[nodiscard]] bool asBool(bool fallback = false) const;
    [[nodiscard]] f64 asNumber(f64 fallback = 0.0) const;
    [[nodiscard]] std::string_view asString(std::string_view fallback = {}) const;
    /// Empty for other types.
    [[nodiscard]] const Array& asArray() const;
    [[nodiscard]] const Object& asObject() const;

    /// The member called `key` of an object, or null (also for non-objects).
    [[nodiscard]] const Value* find(std::string_view key) const;
    /// The member called `key`, or a null value: chains like
    /// `root["asset"]["version"].asString()` never fail.
    [[nodiscard]] const Value& operator[](std::string_view key) const;
    /// Element `index` of an array, or a null value.
    [[nodiscard]] const Value& operator[](usize index) const;
    [[nodiscard]] usize size() const;  ///< Elements or members; 0 otherwise.

private:
    std::variant<std::monostate, bool, f64, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> data_;
};

/// Parses JSON text (RFC 8259). Errors name the line and column. Nesting
/// deeper than `maxDepth` is refused, so hostile files cannot exhaust the
/// stack; a UTF-8 byte order mark is skipped.
[[nodiscard]] Result<Value> parse(std::string_view text, u32 maxDepth = 256);

/// Compact JSON text for `value`, which parse() reads back as the same.
[[nodiscard]] std::string write(const Value& value);

}  // namespace phi::json
