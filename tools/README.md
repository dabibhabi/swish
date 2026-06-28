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

The binary begins with a `magic` u32 and a `version` u32. The engine checks both at load time and throws if they mismatch — this catches most struct-drift errors at startup.

### Color Parsing

`read_color` returns `bool`. All call sites check the return value and emit a diagnostic before aborting:

```cpp
if (!read_color(table, "lane_marking", cfg.lane_marking_color)) {
    std::cerr << "toml_baker: missing or invalid 'lane_marking' color\n";
    return 1;
}
```

Never silently default a color — the resulting visual bug can be very hard to trace.

<details>
<summary><strong>Binary format layout</strong></summary>

```
Offset   Size   Field
0        4      magic  (RoadConfig::MAGIC — 0x52444348 "RDCH")
4        4      version (RoadConfig::VERSION — incremented on breaking changes)
8        4      lane_width     (float, metres)
12       4      lane_count     (uint32_t)
16       4      segment_length (float, metres)
20       4      kCrownSlope    (float, radians)
24       4      billboard_spacing (float, metres)
...      ...    remaining RoadConfig fields in declaration order
```

The engine reads the entire file as `RoadConfig` with `fread(&cfg, sizeof(RoadConfig), 1, f)`. Any padding inserted by the compiler must be identical between the baker and the engine, so both are compiled with the same target ABI. The CMake build ensures this because `toml_baker` is built in the same project.

</details>

---

## car_analyzer (`car_analyzer/`)

Python 3 tool that inspects a GLB car mesh and validates that it meets the engine's normalization requirements **before** handing it to `ModelManager`.

Run it on any new car model to catch normalization issues without starting the engine.

### Setup

```bash
cd tools/car_analyzer
pip install -e .
```

### Usage

```bash
python -m car_analyzer assets/porsche.glb
python -m car_analyzer assets/porsche.glb --verbose   # print per-node report
```

### Validation Checks

| Check | Pass condition |
|-------|---------------|
| Bounding box non-degenerate | No axis has zero extent |
| Positions normalized | All vertices within $[-1, 1]^3$ after applying normalization matrix |
| Root reachability | Every mesh node reachable from the scene root |
| Prefix disambiguation | No two node-name prefixes are exact prefixes of each other (longest-match sort) |
| Glass excluded | `alphaMode == BLEND` primitives not counted in the geometry |

<details>
<summary><strong>Normalization math verified by car_analyzer</strong></summary>

`ModelManager` normalizes positions into a unit AABB:

$$\mathbf{p}_\text{norm} = \frac{\mathbf{p}_\text{baked} - \mathbf{c}}{\max(\text{extents})}$$

where $\mathbf{c}$ is the AABB centroid and $\text{extents} = (\mathbf{p}_\text{max} - \mathbf{p}_\text{min}) / 2$.

`car_analyzer` replicates this computation in Python and checks that every vertex of the normalized mesh satisfies $\|\mathbf{p}\|_\infty \leq 1.0 + \varepsilon$ where $\varepsilon = 10^{-5}$.

</details>

### Implementation Notes

- `model.py` uses `yield from` generators for memory-efficient node traversal
- `@cached_property` on `Model.bounding_box` — computed once, not on every access
- Prefix disambiguation sorts by **descending length** before checking for partial matches — a shorter prefix must not shadow a longer one

</details>
