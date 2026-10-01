# Lessons

## Visual/rendering features: a clean build is NOT "done" — run it and look (2026-06-29)

**Mistake:** Implemented the rain overhaul (4 shader/C++ workstreams), verified `make build`
clean + logic review, and reported done — deferring the visual check to the user. The user ran it
and said "doesn't look like anything changed and the water beads still look unrealistic."

**Why it happened:** I treated compile-correctness + per-function logic review as sufficient. For a
*visual* feature that's a category error — the windshield drops were rendering as faint **ring
outlines** with no rivulets, and tuning constants alone couldn't fix it. The real causes were
structural and only visible by looking:
- the drop **trail was computed but never entered coverage/normal** (rivulets drew nothing);
- the specular glint used the **macro surface normal**, not the per-drop normal, so no bead caught a
  highlight → only the Fresnel rim showed → rings.

**Rule for myself:**
1. If the deliverable is something a human *sees* (shader, UI, layout, animation), the definition of
   done includes **running it and viewing the output** — screenshot via the documented workflow
   ([[swish-visual-verification]]: force the flag on in code, rebuild, `screencapture -R` the window
   bounds). Don't outsource the first look to the user.
2. When a visual result looks wrong, suspect **structural** bugs (a term that's computed but unused,
   a normal that's constant where it should vary) before retuning constants.
3. Tuning loop: change → rebuild (kill the running app first; it loads `.spv` at startup) → focus the
   window (`set frontmost ... "swish"`, verify) → screenshot → judge → repeat. Revert any temp
   force-on flag before finishing.

## `make format` reorders ImGuizmo.h before imgui.h → debug build breaks (2026-07-05)

`.clang-format` has `IncludeBlocks: Regroup` + `SortIncludes: true`, which merges all include groups and
alphabetizes. In `src/debug/DebugUI.cpp`, "ImGuizmo" < "imgui" (ASCII: uppercase before lowercase), so
`make format` sorts `ImGuizmo.h` *before* `imgui.h` — but ImGuizmo needs `ImVec4`/`ImGuiID` from imgui
first, so the (debug-only) build fails with "unknown type name 'ImGuiID'". The committed file relied on
blank-line grouping, which Regroup ignores.

- **Fix:** wrap the imgui + ImGuizmo includes in a `// clang-format off` / `// clang-format on` island
  with imgui.h first. This survives future `make format` runs.
- **Gotcha that bit me first:** the directive is honored ONLY when the line is *exactly* `// clang-format
  off` (and `// clang-format on`). Appending explanatory text to the directive line (`// clang-format off
  — because…`) makes clang-format ignore it and re-sort anyway. Put the explanation on separate lines above.
- **General:** `make format` is not always safe — after running it, rebuild BOTH configs (a release-only
  build won't compile the debug-only TUs where this bites) before declaring done.

## A silent-zero grep is not evidence of absence (2026-07-28)

**Mistake:** During a docs audit I ran `grep -rilE "god-ray\|godray" docs/*.md`, got nothing, and
reported that god-rays / TAA / spray / DOF were **undocumented**. They are all documented, in
`docs/realism-features.md`, with their own sections. I'd told the user the docs had a large hole that
didn't exist.

**Why it happened:** in an ERE (`-E`), `\|` is an *escaped* pipe — it matches a literal `|` character,
not alternation. Every one of my multi-alternative patterns was silently searching for a string
containing a pipe, and every one returned zero matches. Zero matches looked like a finding.

**Rule for myself:**
1. Before concluding something is **absent**, prove the pattern can find something **present** —
   re-run it against a term you know exists. A negative grep is only as trustworthy as its regex.
2. Watch the escaping dialect: `grep -E "a|b"` (alternation) vs `grep "a\|b"` (BRE alternation) vs
   `grep -E "a\|b"` (literal pipe — almost never what I want). When in doubt, use `-e a -e b`.
3. A result that is *uniformly* empty across many patterns is a smell — real codebases are patchy.
   Uniform zero usually means the tool is broken, not the codebase.
4. Correct the record explicitly when this happens. Related: [[swish-visual-verification]] — same
   family of error, trusting an indirect signal instead of looking at the thing itself.

## 2026-10-01 — `make format` is repo-wide; never run it inside a scoped phase

**What happened:** after editing 15 files in the quick-fix phase I ran `make format`, which reformatted
the 20 already-unformatted files across the repo too (Units.h, toml_baker, tests…), mixing unrelated
churn into a one-cause-per-diff branch. Caught it from `git diff --stat` and restored the untouched files.

**Rule for myself:**
1. Format only what I touched: `clang-format -i $(git diff --name-only | grep -E '\.(h|cpp)$')`.
2. If a touched file was already unformatted at baseline, do not reformat it in a refactor phase. Formatting
   the backlog is its own M2 cleanup commit.
3. Always read `git diff --stat` before reporting a phase done.

## 2026-10-01 — Ask decisions as selectable prompts, not a text wall

**What happened:** during the grilling skill I printed 10 numbered questions as markdown. The owner
replied "ask the questions one by one so i can select".

**Rule for myself:** for owner decisions use `AskUserQuestion` (at most 4 per call, options + recommended
first), even when a skill's template shows a text format. Text only for explanations.
