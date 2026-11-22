# Two separate new G-buffer targets, not one combined one, for clearcoat and velocity

Both clearcoat/flake paint and the per-object velocity buffer (2026-08-23) needed new G-buffer
bits — the existing three MRTs (`outAlbedo`, `outNormal`, `outMaterial` in `shaders/gbuffer.frag`)
have every channel already used, so there was no spare space to pack either feature into. We chose
**two separate new attachments** instead of packing both into one combined `RGBA16F` target (which
would have saved an attachment slot). Reason: the two features have different lifetimes — the
clearcoat mask is always-on content the base game needs every frame, while the velocity target only
exists under `SWISH_DEBUG_UI` (matching `TaaPass`, which is debug-only today). Merging them into one
target would have coupled an always-on feature's memory/bandwidth cost to a debug-only one and
complicated the `SWISH_DEBUG_UI` gating that keeps the release build byte-identical.

## Consequences

- Adding either target changes descriptor set 1's (G-buffer) layout — every shader reading set 1
  needs updating in lockstep, per the descriptor-set convention in `CLAUDE.md`.
- If a future feature needs debug-only G-buffer bits, extending the velocity target's free channels
  is the intended next home before adding a fourth attachment.
