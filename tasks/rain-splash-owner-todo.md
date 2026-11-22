# Rain splashes: owner todo (2026-11-20)

> Written by Claude (AI-generated, reviewed by tests and screenshots, not yet by the owner). The plan and decisions are in
> [todo.md](todo.md), "Rain impact splashes". The changelog entry is in [docs/changelog/2026-11.md](../docs/changelog/2026-11.md).

## What was done

- Studied the "burn to Gaussians" effect (`willeastcott/splat-walk`, no licence, so no code was copied). The idea kept:
  every instance is a **stateless function of a hash and a time**. Its phase weights are packed into **one `uint`**,
  and the fragment shader **picks a shape** from them.
- Added debug-only rain impact splashes. Each splash is an expanding crown ring on the road plus up to 4 droplets on
  ballistic arcs. They come from a 64×64 grid of world cells around the camera, so they stay put while the car drives.
  There are no buffers and no compute pass.
- Added 13 tunables under **Wet / Rain** in the debug panel (`rain_splash` TOML table).
- Release build: the executable and image are unchanged, and the `.spv` golden passes.
- Tests: `make test` 438/438, `make test-debug` 463/463.
- Looked at in the app with screenshots:
  - Exaggerated settings: rings and droplets both draw.
  - Defaults: the droplets read as white hail specks, so their weight was cut to 0.35 and the ring band widened to 0.12.
    After that, the droplets are faint and most rings are sub-pixel in the 800×632 window.
  - Rain 0: no splashes.

## How to run

```bash
make debug
```

Press **R** twice to reach heavy rain (startup is rain level 0). Then open **Wet / Rain → Splashes** in the panel.

## Your todo

- [ ] **Look at it at full window size.** Rings are 15 cm across, so at 800×632 most fade out (the `big` factor in the
      frag). Try `Ring radius` 300–600 WU and `Splash brightness` 1–2, and decide whether you want visible rings or a
      sparkle texture.
- [ ] **Drive and check that splashes stay fixed** to the road, not sliding with the car.
- [ ] **Ramps and interchanges:** splashes use a flat plane at the car's height. If they float or sink, adjust
      `Splash height offset`, or plan depth-buffer hits (see "Next math" below).
- [ ] **Night:** the colour is constant, the same as the rain streaks, so splashes may look too bright at night. Decide whether
      to scale them by `camera.sunColor.a` (ambient gain).
- [ ] **GPU time:** read the `Rain` pass in the GPU profiler with Splashes on and off. There are 20 480 instances × 6
      vertices, most of them culled in the vertex shader.
- [ ] **Validation run:** build with `-DCMAKE_BUILD_TYPE=Debug` (only that build turns layers on) and check the log for warnings.
- [ ] **Commit** when you are happy (nothing is committed yet).

## Code snippets

### 1. Stateless phase: when does a cell fire? ([rain_splash.vert](../shaders/rain_splash.vert))

$$t_\text{local} = \operatorname{fract}\!\left(\frac{t}{P} + h(k)\right) P$$

```glsl
float tc = pc.a.x / period + hash11(key);   // rain time / period + per-cell offset
float tl = fract(tc) * period;              // seconds since this cell last fired
uint  ck = key ^ (uint(floor(tc)) * 2654435761u);  // new hash every cycle
if (hash11(ck) >= pc.a.w || tl >= life) { cull(); return; }  // spawn chance = intensity * density
```

Why: no state means no buffers, no CPU upload and no flicker. The same cell and cycle always give the same splash.

### 2. World-fixed grid around the camera

```glsl
ivec2 base = ivec2(floor(camera.camPos.xz / cs));
ivec2 wc   = base + ivec2(int(cell) % G - G / 2, int(cell) / G - G / 2);
uint  key  = (uint(wc.x) * 73856093u) ^ (uint(wc.y) * 19349663u);
```

Why: `rain.vert` wraps drops in a box that moves with the camera, so a splash taken from a drop would slide with the
car. Hashing **world** cells keeps every splash where it landed.

### 3. Crown ring growth and ballistic droplets

$$r(t) = R\left(1 - e^{-t/\tau}\right), \qquad \mathbf{x}(t) = \mathbf{c} + \tfrac12 v_0 t\,\hat{\mathbf{d}} + \left(v_0 t - \tfrac12 g t^2\right)\hat{\mathbf{y}}$$

