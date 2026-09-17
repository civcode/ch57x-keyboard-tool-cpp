#include "ch57x/parser.h"

#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ch57x {

namespace {

// A cursor over the input with simple backtracking primitives.
struct P {
    const std::string& s;
    size_t i = 0;
    explicit P(const std::string& str) : s(str) {}

    char peek() const { return i < s.size() ? s[i] : '\0'; }
    bool atEnd() const { return i >= s.size(); }
    bool startsWith(const char* lit) const {
        size_t n = std::strlen(lit);
        if (i + n > s.size()) return false;
        return s.compare(i, n, lit) == 0;
    }
    void advance(size_t n) { i += n; }

    // Consume one or more characters matching `pred`; returns them, or
    // restores the cursor and returns nullopt if nothing matched.
    template <class Pred>
    std::optional<std::string> takeWhile(Pred pred) {
        size_t st = i;
        while (i < s.size() && pred(s[i])) ++i;
        if (i == st) return std::nullopt;
        return s.substr(st, i - st);
    }
    template <class F>
    auto backtrack(F&& f) -> decltype(f()) {
        size_t st = i;
        auto r = f();
        if (!r) i = st;
        return r;
    }
};

bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAlnum(char c)  { return isAlpha(c) || (c >= '0' && c <= '9'); }
bool isDigit(char c)  { return c >= '0' && c <= '9'; }

std::optional<std::string> alpha1(P& p)  { return p.takeWhile(isAlpha); }
std::optional<std::string> alnum1(P& p)  { return p.takeWhile(isAlnum); }
std::optional<std::string> digits1(P& p) { return p.takeWhile(isDigit); }

// ---------------------------------------------------------------- mod
std::optional<uint8_t> parseModifierToken(P& p) {
    size_t st = p.i;
    auto a = alpha1(p);
    if (!a) return std::nullopt;
    auto m = modifierFromName(*a);
    if (!m) { p.i = st; return std::nullopt; }
    return static_cast<uint8_t>(*m);
}

// Zero or more `modifier-` prefixes.
Modifiers parseModifiersPrefix(P& p) {
    Modifiers mods = 0;
    while (true) {
        size_t st = p.i;
        auto m = parseModifierToken(p);
        if (!m) { p.i = st; break; }
        if (p.peek() != '-') { p.i = st; break; }
        p.advance(1);
        mods |= *m;
    }
    return mods;
}

// ---------------------------------------------------------------- code
std::optional<Code> parseCode(P& p) {
    size_t st = p.i;
    if (p.startsWith("<")) {
        p.advance(1);  // '<'
        auto d = digits1(p);
        if (!d || p.peek() != '>') { p.i = st; return std::nullopt; }
        p.advance(1);  // '>'
        int v = std::stoi(*d);
        if (v < 0 || v > 255) { p.i = st; return std::nullopt; }
        return Code::customCode(static_cast<uint8_t>(v));
    }
    auto a = alnum1(p);
    if (!a) return std::nullopt;
    auto c = wellKnownFromName(*a);
    if (!c) { p.i = st; return std::nullopt; }
    return Code::wellKnown(*c);
}

// ---------------------------------------------------------------- accord
std::optional<Accord> parseAccord(P& p) {
    size_t st = p.i;
    // Branch 1: bare code.
    auto c = parseCode(p);
    if (c) return Accord{0, *c};
    // Branch 2: modifiers then (code | modifier).
    Modifiers mods = parseModifiersPrefix(p);
    auto c2 = parseCode(p);
    if (c2) return Accord{mods, *c2};
    auto m = parseModifierToken(p);
    if (m) return Accord{static_cast<Modifiers>(mods | *m), std::nullopt};
    p.i = st;
    return std::nullopt;
}

std::optional<MacroOptions> parseMacroOptions(P& p) {
    size_t st = p.i;
    if (!p.startsWith("{")) return std::nullopt;
    p.advance(1);
    uint16_t delay = 0;
    while (true) {
        if (p.startsWith("}")) { p.advance(1); break; }
        if (!p.startsWith("delay(")) { p.i = st; return std::nullopt; }
        p.advance(6);  // "delay("
        auto d = digits1(p);
        if (!d || p.peek() != ')') { p.i = st; return std::nullopt; }
        p.advance(1);
        int v = std::stoi(*d);
        if (v < 0 || v > 65535) { p.i = st; return std::nullopt; }
        delay = static_cast<uint16_t>(v);
        if (p.peek() == ',') { p.advance(1); continue; }
        // expect closing brace now
        if (p.peek() == '}') { p.advance(1); break; }
        p.i = st; return std::nullopt;
    }
    return MacroOptions{delay};
}

std::optional<KeyboardEvent> parseKeyboardEvent(P& p) {
    size_t st = p.i;
    MacroOptions opts;  // default delay 0
    auto o = parseMacroOptions(p);
    if (o) opts = *o;

    std::vector<Accord> accords;
    while (true) {
        auto a = parseAccord(p);
        if (!a) break;
        accords.push_back(*a);
        if (p.peek() == ',') { p.advance(1); continue; }
        break;
    }
    if (accords.empty()) { p.i = st; return std::nullopt; }
    return KeyboardEvent{opts, std::move(accords)};
}

// ---------------------------------------------------------------- mouse
std::optional<uint8_t> parseMouseModifierToken(P& p) {  // `modifier-`
    size_t st = p.i;
    auto a = alpha1(p);
    if (!a) return std::nullopt;
    auto m = mouseModifierFromName(*a);
    if (!m || p.peek() != '-') { p.i = st; return std::nullopt; }
    p.advance(1);  // '-'
    return static_cast<uint8_t>(*m);
}

// `button ('+' button)*`
std::optional<MouseButtons> parseMouseButtons(P& p) {
    size_t st = p.i;
    MouseButtons bits = 0;
    while (true) {
        auto a = alpha1(p);
        if (!a) break;
        auto b = mouseButtonBitFromName(*a);
        if (!b) { p.i = st; return std::nullopt; }
        bits |= *b;
        if (p.peek() == '+') { p.advance(1); continue; }
        break;
    }
    if (bits == 0) { p.i = st; return std::nullopt; }
    return bits;
}

std::optional<MouseButtons> parseClickToken(P& p) {
    if (p.startsWith("lclick")) { p.advance(6); return MouseButton::Left; }
    if (p.startsWith("rclick")) { p.advance(6); return MouseButton::Right; }
    if (p.startsWith("mclick")) { p.advance(6); return MouseButton::Middle; }
    if (p.startsWith("click"))  { p.advance(5); return MouseButton::Left; }
    return std::nullopt;
}

std::optional<int8_t> parseDelta(P& p) {
    size_t st = p.i;
    int sign = 1;
    if (p.peek() == '-') { p.advance(1); sign = -1; }
    auto d = digits1(p);
    if (!d) { p.i = st; return std::nullopt; }
    int v = std::stoi(*d);
    if (v > 127) { p.i = st; return std::nullopt; }
    return static_cast<int8_t>(sign * v);
}

std::optional<MouseAction> parseClickAction(P& p) {
    size_t st = p.i;
    // Form 1: click(buttons)
    if (p.startsWith("click(")) {
        p.advance(6);
        auto b = parseMouseButtons(p);
        if (!b || p.peek() != ')') { p.i = st; return std::nullopt; }
        p.advance(1);
        return MouseAction{MouseAction::Kind::Click, *b, 0, 0, 0};
    }
    // Form 2: click_tok ('+' click_tok)*
    MouseButtons bits = 0;
    while (true) {
        auto b = parseClickToken(p);
        if (!b) break;
        bits |= *b;
        if (p.peek() == '+') { p.advance(1); continue; }
        break;
    }
    if (bits == 0) { p.i = st; return std::nullopt; }
    return MouseAction{MouseAction::Kind::Click, bits, 0, 0, 0};
}

std::optional<MouseAction> parseWheel(P& p) {
    if (p.startsWith("wheelup"))    { p.advance(7); return MouseAction{MouseAction::Kind::Wheel, 0, 0, 0, 1}; }
    if (p.startsWith("wheeldown"))  { p.advance(9); return MouseAction{MouseAction::Kind::Wheel, 0, 0, 0, -1}; }
    size_t st = p.i;
    if (!p.startsWith("wheel(")) return std::nullopt;
    p.advance(6);
    auto d = parseDelta(p);
    if (!d || p.peek() != ')') { p.i = st; return std::nullopt; }
    p.advance(1);
    return MouseAction{MouseAction::Kind::Wheel, 0, 0, 0, *d};
}

std::optional<MouseAction> parseMove(P& p) {
    size_t st = p.i;
    if (!p.startsWith("move(")) return std::nullopt;
    p.advance(5);
    auto x = parseDelta(p);
    if (!x || p.peek() != ',') { p.i = st; return std::nullopt; }
    p.advance(1);
    auto y = parseDelta(p);
    if (!y || p.peek() != ')') { p.i = st; return std::nullopt; }
    p.advance(1);
    return MouseAction{MouseAction::Kind::Move, 0, *x, *y, 0};
}

std::optional<MouseAction> parseDrag(P& p) {
    size_t st = p.i;
    if (!p.startsWith("drag(")) return std::nullopt;
    p.advance(5);
    auto b = parseMouseButtons(p);
    if (!b || p.peek() != ',') { p.i = st; return std::nullopt; }
    p.advance(1);
    auto x = parseDelta(p);
    if (!x || p.peek() != ',') { p.i = st; return std::nullopt; }
    p.advance(1);
    auto y = parseDelta(p);
    if (!y || p.peek() != ')') { p.i = st; return std::nullopt; }
    p.advance(1);
    return MouseAction{MouseAction::Kind::Drag, *b, *x, *y, 0};
}

std::optional<MouseEvent> parseMouseEvent(P& p) {
    size_t st = p.i;
    std::optional<MouseModifier> mod;
    auto mt = parseMouseModifierToken(p);
    if (mt) mod = static_cast<MouseModifier>(*mt);

    auto a = parseClickAction(p);
    if (!a) a = parseWheel(p);
    if (!a) a = parseMove(p);
    if (!a) a = parseDrag(p);
    if (!a) { p.i = st; return std::nullopt; }
    return MouseEvent{*a, mod};
}

// ---------------------------------------------------------------- top
std::optional<Macro> parseMacroInner(P& p) {
    // Skip leading whitespace so tokens like " mute" (from flow sequences
    // such as [volumedown, mute, volumeup]) parse the same as "mute".
    while (p.i < p.s.size() && std::isspace(static_cast<unsigned char>(p.s[p.i]))) ++p.i;
    // 1) mouse
    auto me = parseMouseEvent(p);
    if (me) return Macro::makeMouse(*me);
    // 2) media
    {
        size_t st = p.i;
        auto a = alpha1(p);
        if (a) {
            auto mc = mediaFromName(*a);
            if (mc) return Macro::makeMedia(*mc);
        }
        p.i = st;
    }
    // 3) keyboard
    auto ke = parseKeyboardEvent(p);
    if (ke) return Macro::keyboard(*ke);
    return std::nullopt;
}

}  // namespace

Macro parseMacro(const std::string& s) {
    P p(s);
    auto m = parseMacroInner(p);
    if (!m) throw std::runtime_error("cannot parse action: '" + s + "'");
    if (!p.atEnd())
        throw std::runtime_error("cannot fully parse action: '" + s +
                                 "' (trailing '" + s.substr(p.i) + "')");
    return *m;
}

}  // namespace ch57x
