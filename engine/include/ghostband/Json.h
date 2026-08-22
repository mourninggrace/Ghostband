#pragma once

#include <string>
#include <utility>
#include <vector>

namespace gb {

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
