#include "ghostband/Json.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace gb {

const Json& Json::nullValue()
{
    static const Json n;
    return n;
}

bool Json::asBool (bool def) const
{
    if (type_ == Type::Bool)   return bool_;
    if (type_ == Type::Number) return num_ != 0.0;
    return def;
}

double Json::asNumber (double def) const
{
    if (type_ == Type::Number) return num_;
    if (type_ == Type::Bool)   return bool_ ? 1.0 : 0.0;
    return def;
}

int Json::asInt (int def) const
{
    if (type_ == Type::Number) return static_cast<int> (num_ < 0 ? num_ - 0.5 : num_ + 0.5);
    if (type_ == Type::Bool)   return bool_ ? 1 : 0;
    return def;
}

std::string Json::asString (const std::string& def) const
{
    return type_ == Type::String ? str_ : def;
}

bool Json::has (const std::string& key) const
{
    if (type_ != Type::Object) return false;
    for (const auto& kv : obj_)
        if (kv.first == key) return true;
    return false;
}

size_t Json::size() const
{
    if (type_ == Type::Array)  return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

const Json& Json::operator[] (const std::string& key) const
{
    if (type_ == Type::Object)
        for (const auto& kv : obj_)
            if (kv.first == key) return kv.second;
    return nullValue();
}

const Json& Json::operator[] (size_t index) const
{
    if (type_ == Type::Array && index < arr_.size()) return arr_[index];
    return nullValue();
}

std::vector<std::string> Json::keys() const
{
    std::vector<std::string> k;
    if (type_ == Type::Object)
    {
        k.reserve (obj_.size());
        for (const auto& kv : obj_) k.push_back (kv.first);
    }
    return k;
}

bool        Json::boolOr   (const std::string& key, bool def) const               { return has (key) ? (*this)[key].asBool (def)   : def; }
int         Json::intOr    (const std::string& key, int def) const                { return has (key) ? (*this)[key].asInt (def)    : def; }
double      Json::numberOr (const std::string& key, double def) const             { return has (key) ? (*this)[key].asNumber (def) : def; }
std::string Json::stringOr (const std::string& key, const std::string& def) const { return has (key) ? (*this)[key].asString (def) : def; }

std::vector<std::string> Json::stringArray (const std::string& key) const
{
    std::vector<std::string> out;
    const Json& a = (*this)[key];
    if (a.isArray())
    {
        out.reserve (a.size());
        for (size_t i = 0; i < a.size(); ++i)
            out.push_back (a[i].asString());
    }
    return out;
}

//==============================================================================

class JsonParser
{
public:
    JsonParser (const std::string& text) : s (text) {}

    bool run (Json& out, std::string& error)
    {
        skipTrivia();
        if (! parseValue (out)) { error = err; return false; }
        skipTrivia();
        if (pos != s.size()) { error = message ("trailing characters after top-level value"); return false; }
        return true;
    }

private:
    const std::string& s;
    size_t pos = 0;
    std::string err;

    std::string message (const std::string& what) const
    {
        size_t line = 1, col = 1;
        for (size_t i = 0; i < pos && i < s.size(); ++i)
        {
            if (s[i] == '\n') { ++line; col = 1; }
            else ++col;
        }
        std::ostringstream o;
        o << "line " << line << ", column " << col << ": " << what;
        return o.str();
    }

    bool fail (const std::string& what) { if (err.empty()) err = message (what); return false; }

    bool atEnd() const { return pos >= s.size(); }
    char peek() const  { return pos < s.size() ? s[pos] : '\0'; }

    void skipTrivia()
    {
        for (;;)
        {
            while (! atEnd() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n'))
                ++pos;

            if (pos + 1 < s.size() && s[pos] == '/' && s[pos + 1] == '/')
            {
                pos += 2;
                while (! atEnd() && s[pos] != '\n') ++pos;
                continue;
            }

            if (pos + 1 < s.size() && s[pos] == '/' && s[pos + 1] == '*')
            {
                pos += 2;
                while (pos + 1 < s.size() && ! (s[pos] == '*' && s[pos + 1] == '/')) ++pos;
                pos = (pos + 1 < s.size()) ? pos + 2 : s.size();
                continue;
            }

            return;
        }
    }

    bool literal (const char* word)
    {
        size_t n = 0;
        while (word[n] != '\0') ++n;
        if (s.compare (pos, n, word) != 0) return false;
        pos += n;
        return true;
    }

    static void appendUtf8 (std::string& out, unsigned cp)
    {
        if (cp < 0x80) out += static_cast<char> (cp);
        else if (cp < 0x800)
        {
            out += static_cast<char> (0xC0 | (cp >> 6));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char> (0xE0 | (cp >> 12));
            out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (cp & 0x3F));
        }
    }

    bool parseString (std::string& out)
    {
        if (peek() != '"') return fail ("expected a string");
        ++pos;
        out.clear();

        while (! atEnd())
        {
            char c = s[pos++];

            if (c == '"') return true;

            if (c != '\\') { out += c; continue; }

            if (atEnd()) return fail ("unterminated escape sequence");

            char e = s[pos++];
            switch (e)
            {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                {
                    if (pos + 4 > s.size()) return fail ("truncated \\u escape");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        char h = s[pos + static_cast<size_t> (i)];
                        cp <<= 4;
                        if      (h >= '0' && h <= '9') cp |= static_cast<unsigned> (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned> (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned> (h - 'A' + 10);
                        else return fail ("bad hex digit in \\u escape");
                    }
                    pos += 4;
                    appendUtf8 (out, cp);
                    break;
                }
                default: return fail ("unrecognised escape sequence");
            }
        }

        return fail ("unterminated string");
    }

    bool parseValue (Json& out)
    {
        skipTrivia();

        if (atEnd()) return fail ("unexpected end of input");

        char c = peek();

        if (c == '{')
        {
            ++pos;
            out.type_ = Json::Type::Object;
            skipTrivia();
            if (peek() == '}') { ++pos; return true; }

            for (;;)
            {
                skipTrivia();
                std::string key;
                if (! parseString (key)) return false;
                skipTrivia();
                if (peek() != ':') return fail ("expected ':' after object key");
                ++pos;

                Json value;
                if (! parseValue (value)) return false;
                out.obj_.emplace_back (key, value);

                skipTrivia();
                if (peek() == ',') { ++pos; continue; }
                if (peek() == '}') { ++pos; return true; }
                return fail ("expected ',' or '}' in object");
            }
        }

        if (c == '[')
        {
            ++pos;
            out.type_ = Json::Type::Array;
            skipTrivia();
            if (peek() == ']') { ++pos; return true; }

            for (;;)
            {
                Json value;
                if (! parseValue (value)) return false;
                out.arr_.push_back (value);

                skipTrivia();
                if (peek() == ',') { ++pos; continue; }
                if (peek() == ']') { ++pos; return true; }
                return fail ("expected ',' or ']' in array");
            }
        }

        if (c == '"')
        {
            out.type_ = Json::Type::String;
            return parseString (out.str_);
        }

        if (literal ("true"))  { out.type_ = Json::Type::Bool; out.bool_ = true;  return true; }
        if (literal ("false")) { out.type_ = Json::Type::Bool; out.bool_ = false; return true; }
        if (literal ("null"))  { out.type_ = Json::Type::Null; return true; }

        if (c == '-' || (c >= '0' && c <= '9'))
        {
            const char* begin = s.c_str() + pos;
            char* end = nullptr;
            double v = std::strtod (begin, &end);
            if (end == begin) return fail ("malformed number");
            pos += static_cast<size_t> (end - begin);
            out.type_ = Json::Type::Number;
            out.num_ = v;
            return true;
        }

        return fail ("unexpected character");
    }
};

Json Json::parse (const std::string& text, std::string& error)
{
    Json root;
    JsonParser p (text);
    if (! p.run (root, error))
        return Json();
    error.clear();
    return root;
}

bool Json::parseFile (const std::string& path, Json& out, std::string& error)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        error = "could not open " + path;
        return false;
    }

    std::ostringstream buf;
    buf << in.rdbuf();

    std::string text = buf.str();

    // Tolerate a UTF-8 BOM; text editors on Windows add them freely.
    if (text.size() >= 3 && static_cast<unsigned char> (text[0]) == 0xEF
                         && static_cast<unsigned char> (text[1]) == 0xBB
                         && static_cast<unsigned char> (text[2]) == 0xBF)
        text.erase (0, 3);

    out = parse (text, error);
    if (! error.empty())
    {
        error = path + ": " + error;
        return false;
    }
    return true;
}

} // namespace gb
