# Packaging a test build for friends

`scripts/package_mac.sh` (or `make package`) builds a macOS `.app` zip that a friend can run without the repo.
It is for **playtesting only**. Read the [reminder](#reminder-before-any-public-release) before sending one anywhere else.

```sh
make package                  # build/dist/Swish-release-<sha>.zip  (SWISH_DEBUG_UI=OFF)
make package VARIANT=debug    # build/dist/Swish-debug-<sha>.zip    (live-tuning UI)
```

## What is in the zip

| Path in `Swish.app/Contents/` | What |
|---|---|
| `MacOS/swish` | the binary (arm64). Links only system frameworks and the bundled `Frameworks/` |
| `Frameworks/` | `libglfw`, `libglm`, `libvulkan`, `libMoltenVK` copied from Homebrew / the Vulkan SDK; install names rewritten to `@rpath` |
| `Resources/shaders/*.spv` | the 33 compiled shaders |
| `Resources/config/` | `car.toml`, `road.bin` |
| `Resources/textures/` | the PBR texture set |
| `Resources/assets/` | `blend/porsche_rigged.glb`, `Sky/` (HDRIs), `corridor/lie_corridor.bin` |
| `Resources/vulkan/icd.d/` | MoltenVK ICD manifest (relative `library_path`) |
| `Resources/LICENSE.txt`, `Resources/CREDITS.md` | project MIT licence; asset credits and the Porsche's CC BY-NC-SA terms |

The zip also holds `_READ_ME_FIRST.md`: the run steps, the controls and what to send back. The underscore sorts it first in Finder.

## How the app finds its files

Compile-time paths (`SHADER_DIR`, `ASSET_DIR`, ...) point into the repo, so the bundle cannot use them.
[`src/utils/io/AppPaths.h`](../src/utils/io/AppPaths.h) resolves each directory at runtime, in this order:

1. `<exe>/../Resources/<name>/`: the `.app` bundle
2. `<exe>/<name>/`: a plain folder next to the binary
3. the compile-time build-tree macro: unchanged behaviour for `make run` / `make debug`

The pipeline cache moved to a per-user cache dir (`~/Library/Caches/Swish/`), so nothing writes into a bundle.
At startup, `configure_process_environment()` points `VK_ICD_FILENAMES` at the bundled MoltenVK manifest, unless the
user has set one.

## Known limits of a test build

- Not signed with a Developer ID and not notarised. The friend must clear the quarantine flag (`_READ_ME_FIRST.md`).
- Apple Silicon only (the binary is arm64).
- The debug variant's shader hot-reload reads the **repo's** `shaders/` source tree; on another machine it is a no-op.
- `SWISH_CAR` overrides only work for files that are in the bundle. `assets/Charger/` (589 MB) is deliberately left out.

## Reminder: before any public release

This is the list of what a test build does **not** cover. Each item blocks a public release:

- [ ] **Developer ID signing and notarisation** (needs an Apple Developer account). Replace the ad-hoc `codesign --sign -`
      in `scripts/package_mac.sh` and add `notarytool` + `stapler`.
- [ ] **Third-party licence notices** in the bundle: GLFW (zlib), GLM (MIT), Vulkan loader (Apache-2.0),
      MoltenVK (Apache-2.0). Copy their texts into `Resources/`.
- [ ] **Porsche licence.** `porsche_rigged.*` is CC BY-NC-SA 4.0 (see [`assets/CREDITS.md`](../assets/CREDITS.md)):
      non-commercial, attribution, share-alike. Any public or paid release needs a licence decision or a
      replacement car.
- [ ] **Asset audit.** Confirm the licence of every HDRI and texture in the bundle (the Sky and texture sets).
- [ ] **Intel / universal binary**, if Intel Macs should run it.
- [ ] **Minimum macOS version** (`LSMinimumSystemVersion`), tested on it.
- [ ] **Owner playtest of the bundle** on a clean Mac account, with the quarantine flow, before it goes to anyone.

Once the first item is done, remove this section and the "test build" wording from `scripts/friends/_READ_ME_FIRST.md`.
