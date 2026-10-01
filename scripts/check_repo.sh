#!/usr/bin/env bash
# One-command repo hygiene check (`make check`). Fast (about a second) and build-free.
#
#   scripts/check_repo.sh            hard checks fail the run; size + format are report-only
#   scripts/check_repo.sh --strict   size and format problems fail the run too
#   scripts/check_repo.sh --quick    skip the format check (what the pre-commit hook uses)
#   scripts/check_repo.sh --verbose  print the full output of every step
#
# Hard checks (exit 1 if they fail):
#   changelog index   CHANGELOG.md index matches docs/changelog/*.md
#   stray root files  nothing unexpected at the repo root (scripts/check_root.sh)
#   ignored+tracked   no file is both tracked and matched by .gitignore
#   source registry   CMake still globs src/ and shaders/ (scripts/test_source_registry.sh)
# Soft checks (warn only, unless --strict):
#   file sizes        scripts/check_file_sizes.sh (800 lines C++, 400 GLSL)
#   clang-format      scripts/format.sh --check (tree is not fully formatted yet)
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

STRICT=0; QUICK=0; VERBOSE=0
for arg in "$@"; do
    case "$arg" in
        --strict) STRICT=1 ;;
        --quick) QUICK=1 ;;
        --verbose|-v) VERBOSE=1 ;;
        -h|--help) sed -n '2,18p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "check_repo.sh: unknown option $arg" >&2; exit 2 ;;
    esac
done

HARD_FAIL=0
SOFT_FAIL=0
SUMMARY=""

# run_step <label> <hard|soft> <command...>
run_step() {
    local label="$1" kind="$2"; shift 2
    local out rc
    out="$("$@" 2>&1)"; rc=$?
    if [ "$VERBOSE" -eq 1 ]; then
        printf '%s\n' "$out" | sed 's/^/    /'
    elif [ "$rc" -ne 0 ]; then
        # Long soft-check output (e.g. 49 unformatted files) is trimmed: first lines + last line.
        if [ "$kind" = soft ] && [ "$(printf '%s\n' "$out" | wc -l | tr -d ' ')" -gt 9 ]; then
            { printf '%s\n' "$out" | head -n 4; echo "..."; printf '%s\n' "$out" | tail -n 1; \
              echo "(trimmed; run scripts/check_repo.sh --verbose for the full list)"; } | sed 's/^/    /'
        else
            printf '%s\n' "$out" | sed 's/^/    /'
        fi
    fi
    if [ "$rc" -eq 0 ]; then
        SUMMARY="$SUMMARY$(printf '  %-18s ok\n' "$label")"$'\n'
    elif [ "$kind" = hard ]; then
        SUMMARY="$SUMMARY$(printf '  %-18s FAIL\n' "$label")"$'\n'; HARD_FAIL=$((HARD_FAIL + 1))
    else
        SUMMARY="$SUMMARY$(printf '  %-18s warn%s\n' "$label" "$( [ "$STRICT" -eq 1 ] && echo ' (strict: counts as failure)')")"$'\n'
        SOFT_FAIL=$((SOFT_FAIL + 1))
    fi
}

ignored_but_tracked() {
    local files
    files="$(git ls-files -ci --exclude-standard)"
    if [ -n "$files" ]; then
        echo "tracked files that .gitignore matches (untrack with 'git rm --cached', or fix the ignore rule):"
        printf '%s\n' "$files"
        return 1
    fi
}

changelog_index() {
    if ! python3 scripts/changelog_index.py --check; then
        echo "CHANGELOG.md index is stale - run: python3 scripts/changelog_index.py"
        return 1
    fi
}

run_step "changelog index"   hard changelog_index
run_step "stray root files"  hard bash scripts/check_root.sh
run_step "ignored+tracked"   hard ignored_but_tracked
run_step "source registry"   hard bash scripts/test_source_registry.sh
run_step "file sizes"        soft bash scripts/check_file_sizes.sh --strict   # --strict here only so the step reports; the step itself is soft
if [ "$QUICK" -eq 0 ]; then
    run_step "clang-format"  soft bash scripts/format.sh --check
fi

echo "check_repo summary:"
printf '%s' "$SUMMARY"

if [ "$HARD_FAIL" -gt 0 ]; then echo "check_repo: FAILED ($HARD_FAIL hard check(s))"; exit 1; fi
if [ "$STRICT" -eq 1 ] && [ "$SOFT_FAIL" -gt 0 ]; then echo "check_repo: FAILED (--strict, $SOFT_FAIL soft check(s))"; exit 1; fi
if [ "$SOFT_FAIL" -gt 0 ]; then echo "check_repo: OK with $SOFT_FAIL warning(s) (report-only; use --strict to enforce)"; else echo "check_repo: OK"; fi
exit 0
