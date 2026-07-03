# Learning path — graphics algorithms · GPU methods · the math behind Swish

A study map, not a solutions manual. For each domain: **what you've already built** (the math you've
touched), **next to learn** (concept · why-it-matters-here · a resource · a question to chew on), and a
**stop line** where the technique tips into overkill for this project. Everything here already has a home
in the [roadmap](../tasks/todo.md) or the [concepts study aid](concepts.md).

> How to use this: pick one "next" item, read the resource, then try to answer its question *by hand or in
> a scratch shader* before touching engine code. The question is the point — the wow follows the intuition.

---

## Cross-cutting math (shows up in all four domains)

| Technique | Why it matters here | Resource | Question to chew on |
|---|---|---|---|
| **ODE integrators** — explicit Euler vs semi-implicit (symplectic) Euler vs RK4; stiffness & stability | your car physics + rain drops are ODEs; the wrong integrator explodes or drifts | Gaffer on Games, *Integration Basics*; Hairer & Wanner | why does semi-implicit Euler keep a spring *bounded* where explicit Euler blows up? |
| **Exponential relaxation** — $\dot x = (x_\text{target}-x)/\tau$ | your auto-exposure + wetness already ARE this ODE | Strogatz, *Nonlinear Dynamics* ch.2 | derive the discrete form $x \mathrel{+}= (x_\text{target}-x)(1-e^{-\Delta t/\tau})$ — where does the $e$ come from? |
| **Monte Carlo & quasi-MC** — variance reduction, low-discrepancy (Halton/Sobol), blue noise, importance sampling | every AO/SSR/IBL sample is an integral estimate | PBRT ch.13 (free at pbr-book.org) | why does blue noise *look* cleaner than white noise at equal sample count? |
| **Spherical harmonics** — projecting functions on the sphere | 9 SH coefficients ≈ your entire diffuse sky ambient | Ramamoorthi & Hanrahan 2001, *An Efficient Representation for Irradiance* | why are 9 coefficients "enough" for diffuse but hopeless for a mirror? |
| **Signed distance fields** — primitives + smooth-min | your wiper is SDF-adjacent; SDFs give cheap soft shadows / AO | iquilezles.org (Íñigo Quílez) | how would an SDF produce a soft shadow with *one* ray instead of many? |
| **Splines & arc-length** — Bézier / Catmull-Rom / clothoids; Frenet frame | placing lamps/signs *along* a curved road (B1/B4) | Freya Holmér, *The Continuity of Splines* (YouTube) | reparameterising a curve by arc length is itself an ODE — why? |

---

## 🌧 Rain  — *have: terminal velocity, Marshall–Palmer DSD, Voronoi drops, semi-Lagrangian advection*

- **Drop dynamics ODE** — $\dot v = g - k v^2$ (Riccati form; its steady state is the terminal velocity you already use). **Next:** integrate the *transient* with symplectic Euler and watch it settle.
- **Water on glass** — rivulet flow, surface tension, contact angle (von Bernuth $h=\tan(\theta/2)\,d$). Your semi-Lagrangian advection is the *right* level. **Stop line:** full Navier–Stokes for a windshield is overkill.
- **Ripples** — Gerstner waves (sum of sinusoids, dispersion $\omega^2=gk$) is the non-overkill puddle ripple (roadmap E4). **Stop line:** FFT ocean spectra (Tessendorf) — beautiful, overkill here.
- **Optics** — Snell + Fresnel; normal-from-heightfield via finite-difference gradient. Resource: Garg & Nayar (rain appearance), Rousseau (refractive drops).
- ❓ *How do you turn a height field into a refraction offset without ever storing a normal map?*

## 🏎 Car  — *have: dynamic bicycle model, saturating tire, quaternion steering*

