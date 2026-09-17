#include "ch57x/yaml.h"

#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "ch57x/parser.h"

namespace ch57x {

namespace {

struct Entry {
    int indent;
    std::string text;  // trimmed content, comment stripped
};

std::string rtrim(std::string s) {
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    s.resize(e);
    return s;
}
std::string trim(std::string s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}
int leadingSpaces(const std::string& line) {
    int n = 0;
    while (n < (int)line.size() && line[n] == ' ') ++n;
    return n;
}
bool startsWith(const std::string& s, const char* p) {
    return s.compare(0, std::strlen(p), p) == 0;
}

std::string stripComment(const std::string& line) {
    bool inD = false, inS = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"' && !inS) inD = !inD;
        else if (c == '\'' && !inD) inS = !inS;
        else if (c == '#' && !inD && !inS &&
                 (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
            return rtrim(line.substr(0, i));
        }
    }
    return rtrim(line);
}

std::vector<Entry> toEntries(const std::string& text) {
    std::vector<Entry> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t eol = text.find('\n', start);
        std::string line = (eol == std::string::npos)
                               ? text.substr(start)
                               : text.substr(start, eol - start);
        start = (eol == std::string::npos) ? text.size() + 1 : eol + 1;
        std::string s = stripComment(line);
        if (s.empty()) continue;
        int ind = leadingSpaces(s);
        out.push_back({ind, rtrim(s.substr(ind))});
    }
    return out;
}

std::string unquote(const std::string& v) {
    std::string s = rtrim(v);
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

bool splitKeyVal(const std::string& line, std::string& key, std::string& value) {
    size_t c = line.find(':');
    if (c == std::string::npos) return false;
    key = rtrim(line.substr(0, c));
    value = rtrim(line.substr(c + 1));
    if (!value.empty() && value[0] == ' ') value.erase(0, 1);
    return true;
}

bool isNullToken(const std::string& v) {
    std::string s = unquote(v);
    return s.empty() || s == "~" || s == "null";
}

std::vector<std::string> splitFlow(const std::string& body) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool inD = false, inS = false;
    for (char c : body) {
        if (inD) { cur += c; if (c == '"') inD = false; }
        else if (inS) { cur += c; if (c == '\'') inS = false; }
        else if (c == '"') { inD = true; cur += c; }
        else if (c == '\'') { inS = true; cur += c; }
        else if (c == '[' || c == '{' || c == '(') { depth++; cur += c; }
        else if (c == ']' || c == '}' || c == ')') { depth--; cur += c; }
        else if (c == ',' && depth == 0) { out.push_back(rtrim(cur)); cur.clear(); }
        else cur += c;
    }
    std::string last = rtrim(cur);
    if (!last.empty()) out.push_back(last);
    return out;
}

std::vector<std::optional<Macro>> parseFlowRow(const std::string& content) {
    std::string s = rtrim(content);
    if (!startsWith(s, "["))
        throw std::runtime_error("expected a button row like [a, b, c], got: " + s);
    size_t close = s.rfind(']');
    if (close == std::string::npos)
        throw std::runtime_error("unterminated button row: " + s);
    std::string body = s.substr(1, close - 1);
    std::vector<std::optional<Macro>> row;
    for (std::string cell : splitFlow(body)) {
        cell = trim(cell);
        if (cell.empty() || cell == "~" || cell == "null")
            row.push_back(std::nullopt);
        else
            row.push_back(parseMacro(unquote(cell)));
    }
    return row;
}

void parseKnobEntries(const std::vector<Entry>& it, Knob& knob) {
    if (it.empty()) return;
    int base = it[0].indent;
    for (const Entry& e : it) {
        if (e.indent != base) continue;
        std::string key, val;
        if (!splitKeyVal(e.text, key, val)) continue;
        if (key == "ccw")      knob.ccw = parseMacro(unquote(val));
        else if (key == "press") knob.press = parseMacro(unquote(val));
        else if (key == "cw")    knob.cw = parseMacro(unquote(val));
        else throw std::runtime_error("unexpected knob key: " + key);
    }
}

// A block sequence of mapping items. Each item's first line is "- key: ...";
// deeper following lines belong to the item. `parseItem(item, obj)` fills one.
template <class Obj, class ParseItem>
size_t parseMappingList(const std::vector<Entry>& e, size_t i, int listIndent,
                        std::vector<Obj>& out, ParseItem&& parseItem) {
    while (i < e.size() && e[i].indent == listIndent && startsWith(e[i].text, "- ")) {
        std::vector<Entry> item;
        item.push_back({listIndent + 2, e[i].text.substr(2)});
        ++i;
        while (i < e.size() && e[i].indent > listIndent) {
            item.push_back(e[i]);
            ++i;
        }
        Obj obj;
        parseItem(item, obj);
        out.push_back(std::move(obj));
    }
    return i;
}

// Buttons: a block sequence of flow rows (or bare single-token rows).
size_t parseButtonRows(const std::vector<Entry>& e, size_t j, int base,
                       std::vector<std::vector<std::optional<Macro>>>& buttons) {
    while (j < e.size() && e[j].indent > base && startsWith(e[j].text, "- ")) {
        std::string content = e[j].text.substr(2);
        if (startsWith(content, "[")) {
            buttons.push_back(parseFlowRow(content));
        } else {
            std::vector<std::optional<Macro>> row;
            if (isNullToken(content)) row.push_back(std::nullopt);
            else row.push_back(parseMacro(unquote(content)));
            buttons.push_back(std::move(row));
        }
        ++j;
    }
    return j;
}

void parseLayerMapping(const std::vector<Entry>& it, Layer& layer) {
    if (it.empty()) return;
    int base = it[0].indent;
    size_t j = 0;
    while (j < it.size() && it[j].indent == base) {
        std::string key, val;
        if (!splitKeyVal(it[j].text, key, val))
            throw std::runtime_error("expected 'key:' in layer, got: " + it[j].text);
        ++j;
        if (key == "buttons") {
            if (val.empty())
                j = parseButtonRows(it, j, base, layer.buttons);
            else if (val == "[]") { /* empty */ }
            else if (startsWith(val, "["))
                layer.buttons.push_back(parseFlowRow(val));
            else
                throw std::runtime_error("unsupported 'buttons' value: " + val);
        } else if (key == "knobs") {
            if (val.empty()) {
                if (j < it.size() && it[j].indent > base && startsWith(it[j].text, "- ")) {
                    j = parseMappingList<Knob>(
                        it, j, it[j].indent, layer.knobs,
                        [](const std::vector<Entry>& item, Knob& knob) {
                            parseKnobEntries(item, knob);
                        });
                }
            } else if (val == "[]") {
                /* empty */
            } else {
                throw std::runtime_error("unsupported 'knobs' value: " + val);
            }
        } else {
            throw std::runtime_error("unexpected layer key: " + key);
        }
    }
}

}  // namespace

