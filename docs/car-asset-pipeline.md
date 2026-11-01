# AI-Generated Car Asset Pipeline — plan

> **Status: PLANNED, nothing built.** Three-agent research pass (AI mesh-gen tooling · modular rig
> conventions · PBR clearcoat/flake texturing), synthesized 2026-08-23. No code or assets changed.
> This doc is the plan to work from when you actually start producing car models; it does not
> replace [`docs/car_system.md`](car_system.md) (how the *current* GLB loader works) or
> [`docs/wheels/README.md`](wheels/README.md) (the wheel/suspension articulation plan this asset
> pipeline is designed to plug into).

## Why this exists

The current car (`assets/Porsche/porsche.glb`) is a downloaded Sketchfab asset: 1,667 flat mesh
nodes, no clean per-part naming, no authored pivots. The loader (`ModelManager::load_car()`)
reverse-engineers wheel/caliper/steering-wheel grouping by clustering nodes with bit-identical
translation vectors and grepping node-ancestry names for substrings like `Combined3DWheel` — clever,
but fragile, and it can't be reused for a second car without re-deriving everything by hand. The
goal going forward: **build your own cars with AI-assisted modeling**, so you own them, they're
licensed for whatever you want to do with them, and — critically — every car shares one **modular,
consistently-named rig** so the loader can look parts up **by name** instead of by heuristic.

