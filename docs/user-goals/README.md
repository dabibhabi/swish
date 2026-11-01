# User goals

This directory is the **product source of truth** for what you want Swish to become next.
Claude Code's `product-manager` agent (and SessionStart hooks) read here first.

## How to use

1. Edit **[`CURRENT.md`](CURRENT.md)** whenever your active goal changes (what success looks like,
   constraints, non-goals, which agents to involve).
2. When a goal is done or abandoned, move/copy it into `history/YYYY-MM-DD-short-slug.md` and
   rewrite `CURRENT.md` for the next goal.
3. Keep goals **outcome-focused** (user-visible result), not a dump of implementation tasks —
   specialists turn the goal into tasks.

## Files

| File | Role |
|------|------|
| `CURRENT.md` | **Active goal** — always read this |
| `history/` | Archived goals |
| `README.md` | This file |

## Tip

One current goal beats five vague ones. Put backlog ideas in `tasks/todo.md`; put the *active*
intent here.
