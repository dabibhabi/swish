# Rain tuning notes

> Hand-tuning guide for the rain look. One section per phase, newest last. Each section lists:
> - the knobs, with their defaults, safe ranges and what they do;
> - the math behind them;
> - the university / academic sources, marked **opened** (read by the agent) or **unverified**.
>
> Written largely by Claude (AI-generated), 2026-11-20 onward. Check sources marked unverified before relying on them.
> Reference photos live in `scratch/rain-refs/` (gitignored, never committed). The plan is in
> [`tasks/todo.md`](../../tasks/todo.md), "Rain round 3".

How to tune:
1. Start the debug build:

   ```bash
   make debug
   ```

2. Press **R** twice (heavy rain) and wait about 20 s. Wetness rises at 0.08/s, so it needs about 30 s to pass 0.9.
3. Open **Wet / Rain** in the panel. Press **C** to cycle the camera.
4. Compare against `scratch/rain-refs/` and your own photos.

---

## Phase 0 — impact splashes and the rain look (rounds 1–2, 2026-11-20)

### Knobs

| Knob (panel → field) | Default | Safe range | What it does |
|---|---|---|---|
| Wet / Rain → Splashes | on | on/off | Draws road impact splashes (debug only). |
| Splash cell (WU) `splashCellSize` | 600 | 300–1500 | Grid spacing. Smaller means more, denser splashes over a smaller area (64 cells per side). |
| Splash density `splashDensity` | 0.5 | 0–1 | Chance a cell fires per cycle, × rain intensity. |
| Splash period (s) | 0.6 | 0.3–1.5 | Seconds between a cell's firings. |
| Splash life (s) | 0.35 | 0.15–0.6 | Lifetime of one splash (clamped to the period). |
| Ring radius (WU) | 80 | 30–200 | Final crown radius. Real crowns are 1–8 cm. |
| Ring growth tau (s) | 0.06 | 0.02–0.15 | How fast the ring reaches its radius. |
| Ring band width | 0.12 | 0.05–0.25 | Ring thickness, in quad UV. |
| Droplets per splash | 3 | 0–4 | Secondary droplets. |
| Droplet speed (WU/s) | 1500 | 500–3000 | Launch speed, which sets the arc height ($h = v^2/2g$, so about 11 cm at 1500). |
| Droplet size (WU) | 25 | 10–60 | Billboard half-size. |
| Splash brightness | 0.6 | 0.2–1.5 | Overall additive strength. |
| Splash height offset (WU) | 0 | −200–200 | Moves the splash plane if it floats or sinks on ramps. |
| `fogMax` (DebugParams) | 0.9 | 0.6–0.95 | Rain-fog ceiling. Wetness-gated, so dry roads are unaffected. |
| `fogDist63` (WU) | 150 000 | 80 000–400 000 | Distance where rain fog reaches 63% at full wetness. Reference photos suggest 100–200 m. |
| Wet roughness `wetRoughness` | 0.35 debug / 1.0 release | 0.15–0.6 | Multiplies asphalt roughness when wet. Lower means glossier. |

Hard-coded in shaders, so editing them means a rebuild:
- `kRingWeight` 0.5 and `kDropWeight` 0.35 in `rain_splash.vert`.
- The bead refraction ×2.3 and the rim darkening to 0.35 in `windshield_rain.frag`.
- In `WeatherController.cpp`: rain pushes cloud cover with $\operatorname{smoothstep}(0, 0.6, r)$, sets clarity $\le 1 - c$, and shifts the EV clamp by $-4c$.

### Math

$$t_\text{local} = \operatorname{fract}\!\left(\tfrac{t}{P} + h(k)\right)P,\qquad r(t) = R\left(1 - e^{-t/\tau}\right),\qquad \mathbf{x}(t) = \mathbf{c} + \tfrac12 v_0 t\,\hat{\mathbf d} + \left(v_0 t - \tfrac12 g t^2\right)\hat{\mathbf y}$$

Rain fog (Koschmieder-style, in `lighting.frag`): $f = \left(1 - e^{-d\,w/d_{63}}\right) f_{max}\,(1 - \text{clarity})$, with $w$ the wetness.

Physically the ring grows as $\sqrt t$: $a(t) = \sqrt{3RVt}$ (Riboux & Gordillo). Swapping the exponential for this is a cheap precision upgrade.

### Sources

