---
verified: 2026-09-29
against: c843ebe + uncommitted refactor   # git rev-parse --short HEAD; only the code reference below was checked
owner: docs-scribe
---

# Environment / roadside asset pipeline — trees, foliage, highway props

> **Status: PLANNED, nothing built.** Research pass on vegetation asset tooling, synthesized
> 2026-08-23, grounded against the corridor already built in
> [`docs/endless-road.md`](endless-road.md) and [`docs/glen-cove-lie.md`](glen-cove-lie.md). No
> code or assets changed. This doc answers a narrower question than those two: **should trees,
> leaves, and other roadside "highway material" be AI-generated the same way the car is, or does
> the environment need a different asset strategy?** Short answer: mostly a different strategy —
> see below.

## Why this is a separate question from the car

The car is one rigid, symmetric, man-made hero object rendered once. The environment is the
opposite: a corridor of NY's Long Island Expressway (Queens → Nassau, a compact first pass)
that has to repeat cheaply for kilometres, and its hardest asset category — trees — is organic,
branching, and needs to look right both as a wall of foliage at highway speed and, eventually, up
close. Treat mesh-gen tool choice, LOD, and regional authenticity as three separate decisions, not
one "AI-generate everything" pass.

## 1. Trees / leaves — don't spend AI mesh-gen credit on the tree structure itself

**Current state (for context):** `generate_marginal_trees` in `RoadScene`
([`RoadScene_Furniture.cpp`](../src/scene/RoadScene/RoadScene_Furniture.cpp)) draws simple cross-billboard quads
textured with a single `MAT_TREE` (`tree_leaves`) texture on the intro's north marginal only — an
explicitly-documented interim stand-in, with the full-corridor tree wall deferred (GLB later) per
[`docs/glen-cove-lie.md`](glen-cove-lie.md). (The older full-corridor `generate_trees` was dead code and was deleted
on 2026-09-29.)

**AI mesh generators (Tripo3D / Meshy / Rodin / Hunyuan3D) are not a good fit for the branching
trunk+canopy structure.** These tools are trained predominantly on rigid, symmetric, man-made
shapes — exactly the car case — and organic branching geometry is a known weak spot: expect
polygon-soup, non-manifold branch topology needing heavy cleanup, the same category of failure as
mushed character fingers/faces, just for branches instead. There's active 2025-2026 academic work
(e.g. TreeMeshGPT, autoregressive tree-growth generators) specifically *because* generic mesh
diffusion doesn't handle trees well — a sign the mainstream tools aren't there yet. Where AI
mesh-gen *can* help: a coarse, leafless trunk+major-branch "hero" skeleton as a starting reference,
not a finished, leaf-covered asset.

**The actual industry pipeline is procedural trunk/branch generation + leaf cards, not full mesh
generation of any kind.** SpeedTree remains the dominant commercial tool because it *grows* the
trunk/branch skeleton from botanical rules, then dresses it with **leaf cards** — small
alpha-cutout textured quads clustered on the branches — rather than modeling individual leaves.
On the Blender side, Botaniq (paid, actively maintained) and the built-in Sapling/Sapling2 addon
(free, rougher) are the equivalent procedural-generator options. **Leaf cards are the one place AI
*image* generation is a genuinely good fit here** — a leaf card is a flat, alpha-matted 2D texture
problem, not a topology problem, so an image model (or a foliage texture atlas from Quixel
Megascans/Fab.com, or the free-tier equivalent at Poly Haven) is the right tool, not a 3D mesh
generator.

**Wind animation needs nothing beyond a per-vertex shader offset** — consistent with how this
project already avoids skinning elsewhere (wheels/steering are rigid-transform composition, not
bones). The standard technique bakes per-vertex weights into vertex-color channels (high-frequency
leaf/twig flutter, medium branch bend fading toward the trunk join, low-frequency trunk sway, roots
pinned at zero) sampled against a world-position noise/gust field and a wind uniform — no CPU
skinning pass, and it gives a natural hook to scale sway with the existing rain/weather system.

**LOD/impostor thresholds for trees are looser than for the car or the already-planned lamp/sign
impostors (roadmap item B2).** Trees tolerate impostor swaps earlier than hero objects because
irregular, high-frequency canopy silhouette hides seams a flat sign face would show. A common rule
of thumb: impostor below roughly 5% of screen coverage, or once screen-space width drops under
~80–120 px; "primarily viewed beyond 20–30 m" is a typical default impostor threshold. Practically,
**the existing cross-billboard is already a reasonable furthest-LOD** — the missing piece is a
nearer mesh LOD (a SpeedTree-style leaf-card tree) that geomorphs/alpha-fades into that same
billboard, matching the B2 plan already on the books rather than replacing it.