Config parseYamlConfig(const std::string& text) {
    std::vector<Entry> e = toEntries(text);
    Config cfg;
    size_t n = e.size();
    size_t i = 0;
    while (i < n) {
        std::string key, val;
        if (!splitKeyVal(e[i].text, key, val))
            throw std::runtime_error("expected 'key:' at top level, got: " + e[i].text);
        if (key == "model") {
            cfg.model = modelFromName(unquote(val));
            if (!cfg.model) throw std::runtime_error("unknown model: " + val);
            ++i;
        } else if (key == "orientation") {
            auto o = orientationFromName(unquote(val));
            if (!o) throw std::runtime_error("unknown orientation: " + val);
            cfg.orientation = *o;
            ++i;
        } else if (key == "rows") {
            cfg.rows = static_cast<uint8_t>(std::stoi(val));
            ++i;
        } else if (key == "columns") {
            cfg.columns = static_cast<uint8_t>(std::stoi(val));
            ++i;
        } else if (key == "knobs") {
            cfg.knobs = static_cast<uint8_t>(std::stoi(val));
            ++i;
        } else if (key == "layers") {
            ++i;
            if (i < n && e[i].indent > 0) {
                i = parseMappingList<Layer>(
                    e, i, e[i].indent, cfg.layers,
                    [](const std::vector<Entry>& item, Layer& layer) {
                        parseLayerMapping(item, layer);
                    });
            }
        } else {
            throw std::runtime_error("unknown top-level key: " + key);
        }
    }
    if (cfg.layers.empty()) throw std::runtime_error("config has no layers");
    return cfg;
}

}  // namespace ch57x