The full table, with links, is in [`tasks/rain-splash-owner-todo.md`](../../tasks/rain-splash-owner-todo.md), "University sources". The ones these knobs rest on:
- Garg & Nayar, "Vision and Rain", IJCV 2007, Columbia. [PDF](https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_IJCV07.pdf) (**opened**). Streak brightness is $\Delta I = \frac{\tau}{T}(\bar E_r - E_b)$, which is why the falling streaks stay faint.
- Riboux & Gordillo, "The critical impact speed for the splash of a drop", Univ. de Sevilla, 2014. [arXiv](https://arxiv.org/pdf/1401.6943) (**opened**). Crown radius $\sqrt{3RVt}$.
- Josserand & Thoroddsen, "Drop Impact on a Solid Surface", Annu. Rev. Fluid Mech. 2016. [PDF](https://yakari.polytechnique.fr/people/josserand/papier/annrev.pdf) (**opened**). Splash threshold $We\sqrt{Re} \gtrsim 3000$, finger count $N \approx 1.14\,We^{1/2}$.
- Garg & Nayar, "Photometric Model of a Rain Drop", Columbia TR 2004. [PDF](https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TR04.pdf) (**opened**). A drop is a ~165° fisheye lens, the basis for the bead refraction.
- Lagarde, "Water drop 2b – Dynamic rain and its effects", 2013. [blog](https://seblagarde.wordpress.com/2013/01/03/water-drop-2b-dynamic-rain-and-its-effects/) (**opened**, industry). Wet diffuse ×0.3, gloss ×2.5, puddle F0 0.02, procedural ripples. It cites Nakamae et al., "A Lighting Model Aiming at Drive Simulators" (SIGGRAPH 1990, Hiroshima Univ.) (**unverified**) for the dry / wet / drenched / puddle classes.
- Tatarchuk & Isidoro, "Artist-Directable Real-Time Rain Rendering in City Environments", EG Workshop on Natural Phenomena 2006. [PDF](http://developer.amd.com/wordpress/media/2012/10/Tatarchuk-Isidoro-Rain%28EGWNph06%29.pdf) (**unverified**). Misty glow, windshield drops, ripples.
- Creus López, "Real-time Realistic Rain Rendering", Master thesis, UPC Barcelona. [PDF](http://upcommons.upc.edu/pfc/bitstream/2099.1/11303/1/Carles%20Creus%20Lopez.pdf) (**unverified**). A survey table of rain-effect algorithms.

### Screenshots

Taken 2026-11-20, local only:
- `scratch/rain-refs/swish-procedural-overcast.png`: after round 2, chase view.
- `scratch/rain-refs/swish-hdri-path-quarry-cloudy.png`: the HDRI path before rain coupling.

---

## Phase P1 — rain-coupled HDRI sky (2026-11-20)

Any rain (R key level 0.35 or 1.0) now swaps the sky to an overcast HDRI, even with "Procedural sky" ticked. Back to
dry, the procedural sky returns and rebakes. One rule decides it, `procedural_sky_active()` in `DebugParams.h`, and
both the weather controller and the shader flag read it.

### Knobs

| Knob (panel → field) | Default | Safe range | What it does |
|---|---|---|---|
| Sky → Rain HDRI `rainEnvironmentHdri` | 9 (Misty Morning) | any catalog entry | The sky and IBL while it rains. It wins over "Environment HDRI" and over the weather preset's file. Try 8 (Mud Road) for more cloud shape. |
| Sky → Rain EV shift (stops) `rainEvShift` | 0 | 0–2 | Lowers the auto-exposure clamp under rain, which brightens the image. The HDRI path is on the legacy light scale, so 0 is right. 3 washed the road white. |
| HDRI exposure (`HdriCatalog.h`, per file) | 1.0 | 0.6–1.4 | Brightness of that HDRI, and so of the whole rain scene's ambient light. Rebuild to change it. |
| `fogDist63`, `fogMax` | 150 000 WU, 0.9 | see Phase 0 | Unchanged. The fog colour is now the sky cube's horizon (mip 3), so it matches the HDRI. |

Fixed by rain, not knobs: sun lux 0 (no shadows, sun glint or god-ray source) and clarity 0 (full rain fog, more
windshield blur).

### Math

Rain fog (unchanged), now with a sky-matched airlight $L_{air}$:

$$L = L_\text{surface}\,(1 - f) + L_{air}\,f,\qquad f = \left(1 - e^{-d\,w/d_{63}}\right) f_{max},\qquad L_{air} = L_\text{sky}(\hat{\mathbf d}_{horizon})$$

This is Koschmieder's law. Meteorological visibility is $V \approx 3.912/\beta$ for a 2% contrast threshold. With
$d_{63} = 1/\beta$, our 150 m gives $V \approx 590$ m. To set fog from a visibility you want, use
$d_{63} = V/3.912$: 200 m visibility means `fogDist63` ≈ 51 000 WU.

The CIE standard overcast sky (what Misty Morning approximates, and what the procedural overcast uses):
$L(\theta) = L_z\,\frac{1 + 2\sin\theta}{3}$, so the horizon is a third of the zenith.

### Sources

- Moon & Spencer, "Illumination from a non-uniform sky", Illuminating Engineering 37, 1942. This is the CIE overcast law (**unverified**, standard reference).
- Koschmieder, "Theorie der horizontalen Sichtweite", Beiträge zur Physik der freien Atmosphäre 12, 1924. This is $V = 3.912/\beta$ (**unverified**, standard reference).
- Debevec, "Rendering Synthetic Objects into Real Scenes", SIGGRAPH 1998, USC. This is HDRI image-based lighting. [PDF](https://www.pauldebevec.com/Research/IBL/debevec-siggraph98.pdf) (**unverified**).
- Garg & Nayar, "Vision and Rain", IJCV 2007, Columbia (**opened**, see Phase 0). Rain adds a uniform veil, which supports a strong fog.
- Poly Haven, "Kloofendal Misty Morning (Pure Sky)" by Greg Zaal, CC0. Its overcast, fog and low-contrast tags were checked visually in the browser.

### Screenshots

Local only, in `scratch/rain-refs/`:
- `p1-heavy-chase.png`: rain 1.0, chase.
- `p1-light-cockpit.png`: rain 0.35.
- `p1-dry-cockpit.png`: rain 0.

### Seen but not P1's job

- The wet road reads near black against the bright sky. This is P2 (reflections, Lekner–Dorf).
- The spray is big white blobs (CURRENT.md issue 2).
- Dry procedural noon looks dark and blue-ish right after leaving rain. That's the procedural exposure, worth a check.

---

## Phase P1b — rain-sky tweaks: latch, fade-in, two more skies (2026-11-20)

The owner asked for four tweaks: dark dry noon, sky brightness and fog, another HDRI, and a smooth transition.

### What was wrong with "dark dry noon"

Dry startup noon is fine (`p1b-dry-noon-startup.png`). The dark noon appeared only right after rain. The sky snapped
back to a clear procedural sky while the road was still soaked. Wetness drains slowly:
$\dot w = -0.012\,w$ per second. Meanwhile `lighting.frag` darkens the sky by up to $0.45\,w$. The result was a dark
blue sky over a wet road. The fix is the latch below.

### Knobs

| Knob (panel → Sky) | Default | Safe range | What it does |
|---|---|---|---|
| Sky clears below wetness `rainSkyClearWetness` | 0.15 | 0.05–0.4 | After the rain stops, the overcast holds until the road dries below this. Higher clears sooner. |
| Rain sky fade-in (s) `rainSkyFadeSeconds` | 20 | 5–60 | Rain onset fades the visible sky from procedural to the rain HDRI ("clouds rolling in"). |
| Rain HDRI | 9 Misty Morning | 8, 9, 10, 11 | New: 10 = Kloofendal Overcast, 11 = Storm Sky (ambientCG, rain shafts on the horizon). |

How long the overcast lingers after heavy rain stops (wetness starts at $w_0 \approx 1$):

$$t_{clear} = \frac{\ln(w_0 / w_{clear})}{0.012\ \mathrm{s^{-1}}} \approx 158\ \text{s at } w_{clear} = 0.15 \qquad (\approx 58\ \text{s at } 0.5)$$

The fade is $b(t) = \min\!\left(1,\ t / T_{fade}\right)$, and the visible sky is $L = \operatorname{mix}(L_{proc}, L_{HDRI}, b)$.

Limit: only the visible sky fades. The IBL light (road, car, grass) swaps at the first frame of rain, because there
is one cube set. A full lighting crossfade would need a second IBL cube set (about 2× IBL memory) and blended
sampling. Logged for later.

### HDRI comparison (heavy rain, chase view, `p1b-hdri-grid-mud-misty-kloof-storm.png`)

| Sky | Read |
|---|---|
| 8 Mud Road | Soft cloud shapes, bright even horizon. |
| 9 Misty Morning (default) | Flattest. Distance dissolves fastest. Closest to the owner's photos 2 and 4. |
| 10 Kloofendal Overcast | Light cloud texture, slightly brighter patch. |
| 11 Storm Sky | The most structure, dark cells and rain shafts at the horizon. Closest to photo 1. |

All four give neutral grey, and the fog matches each horizon. **In every one the road reads near black.** That's P2,
not the sky. Sky brightness per file: edit the `exposure` column in `HdriCatalog.h` (1.0 now); ±0.2 steps are visible.

### Sources

- The same as P1 (Koschmieder fog, the CIE overcast sky, Debevec IBL).
- For drying: the Lagarde "Water drop 2b" timeline (**opened**). Wet level drops quickly after rain, puddles slowly, and the sky
  clears independently. Our latch keys the sky to the wet level.
- ambientCG, "Day Sky HDRI 068 B", CC0 (**opened**, preview viewed). EXR converted to Radiance `.hdr` with headless Blender
  (`bpy` image load and save).

---

## Phase P2 — wet road: SSAO bug, Lekner–Dorf darkening, water F0 (2026-11-20)

### The big one: the black road was an SSAO bug

The ratio of road to sky luminance, measured on screenshots (linear = $(\text{sRGB}/255)^{2.2}$):

| Frame | Road / sky (sRGB) | Road / sky (linear) |
|---|---|---|
| Owner's photo 2 (LIE, rain) | 116 / 208 = 0.56 | 0.28 |
| Owner's photo 3 | 0.67–0.79 | 0.4–0.6 |
| Swish before P2 | 50 / 157 = 0.32 | 0.08 |
| Swish after P2 | 147 / 180 = 0.82 (mid road) | 0.64 |

Physical expectation under a uniform overcast sky of radiance $L$:

$$L_{road} \approx \underbrace{A_w L}_{\text{diffuse}} + \underbrace{F(\theta)\,L}_{\text{water film}},\qquad F(\theta) = F_0 + (1-F_0)(1-\cos\theta)^5$$

At a 10–15° grazing view, $\cos\theta \approx 0.2$, so $F \approx 0.34$ and $L_{road} \approx 0.4L$.

Two experiments isolated the cause:
1. A mirror-smooth wet road came out *black* while the car hood still reflected the sky.
2. Forcing `ssao = 1` fixed it.

In `ssao.frag` the normal came from `cross(dFdx(P), dFdy(P))`. With Vulkan's y-down axes and reverse-Z it pointed into the road, so the "flip below-surface taps" step pushed all 16 taps under the asphalt and AO read about 0. Specular occlusion (Lagarde) then zeroed reflections too. Fix: `if (dot(n, -P) < 0) n = -n`. In dry weather the unoccluded sun term hid the bug.

### Knobs

| Knob (panel) | Default | Safe range | What it does |
|---|---|---|---|
| SSAO radius `ssaoRadius` (WU) | 400 | 300–1500 | Contact darkening reach. Taps reach ≤ 0.43 × radius, about 17 cm now, so the car reads slightly "floating". Try 1000 for a visible contact shadow under the car. |
| SSAO intensity | 1.0 | 0.5–1.5 | Occlusion strength. Now safe to raise, since open ground is no longer falsely occluded. |
| Wet roughness | 0.35 | 0.15–0.5 | Lower = sharper sky and lane reflections in the water film. |
| Puddle coverage | 0.501 | 0.3–0.8 | Mirror pools (roughness 0.04). |
| SSR intensity | 0.714 | 0.5–1.5 | Strength of screen-space reflections of the car, poles and lines. **Not retuned in P2**; check it in the app with traffic. |
| Wet porosity | 0.631 | — | Release only now. The debug build uses Lekner–Dorf (no knob; the constants are physical). |

### Lekner–Dorf wet darkening (debug)

From the paper (opened 2026-11-20, Eq. 1, 9, 11), per colour channel, with $a = 1 - \text{dry albedo}$:

$$a_w = a\,[1 + 0.08(1-a)],\qquad A = \frac{(1-R_1)\,a_w}{1 - p\,(1-a_w)},\qquad \text{wet albedo} = 1 - A$$

$$R_1 = \left(\tfrac{n-1}{n+1}\right)^2 = 0.02,\qquad p = 1 - \tfrac{1}{n^2}\,[1 - \bar R(n)] = 0.475 \quad (n = 1.33)$$

| Dry albedo | Wet (L–D) | Ratio | Old porosity model |
|---|---|---|---|
| 0.05 | 0.044 | 0.89 | 0.63 |
| 0.10 (asphalt) | 0.070 | 0.70 | 0.63 |
| 0.20 | 0.126 | 0.63 | 0.63 |
| 0.40 (concrete, paint) | 0.259 | 0.65 | 0.63 |

Darkening is strongest for weakly absorbing (light) surfaces, as the paper says. The old model darkened black asphalt too much.

### Water F0 (debug)

The wet film's F0 now goes to 0.02 (water–air) instead of up to 0.06. The wet look comes from Fresnel at grazing angles plus low roughness. A raised F0 would make the road shiny at normal incidence, which water is not.

### Sources

- Lekner & Dorf, "Why some things are darker when wet", Applied Optics 27(7):1278–1280, 1988, Victoria Univ. of Wellington. [annotated full text](https://fermatslibrary.com/s/why-some-things-are-darker-when-wet) (**opened**; Eq. 1, 9, 11 and $p = 0.475$ checked).
- Jensen, Legakis & Dorsey, "Rendering of Wet Materials", EGWR 1999, MIT. [PDF](https://groups.csail.mit.edu/graphics/pubs/wet_materials_egwr99.pdf) (**opened** by the research pass). Water-film Fresnel over a darkened base.
- Lagarde, "Water drop 2b" (**opened**). Puddle F0 0.02; also the specular-occlusion formula used in `lighting.frag`.
- Bavoil & Sainz, "Screen Space Ambient Occlusion", NVIDIA 2008 (**unverified**). Hemisphere SSAO; normals must face the viewer.

### Screenshots

Local, in `scratch/rain-refs/`:
- `p2-before-ssao-fix-mirror-test.png`: the black mirror road.
- `p2-ssao-off-test.png`
- `p2-heavy-chase-final.png`
- `p2-dry-chase.png`: no dry regression.

### Next precision steps (P3/P4 or owner)

- Wheel-track ruts (P3) and ripples (P4).
- SSR check with traffic tail lights.
- A larger SSAO radius for car contact shadows.

---

## Phase P2b — SSR and AO tuning (2026-11-20)

### SSR: why near reflections were missing

`ssr.frag` marches the reflected ray with a geometric step, $s_k = s_0 \cdot 1.4^k$, so the distance covered after $k$ steps is

$$d_k = s_0\,\frac{1.4^{k} - 1}{0.4}$$

The old $s_0$ = `ssrStride` = 8063 WU (8 m) jumps past anything closer than 8 m. That includes the car body seen in the road just behind it, the lane lines and the barrier base. Those reflections never hit, and only far geometry could. With $s_0 = 400$ WU (0.4 m), 40 steps still reach far beyond `ssrMaxDist` (36 m): $d_{12} \approx 0.4\,(1.4^{12} - 1)/0.4 \approx 56$ m. Release keeps 8063 (registry `rel` slot).

The SSR geometric normal had the same sign issue as SSAO. `reflect()` doesn't care, but the Fresnel term `dot(N, V)` does: a flipped N gave grazing = 1, so every reflection ran at full strength. Fixed the same way (debug-gated).

### Knobs

| Knob (panel) | Default (debug) | Safe range | What it does |
|---|---|---|---|
| SSR → Stride (WU) `ssrStride` | 400 (release 8063) | 200–1500 | First march step. Smaller catches nearer reflections; too small (<200) runs out of steps before `Max dist`. |
| SSR → Intensity | 0.714 | 0.5–1.2 | Reflection strength. SSR is *added* on top of the env reflection, so above about 1 hits look double-bright. |
| SSR → Thickness (WU) | 1835 | 500–3000 | How thick a surface counts for a hit. Too small leaves gaps in reflections; too big lets reflections "smear" behind objects. |
| SSAO → Radius (WU) `ssaoRadius` | 1000 (was 400) | 600–2000 | Contact darkening reach. Taps reach ≤ 0.43 × radius. |
| Splash brightness | 0.35 (was 0.6) | 0.2–0.6 | Rings and droplets read as white circles on the now-brighter road above about 0.5. |

### Seen

- The chase view (`p2b-ssr-car-reflection-chase.png`) shows a soft mirrored car body on the wet road behind the car.
- The hood view (`p2b-hood-wet-road.png`) shows lane lines and the barrier reflecting.

### Not solved (precision items)

- **Contact shadow under the car is weak.** Screen-space AO cannot see under the car body (it's hidden from the camera). The physical fix is an analytic occluder: treat the car as a box and reduce sky visibility under it, $V = 1 - \frac{\Omega_{car}}{2\pi}$, where $\Omega$ is the solid angle the underbody subtends from the road point. The car showcase already uploads a car box (`lights.carBoxCenter`) that this could reuse.
- **SSR is added, not swapped** for the env reflection on a hit. The correct composite is $L = L_{env}(1 - m) + L_{ssr}$, with $m$ the hit mask. Today it's $L_{env} + L_{ssr}$, which is slightly too bright where SSR hits.

### Sources

- McGuire & Mara, "Efficient GPU Screen-Space Ray Tracing", JCGT 3(4), 2014, Williams College / NVIDIA. [paper](https://jcgt.org/published/0003/04/04/) (**unverified**). Ray-march step, thickness and stride trade-offs.
- Lagarde & de Rousiers, "Moving Frostbite to PBR", SIGGRAPH 2014 course (**unverified**). Specular occlusion and replacing env with SSR.
- Bavoil & Sainz, NVIDIA 2008 (**unverified**). SSAO limits: off-screen and hidden occluders are invisible.

---

## Phase P2c — car contact shadow: analytic underbody sky visibility (2026-11-20)

SSAO can't darken the road under the car, because the underbody is hidden from the camera. P2c traces the sky
against the car's box instead. The box is the one the car showcase already uploads: center at +0.72 m, half extents
2.25 × 0.60 × 0.95 m, so 0.12 m of ground clearance (`App::update_camera`).

### Math

Diffuse sky light on a surface with normal $\mathbf n$ under a sky that is uniform enough is
$E = \int_{\Omega^+} L\,\cos\theta\,d\omega$. Take $K$ directions spread evenly over the unit disk and lift them onto
the hemisphere (Malley's method). Those directions are distributed $\propto \cos\theta$, so a plain average gives
the cosine-weighted visibility:

$$V_d = 1 - \frac{1}{K}\sum_{k=1}^{K} b(\mathbf p, \mathbf p + 5\,\text{m}\cdot\boldsymbol\omega_k),\qquad \boldsymbol\omega_k = r_k\cos\phi_k\,\mathbf t + r_k\sin\phi_k\,\mathbf b + \sqrt{1-r_k^2}\,\mathbf n$$

with $r_k = \sqrt{(k+\tfrac12)/K}$, $\phi_k = k \cdot 137.5°$ (golden angle), $K = 8$. $b \in [0,1]$ is the soft
ray–box block (`carBoxBlock`). Specular gets one ray along the mirror direction,
$V_s = 1 - b(\mathbf p, \mathbf p + 5\,\text{m}\cdot\mathbf r)$. So the road behind the car mirrors the car (SSR)
instead of the sky.

Cost: 9 ray–box tests per pixel, only within 6 m of the car.

### Knobs

| Knob | Default | Range | What it does |
|---|---|---|---|
| Car showcase → "Grounding shadow under area lights" `groundingShadow` | on | on/off | Also switches this sky term (same car box). |
| Box size (code, `App.cpp`) | 4.5 × 1.2 × 1.9 m, 0.12 m clearance | match the car | Clearance sets how dark the band right under the sills gets: lower clearance means a darker and tighter shadow. |
| Ray length 5 m, radius 6 m, $K = 8$ (shader constants) | — | K 8–16 | More rays give smoother penumbrae at more cost. |

### Seen

`p2c-contact-shadow-chase.png`: the car now sits on the road, darker under the body, with a dark mirrored band
behind it. Compare the cars in the owner's LIE photo 2.

### Sources

- Malley's method / cosine-weighted hemisphere sampling: Pharr, Jakob & Humphreys, *Physically Based Rendering*, 3rd ed., §13.6.3 (**unverified** section number). [online](https://pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations) (**unverified**).
- Vogel 1979 / golden-angle disk spiral (phyllotaxis) (**unverified**).
- Iñigo Quílez, "box occlusion" analytic AO (industry, **unverified**). This is a closed-form alternative to the 8 rays if more precision is needed.

---

## Phase P2d — SSR replaces the env reflection instead of adding to it (2026-11-20)

### Math

Before, the composite did $L = L_{lit} + w\,L_{hit}$, where $L_{lit}$ already contains the env (sky) reflection
$E = (F_0 A + B)\,L_{env}$ plus the wet sheen. Where SSR hits, the reflection was counted twice. Now:

$$L = L_{lit} - f\,\min(I, 1)\,\bar E + f\,w\,L_{hit} \;\approx\; L_{diffuse} + E\,(1 - f) + f\,w\,L_{hit}$$

Here $f$ is SSR's hit fade (screen-edge fade), $w$ its reflection weight (gloss × Fresnel × intensity $I$), and
$\bar E$ the luminance of the env reflection. `lighting.frag` stores $\bar E$, after haze and fog attenuation, in the
HDR alpha channel. Nothing else reads HDR alpha (all consumers use `.rgb`), and SSR runs right after lighting.
$\bar E$ is subtracted as grey, so it's exact under overcast; under a saturated blue sky expect a small hue shift
in reflections. The composite clamps to ≥ 0.

### Fix found while checking: car-occluded reflections were black

With the swap, the road behind the car turned near black wherever P2c had removed the sky but SSR did not resolve
the car's body on screen. The mirror ray there hits the car, so the road should mirror the car body. Under overcast a light body is
about 0.45 × the sky it replaces:

$$V_s = 1 - (1 - k_{car})\, b(\mathbf p, \mathbf p + 5\,\text{m}\cdot\mathbf r),\qquad k_{car} = 0.45$$

### Knobs

| Knob | Default | Range | What it does |
|---|---|---|---|
| SSR → Intensity | 0.714 | 0.4–1.0 | Also how much env reflection is swapped out at a hit (`min(I,1)`). Above 1 there is no extra swap, so hits get brighter again. |
| `kCarSpecRadiance` (shader const, `lighting.frag`) | 0.45 | 0.15 (dark car) – 0.7 (white car) | What the road mirrors where the car blocks the sky and SSR misses. |

### Seen

- `p2d-before-car-radiance-black.png`: the black hole behind the car.
- `p2d-ssr-swap-chase.png`: after. A soft mirrored car, and puddles mirroring the light poles as dark streaks
  (compare the LIE photos).

### Sources

- Lagarde & de Rousiers, "Moving Frostbite to PBR", SIGGRAPH 2014 course notes (**unverified**). SSR replaces the IBL specular where it is valid, weighted by the SSR confidence.
- McGuire & Mara, JCGT 2014 (**unverified**). The hit fade and confidence.

---

## Phase P3 — wheel-track ruts (2026-11-20)

Tyres polish and slightly depress two bands per lane, and after rain those bands hold standing water.
`shaders/common/ruts.glsl` gives the profile from the road's UVs ($u, v$ = lateral, arc / asphalt tile).
`gbuffer.frag` packs it into the road tag, `lighting.frag` and `ssr.frag` decode it, and the ruts join the puddle
term: flatter normal, mirror roughness, and darker.

### Math

Lane coordinate on the endless corridor (`config/lie_corridor.toml`: lanes start at the median half-width plus the
inner shoulder, $x_0 = 5.25$ ft $= 1600.2$ WU; lane width $w = 12$ ft $= 3657.6$ WU):

$$x = |\ell| - x_0,\qquad s = (x \bmod w) - \tfrac{w}{2},\qquad d = \min(|s - o|,\ |s + o|)$$

$$r(\ell, a) = \exp\!\left(-\frac{d^2}{2\sigma^2}\right)\cdot\operatorname{mix}(0.55,\ 1,\ \text{wear}(a))$$

The wheel-path offset $o = 850$ WU is half a ~1.7 m track. $\sigma = 180$ WU gives a FWHM of $2.355\sigma \approx$ 0.42 m.
"wear" is smoothed per-lane value noise on 6 m cells along the arc $a$. Only lanes 1–3 are rutted, so a 3-lane
stretch doesn't rut its outer shoulder.

Encoding: road tag $= 0.55 + 0.45\,r$ (asphalt stays ≥ 0.5, so the puddle gate is unchanged); decode
$r = (t - 0.55)/0.45$. Water in the rut: $w_r = r \cdot k_{water} \cdot \operatorname{smoothstep}(0.1, 0.6, \text{wetness})$, which joins
`puddle = max(puddle, w_r)`. Then roughness is mixed toward `rutRoughness`, and the albedo is multiplied by $1 - 0.4\,w_r$.

### Measured

The rut-mask diagnostic (`p3-rut-mask-diagnostic.png`) shows two bands per lane in the tyre paths.

Final heavy-rain frame, row $y = 1100$:

| | Brightness (0–255) |
|---|---|
| Between wheel paths | 80–84 |
| In the left wheel path | 30–72 |

Under a uniform overcast the ruts are **subtle**. Mirror and glossy reflections of a uniform sky look alike, so ruts
show most against structure: lane paint, cars, and tail lights.

### Knobs (panel → Wet / Rain → ruts)

| Knob | Default | Range | What it does |
|---|---|---|---|
| Wheel-track ruts `rutEnabled` | on | on/off | Turns the ruts on or off. |
| Rut water `rutWater` | 0.85 | 0–1 | Standing-water level in the ruts: darkening, flattening, mirror. |
| Rut water roughness `rutRoughness` | 0.06 | 0.02–0.3 | Sharpness of reflections in the ruts. |
| Asphalt UV tile `rutUvTile` | 3000 | must equal `road.toml` asphalt | Recovers the lateral position from the UVs. Pinned by a test. |
| Shader consts (`ruts.glsl`) | $o$ 850, $\sigma$ 180, 3 lanes | $o$ 750–950, $\sigma$ 120–250 | Truck traffic: $o \approx 900$, $\sigma$ wider. |
| Darkening 0.4 (`lighting.frag`) | 0.4 | 0.2–0.6 | Phenomenological; raise it if the tracks should read darker near the camera. |

Limits:
- The authored intro straight uses 13 ft lanes from the barrier, so its ruts sit about 0.4 lane off-centre.
- Ramps and the frontage road use other UV origins, so their ruts don't follow lanes there.

### Sources

- Lagarde, "Water drop 2b" (**opened**). Accumulated water in surface depressions is treated as puddles (Nakamae types 3 and 4), with Gloss → 1, F0 0.02 and N → up.
- Nakamae, Kaneda, Okamoto & Nishita, "A Lighting Model Aiming at Drive Simulators", SIGGRAPH 1990, Hiroshima Univ. (**unverified**). Dry, wet, drenched and puddle road classes.
- Lekner & Dorf 1988 (**opened**). Darkening from total internal reflection in the film.
- FHWA, "Pavement rutting" (wheel-path spacing ≈ vehicle track 1.5–1.8 m) (**unverified**, general road-engineering reference).

---

## Phase P4 — procedural rain ripples on standing water (2026-11-20)

Raindrops hitting standing water (puddles and rut water) send out rings. There's no texture: a jittered world-space
grid, where each cell fires one drop per period. The normal is tilted by the analytic gradient of a ring wave packet.

### Math

For a cell with drop centre $\mathbf c$ and time since impact $t \in [0, T_{life}]$, at distance $d = |\mathbf p_{xz} - \mathbf c|$:

$$x = d - v\,t,\qquad h(x) = \sin(kx)\,e^{-x^2/2w^2}\,\Big(1 - \tfrac{t}{T_{life}}\Big)^2$$

$$\frac{dh}{dd} = \Big(k\cos kx - \frac{x}{w^2}\sin kx\Big)\,e^{-x^2/2w^2}\,\Big(1 - \tfrac{t}{T_{life}}\Big)^2,\qquad \nabla h = \frac{dh}{dd}\,\frac{\mathbf p_{xz} - \mathbf c}{d}$$

The gradients are summed over the 3×3 neighbouring cells, so a ring crossing a cell border stays whole. The normal is
$\mathbf N \leftarrow \operatorname{normalize}\big(\mathbf N - (\partial_x h, 0, \partial_z h)\cdot s\big)$, with
$s = 0.02\,C \cdot \text{strength} \cdot \text{puddle} \cdot \text{upFacing}$.

Constants, with $C$ = cell size: period 0.9 s, life 0.75 s, about 70% of cells fire per period, wavelength $\lambda = 0.12\,C$
($k = 2\pi/\lambda$), packet half-width $w = 0.06\,C$, and ring speed chosen so a ring reaches ~70% of a cell over its life.

Rings show through **Fresnel**: a tilted normal changes $\mathbf N\cdot\mathbf V$ and so the reflectance at grazing.
They read as thin, slightly darker double rings, so they don't need a lit sky.

Drop rate: real heavy rain ($R \approx 25$ mm/h, Marshall–Palmer) puts several hundred drops per m² per second on
the ground. Visible rings are far fewer, because most are small and overlap. The cell grid is an artistic density,
not a physical one. Strength scales with $\min(2 \cdot \text{rain intensity}, 1)$.

### Knobs (panel → Wet / Rain → ripples)

| Knob | Default | Range | What it does |
|---|---|---|---|
| Rain ripples `rippleEnabled` | on | on/off | Turns the ripples on or off. |
| Ripple strength `rippleStrength` | 0.6 | 0.2–1.2 | Normal tilt at the crest. Above ~1 the rings look like grooves. |
| Ripple cell (WU) `rippleCell` | 350 | 200–800 | Spacing and size: a smaller cell gives more, smaller, faster rings (all lengths scale with it). |
| Shader consts `rippleGradient` | period 0.9 s, life 0.75 s, 70% fire | — | Raise the fire fraction for a downpour; lengthen the life for slow drizzle rings. |

Gate: `puddle * upFacing > 0.02`. Ripples need standing water; the thin film alone doesn't ring.

### Seen

`p4-ripples-closeup.png`: concentric rings growing and fading at different phases on the smooth puddle water, with
the asphalt texture visible outside the puddle.

### Sources

- Lagarde, "Water drop 2b – Dynamic rain and its effects", 2013 (**opened**). The procedural animated-circle ripple scheme and the rule that ripples only appear with enough water. Swish uses analytic rings instead of his baked circle texture.
- Tatarchuk, "Artist-directable real-time rain rendering in city environments", SIGGRAPH 2006 course / ATI ToyShop (**unverified**). A ripple simulation driven by raindrops.
- Marshall & Palmer, "The distribution of raindrops with size", J. Meteorology 5, 1948, McGill (**unverified** here; used in Phase 0 via Garg & Nayar).
