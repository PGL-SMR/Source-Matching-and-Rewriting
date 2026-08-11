#pragma once

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

namespace ddg {

using yaml =
    llvm::StringMap<llvm::StringMap<llvm::StringMap<llvm::StringSet<>>>>;

/// \brief Dialect-wise configurations.
///
/// This is the user's way to tell use which attributes are relevant for each
/// operation, which operations write to memory, etc.
/// TODO: This should be read from a YAML file.
// NOLINTNEXTLINE(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables)
static yaml Configs = {
    {"std",
     {{"constant", {{"must-match-attr", {"value"}}}},
      {"cmpi", {{"must-match-attr", {"predicate"}}}},
      {"cmpf", {{"must-match-attr", {"predicate"}}}}}},
    {"fir",
     {{"cmpf", {{"must-match-attr", {"predicate"}}}},
      {"if", {{"must-match-attr", {"predicate"}}}},
      {"load", {{"reads-from", {"0"}}}},
      {"store", {{"writes-to", {"1"}}}}}},
    {"memref", {{"global", {{"must-match-attr", {"initial_value"}}}}}},
    {"cir",
     {
         {"global", {{"must-match-attr", {"initial_value"}}}},
         {"load", {{"reads-from", {"0"}}}},
         {"store", {{"writes-to", {"1"}}}},
         {"binop", {{"must-match-attr", {"kind"}}}},
         {"unary", {{"must-match-attr", {"kind"}}}},
         {"cmp", {{"must-match-attr", {"kind"}}}},
         {"const", {{"must-match-attr", {"value"}}}},
         {"get_member",
          {{"reads-from", {"0"}}, {"must-match-attr", {"index"}}}},
         {"insert_member", {{"must-match-attr", {"index"}}}},
     }},
};

} // namespace ddg
