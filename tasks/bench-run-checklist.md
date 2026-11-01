# Benchmark run checklist — dynamic workflow vs. hand-authored agents

Goal: run Wheel Phase B through both arms without the run itself introducing noise into the
comparison (wrong branch, wrong opt-in phrasing, lost token numbers, non-identical task wording).

## 0. Pre-flight (do this once, before opening either session)

- [ ] Confirm both worktrees exist and are on the right commit:
  ```bash
  git -C /Users/adminh/Current_Projects/swish-bench-dynamic-workflow log --oneline -1
  git -C /Users/adminh/Current_Projects/swish-bench-manual-agents log --oneline -1
  ```
  Both must show `c843ebe`. If either doesn't, **stop** — re-basing mid-benchmark invalidates
  the "identical starting point" premise.
- [ ] Confirm no stray uncommitted changes in either worktree (`git status --short` should be
  empty in both). A worktree that already has drift before you start will confound the diff-stat
  metric at the end.
- [ ] Open `tasks/bench-wheel-phase-b-spec.md` and have the task prompt block ready to paste —
  don't retype it from memory in either session; use the same copy-pasted text verbatim in both.
- [ ] Decide and write down *before* either run starts: what counts as "done" if a session stalls
  or goes in circles (e.g. a 45–60 min wall-clock cap). Deciding this after seeing one arm
  struggle biases the comparison.
- [ ] Close any other Claude Code sessions/agents that might share rate limits or token pools with
  the ones you're about to measure — a shared quota inflates one arm's apparent cost.
- [x] **Assets verified present in both worktrees** (checked 2026-08-14): `assets/` — including
  `assets/Porsche/porsche.glb`, the hero car `App.cpp:251` actually loads — is git-tracked, so
  both worktrees already have all 45 files, byte-identical. Gitignored dirs (`cars/` 72M,
  `archive/local/examples/` 3.1G, `.cache/`, `archive/local/zip_files/` 140M, `build*/`) are either unreferenced by `src/` or
  regenerable build output — deliberately NOT copied per-worktree; `build/` in particular must
  stay separate so a stale `.spv` shader from one arm can't corrupt the other's visual check. No
  action needed here unless you've added new binary assets since this check.

## 1. Dynamic-workflow arm (`bench/dynamic-workflow`)

- [ ] Open a **fresh** Claude Code session (not a continuation of any prior session) with cwd
  `/Users/adminh/Current_Projects/swish-bench-dynamic-workflow`.
- [ ] Verify cwd and branch inside that session before doing anything else:
  ```bash
  pwd && git branch --show-current
  ```
- [ ] Record the start timestamp.
- [ ] Paste the task prompt with an **explicit opt-in phrase** prepended — this is the step most
  likely to silently invalidate the run, because without it Claude will just do the task directly
  and you will have measured nothing about workflows. Use:
  ```
  ultracode: implement Wheel Phase B. <paste the task prompt from tasks/bench-wheel-phase-b-spec.md>
  ```
- [ ] Confirm the `Workflow` tool actually fired — don't just trust Claude's narration:
  - It should return immediately with a task/run ID (background execution), not a synchronous
    wall of edits.
  - Run `/workflows` in that session to watch live progress and confirm agents are spawning.
  - If it did NOT fire (Claude just started editing files directly), stop, note this as a failed
    trial, and re-prompt with a more explicit ask ("use a workflow / multi-agent orchestration
    for this") before continuing — don't let a non-workflow run count as your "dynamic" data
    point.
- [ ] Let it run to completion. Don't intervene mid-run unless it's genuinely stuck past your
  pre-decided time cap — an interrupted run isn't comparable to a completed one.
- [ ] Record the end timestamp.

## 2. Manual-agents arm (`bench/manual-agents`)

- [ ] Open a **fresh** Claude Code session with cwd
  `/Users/adminh/Current_Projects/swish-bench-manual-agents`.
- [ ] Verify cwd and branch, and confirm the four role files are visible to this session:
  ```bash
  pwd && git branch --show-current && ls .claude/agents
  ```
  Expect: `wheel-physics-planner.md`, `cpp-integration-implementer.md`,
  `debug-ui-implementer.md`, `verifier.md`.
- [ ] Record the start timestamp.
- [ ] Paste the **same** task prompt (verbatim, same copy as arm 1) with the explicit
  orchestration instruction:
  ```
  Use the wheel-physics-planner, cpp-integration-implementer, debug-ui-implementer, and verifier
  agents in .claude/agents/ for this, in that order. <paste the same task prompt>
  ```
- [ ] Confirm it's actually using those agents (via the `Agent` tool) rather than doing the work
  itself inline — if Claude skips straight to editing files without invoking any of the four
  named agents, stop and re-prompt more explicitly before letting the clock run.
- [ ] Let it run to completion, same non-intervention rule as arm 1.
- [ ] Record the end timestamp.

## 3. Capture metrics — do this BEFORE closing either session

Numbers you can't get after the session window closes: token usage and per-agent breakdown.
Get these first, before anything else in this section.

- [ ] In **each** session, run `/cost` (or your token-accounting equivalent) and copy the exact
  numbers (input/output/cached tokens) into the bookkeeping table in
  `tasks/bench-wheel-phase-b-spec.md` — do this immediately on completion, not after you've
  moved on to verification steps.
- [ ] For the dynamic-workflow arm specifically: note the generated script's file path (the
  `Workflow` tool prints it on invocation) and copy it somewhere durable — it tells you the
  actual orchestration structure Claude chose (how many agents, what stages), which is data you
  want for the writeup, not just the token total.