```glsl
float r = pc.b.z * (1.0 - exp(-tl / pc.b.w));                    // ring: fast start, slows down
vec3 pos = centre + dir * (0.5 * v0 * tl)
         + vec3(0.0, v0 * tl - 0.5 * kGravityWU * tl * tl, 0.0); // droplet arc
if (pos.y < pc.a.y && tl > 0.02) { cull(); return; }             // landed
```

### 4. Pack the state into one uint, and pick the shape in the fragment shader ([rain_splash.frag](../shaders/rain_splash.frag))

```glsl
// vert: x = ring, y = dot, z = fade, w = brightness variation
vFx = packUnorm4x8(vec4(1.0, 0.0, fade, vary));        // ring instance
vFx = packUnorm4x8(vec4(0.0, kDropWeight, fade, vary)); // droplet instance (0.35)

// frag
vec4  fx   = unpackUnorm4x8(vFx);
float fw   = fwidth(r);
float big  = 1.0 - smoothstep(0.15, 0.4, fw);           // sub-pixel rings would fill in: fade them
float ring = (1.0 - smoothstep(0.0, max(pc.c.x, fw * 1.5), abs(r - kRingUV))) * big;
float dotA = 1.0 - smoothstep(0.5, 1.0, r);
float a    = (fx.x * ring + fx.y * dotA) * fx.z * fx.w * pc.d.x;
```

### 5. C++: debug-only push block and draw ([RainSystem.cpp](../src/renderer/RainSystem/RainSystem.cpp))

```cpp
#ifdef SWISH_DEBUG_UI
    m_splashOn     = t.splashEnabled && t.splashDensity > 0.0f;
    m_splashPush.a = Vec4(m_time, u.carPosition.y + t.splashRoadOffset, t.splashCellSize,
                          m_intensity * t.splashDensity);
    // ... b, c, d: period/life/ring, ring width/droplets, brightness/grid
#endif

// in record_draws, after the near rain streaks (same render pass, additive, depth test, no depth write):
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_splashPipeline);
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_splashLayout, 0, 1, &camSet, 0, nullptr);  // rebind: different push range = different layout
vkCmdPushConstants(cmd, m_splashLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                   sizeof(SplashPush), &m_splashPush);  // 64 B, a 16-byte multiple (MoltenVK)
vkCmdDraw(cmd, 6, kSplashCells * kSplashSubs, 0, 0);    // no vertex buffer
```

## University sources: rain math for the road and the windshield

Gathered by a Claude research pass on 2026-11-20. "Opened" means the link was opened. Items marked **unverified** were
not checked against the primary text, so read them before you rely on them. Gaps: no academic source on puddle ripples
was found. The Kaneda CA'93 equations are images in the PDF, so their exact forms below are inferred from the prose.

### A) Road: falling rain, splashes, wet asphalt, spray