**Regional authenticity — worth a light pass, not a deep one.** NY DOT roadside planting near the
LIE (Bayside/Oakland Gardens widening records) skews toward red maple, sweetgum, and willow oak,
with oak/pitch pine more typical further out toward the Nassau pine barrens; there isn't strong
evidence of formal London-plane boulevard planting on the LIE itself (that's more an NYC
street-tree pattern). This project already grounds its interim marginal billboards in Street View
reference for the same corridor — apply the same discipline to leaf-card silhouettes/textures
(reference maple/oak/sweetgum leaf shapes) rather than generic "tree" stock. Cheap, and raises
perceived authenticity more than mesh accuracy would.

## 2. Guardrails, signage, lamp poles — these are already the right kind of asset; keep them procedural

Unlike trees, these are **already generated as parametric C++ geometry** (`generate_guardrail`,
`generate_street_lamps`, sign generators — pole/arm/fixture primitives combined via the project's
`MeshBuilder`), not imported models. This is a genuine strength, not a gap: procedural generation
scales to an arbitrary corridor length with zero per-instance asset cost, which is exactly what an
"endless" road needs. **Don't replace this with individually AI-modeled meshes.** The one place AI
tooling helps here is the same texturing workflow already recommended for car paint
([`docs/car-asset-pipeline.md`](car-asset-pipeline.md) §3) — a text-to-PBR-material pass (Substance
3D Sampler, or a decent stock/AI-sourced tileable texture) for galvanized guardrail steel, aged
asphalt, worn lane paint, and rusted sign backing — applied to the existing `MAT_ASPHALT` /
`MAT_CONCRETE` / `MAT_METAL` / `MAT_SIGN_0..7` slots (`docs/data-types.md`), not new meshes.

If a specific "hero" prop ever needs one-off detail beyond what a primitive can express — a
particular LIE-specific overpass truss, a distinctive exit-gantry shape — model or AI-generate
**one** asset and let the existing procedural placement system instance/repeat it along the
corridor, the same pattern trees are moving toward, rather than hand-placing unique geometry per
instance.

## 3. Suggested order of work

1. Don't spend Tripo/Hunyuan3D credits on tree structure. If you want a mesh LOD beyond the current
   billboard, start from a procedural generator (Botaniq or Sapling2 in Blender) for the trunk/
   branch skeleton.
2. Source or AI-generate leaf-card texture atlases (alpha-cutout, species-referenced per §1) —
   this is the one genuinely good AI-assisted step in this whole category, and it's an image-model
   problem, not a mesh-gen one.
3. Add a per-vertex wind shader (vertex-color weighted, no bones) when the mesh LOD lands; not
   needed for the current billboard.
4. Leave guardrails/lamps/signage exactly as procedural as they are today; spend any AI-texturing
   effort there on materials (`MAT_ASPHALT`/`MAT_METAL`/`MAT_SIGN_*`), not geometry.
5. When it's time for the mesh LOD, revisit roadmap item **B2** (distance LOD + billboard
   impostors) in [`tasks/todo.md`](../tasks/todo.md) — this doc's tree-LOD recommendation is the
   same feature, just scoped to trees specifically.

## Reading list

- [`docs/endless-road.md`](endless-road.md) — the corridor this all has to repeat along (chunk
  treadmill, culling, service roads).
- [`docs/glen-cove-lie.md`](glen-cove-lie.md) — the current roadside-detail pipeline (procedural
  guardrail/lamp/sign generators; the interim marginal-tree billboard wall this doc's tree-LOD
  plan eventually replaces).
- [`docs/car-asset-pipeline.md`](car-asset-pipeline.md) — the car-modeling counterpart to this doc;
  §3's Substance/PBR texturing workflow is the same one recommended here for highway materials.
- [`docs/data-types.md`](data-types.md) — the `MaterialId` table (`MAT_ASPHALT`, `MAT_TREE`,
  `MAT_SIGN_0..7`, etc.) any new texture work targets.

---
*Research: one AI agent (vegetation/foliage tooling), synthesized 2026-08-23. Review before relying
on it — tool landscape moves fast.*