Modular breakdown (per the user's own framing): **Body · Wheel ×4 · SteeringWheel · Seats ·
Suspension ×4 · Engine (decorative)**. Exterior priority is photorealism (paint, panel gaps, PBR
correctness); interior only needs to *read* correctly at cockpit-camera distance (steering wheel +
dashboard visible and shaped right — see the "baked pixels, not geometry" note in
[`tasks/todo.md`](../tasks/todo.md) about the current tachometer).

---

## 1. Reality check on AI mesh generation (2026)

There is no one-click "photorealistic AI car" tool. The usable field has consolidated to four
tools worth actually trying, each with real limitations:

| Tool | Topology out | PBR output | Export | Cost / license | Vehicle fit |
|------|--------------|------------|--------|-----------------|-------------|
| **Tripo3D** (P2.0 / Smart Mesh) | Quad-dominant *directly* — best of the group | Separate albedo/normal/roughness/metallic | GLB, OBJ | ~$20/mo Pro = private commercial rights; free tier is non-commercial | Tripo explicitly calls out vehicles/mechanical assemblies as a strong category |
| **Hunyuan3D** (Tencent, v3.1) | Watertight meshes, clean quad topology, smart UV unwrap | Separate albedo/normal/roughness/metallic, up to 4K | GLB, USDZ, OBJ, FBX, glTF | **~20 free generations/day** on the hosted global app (some sign-up promos advertise a larger one-time bonus, e.g. 30 — treat the exact number as promo-dependent, not fixed); hosted free-tier output is personal/non-commercial only; **the open-source self-hosted model is separately licensed for commercial use under the Tencent Hunyuan Community License** (with a carve-out — it doesn't apply in the EU/UK/South Korea) | No explicit vehicle-tuning claim either way — worth testing directly given the free daily quota |
| **Meshy 6** | Triangle soup by default; has a separate **Smart Remesh** step to hit a quad/tri budget | Separate albedo/normal/roughness/metallic + emission, up to 8K | FBX/OBJ/GLB/USDZ/STL | Free tier CC BY 4.0 (attribution); paid $20–60/mo for private commercial use | General-purpose, not vehicle-tuned |
| **Rodin (Hyper3D Gen-2.5)** | Choice of tri/quad, up to 2M polys, has a symmetry toggle | Optional PBR textures | GLB/FBX | Tiered by quality/poly budget | Vehicle-symmetry marketing exists, but at least one independent review calls it weaker for "product/mechanical" — **verify empirically before committing** |

Dead ends as of this research: **CSM.ai** shut down (acquired by Google, Jan 2026). **Luma Genie**
was pulled as Luma pivoted to video. **Kaedim** is a human-in-the-loop studio service, not a fit
for casual iteration. **Spline AI** targets interactive web content, not photoreal export.
**Stability AI / Adobe Firefly** have no maintained car-capable 3D generator.

**On Hunyuan3D specifically** — it's worth adding to the shortlist precisely because the free quota
is generous enough to actually A/B against Tripo on the same reference car at no cost: generate the
same shell in both, compare quad-flow quality and how clean the separated PBR maps come out, and
only spend money once you know which one needs less retopo/re-texture work for *this* car's shape.
Same caveat as the others applies in full: whatever comes out is a reference shell, not a finished
photoreal asset, and if you plan to sell or publicly ship anything built from the hosted free tier,
read the exact terms first — free-tier output there is personal/non-commercial, and the
commercial-safe path is either a paid Hunyuan3D tier or the separately-licensed open-source model.

**The interior does not come for free.** None of these tools reliably infer a real, geometrically
distinct steering wheel + dashboard from an exterior-shell generation — what's behind the glass is
guessed or absent. Plan on generating (or hand-modeling) the interior as its own pass, separate
from the exterior shell.

**The baked-lighting trap — read this before trusting any AI-textured albedo.** AI multiview-diffusion
texturing frequently bakes the *reference image's own shadows/highlights* into the albedo map. It
looks fine in the tool's own preview and is silently wrong once it hits Swish's actual lighting
stack — CSM sun shadows, SSAO, SSR, and split-sum IBL will draw *their own* shadow on top of a
texture that already has one painted in, double-darkening panels. Newer research (LumiTex,
MaterialMVP, Chord) targets illumination/albedo separation, but assume it's imperfect and eyeball
every exported albedo map for baked directional shading before it goes into `MAT_CAR_*`.

### Recommended pipeline (realistic, not one-click)

1. **Generate a reference shell** — Tripo (best quad topology, vehicle-tuned, cheap commercial
   tier) and/or Hunyuan3D (generous free daily quota, also clean quad output) from multi-view
   photos/renders of the target car; comparing both costs nothing given Hunyuan3D's free tier.
   Treat the output as a *proportions and silhouette* reference, not a finished asset.
2. **Retopo in Blender** — run Smart Remesh/Smart Mesh or manual retopo to real quad panels with
   edge flow that supports the panel-gap detail the project wants. This step is not skippable.
3. **Model the interior separately** — steering wheel, dashboard, seats, by hand or single-part AI
   generation, then assembled to the naming convention below (§2).
4. **Re-texture for PBR correctness** — don't trust the mesh tool's baked albedo. Use
   **Substance 3D Sampler** (AI text→PBR material set: albedo/normal/roughness/metallic/height/AO,
   ~$25/mo) and/or **Substance 3D Painter** for hand-detail (chips, gap dirt, decals) — this is the
   only path here that reliably gives clean, *separated*, non-baked-lighting PBR channels.
5. **Export GLB from Blender**, not from the AI tool — Blender's own glTF exporter is more
   controllable for the node-naming/pivot requirements in §2.

**Photogrammetry / 3D-Gaussian-splat alternative** (capture a real car, reconstruct via
COLMAP/Polycam/Kiri, convert splats→mesh): real, and can look more photoreal under its *original*
capture lighting, but yields a baked, view-dependent texture with the same relighting problem as
above, still needs full manual retopo and rigging, and requires a physical car to shoot. Not
obviously better than generate + re-texture for this project; mention only as a known alternative.

---

## 2. Modular rig convention — one skeleton for every car

Swish's renderer only needs **rigid per-node transforms**, never skinning: wheel spin/steer,
suspension travel, and steering-wheel rotation are all `M = M_car · T(pivot) · R · T(-pivot)`
composed per named group (see [`docs/wheels/README.md`](wheels/README.md)). glTF's plain node
hierarchy (parent/child, each with its own local TRS) is sufficient for all of this — **no
Armature/skin is needed or wanted**. This matters because Blender's glTF exporter can silently
drop or rename empties parented *through* an Armature, or force a skinning path you don't want;
plain Empties parented as ordinary objects round-trip as plain glTF nodes with the pivot baked
into the node's own translation.

> **Built (2026-10-12):** the wheel, caliper, steering-wheel, needle and wiper part of this convention now ships as the
> named-rig loader. For the as-built names, axes and Blender export settings, see [car-rig.md](car-rig.md).

Proposed universal node skeleton, so the loader can eventually resolve every part **by exact
name** instead of clustering translations / grepping ancestry strings:

```
Body                          — root mesh (may have material-only child submeshes)
Wheel_FL / Wheel_FR / Wheel_RL / Wheel_RR   — Empty at hub center, wheel+tire mesh parented under it
Caliper_FL / _FR / _RL / _RR  — Empty at the same hub point (steers with fronts, never spins)
Suspension_FL / _FR / _RL / _RR — Empty at the upper-mount pivot, parented BETWEEN Body and the
                                   matching Wheel_*, so ride height is one extra translation on a
                                   node that already sits in the chain — matches the already-shipped
                                   Phase B model (pure spring output, no linkage geometry)
SteeringWheel                 — Empty on the column axis, wheel mesh parented under it
Seat_Driver / Seat_Passenger  — static, no articulation needed
Engine                        — static decorative child of Body (visual only, not simulated)
Wiper_Driver [/ Wiper_Passenger] — Empty on the blade's own pivot arm (NOT the windshield);
                                   optional — see note below
```

**On wiper blades — model them well, since the cockpit camera sees them up close.** Today Swish's
"wiper" (`windshield_wetness.frag`, [`archive/docs/rain/technique-wiper.md`](../archive/docs/rain/technique-wiper.md)) has
**no visible blade mesh at all** — it's a screen-space SDF that clears a swept wetness *sector* on
the glass, nothing is drawn. The cockpit camera sits close behind the wheel (`kSeatEye`, see
[`docs/car_system.md`](car_system.md)), so the base of the windshield — where a resting wiper blade
would sit — is genuinely in view in interior mode, and a badly-modeled or missing blade will read
as "wrong" exactly where the player is looking most. Treat it as its own small, careful modeling
pass rather than an afterthought of the AI-generated exterior shell:

- **Don't expect AI mesh-gen to get this right.** Thin, mechanically precise parts (a slim rubber
  insert clipped into a metal/plastic arm, a pivot hinge, a spring-tension bracket) are exactly the
  kind of small hard-surface detail generic mesh generators tend to blob or lose — the same
  concern that applies to organic foliage (§ below) applies here for the opposite reason: it's too
  *fine*, not too *organic*. Model this part by hand in Blender from reference photos of the actual
  target car's wiper, or at minimum treat any AI-generated version as a rough placeholder to
  rebuild — it's small enough that hand-modeling is genuinely fast.
- **Two materials, not one.** A real blade reads as two distinct surfaces: a satin/anodized metal
  (or black plastic) arm, and a matte black rubber insert with a different roughness value —
  author these as two material-table entries (or two glTF materials) rather than one flat black
  material, or it will look like a toy.
  Give the metal arm subtle anisotropic-looking brushed streaks if you're already hand-painting in
  Substance for the body panels; it's cheap once that workflow is open anyway.
- **Leave a real standoff gap from the glass, in world units.** `WORLD_SCALE = 1000` (1 scene-metre
  = 1000 WU) — a blade resting "on" the glass in real life still has a few millimetres of physical
  clearance once you account for glass curvature and the arm's spring tension; author it with a
  small, deliberate gap (a few WU, not zero) rather than coincident geometry, or it will z-fight
  against the windshield's `alphaMode == BLEND` glass prim. This project has already been bitten
  once by a scale-order bug on thin offsets (`docs/wheels/README.md`'s Phase B suspension ×1000
  double-scale bug) — verify the gap survives the same `M_car` entity-scale multiply the rest of
  the body goes through, not a separate hand-tuned constant that drifts out of sync.
- **Rig it later, not now.** If/when you do want it to sweep (the one-pivot-one-rotation technique
  already shipped for `SteeringWheel`/`is_steering_wheel`/`sw_pivot_frame`, driven by the
  `wiperAngle` already sitting in the wiper UBO), the modeling above already supports it — the
  pivot just needs to sit on the blade's own arm axis, not the windshield. Not required for a
  first car.

<details>
<summary>Blender authoring rules that prevent the wobble-class bugs this project has already hit</summary>

- **Never use Armature/Bones for this.** An Empty (plain axes) parented normally as an object round-
  trips through glTF export as a node with the correct local translation; a bone used the same way
  can be dropped, renamed, or force the mesh onto a skinning path (`glTF-Blender-IO` issue #511).
- **Apply transforms on meshes, never on the pivot Empties.** Running "Apply → All Transforms" on a
  pivot Empty *after* placing it bakes the pivot location into the child mesh's vertices instead of
  leaving it as the node's translation — this is exactly the "baked-tilt wobble" class of bug
  already logged in `docs/wheels/README.md`'s risk register.
- **Parent with "Keep Transform"** so the mesh's own origin/vertices don't shift when you parent it
  under the pivot Empty.
- **Mirror left/right by 180° Y-rotation, never by negative scale.** A negative-scale mirror flips
  winding and bakes a scale-flip that has to be undone per-side later. Swish's *own measured* GLB
  already shows the correct pattern to imitate: left wheels are the right-side instance rotated
  ~180° about Y (not mirrored via scale) — keep every pivot Empty's scale at `(1,1,1)` on both
  sides so the loader can treat left/right identically.
- **Validate node survival after export** — dump node names from the exported GLB (or run
  `gltf-validator`) and confirm all ~17 named nodes above survived with unchanged local
  translations before importing into Swish.
- **Suspension stays a rigid dummy, not a linkage.** Since Swish's suspension is a pure
  spring-damper *output* (a translation, no simulated control-arm geometry), a single Empty per
  corner is the correct, industry-standard lightweight approach — only move to modeled,
  independently-rotating control arms if a future goal is to actually show the arms moving.

</details>

---

## 3. Photoreal exterior paint — texture spec, tied to the planned clearcoat shader

`tasks/todo.md` already lists **clearcoat + flake car paint** as the #1 open realism item — "a
second specular lobe over the GGX base + stochastic normal perturbation." Model authoring should
target that shader directly rather than a generic material.

**What's a texture vs. what's a scalar.** Real clearcoat implementations (Disney's BRDF extension,
Unreal's `ClearCoat` shading model, Blender's Principled BSDF Coat group, the emerging OpenPBR
`coat_*` spec) split into a base coat (albedo, orange-peel normal, roughness/metallic) and a thin
clear top coat (weight, roughness, usually a near-flat or very-subtle-orange-peel normal) — all of
that is standard PBR texture-map territory. **Flake is different: it is normally *procedural*, not
a baked texture.** Flake sparkle is generated at shade time from a handful of scalars (flake
density, flake size/scale, flake normal-variance/roughness, flake color/intensity) via a cellular
noise function in-shader — baking it into a texture at fixed resolution kills the view-dependent
sparkle and aliases under TAA/motion. A flake *density mask* texture (denser near edges, sparser on
flat panels) is a reasonable optional addition, but the sparkle geometry itself should stay a
shader parameter, matching what CLAUDE.md already anticipates.

**No current AI tool solves the coat layer.** Meshy, Tripo3D, and Rodin all now export proper
separate basecolor/normal/roughness/metallic channels (usable in the deferred G-buffer), but none
of them output a clearcoat channel or flake parameters — they solve the base-paint problem, not the
automotive coat. Plan: AI tool or hand-modeling for basecolor/normal/roughness/metallic → hand-set
clearcoat weight/roughness and flake density/scale/color as new scalar fields in this project's own
material table, not something to expect from a generator.

**Export format decision.** glTF's `KHR_materials_clearcoat` extension (`clearcoatFactor`,
`clearcoatRoughnessFactor`, `clearcoatNormalTexture`) is the right interchange target for the parts
it covers — Blender and Substance both round-trip it, and the existing tinygltf-based loader
(`ModelManager.cpp`) already parses `pbrMetallicRoughness.*` per glTF material into the fixed
`MAT_CAR_0..MAT_CAR_19` table (`SceneTypes.h`). Recommended: **read `KHR_materials_clearcoat` where
present** to seed clearcoat weight/roughness (fall back to sane per-slot defaults otherwise), and
**add flake density/scale/color as new fields on this project's own per-`MaterialId` table** —
there is no glTF extension for flake, so don't try to invent one just to smuggle it through export.

**Other exterior materials.** Chrome/trim, calipers/rotors, and rubber tires are small, simple, and
faster to hand-author directly as material-table entries than to fight an AI texturing tool for —
generic mesh generators tend to produce muddy automotive metal/rubber. Headlight/taillight lenses
want the same Fresnel + "faked interior" treatment this project already gives the windshield via
`GlassPass` (`alphaMode == BLEND`) — extend that pattern rather than routing lenses through the
clearcoat shader.

---

## 4. Suggested order of work

1. A/B **Tripo** and **Hunyuan3D** on a first exterior-shell test (Hunyuan3D's free daily quota
   makes this a zero-cost comparison); budget real Blender retopo time regardless of which wins.
2. Build the interior (steering wheel + dashboard shapes only, not full detail) to the naming
   convention in §2, on the *current* car first if useful as a dry run, before committing to a
   full new AI-generated body.
3. Re-texture the exterior in Substance (Sampler for the base PBR set, Painter for panel detail)
   and hand-set clearcoat/flake scalars once the clearcoat shader itself exists — this doc doesn't
   replace writing that shader, see `tasks/todo.md` item #1.
4. Export GLB from Blender, validate node names survive, and only then attempt a loader change from
   translation-clustering to name-based lookup — keep the existing heuristic loader working for the
   current Porsche asset so nothing regresses; a name-based path can be additive (try names first,
   fall back to the heuristic) rather than a rip-and-replace.

## Reading list

- [`docs/car_system.md`](car_system.md) — how the *current* GLB loader normalizes/grounds/scales
  and why it needs heuristics at all.
- [`docs/wheels/README.md`](wheels/README.md) — the pivot-frame / `ArtGroup` articulation model this
  asset pipeline's naming convention is designed to feed directly, plus the wobble/mirroring risk
  register this doc's Blender rules are lifted from.
- [`docs/data-types.md`](data-types.md) — `MaterialId` / `MAT_CAR_0..19` table this doc's clearcoat/
  flake fields would extend.
- [`tasks/todo.md`](../tasks/todo.md) — clearcoat + flake is open item #1; this pipeline should
  target that shader's actual inputs once it's written.
- [`docs/environment-asset-pipeline.md`](environment-asset-pipeline.md) — the same question for
  roadside trees/foliage/highway props; short answer there is a different pipeline (procedural
  trunk + leaf cards), not more AI mesh-gen.

---
*Research: three parallel AI agents (mesh-gen tooling · modular rig conventions · PBR clearcoat
texturing), synthesized 2026-08-23. Review before relying on it — tool landscape moves fast.*
