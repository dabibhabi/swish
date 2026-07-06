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
