#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace sta {

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    using Value = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Json() : value_(nullptr) {}
    explicit Json(Value value) : value_(std::move(value)) {}
    [[nodiscard]] const Json& at(const std::string& key) const;
    [[nodiscard]] const Json* find(const std::string& key) const;
    [[nodiscard]] const Array& array() const;
    [[nodiscard]] const Object& object() const;
    [[nodiscard]] std::string string() const;
    [[nodiscard]] double number() const;
    [[nodiscard]] bool is_null() const;
    static Json parse_file(const std::string& path);

private:
    Value value_;
};

} // namespace sta
