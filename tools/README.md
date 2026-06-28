# tools

Developer tools that run outside the engine — asset pipeline and analysis utilities.

## toml_baker (`toml_baker.cpp`)

Converts a human-authored `road.toml` configuration into a compact binary `road.bin` that the engine loads at runtime.

**Why bake?** The binary format avoids a TOML dependency in the engine and lets the engine `mmap` or `fread` the config directly into a `RoadConfig` struct.

### Build

```bash
cmake --build build --target toml_baker
```

### Usage

```bash
./build/toml_baker assets/road.toml assets/road.bin
```

### Sync requirement

`toml_baker` writes fields in the exact memory layout of `RoadConfig` (defined in `src/scene/RoadScene/RoadConfig.h`). If you add, remove, or reorder fields in `RoadConfig`, you **must** update `toml_baker.cpp` to match. The engine will silently read garbage otherwise.

`read_color` returns `bool` — all call sites check the return value and emit a diagnostic on failure.

---

## car_analyzer (`car_analyzer/`)

Python 3 tool that inspects a GLB car mesh and validates that it meets the engine's normalization requirements before loading.

### Setup

```bash
cd tools/car_analyzer
pip install -e .
```

### Usage

```bash
python -m car_analyzer path/to/porsche.glb
```

### What it validates

- Vertex positions are within the unit AABB `[-1, 1]³` after normalization
- All mesh nodes are reachable from the scene root
- Bounding box is non-degenerate (no zero-size axis)
- Prefix disambiguation (longest-match prefix sort prevents partial collisions)

The `model.py` module uses `yield from` generators and `@cached_property` for lazy computation — avoid calling expensive properties in hot loops.
