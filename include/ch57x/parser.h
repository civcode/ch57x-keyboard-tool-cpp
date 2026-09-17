// parser.h — parses the textual action grammar used in the config file.
//
// Grammar (port of src/parse.rs), tried in this order for a whole token:
//   macro  := mouse_event | media_code | keyboard_event
//
//   keyboard_event := [ '{' delay(N) '}' ] accord (',' accord)*
//   accord         := code
//                  |  modifier ('-' modifier)* ('-' (code | modifier))?
//   code           := '<' digits '>'            (custom HID usage)
//                  |  <well-known key name>
//   modifier       := ctrl | shift | alt(opt) | win(cmd) | rctrl | rshift | ralt(ropt) | rwin(rcmd)
//
//   mouse_event := [ mouse_modifier '-' ] ( click | wheel | move | drag )
//   click       := 'click(' buttons ')'  |  click_tok ('+' click_tok)*
//   click_tok   := click|lclick (left) | rclick (right) | mclick (middle)
//   buttons     := button ('+' button)*
//   wheel       := wheelup | wheeldown | wheel(<i8>)
//   move        := move(<i8>,<i8>)
//   drag        := drag(buttons,<i8>,<i8>)
//   media_code  := <media key name>
#pragma once

#include <string>

#include "ch57x/macro.h"

namespace ch57x {

// Parse a complete action string into a Macro.
// Throws std::runtime_error if the string is not a valid macro or is not
// fully consumed (mirrors Rust's `from_str` all-consuming requirement).
Macro parseMacro(const std::string& s);

}  // namespace ch57x
