#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace gb {

// NUMBERS IN FILES, WRITTEN AND READ THE SAME WAY EVERYWHERE, IN ANY LOCALE.
//
// printf's %g and strtod both follow the C locale, which is process-wide: one
// plugin in the same host calling setlocale is enough for Ghostband to write
// "120,5" (not JSON) and read "0.5" as 0. And %.4g could not hold a dial the
// dice set at full precision, so a saved song reloaded as a different
// performance. std::to_chars / from_chars ignore the locale entirely, and the
// shortest form to_chars chooses reads back as exactly the same double - 0.55
// is still written "0.55", so a hand-edited file stays readable.
std::string formatNumber (double v);

// Parses a JSON number at `text`; sets `used` to the characters consumed.
// Returns false for anything that is not a finite number.
bool parseNumber (const char* text, const char* end, double& v, size_t& used);

// EVERY PATH THE ENGINE OPENS IS UTF-8, and goes through this. A narrow
// std::string path handed to fstream, remove or rename is read by Windows in
// the ANSI code page, so a user called Zoe-with-a-diaeresis, or with a Cyrillic
// or Asian name, could not load or save a song under their own Documents.
inline std::filesystem::path utf8Path (const std::string& p)
{
    return std::filesystem::u8path (p);
}

// Minimal JSON reader. Deliberately hand-rolled so the engine keeps zero
// third-party dependencies. Extended with // and /* */ comments, because
// driver profiles are hand-edited config files and need to explain themselves.
class Json
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;

    static Json parse (const std::string& text, std::string& error);
    static bool parseFile (const std::string& path, Json& out, std::string& error);

    Type type() const noexcept       { return type_; }
    bool isNull()   const noexcept   { return type_ == Type::Null; }
    bool isBool()   const noexcept   { return type_ == Type::Bool; }
    bool isNumber() const noexcept   { return type_ == Type::Number; }
    bool isString() const noexcept   { return type_ == Type::String; }
    bool isArray()  const noexcept   { return type_ == Type::Array; }
    bool isObject() const noexcept   { return type_ == Type::Object; }

    // Accessors never throw; they fall back to the supplied default.
    bool        asBool   (bool def = false) const;
    double      asNumber (double def = 0.0) const;
    int         asInt    (int def = 0) const;
    std::string asString (const std::string& def = std::string()) const;

    bool        has  (const std::string& key) const;
    size_t      size () const;

    const Json& operator[] (const std::string& key) const;
    const Json& operator[] (size_t index) const;

    std::vector<std::string> keys() const;

    bool        boolOr   (const std::string& key, bool def) const;
    int         intOr    (const std::string& key, int def) const;
    double      numberOr (const std::string& key, double def) const;
    std::string stringOr (const std::string& key, const std::string& def) const;

    // Reads an array of strings; returns an empty vector if absent or wrong type.
    std::vector<std::string> stringArray (const std::string& key) const;

private:
    friend class JsonParser;

    Type        type_ = Type::Null;
    bool        bool_ = false;
    double      num_  = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;

    static const Json& nullValue();
};

} // namespace gb
