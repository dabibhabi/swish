# Swish: playtest guide

Thanks for testing! This is a **private playtest**. Please don't redistribute the game.

## What you need

| Requirement | How to get it |
|---|---|
| Apple Silicon Mac (M1 or newer) | Intel Macs are not supported yet |
| Xcode command line tools | `xcode-select --install` |
| Homebrew | [brew.sh](https://brew.sh) |
| CMake, GLFW, GLM | `brew install cmake glfw glm` |
| Vulkan SDK for macOS | [LunarG SDK download](https://vulkan.lunarg.com/sdk/home). Install it, then set `VULKAN_SDK` (see below) |

Set `VULKAN_SDK` to the SDK folder, for example `~/VulkanSDK/<version>/macOS`. Add this line to `~/.zshrc`, then open a new Terminal window:

```sh
export VULKAN_SDK=~/VulkanSDK/<version>/macOS
```

## Get the game

| Step | Command |
|---|---|
| 1. Clone | `git clone https://github.com/dabibhabi/swish.git` |
| 2. Enter the folder | `cd swish` |
| 3. Switch to the playtest branch | `git switch debug-build` |

To update later, run `git pull` inside the `swish` folder.

## Run it

| Build | Command | What you get |
|---|---|---|
| Normal game | `make run` | The game, release build |
| Tuning panel | `make debug` | The game with the live tuning panel (Dear ImGui) |

The first build takes several minutes. Later runs are much faster.

## Controls

**Driving**

| Key | Action |
|---|---|
| ↑ | Throttle |
| ↓ | Brake |
| ← / → | Steer |

**Changing things**

| Key | Action |
|---|---|
| C | Cycle camera views (cockpit, chase, hood, bumper) |
| G | Cycle weather (partly cloudy, cloudy, very cloudy, golden hour) |
| R | Rain: off, light, heavy |
| V | Wipers: off, intermittent, low, high |
| Esc | Release or capture the mouse |

**`make debug` only**

| Key | Action |
|---|---|
| `` ` `` (backtick) | Switch between driving and the tuning panel |
| F12 | Save a frame capture (hold Shift for HDR) |

## Credits

The Porsche 911 Carrera RS (993) model is **"1995 Porsche 911 Carrera RS (993)"** by **[ddiaz-design](https://sketchfab.com/ddiaz-design)** on Sketchfab, licensed under [CC BY-NC-SA 4.0](http://creativecommons.org/licenses/by-nc-sa/4.0/). It is rigged for Swish. Source: [sketchfab.com link](https://sketchfab.com/3d-models/1995-porsche-911-carrera-rs-993-e461497e3c5f4f69bc57ce7614739930).

## What to send back

Please include:

1. **Mac model and macOS version** (Apple menu → About This Mac)
2. **Which build** you ran (`make run` or `make debug`)
3. **What you did** (for example, "drove at night in heavy rain, chase cam")
4. **What went wrong or looked odd**, with a screenshot if you can (Cmd + Shift + 4)
5. **Did it crash?** If so, roughly when, and any error text from Terminal

Thanks again!