- **Coupled vehicle ODEs** — lateral velocity + yaw rate (you have this). Tire models on a ladder: linear → brush → **Pacejka "magic formula"**. Resource: Beckman, *The Physics of Racing*; Pacejka, *Tyre and Vehicle Dynamics*.
- **Suspension = damped harmonic oscillator** — $m\ddot x + c\dot x + kx = F$ → weight transfer under brake/turn (feeds the FOV-kick idea, F1). **Next:** why stiff springs force semi-implicit/RK4.
- **Orientation** — quaternions + SLERP (you use quats for the wheel); Ackermann steering geometry. **Stop line:** full multibody dynamics / FEM tire carcass.
- ❓ *What makes a spring "stiff," and why does that specific property break explicit Euler?*

## 🛣 Road  — *have: straight centerline, lane/lamp placement, 4.2 km LIE*

- **Curves** — clothoid centerline (B1) → **Fresnel integrals** (curvature linear in arc length); Catmull-Rom through control points; Frenet frame to orient roadside objects.
- **Procedural detail** — Perlin/Simplex/value noise, **fBm**, domain warping, Worley/Voronoi; L-systems / Parish–Müller for road networks (B4). Resource: *Texturing & Modeling: A Procedural Approach*; iq's noise articles.
- **LOD** — screen-space projected error, geomorphing, billboard impostors (B2). **Stop line:** full OpenDRIVE import / Nanite-style virtual geometry.
- ❓ *Given a clothoid, how do you place a lamp every 40 m **along the curve** rather than every 40 m in X?*

## 💡 Light  — *have: deferred GGX PBR, split-sum IBL, CSM+PCF, SSAO, SSR, AgX, auto-exposure*

- **Radiometry & the rendering equation** — Cook-Torrance = GGX NDF + Smith geometry + Fresnel-Schlick; **multiscatter energy compensation** (Kulla–Conty, roadmap E1). Resource: PBRT; Lagarde & de Rousiers, *Moving Frostbite to PBR* (2014).
- **IBL, deeper** — split-sum (Karis, *Real Shading in UE4* 2013) → SH diffuse + prefiltered specular + BRDF LUT + GGX importance sampling.
- **Shadows** — PCF (have) → **PCSS** (penumbra ∝ blocker distance) → variance/moment shadow maps.
- **Volumetrics** — radiative-transfer equation; Beer–Lambert; **phase functions** (Henyey–Greenstein $g$, Rayleigh/Mie); ray-march transmittance; froxel front-to-back integration. Your **height fog (D1) is the closed-form solution of the transmittance ODE $\frac{dT}{ds} = -\beta(s)\,T$** along the ray — one elegant integral. Resource: Hillaire & Wronski volumetric-fog talks; Bruneton atmosphere.
- **GI** — hemisphere AO integral (Monte Carlo) → SSGI (one bounce) → DDGI irradiance probes (SH / octahedral encoding), roadmap H1.
- **Tone & colour** — log-average / histogram exposure; eye-adaptation ODE (have); AgX; von Kries white balance.
- **Stop line:** full path tracing / ReSTIR / spectral rendering.
- ❓ *Height fog is $\int e^{-\int \beta}$. Why does making $\beta$ depend **only on altitude** collapse that double integral into a per-pixel closed form?*

---

## Canonical shelf

- **Books** — *Real-Time Rendering* 4e (the reference); *Physically Based Rendering* (free, pbr-book.org); *GPU Gems* 1–3 (free, NVIDIA); *GPU Pro* / *GPU Zen* series.
- **Talks / blogs** — Karis (UE4 shading), Lagarde (Frostbite PBR + wet surfaces), Jimenez (*Next-Gen Post Processing*, bloom), Hillaire (sky/volumetrics), McGuire & Mara (SSR, motion blur).
- **Math intuition** — 3Blue1Brown; Freya Holmér (splines/vectors); Strogatz (ODEs / nonlinear dynamics); Gaffer on Games (game-physics integration).
- **Hands-on** — LearnOpenGL, Catlike Coding, ScratchAPixel, iquilezles.org, Sebastian Lague (YouTube).
