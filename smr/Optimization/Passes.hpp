#pragma once

#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "llvm/ADT/ArrayRef.h"
#include <atomic>
#include <memory>
#include <string>

namespace optimization {

/// What a pass changed, summed over the functions it runs on. A pass created
/// with a counter adds one for each loop it transforms (or each operation it
/// hoists), so the caller can tell a directive that found nothing to do.
using ChangeCounter = std::atomic<unsigned>;

/// Unrolls the innermost cir.for loops of a function by Factor.
std::unique_ptr<mlir::Pass> createUnrollPass(unsigned Factor,
                                             ChangeCounter *Changed = nullptr);

/// Tiles the cir.for loops of a function with tiles of Size iterations.
std::unique_ptr<mlir::Pass> createTilingPass(unsigned Size,
                                             ChangeCounter *Changed = nullptr);

/// Hoists loop invariant code out of the cir.for loops of a function. Calls to
/// the functions in PureFunctions are hoisted too.
std::unique_ptr<mlir::Pass>
createLICMPass(llvm::ArrayRef<std::string> PureFunctions = {},
               ChangeCounter *Changed = nullptr);

/// Adds the passes of dce.
void addDCEPasses(mlir::OpPassManager &PM);

/// Inlines the calls to functions defined in the module.
std::unique_ptr<mlir::Pass> createInlinePass(ChangeCounter *Changed = nullptr);

/// Eliminates common subexpressions in CIR operations.
std::unique_ptr<mlir::Pass> createCSEPass(ChangeCounter *Changed = nullptr);

/// Folds constant expressions in CIR operations.
std::unique_ptr<mlir::Pass> createConstantFoldingPass(ChangeCounter *Changed = nullptr);

/// Performs constant propagation in CIR operations, replacing loads of known
/// constant values with the constants themselves.
std::unique_ptr<mlir::Pass> createConstantPropagationPass(ChangeCounter *Changed = nullptr);   

/// Transforms memory accesses to promote allocas to SSA values, eliminating
/// the allocas and their loads and stores.
std::unique_ptr<mlir::Pass> createMem2RegPass(ChangeCounter *Changed = nullptr);

/// Performs fast math optimizations.
std::unique_ptr<mlir::Pass> createFastMathPass(ChangeCounter *Changed = nullptr);

/// Vectorizes canonical cir.for reduction and elementwise loops. 
std::unique_ptr<mlir::Pass> createVectorizePass(ChangeCounter *Changed = nullptr);

/// Adds CIR's own cleanups: cir-canonicalize, then cir-simplify.
void addCanonicalizePasses(mlir::OpPassManager &PM);

/// Registers the passes above as smr-opt flags (--smr-unroll, ...).
/// addCanonicalizePasses is the --smr-canonicalize pipeline.
void registerSmrPasses();

} // namespace optimization