- [ ] Count subagent/tool-call invocations for both arms as best you can from the transcript —
  exact parity isn't possible (the two tools report progress differently), so note this as an
  approximate metric, not a precise one, when you write up results.

## 4. Verify correctness — same rubric, applied identically to both branches

Run these on BOTH worktrees, same commands, same order, so a difference in *how* you verified
doesn't leak into the "which is more correct" answer.

- [ ] `git status --short` — confirm nothing was left uncommitted that should've been, and no
  unrequested commit/push happened (check `git log` against your own — did it commit/push
  without being asked?).
- [ ] `git diff --stat c843ebe..HEAD` on each branch — record files touched and lines changed.
- [ ] Kill any running instance of the app before rebuilding (it loads `.spv` shaders at
  startup — a stale process will make the visual check pass/fail on the wrong binary).
- [ ] `make build` on each — clean build, no new warnings from the new files.
- [ ] `make test` on each — record the pass count (baseline 56/56; note the new count and
  whether the two new test cases from the spec were actually added, not just passing by omission).
- [ ] Visual check, same script for both:
  - `make debug`, drive.
  - Window-only screenshots only — `screencapture -R <window bounds>`, **never** the full
    display (grabbing the full screen on both runs wastes time and risks capturing unrelated
    windows/content).
  - Check: crown lean while lane-keeping, one damped overshoot on a lane change across the
    crown, both slider extremes bounded over 10s, ramp behavior still calm.
  - Revert any temporary forced defaults you used to hit an extreme before moving to the next
    branch — a forced default left in place on one branch and not the other is a silent
    confound.
- [ ] Grep each branch's diff for writes to `m_position`/`m_rotation` inside the new suspension
  path — must be none in either branch.
- [ ] Confirm a CHANGELOG.md entry exists on both branches, following the same rubric (root
  cause, file-change table, math in LaTeX).

## 5. Common miscalculation traps — check these explicitly

- [ ] **Non-identical prompts.** If you edited/improved the task prompt between arm 1 and arm 2
  (even fixing a typo), the runs are no longer comparable — re-run the affected arm.
- [ ] **Different base commits.** If either worktree drifted from `c843ebe` (e.g. you `git pull`ed
  or cherry-picked something into one but not the other), stop and re-sync before trusting the
  diff-stat numbers.
- [ ] **Opt-in didn't actually fire.** The single most likely silent failure — see step 1's
  verification. A "dynamic workflow" run that never invoked the `Workflow` tool is actually a
  plain single-agent run mislabeled.
- [ ] **Interrupted runs counted as complete.** A session you stopped early (context limit,
  impatience, a crash) isn't the same data point as one that ran to completion — mark it as a
  partial trial in your notes, don't average it in with completed runs.
- [ ] **Verifying at different rigor.** Don't spend 20 minutes screenshot-checking one branch and
  30 seconds eyeballing the other — apply section 4's checklist identically, in the same order,
  same day (lighting/driving conditions in the sim should be deterministic, but your own
  attention isn't).
- [ ] **Token pool contamination.** Two sessions open at once against a shared account-level quota
  can make one arm's `/cost` numbers look inflated if the harness attributes shared overhead
  unevenly — prefer running the two arms sequentially, not simultaneously, if you're not sure
  how your token accounting handles concurrent sessions.
- [ ] **Judging on a single run.** One trial per arm tells you what happened once, not which
  approach is generally more efficient — if the result surprises you or looks close, re-run
  before drawing a conclusion.

## 6. After both arms are done

- [ ] Fill in the full bookkeeping table in `tasks/bench-wheel-phase-b-spec.md` for both arms.
- [ ] Write a short comparison note: which arm was correct, which was faster, which used fewer
  tokens, and whether the *cheaper/faster* arm also passed every item in section 4 — a fast run
  that fails visual verification isn't a win.
- [ ] Decide whether one trial settles it or whether the result is close enough to warrant a
  second task (a different todo item) before concluding which approach is generally better for
  this codebase.
