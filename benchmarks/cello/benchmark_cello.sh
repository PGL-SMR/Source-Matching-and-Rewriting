#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# Direct Local Matrix Multiplication Benchmark Script (SMR / ClangIR Evaluation)
#
# Purpose:
#   Compiles './matmul.c' with 'smr-cc' (-c to object file, then links),
#   parses SMR's compiler output ('[INFO] Applying rewrite ...') to count the
#   exact number of applied rewrites, and compares execution speed across 10
#   runs with and without pattern rewrites.
#
# Usage:
#   ./benchmark_matmul.sh [path/to/patterns.pat]
#
# Optional Environment Variables:
#   SMR_CC      - Compiler wrapper binary (default: smr-cc)
#   SRC_FILE    - Path to C source file (default: ./matmul.c)
#   PAT_FILE    - Path to .pat file or directory (default: auto-detect in .)
#   NUM_RUNS    - Number of benchmark iterations per phase (default: 10)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1. Configuration & Prerequisite Validation
# ------------------------------------------------------------------------------
SMR_CC="${SMR_CC:-smr-cc}"
SRC_INPUT="${SRC_FILE:-./matmul.c}"
PAT_INPUT="${1:-${PAT_FILE:-}}"
NUM_RUNS="${NUM_RUNS:-50}"
ROOT_DIR="$(pwd)"

if ! command -v "$SMR_CC" >/dev/null 2>&1; then
  echo "Error: compiler wrapper '$SMR_CC' not found in PATH." >&2
  exit 1
fi

if [ ! -f "$SRC_INPUT" ]; then
  echo "Error: source file not found at '$SRC_INPUT'." >&2
  exit 1
fi

REAL_SMR_CC="$(command -v "$SMR_CC")"
TARGET_SRC="$(realpath "$SRC_INPUT")"

# ------------------------------------------------------------------------------
# 2. Isolated Temporary Workspace & Cleanup
# ------------------------------------------------------------------------------
TMP_DIR="$(mktemp -d)"

cleanup() {
  local exit_code=$?
  cd "$ROOT_DIR" 2>/dev/null || true
  if [ -n "${TMP_DIR:-}" ] && [ -d "$TMP_DIR" ]; then
    rm -rf "$TMP_DIR"
  fi
  exit "$exit_code"
}
trap cleanup EXIT

# ------------------------------------------------------------------------------
# 3. Pattern File Resolution & Header Search Paths
# ------------------------------------------------------------------------------
resolve_pat_file() {
  local target="$1"
  local out_pat="$TMP_DIR/combined_patterns.pat"

  # Auto-detect .pat file(s) in current directory if no explicit path was given
  if [ -z "$target" ]; then
    if [ -f "./pat.pat" ]; then
      realpath "./pat.pat"
      return 0
    fi
    target="."
  fi

  if [ -f "$target" ]; then
    realpath "$target"
    return 0
  fi

  if [ -d "$target" ]; then
    local pat_list=()
    while IFS= read -r -d '' f; do
      pat_list+=("$f")
    done < <(find "$target" -maxdepth 1 -type f -name '*.pat' -print0 | sort -z)

    if [ "${#pat_list[@]}" -eq 0 ]; then
      echo "Error: no .pat files found in '$target'." >&2
      exit 1
    fi

    if [ "${#pat_list[@]}" -eq 1 ]; then
      realpath "${pat_list[0]}"
      return 0
    fi

    : > "$out_pat"
    local pf
    for pf in "${pat_list[@]}"; do
      printf '/* === %s === */\n' "$(basename "$pf")" >> "$out_pat"
      cat "$pf" >> "$out_pat"
      printf '\n\n' >> "$out_pat"
    done
    echo "$out_pat"
    return 0
  fi

  echo "Error: pattern path not found at '$target'." >&2
  exit 1
}

setup_cpath() {
  local extra=""
  local d
  for d in "/usr/include/openblas" "/usr/local/include"; do
    if [ -d "$d" ]; then
      extra="${extra:+${extra}:}${d}"
    fi
  done
  if [ -n "$extra" ]; then
    export CPATH="${extra}${CPATH:+:${CPATH}}"
  fi
}

PAT_FILE="$(resolve_pat_file "$PAT_INPUT")"
setup_cpath

# ClangIR compatibility options for smr-cc
export CCC_OVERRIDE_OPTIONS="+-fno-builtin +-Wframe-larger-than=4294967295 +-fno-omit-frame-pointer"

