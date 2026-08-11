#pragma once

namespace frontend {

/// \brief SMR supported source languages.
enum Source {

  // Fortran
  F90,
  F70,
  F,

  // C
  C,
  // C++
  CPP,

  // Generic MLIR
  MLIR,
};

} // namespace frontend
