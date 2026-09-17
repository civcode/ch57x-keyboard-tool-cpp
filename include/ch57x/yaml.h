// yaml.h — parses the small YAML subset used by the mapping config.
//
// Supported subset (deliberately small so the project has no external YAML
// dependency and builds anywhere):
//   model: ch57x-1
//   orientation: normal
//   rows: 1
//   columns: 3
//   knobs: 0
//   layers:
//     - buttons:
//         - [a, b, c]
//       knobs:
//         - ccw: "x"
//           press: "y"
//           cw: "z"
//
// Button rows are flow sequences. Cells that are `null`/`~`/empty map to an
// unbound key. Action strings containing spaces/commas must be quoted.
#pragma once

#include <string>

#include "ch57x/config.h"

namespace ch57x {

// Parse config text. Throws std::runtime_error on invalid input.
Config parseYamlConfig(const std::string& text);

}  // namespace ch57x