echo "=== Starting Local Matmul Benchmark (smr-cc) ==="
echo "Date          : $(LC_ALL=C date)"
echo "SMR Compiler  : $REAL_SMR_CC"
echo "Target Source : $TARGET_SRC"
echo "Pattern File  : $PAT_FILE"
echo "Runs / Phase  : $NUM_RUNS"
echo ""

# ------------------------------------------------------------------------------
# 4. Compilation & Benchmark Execution
# ------------------------------------------------------------------------------

compile_and_run_phase() {
  local label="$1"
  local pat_path="$2"
  local out_obj="$TMP_DIR/matmul_${label}.o"
  local out_bin="$TMP_DIR/matmul_${label}"
  local comp_log="$TMP_DIR/compile_${label}.log"
  local link_log="$TMP_DIR/link_${label}.log"
  local run_out="$TMP_DIR/run_${label}.log"

  echo "=========================================="
  echo ">>> Phase: $label (SMR_PATS='$pat_path')"
  echo "=========================================="
  echo ">>> Compiling $TARGET_SRC -> object with smr-cc (-c)..."

  # Step 1: Compile with -c so smr-cc triggers the SMR/ClangIR rewrite pipeline
  if ! SMR_PATS="$pat_path" \
       "$REAL_SMR_CC" -O3 -std=gnu99 -c "$TARGET_SRC" -o "$out_obj" >"$comp_log" 2>&1; then
    echo "Error: smr-cc compilation failed ($label):" >&2
    cat "$comp_log" >&2
    exit 1
  fi

  # Step 2: Link the generated object file into the final executable
  if ! SMR_PATS="" \
       "$REAL_SMR_CC" "$out_obj" -lm -o "$out_bin" >"$link_log" 2>&1; then
    echo "Error: linking failed ($label):" >&2
    cat "$link_log" >&2
    exit 1
  fi

  

  echo ">>> Running benchmark $NUM_RUNS times ($label)..."
  local sum_wall="0"
  local run start_t end_t wall_t avg_wall

  for run in $(seq 1 "$NUM_RUNS"); do
    start_t=$(LC_ALL=C date +%s.%N)
    if ! "$out_bin" >"$run_out" 2>&1; then
      echo "Error: execution failed ($label, run $run/$NUM_RUNS):" >&2
      cat "$run_out" >&2
      exit 1
    fi
    end_t=$(LC_ALL=C date +%s.%N)

    wall_t=$(LC_ALL=C awk -v s="$start_t" -v e="$end_t" 'BEGIN { printf "%.6f", e - s }')
    echo "    Run ${run}/${NUM_RUNS}: wall-clock=${wall_t} s"

    sum_wall=$(LC_ALL=C awk -v a="$sum_wall" -v b="$wall_t" 'BEGIN { printf "%.9f", a + b }')
  done

  avg_wall=$(LC_ALL=C awk -v s="$sum_wall" -v n="$NUM_RUNS" 'BEGIN { printf "%.6f", s / n }')
  echo "    Average (${NUM_RUNS} runs): wall-clock=${avg_wall} s"

  eval "AVG_WALL_${label}='$avg_wall'"
}

# ------------------------------------------------------------------------------
# 5. Run Both Phases & Report Speedup
# ------------------------------------------------------------------------------
compile_and_run_phase "WITH_PAT" "$PAT_FILE"
compile_and_run_phase "WITHOUT_PAT" ""

SPEEDUP_WALL_RATIO=$(LC_ALL=C awk -v base="$AVG_WALL_WITHOUT_PAT" -v opt="$AVG_WALL_WITH_PAT" 'BEGIN {
  if (opt > 0) printf "%.4fx", base / opt; else print "N/A"
}')
SPEEDUP_WALL_PCT=$(LC_ALL=C awk -v base="$AVG_WALL_WITHOUT_PAT" -v opt="$AVG_WALL_WITH_PAT" 'BEGIN {
  if (base > 0) printf "%.2f%%", ((base - opt) / base) * 100.0; else print "N/A"
}')

echo ""
echo "=========================================="
echo "=== MATMUL BENCHMARK RESULTS (${NUM_RUNS} RUNS) ==="
echo "=========================================="
echo "Target Source                                : $TARGET_SRC"
echo "Avg wall-clock time WITHOUT pat (smr-cc)     : ${AVG_WALL_WITHOUT_PAT} s"
echo "Avg wall-clock time WITH pat    (smr-cc)     : ${AVG_WALL_WITH_PAT} s"
echo "Speedup (avg wall-clock)                     : ${SPEEDUP_WALL_RATIO} (${SPEEDUP_WALL_PCT} improvement)"
echo "=========================================="