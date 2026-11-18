#pragma once
// Just enough JSON to build responses and read the two fields POST /post
// needs. Writing a full parser was not the point of this assignment, so this
// only handles flat objects with string and number values.

#include <string>
#include <sstream>

inline std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[7];
                    snprintf(b, sizeof(b), "\\u%04x", c);
                    out += b;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Pulls "key":"value" out of a flat JSON object. Returns false if the key is
// missing, so the caller can send a 400 instead of silently inserting junk.
inline bool json_get_string(const std::string& body, const std::string& key,
                            std::string& out) {
    std::string needle = "\"" + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return false;
    size_t colon = body.find(':', k + needle.size());
    if (colon == std::string::npos) return false;
    size_t q1 = body.find('"', colon);
    if (q1 == std::string::npos) return false;

    // Walk to the closing quote, skipping anything that was escaped.
    std::string val;
    for (size_t i = q1 + 1; i < body.size(); ++i) {
        if (body[i] == '\\' && i + 1 < body.size()) { val += body[++i]; continue; }
        if (body[i] == '"') { out = val; return true; }
        val += body[i];
    }
    return false;
}

inline bool json_get_int(const std::string& body, const std::string& key,
                         long long& out) {
    std::string needle = "\"" + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return false;
    size_t colon = body.find(':', k + needle.size());
    if (colon == std::string::npos) return false;
    try {
        out = std::stoll(body.substr(colon + 1));
        return true;
    } catch (...) {
        return false;
    }
}
