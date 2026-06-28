# tools

Developer tools that run outside the engine — asset pipeline and mesh analysis utilities.

---

## toml_baker (`toml_baker.cpp`)

Converts a human-authored `road.toml` configuration into a compact binary `road.bin` that the engine loads at runtime via a direct struct read.

**Why bake?** The binary format eliminates a TOML parser dependency in the engine. The engine reads `RoadConfig` with a single `fread` into the struct — no allocation, no parsing. The baked file is committed alongside the source TOML so the CI never needs to run the baker.

### Build

```bash
cmake --build build --target toml_baker
```

### Usage

```bash
./build/toml_baker config/road.toml build/config/road.bin
```

### Struct Layout Sync

`toml_baker` writes fields in the **exact memory layout** of `RoadConfig` (`src/scene/RoadScene/RoadConfig.h`). The two files must stay in sync:

| If you do this in `RoadConfig.h` | You must also |
|----------------------------------|--------------|
| Add a field | Add a matching write in `toml_baker.cpp` |
| Remove a field | Remove the write and regenerate `.bin` |
| Reorder fields | Reorder writes in `toml_baker.cpp` |
| Change a field's type | Update both files and regenerate |

The binary begins with `magic = 0x53575243` ("SWRC") and `version = 3` (both `static constexpr` on `RoadConfig`). `load_road_config()` checks both and throws on mismatch — this catches struct-drift errors at startup. All numeric fields are stored in **world units** (already converted from feet/metres by the baker).

### Color Parsing

`read_color` returns `bool`. All call sites check the return value and emit a diagnostic before aborting:

```cpp
if (!read_color(table, "barrier_tint", cfg.barrier_tint)) {
    std::cerr << "toml_baker: missing or invalid 'barrier_tint' color\n";
    return 1;
}
```

Never silently default a color — the resulting visual bug can be very hard to trace.

<details>
<summary><strong>Binary format layout</strong></summary>

`RoadConfig` is `#pragma pack(push, 1)` — **byte-packed, no padding** — so the baker's sequential writes and the engine's single `fread` agree without any ABI assumptions. A `static_assert(std::is_trivially_copyable_v<RoadConfig>)` guards this.

```
Offset  Size  Field
0       4     magic       (uint32_t = 0x53575243 "SWRC")
4       4     version     (uint32_t = 3)
8       4     road_length (float, world units)
12      4     lane_width  (float)
16      4     lane_count  (int32_t)
20      4     shoulder_width_wb (float)
24      4     shoulder_width_eb (float)
28      4     grass_extent      (float)
32..    ...   barrier_*, rail_*, curb_*, *_tile, marking/dash params (float)
...     16ea  color tints — float[4] each (shoulder_tint, barrier_tint, … black_tint)
```

`kCrownSlope` (0.02) is a `static constexpr`, **not** serialized — it is identical in every build, so it lives in the header rather than the file. The engine reads the whole file with `fread(&cfg, sizeof(RoadConfig), 1, f)`.

</details>

---

## car_analyzer (`car_analyzer/`)

A **Blender/bpy GLB·GLTF node-hierarchy inspector**. It is a diagnostic tool, not a validator — run it on a new car model to understand its structure (mesh vs pivot nodes, materials, bounding boxes, part categories) before wiring it into `ModelManager`.

### Running

`car_analyzer` imports the model through Blender's `bpy`, so it needs Blender available — either run it *inside* Blender, or standalone with `bpy` installed from PyPI:

```bash
# Inside Blender (headless)
blender --background --python tools/car_analyzer/run.py -- assets/porsche.glb [flags]

# Standalone (requires the 'bpy' wheel)
uv run python -m car_analyzer assets/porsche.glb [flags]
```

### Flags

| Flag | Effect |
|------|--------|
| `--shorten` | Strip long Sketchfab-style prefixes from node names |
| `--materials` | Show material names on mesh nodes |
| `--bbox` | Show bounding-box dimensions (W×H×D) on mesh nodes |
| `--stats` | Print summary statistics only — skip the full tree |
| `--group` | Print nodes grouped by car-part category |

### What It Reports

- **StatsReporter** — totals: objects, meshes, pivots, materials, roots, leaves, max tree depth, total verts/faces.
- **TreeRenderer** — the full node hierarchy (unless `--stats`).
- **GroupReporter** (`--group`) — mesh nodes partitioned by part category.

<details>
<summary><strong>Part classification (PartClassifier)</strong></summary>

`parts.py` classifies each node's cleaned short-name with an **ordered list of case-insensitive regex rules — first match wins.** Order encodes specificity:

- `steering` is tested **before** `wheel`, so `steering_wheel` → *Gauges & Steering*, not *Wheels*.
- The wheel rule uses `(?<!t)rim` so `trim` does **not** match as a rim.
- Broad categories (`Interior`, `Body Panels`, `Trim / Rubber`, `Lights (other)`) come last so specific parts win first.

Categories include Wheels, Brakes, Bumpers, Headlights, Tail Lights, Glass / Windows, Doors, Hood, Trunk, Exhaust, Mirrors, Seats, Interior, Body Panels, Emblems, Chassis. Anything unmatched is `Uncategorized`.

</details>

<details>
<summary><strong>Implementation notes</strong></summary>

- **`_NameCleaner`** strips Sketchfab `LABEL:LABEL_` prefixes, trailing `_NverticesNfaces` tokens, and trailing material suffixes (`_EXT_0`). Detected prefixes are sorted **longest-first** (`key=len, reverse=True`) so a shorter prefix can't shadow a longer one.
- **`model.py`** uses `@functools.cached_property` for `mesh_nodes` / `pivot_nodes`, and a `yield from` recursive generator in `CarNode.all_descendants()`. `CarNode.depth()` is intentionally a *method*, not a property, so its O(depth) cost is visible at the call site.
- **`_compute_bbox`** uses `zip(*obj.bound_box)` and remaps Blender's Z-up axes to the report's `W×H×D` = (X, Z, Y).

</details>
