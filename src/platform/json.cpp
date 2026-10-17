#include "platform/json.h"

#include <charconv>
#include <stdexcept>

namespace beachlife::platform {
namespace {
constexpr std::size_t max_document = 32u << 20;
constexpr int max_depth = 64;

const JsonValue& null_value() {
    static const JsonValue null;
    return null;
}

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) {
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
} // namespace

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    JsonValue document() {
        if (text_.size() > max_document) fail("document too large");
        // A UTF-8 byte order mark is tolerated before the document.
        if (text_.substr(0, 3) == "\xEF\xBB\xBF") position_ = 3;
        JsonValue result = value(0);
        skip_space();
        if (position_ != text_.size()) fail("unexpected data after the document");
        return result;
    }

private:
    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error("Invalid JSON at byte " + std::to_string(position_) + ": " + what);
    }

    void skip_space() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++position_;
        }
    }

    bool consume(std::string_view literal) {
        if (text_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    JsonValue value(int depth) {
        if (depth > max_depth) fail("nesting too deep");
        skip_space();
        if (position_ >= text_.size()) fail("unexpected end");
        JsonValue result;
        const char c = text_[position_];
        if (c == '{') {
            result.type_ = JsonValue::Type::Object;
            ++position_;
            skip_space();
            if (consume("}")) return result;
            for (;;) {
                skip_space();
                if (position_ >= text_.size() || text_[position_] != '"') fail("expected a member name");
                result.names_.push_back(string());
                skip_space();
                if (!consume(":")) fail("expected ':'");
                result.items_.push_back(value(depth+1));
                skip_space();
                if (consume("}")) return result;
                if (!consume(",")) fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            result.type_ = JsonValue::Type::Array;
            ++position_;
            skip_space();
            if (consume("]")) return result;
            for (;;) {
                result.items_.push_back(value(depth+1));
                skip_space();
                if (consume("]")) return result;
                if (!consume(",")) fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            result.type_ = JsonValue::Type::String;
            result.text_ = string();
            return result;
        }
        if (consume("true")) {
            result.type_ = JsonValue::Type::Boolean;
            result.boolean_ = true;
            return result;
        }
        if (consume("false")) {
            result.type_ = JsonValue::Type::Boolean;
            return result;
        }
        if (consume("null")) return result;
        if (c == '-' || (c >= '0' && c <= '9')) {
            result.type_ = JsonValue::Type::Number;
            result.text_ = number();
            return result;
        }
        fail("unexpected character");
    }

    std::string number() {
        const std::size_t start = position_;
        auto digits = [&] {
            const std::size_t first = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            return position_-first;
        };
        if (text_[position_] == '-') ++position_;
        if (position_ < text_.size() && text_[position_] == '0') ++position_;
        else if (!digits()) fail("invalid number");
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            if (!digits()) fail("invalid number");
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            if (!digits()) fail("invalid number");
        }
        return std::string(text_.substr(start, position_-start));
    }

    std::uint32_t hex4() {
        if (position_+4 > text_.size()) fail("truncated escape");
        std::uint32_t code = 0;
        for (int n = 0; n < 4; ++n) {
            const char c = text_[position_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<std::uint32_t>(c-'0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<std::uint32_t>(c-'a'+10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<std::uint32_t>(c-'A'+10);
            else fail("invalid escape");
        }
        return code;
    }

    std::string string() {
        ++position_; // the opening quote
        std::string out;
        for (;;) {
            if (position_ >= text_.size()) fail("unterminated string");
            const char c = text_[position_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (position_ >= text_.size()) fail("unterminated string");
            switch (text_[position_++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t code = hex4();
                if (code >= 0xD800 && code <= 0xDBFF && consume("\\u")) {
                    const std::uint32_t low = hex4();
                    if (low >= 0xDC00 && low <= 0xDFFF) code = 0x10000+((code-0xD800) << 10)+(low-0xDC00);
                    else {
                        append_utf8(out, 0xFFFD); // an unpaired high surrogate
                        code = low >= 0xD800 && low <= 0xDFFF ? 0xFFFD : low;
                    }
                } else if (code >= 0xD800 && code <= 0xDFFF) code = 0xFFFD;
                append_utf8(out, code);
                break;
            }
            default: fail("invalid escape");
            }
        }
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

std::optional<std::int64_t> JsonValue::integer() const {
    if (type_ != Type::Number) return std::nullopt;
    std::int64_t value = 0;
    const auto result = std::from_chars(text_.data(), text_.data()+text_.size(), value);
    if (result.ec != std::errc{} || result.ptr != text_.data()+text_.size()) return std::nullopt;
    return value;
}

const JsonValue& JsonValue::operator[](std::string_view name) const {
    if (type_ == Type::Object)
        for (std::size_t n = 0; n < names_.size(); ++n)
            if (names_[n] == name) return items_[n];
    return null_value();
}

JsonValue parse_json(std::string_view text) { return JsonParser(text).document(); }

} // namespace beachlife::platform
