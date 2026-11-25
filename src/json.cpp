#include "sta/json.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sta {
namespace {

class Parser {
public:
    explicit Parser(std::string text) : text_(std::move(text)) {}

    Json parse() {
        skip_space();
        Json result = value();
        skip_space();
        if (pos_ != text_.size()) fail("unexpected trailing data");
        return result;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("Invalid Yosys JSON at byte " + std::to_string(pos_) + ": " + message);
    }

    void skip_space() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' ||
               text_[pos_] == '\r' || text_[pos_] == '\t')) ++pos_;
    }

    bool take(char ch) {
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char ch) {
        if (!take(ch)) fail(std::string("expected '") + ch + "'");
    }

    std::string string_value() {
        expect('"');
        std::string result;
        while (pos_ < text_.size()) {
            char ch = text_[pos_++];
            if (ch == '"') return result;
            if (ch == '\\') {
                if (pos_ == text_.size()) fail("unterminated escape");
                ch = text_[pos_++];
                switch (ch) {
                case '"': case '\\': case '/': result.push_back(ch); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > text_.size()) fail("short unicode escape");
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char hex = text_[pos_++];
                        code <<= 4;
                        if (hex >= '0' && hex <= '9') code |= static_cast<unsigned>(hex - '0');
                        else if (hex >= 'a' && hex <= 'f') code |= static_cast<unsigned>(hex - 'a' + 10);
                        else if (hex >= 'A' && hex <= 'F') code |= static_cast<unsigned>(hex - 'A' + 10);
                        else fail("invalid unicode escape");
                    }
                    if (code <= 0x7f) result.push_back(static_cast<char>(code));
                    else if (code <= 0x7ff) {
                        result.push_back(static_cast<char>(0xc0 | (code >> 6)));
                        result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
                    } else {
                        result.push_back(static_cast<char>(0xe0 | (code >> 12)));
                        result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                        result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
                    }
                    break;
                }
                default: fail("invalid escape sequence");
                }
            } else {
                result.push_back(ch);
            }
        }
        fail("unterminated string");
    }

    Json value() {
        skip_space();
        if (pos_ == text_.size()) fail("expected value");
        if (text_[pos_] == '"') return Json(Json::Value(string_value()));
        if (take('{')) {
            Json::Object object;
            if (take('}')) return Json(Json::Value(std::move(object)));
            do {
                skip_space();
                if (pos_ == text_.size() || text_[pos_] != '"') fail("expected object key");
                const std::string key = string_value();
                expect(':');
                object.insert_or_assign(key, value());
            } while (take(','));
            expect('}');
            return Json(Json::Value(std::move(object)));
        }
        if (take('[')) {
            Json::Array array;
            if (take(']')) return Json(Json::Value(std::move(array)));
            do array.push_back(value()); while (take(','));
            expect(']');
            return Json(Json::Value(std::move(array)));
        }
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return Json(Json::Value(true));
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return Json(Json::Value(false));
        }
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return Json();
        }
        const std::size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        while (pos_ < text_.size() && ((text_[pos_] >= '0' && text_[pos_] <= '9') ||
               text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E' ||
               text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
        if (start == pos_) fail("unexpected token");
        try {
            return Json(Json::Value(std::stod(text_.substr(start, pos_ - start))));
        } catch (const std::exception&) {
            fail("invalid number");
        }
    }

    std::string text_;
    std::size_t pos_ = 0;
};

} // namespace

const Json& Json::at(const std::string& key) const {
    const auto* value = find(key);
    if (!value) throw std::runtime_error("Missing JSON key: " + key);
    return *value;
}

const Json* Json::find(const std::string& key) const {
    const auto* object = std::get_if<Object>(&value_);
    if (!object) throw std::runtime_error("JSON value is not an object");
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

const Json::Array& Json::array() const {
    const auto* result = std::get_if<Array>(&value_);
    if (!result) throw std::runtime_error("JSON value is not an array");
    return *result;
}

const Json::Object& Json::object() const {
    const auto* result = std::get_if<Object>(&value_);
    if (!result) throw std::runtime_error("JSON value is not an object");
    return *result;
}

std::string Json::string() const {
    if (const auto* result = std::get_if<std::string>(&value_)) return *result;
    if (const auto* number = std::get_if<double>(&value_)) {
        std::ostringstream stream;
        stream << std::setprecision(16) << *number;
        return stream.str();
    }
    if (const auto* boolean = std::get_if<bool>(&value_)) return *boolean ? "true" : "false";
    throw std::runtime_error("JSON value is not a scalar");
}

double Json::number() const {
    const auto* result = std::get_if<double>(&value_);
    if (!result) throw std::runtime_error("JSON value is not a number");
    return *result;
}

bool Json::is_null() const {
    return std::holds_alternative<std::nullptr_t>(value_);
}

Json Json::parse_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open JSON file: " + path);
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return Parser(std::move(contents)).parse();
}

} // namespace sta
