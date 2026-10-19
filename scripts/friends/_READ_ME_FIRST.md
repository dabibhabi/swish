# Swish: playtest build

Thanks for testing! This is a **private build**. Please don't pass it on.

## Which app to open

| App | What it is | Use it for |
|---|---|---|
| **Swish.app** | Normal game | Playing and judging how it looks and feels |
| **Swish-Debug.app** | Same game plus a live tuning panel (Dear ImGui) | Tweaking settings while driving. Ask before using it |

Both come in the same kind of zip, and both run the same way.

## What you need

| Requirement | Notes |
|---|---|
| Apple Silicon Mac (M1 or newer) | Intel Macs won't run it |
| A recent macOS | minimum version still being tested |
| Nothing else | no install, no Homebrew, no Xcode. The game carries its own graphics driver |

## How to run it

| Step | What to do |
|---|---|
| 1 | Unzip the file you were sent |
| 2 | Open **Terminal** and run the command for your app (below) |
| 3 | Double-click the app |

### Terminal commands

Run the one for your app. Each one clears the macOS "unsigned app" block, which is needed once after unzipping.

| App | Command |
|---|---|
| Swish.app | `xattr -dr com.apple.quarantine ~/Downloads/Swish-*/Swish.app` |
| Swish-Debug.app | `xattr -dr com.apple.quarantine ~/Downloads/Swish-*/Swish-Debug.app` |

These assume you unzipped into **Downloads**. If you unzipped somewhere else, type `xattr -dr com.apple.quarantine ` (with a space at the end) and drag the app into the window instead.

To open the app from Terminal instead of Finder:

| App | Command |
|---|---|
| Swish.app | `open ~/Downloads/Swish-*/Swish.app` |
| Swish-Debug.app | `open ~/Downloads/Swish-*/Swish-Debug.app` |

**Blocked?** Double-click the app once, then open **System Settings → Privacy & Security** and click **Open Anyway**. Then double-click it again.

The first launch can be slow while the shaders compile. Later launches are faster.

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

**Swish-Debug.app only**

| Key | Action |
|---|---|
| `` ` `` (backtick) | Switch between driving and the tuning panel |
| F12 | Save a frame capture (hold Shift for HDR) |

## What to send back

Please include:

1. **Mac model and macOS version** (Apple menu → About This Mac)
2. **Which app** you ran (Swish.app or Swish-Debug.app)
3. **What you did** (for example, "drove at night in heavy rain, chase cam")
4. **What went wrong or looked odd**, with a screenshot if you can (Cmd + Shift + 4)
5. **Did it crash?** If so, roughly when

Thanks again!