| Source | Link | Math you can use | Where in Swish |
|---|---|---|---|
| Garg & Nayar, "Vision and Rain", IJCV 75(1), 2007, Columbia | [PDF](https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_IJCV07.pdf) (opened) | A drop covers a pixel for $\tau < 1.18$ ms, far less than the exposure $T$, so $\Delta I = \frac{\tau}{T}(\bar E_r - E_b)$. Streaks are faint and additive. | `rain.frag` alpha: tie streak brightness to $\tau/T$ instead of a constant. |
| Garg & Nayar, "Photometric Model of a Rain Drop", tech report, Columbia, 2004 | [PDF](https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_TR04.pdf) (opened) | Marshall–Palmer $N(a) = 8\times10^6 e^{-8200 h^{-0.21} a}$. Speed $v = 9.40(1 - e^{-3.45\times10^3 a^{1.31}})$ m/s. A drop refracts about a 165° field of view, so treat it as a fisheye onto the environment. | Already used in `rain.vert`. The fisheye idea fits windshield drop refraction. |
| Garg & Nayar, "Photorealistic Rendering of Rain Streaks", SIGGRAPH 2006 | [page](https://history.siggraph.org/?p=113590) (page only) | Streak appearance comes from a database indexed by light and view direction (drop oscillations). | Optional streak lookup texture. |
| Riboux & Gordillo, "The critical impact speed for the splash of a drop", Univ. de Sevilla, PRL 2014 (article number not checked) | [arXiv](https://arxiv.org/pdf/1401.6943) (opened) | Wetted radius $a(t) = \sqrt{3RVt}$. Splash when $\beta \approx 0.14$. | Swap the ring law $R(1-e^{-t/\tau})$ for $\sqrt{t}$ growth. |
| Josserand & Thoroddsen, "Drop Impact on a Solid Surface", Annu. Rev. Fluid Mech. 48, 2016 | [PDF](https://yakari.polytechnique.fr/people/josserand/papier/annrev.pdf) (opened) | $K = We\sqrt{Re} \gtrsim 3000$ splashes, with $We = \rho V^2 D/\sigma$ and $Re = \rho V D/\mu$. Finger count $N \approx 1.14\,We^{1/2}$. Max spread $\beta_{max} \approx 0.87 Re^{1/5} - 0.4 Re^{2/5} We^{-1/2}$. | Droplet count and ring size per drop diameter (from Marshall–Palmer). |
| Yarin, "Drop Impact Dynamics", Annu. Rev. Fluid Mech. 38, 2006, Technion | DOI 10.1146/annurev.fluid.38.050304.092144 (not opened) | Crown on a wet film: $r \propto (t-t_0)^{1/2}$. **Prefactor unverified.** | Same as Riboux: $\sqrt{t}$ ring. |
| Lekner & Dorf, "Why some things are darker when wet", Applied Optics 27(7), 1988, Victoria Univ. of Wellington | [annotated copy](https://fermatslibrary.com/s/why-some-things-are-darker-when-wet) (opened) | $A_w = 1 - \frac{(1-R_1)(1-A)}{1-pA}$, with $R_1 \approx 0.02$ and $p \approx 0.475$. Apply per channel to the asphalt albedo. | `lighting.frag` wet darkening (`wetPorosity`). Check the current curve against this. |
| Jensen, Legakis & Dorsey, "Rendering of Wet Materials", EGWR 1999, MIT | [PDF](https://groups.csail.mit.edu/graphics/pubs/wet_materials_egwr99.pdf) (opened) | A water film gives a Fresnel specular over a darkened base. Water in the pores means more forward scattering and a lower albedo. | Supports keeping porosity separate from puddles. |
| Gaylard, Kirwan & Lockerby, "Surface contamination of cars: a review", Proc. IMechE D, 2017, Warwick | [PDF](https://wrap.warwick.ac.uk/86801/7/WRAP-surface-contamination-cars-Kirwan-2017.pdf) (opened) | Bow wave and side splash are large ballistic drops. Tread pickup gives drops under 1 mm up to about 4 mm. Capillary mist is about 1% of the volume, mean diameter 87 µm at 97 km/h. | `spray_sim.comp`: two particle classes (big drops plus a mist), CURRENT.md issue 2. |

### B) Windshield: drops on glass, runback, refraction

| Source | Link | Math you can use | Where in Swish |
|---|---|---|---|
| Kaneda, Kagawa & Yamashita, "Animation of Water Droplets on a Glass Plate", Computer Animation '93, Hiroshima Univ. | [PDF](https://home.hiroshima-u.ac.jp/~kin/publications/CA93/wdroplet.pdf) (opened, forms inferred) | Lattice of cells, each with affinity and mass. A drop moves when $m > m_{static}(\theta)$. It steps to one of 3 downhill cells, preferring wet ones, and leaves a residue. Merging: $v = \frac{m_1v_1 + m_2v_2}{m_1+m_2}$. | A wetness-map compute pass for drop trails (today the drops are procedural only). |
| Kaneda, Ikeda & Yamashita, "Animation of Water Droplets Moving Down a Surface", JVCA 10(1), 1999 | [page](https://home.hiroshima-u.ac.jp/kin/publications/JVCA/index.html) (page only) | Adds wipers and an environment-mapped windshield. No equations seen. | Wiper sweeping drops. |
| Kaneda, Zuyama, Yamashita & Nishita, "Animation of Water Droplet Flow on Curved Surfaces", PG '96 | [page](https://home.hiroshima-u.ac.jp/kin/publications/PG96/droplet.html) (PostScript only) | Lattice on curved meshes with a random meander rule. | Curved glass UVs. |
| Nakata, Kakimoto & Nishita, "Real-time animation of water droplets on a hydrophobic windshield", IPSJ 2012, Univ. of Tokyo | [PDF](https://ipsj.ixsq.nii.ac.jp/record/82626/files/IPSJ-CG12147005.pdf) (opened, Japanese with English abstract) | Pinning $F_{hys} = \tfrac12\pi\gamma_L(\cos\theta_r - \cos\theta_a)$, drag $\tfrac12\rho C_D S V^2$. Below about 7 m/s air speed drops run down, 7–11 m/s they stall, above 11 m/s they run up the glass. | Runback direction versus car speed in `windshield_rain.frag`. |
| Furmidge, J. Colloid Sci. 17, 1962 | DOI 10.1016/0095-8522(62)90011-9 (**not opened**) | Slides when $mg\sin\alpha > k\,w\,\gamma(\cos\theta_r - \cos\theta_a)$. | The static versus sliding drop threshold. |
| Milne & Amirfazli, "Drop Shedding by Shear Flow", Langmuir 25(24), 2009, Univ. of Alberta | DOI only; [thesis page](https://era.library.ualberta.ca/items/454b5aa8-b6ab-4bfe-8097-abe585914cf7) | The critical air speed rises with the drop's base length over its frontal area. **Coefficients unverified.** | Bigger drops shed first at speed. |
| Behroozi & Behroozi, "Contact Angle from the Height and Volume of Sessile Drops", arXiv 2018 | [arXiv](https://arxiv.org/pdf/1808.02206) (opened) | Spherical cap, valid while $\rho g R^2/\sigma \ll 1$: $h = a\tan(\theta/2)$, $V = \frac{\pi}{3}R^3(1-\cos\theta)^2(2+\cos\theta)$. | The drop height profile, which gives the refraction normal. |
| Stuppacher & Supan, "Rendering of Water Drops in Real-Time", CESCG 2007, FH Hagenberg | [page](http://old.cescg.org/CESCG-2007/papers/Hagenberg-Stuppacher-Ines/index.htm) (opened) | Water-mass height map, blurred, then turned into normals, with Fresnel and Snell refraction. | The academic version of today's "Heartfelt" drops. |

### What the reference photos say (your 5 images, 2026-11-20)

1. **Haze dominates.** Everything beyond about 100–200 m washes to flat grey. This matters more than the streaks.
2. **No sun.** A flat, bright-grey sky that is brightest at the horizon and casts no shadows.
3. **Road** is mid-grey and mirror-wet, with long vertical red tail-light streaks (SSR plus wet specular) and mist behind cars.
4. **Falling streaks are almost invisible.** They read as haze, which matches Garg–Nayar's $\tau/T$.
5. **Windshield:** dense small round beads with dark refracted rims, horizontal smears where the wiper passed, and the scene behind blurred.

## Rain look pass (2026-11-20, second round)

What changed (details in the changelog, "Rain look pass"):

- **Pink cast fixed (both builds).** The AgX matrices in `composite.frag` were transposed. Grey 0.18 now maps to
  (55,55,55) instead of (62,51,51). The release image changes on purpose (golden line updated).
- **Debug was always golden hour.** `time_of_day.toml` was never copied next to the binary. Fixed in CMake.
- **Rain means overcast.** With the procedural sky, rain pushes cloud cover to 1 and clarity to 0, and lowers the EV clamp by 4 stops.
- **Fog** is 150 m to 63% at full wetness (`fogMax` 0.9). **Wet gloss** is 0.35 in debug (release keeps 1.0).
- **Windshield beads** use 2.3× refraction plus a dark rim (debug only). **Splash rings** are 8 cm, half weight, and lifted 1.5 cm off the road.
- New HDRIs in the debug "Environment HDRI" list: **Mud Road (overcast rain)** and **Misty Morning (heavy rain haze)**.

### Your todo, next round

- [ ] `make debug`, press **R** twice, wait about 20 s for the wetness to build. Compare against your 5 photos.
- [ ] Try the HDRI path: untick "Procedural sky", then pick Mud Road or Misty Morning under Environment HDRI.
      Decide which sky you prefer for rain. The procedural overcast is flat by design; the HDRIs have cloud structure.
- [ ] **Road is still too dark and not mirror-like enough.** Reference 2 shows lane lines and sky reflected. Knobs:
      `Puddle coverage`, SSR `Intensity`, `Wet roughness` (try 0.2). The next code step is a Lagarde-style puddle
      flood level and ripple normal (see the sources above).
- [ ] **Cabin reads near black.** The auto-exposure meters the bright windshield. Try `evCompensation` +0.5, or
      decide that it is right (your photos also show a dark dash).
- [ ] **No traffic tail lights to reflect.** The red streaks in your photos need NPC cars with lit tail lights on
      the wet road (traffic-engineer plus car lights).
- [ ] Release now loses its pink cast too. Check `make run` once and decide whether `saturation` 1.2 still feels
      right (it may have been compensating for the cast).
- [ ] Commit when happy (nothing is committed).
