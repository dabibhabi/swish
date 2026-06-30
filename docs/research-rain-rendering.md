# Realistic Rain Rendering — Research Brief

> **AI-assisted research artifact.** This brief was compiled with AI (Claude) doing web search and
> URL verification, then reviewed for inclusion. Citations and URLs were cross-checked; items that
> could only be confirmed indirectly are flagged inline. Treat as a curated starting point, not a
> substitute for reading the primary sources.

> Scope: techniques for real-time rainy-night driving rendering, scoped to *swish*'s architecture —
> GPU-instanced billboard streaks, a screen-space persistent windshield wetness map (ping-pong +
> semi-Lagrangian advection), a Heartfelt-style refractive drop-on-glass shader, deferred point-light
> streetlamps, MoltenVK on macOS. The three PDFs already in `References/` (Tatarchuk "Rain" course,
> Garg & Nayar TOG 2006, the GPU rainfall sim) are **not** re-cited except where a distinct sibling
> work is relevant.
>
> Companion brief: [`research-night-scene-realism.md`](research-night-scene-realism.md) covers
> everything *except* the rain particles (wet-road BRDF, many-lights, tone mapping, atmospherics,
> procedural road, motion/camera).

---

## 1. Rain streak appearance & light interaction

- **Garg, K. & Nayar, S. K. — "Detection and Removal of Rain from Videos." IEEE CVPR 2004.**
  - URL: https://www.cs.columbia.edu/CAVE/publications/pdfs/Garg_CVPR04.pdf
  - The precursor to the TOG 2006 streak database. Establishes the two models under all later streak
    work: a *photometric model* (a motion-blurred streak is brighter than its background and, because
    the drop acts as a lens integrating the whole hemisphere, its intensity is roughly independent of
    background) and a *dynamics/correlation model* of falling drops. Streaks are visible in only a
    single frame due to fast motion.
  - **swish:** Justifies why streak billboards should be *additive / brighter-than-background* and
    short-lived, and gives the physical reason streak brightness is largely view/lighting-driven
    rather than scene-driven — directly informs the alpha/emissive blend mode of the streak pass.

- **Garg, K. & Nayar, S. K. — "Photorealistic Rendering of Rain Streaks." ACM TOG 25(3), SIGGRAPH
  2006** (doc id 1141911.1141985). *[Already in `References/` — listed to anchor the database its
  successors use.]* CAVE mirror: https://cave.cs.columbia.edu/old/publications/pdfs/Garg_TOG06.pdf
  - The streak database: drops *oscillate* as they fall, producing speckles, multiple smeared
    highlights, and curved brightness contours within a single motion-blurred streak. Thousands of
    streaks rendered across lighting × viewing directions and stored as a queryable database.

- **von Bernuth, A., Volk, G. & Bringmann, O. — "Simulation of Falling Rain for Robustness Testing
  of Video-Based Surround Sensing Systems." IEEE Intelligent Vehicles Symposium (IV) 2016.**
  - URL (ResearchGate record): https://www.researchgate.net/publication/304010109
  - Models *falling* rain (as opposed to drops on glass) for an automotive camera: parameterized
    intensity, fall angle, drop size and color, with brightness reduction across the frame.
  - **swish:** A modern, automotive-specific parameterization (fall angle, intensity, per-frame
    brightness drop) you can map straight onto streak-emitter uniforms for the cockpit camera; pairs
    with the older Garg–Nayar physics.

- **Halder, S. S., Lalonde, J.-F. & de Charette, R. — "Physics-Based Rendering for Improving
  Robustness to Rain." ICCV 2019.**
  - URL: https://openaccess.thecvf.com/content_ICCV_2019/papers/Halder_Physics-Based_Rendering_for_Improving_Robustness_to_Rain_ICCV_2019_paper.pdf
  - Composites physically-based rain over images using a particle simulator plus a *photometric streak
    renderer* derived from the Garg–Nayar database, accounting for per-streak illumination from scene
    light sources.
  - **swish:** A clean, recent recipe for *light-source-dependent* streak appearance — streaks near a
    streetlamp should be brighter and tinted by it. Useful for coupling the streak pass to the
    deferred point-light list.

---

## 2. Raindrops on glass / windshield

- **Steinrücken, M. ("BigWings") — "Heartfelt." Shadertoy, 2017.**
  - URL: https://www.shadertoy.com/view/ltffzl (page blocks scrapers; the shader is live in-browser,
    authorship confirmed across mirrors). Line-by-line writeup: https://greentec.github.io/rain-drops-en/
  - The canonical procedural rain-on-glass shader and the acknowledged basis for *swish*'s drop-on-glass
    look. Technique: a layer of *static* SDF drops plus *sliding trail* drops; the glass fogs over;
    sliding drops cut clear trails through the fog; drop normals offset the UV lookup into the
    background to fake refraction.
  - **swish:** This *is* the reference implementation for the refractive drop shader. Use it to validate
    trail-cutting-through-fog behaviour and the normal-from-SDF → UV-offset refraction. Reputable ports
    to verify against: HLSL/Unity https://github.com/yumayanagisawa/Unity-Raindrops and GLSL mirror
    https://github.com/sanxincao/shadertoy/blob/master/heartfelt.glsl.

- **von Bernuth, A., Volk, G. & Bringmann, O. — "Rendering Physically Correct Raindrops on
  Windshields for Robustness Verification of Camera-based Object Recognition." IEEE IV, June 2018.**
  - URL: https://www.embedded.uni-tuebingen.de/assets/publications/vonBernuth-Volk-Bringmann_Raindrops.pdf
  - The academic counterpart to Heartfelt: distributes drops on the glass plane in front of the camera
    and **ray-traces** each drop as a refractive lens (including reflections), with physically derived
    geometry. Drop height from contact angle: $h = \tan(\theta/2)\cdot d$, with measured contact angle
    $\theta \approx 87^\circ$ for $r \approx 2\,\text{mm}$ drops.
  - **swish:** Gives the *physical* drop shape and the lens/refraction grounding behind the Heartfelt
    fakery. The contact-angle formula lets you size drop bulge vs radius correctly; the ray-traced
    reference is a ground-truth to compare the screen-space approximation against.

- **Buckley, M. (Toadstorm) — "VR Conservatory Part 1: Rainy Glass Shader" + RainyGlassShader (Unity).**
  - Article: https://www.toadstorm.com/blog/?p=742 — Code: https://github.com/toadstorm/RainyGlassShader
  - A production write-up of a cheap, plausible rain-on-glass effect: accumulate a drop/heightmap,
    compute its normal, displace screen-texture UVs for fake refraction, custom SDF for rounded drops.
    Explicitly notes that simulating every rivulet in real time is too expensive — the trick is an
    *impression* of streaking.
  - **swish:** Practical guidance on the persistent drop-map → normal → UV-offset pipeline, which
    mirrors *swish*'s ping-pong wetness map. Confirms the "don't simulate every rivulet" design choice.

- **Lagarde, S. — "Water drop 2a/2b – Dynamic rain and its effects." Dontnod / *Remember Me*, 2013.**
  - URL: https://seblagarde.wordpress.com/2013/01/03/water-drop-2b-dynamic-rain-and-its-effects/
  - Covers dynamic rain layered onto surfaces and camera/glass: drop accumulation, sliding, and the
    visual cues that sell wetness.
  - **swish:** A production checklist for which dynamic drop behaviours matter most visually, so you
    can prioritise what the windshield shader animates.

---

## 3. GPU rain particle systems & parallax layering, billboards, LOD

- **Tariq, S. — "Rain" (NVIDIA Direct3D 10 SDK Whitepaper). NVIDIA, 2007.** *[indirect — PDF
  binary-only on automated fetch]*
  - URL: https://developer.download.nvidia.com/SDK/10/direct3d/Source/rain/doc/RainSDKWhitePaper.pdf
  - The reference GPU-particle rain pipeline: particles advanced on-GPU via stream-out, expanded to
    **view-oriented billboards in the geometry shader**, textured by sampling the **Garg–Nayar streak
    array indexed by light/view direction**, with wind and scene integration.
  - **swish:** The closest published analogue to *swish*'s GPU-instanced billboards. Even without a
    geometry shader (MoltenVK favours instancing/compute), the *view-aligning billboard + streak-
    texture-indexed-by-direction* contract is exactly the pattern to replicate; gives a concrete
    LOD-by-distance and density story.

- **Wang, N. & Wade, B. — "Rendering Falling Rain and Snow." ACM SIGGRAPH 2004 Technical Sketches**
  (Microsoft Flight Simulator 2004).
  - URL: https://history.siggraph.org/wp-content/uploads/2022/12/2004-Talks-Wang_Rendering-Falling-Rain-and-Snow.pdf
  - The origin of the *double-cone / parallax-layer* trick: several rain textures scrolled at different
    speeds on a double cone centred on the camera, scaled down per layer so distant layers parallax
    correctly. Cheap, scales to any density.
  - **swish:** Directly validates *swish*'s parallax/double-layer streak concept and gives the per-layer
    scale-factor recipe for the depth illusion — a near-free far-field complement to the instanced
    near-field streaks.

- **Creus, C. & Patow, G. A. — "R4: Realistic Rain Rendering in Realtime." Computers & Graphics
  37(1–2), Feb 2013.**
  - Record: https://www.sciencedirect.com/science/article/abs/pii/S0097849312001781 — Open PDF (thesis):
    https://upcommons.upc.edu/handle/2099.1/11303
  - Full GPU pipeline: particles simulated entirely on hardware with scene collision (enabling
    splashes), adaptive sampling of the storm region around the observer, precomputed streak images
    combined for complex illumination, plus **fog, halos, and light glows** as participating-media cues.
  - **swish:** The single best end-to-end blueprint to mirror: streaks + splashes + adaptive density
    around the camera + halos in one budget. The adaptive "only simulate near the observer" idea is a
    strong LOD strategy for the cockpit view.

- **Rousseau, P., Jolivet, V. & Ghazanfarpour, D. — "Realistic Real-Time Rain Rendering." Computers &
  Graphics 30(4), 2006.**
  - URL: https://www.sciencedirect.com/science/article/abs/pii/S0097849306000859 (in-repo mirror cited
    in [`rain/README.md`](rain/README.md))
  - Renders each raindrop as a refractive-and-reflective particle that captures the scene behind it
    (precomputed environment lookup), giving per-drop optical realism at interactive rates.
  - **swish:** If you want falling streaks (not just glass drops) to *refract* streetlamps, this is the
    per-particle optics model — bridges §1 and §2 for the near-field streaks closest to the camera.

---

## 4. Wet surfaces & reflections (wet-road BRDF, puddles, ripples)

> Cross-references the companion night-scene brief §2; included here for the rain-physics angle.

- **Jensen, H. W., Legakis, J. & Dorsey, J. — "Rendering of Wet Materials." Eurographics Rendering
  Workshop (EGWR) 1999.**
  - URL: http://graphics.ucsd.edu/~henrik/papers/egwr99/rendering_wet_materials_egwr99.pdf
  - The foundational physical account of why wet surfaces look darker and glossier: water fills surface
    pores/microgeometry, replacing rough diffuse reflection with near-specular water reflection and
    trapping light via total internal reflection.
  - **swish:** The physics behind wet-road darkening + increased specularity. Grounds wet-road material
    parameters and explains the "darker + shinier" target for asphalt under the streetlamps.

- **Lagarde, S. — "Water drop" series, esp. "3a/3b – Physically Based Wet Surfaces" and "1 – Observe
  Rainy World." 2012–2013.**
  - 3b: https://seblagarde.wordpress.com/2013/04/14/water-drop-3b-physically-based-wet-surfaces/ —
    series index: https://seblagarde.wordpress.com/tag/physically-based-wet-surfaces/
  - The production PBR recipe used across AC3/Crysis/MGSV: don't dual-layer — *interpolate dry↔wet BRDF
    in one lighting pass.* Diffuse darkening `Diffuse *= lerp(1.0, porosityFactor, WetLevel)` (factor
    ≈ 0.2–0.68 by porosity); push gloss toward 1.0; blend normal toward the geometric normal
    proportionally to accumulated water height (thin film → puddle).
  - **swish:** The exact, GPU-cheap wet-road shading model to implement in the deferred lighting pass —
    a single `WetLevel`-driven lerp on diffuse/roughness/normal, with the streetlamp point lights
    producing the stretched specular highlights Lagarde calls out for night scenes.

- **fxguide — "Game Environments – Part C: Making Wet Environments."**
  - URL: https://www.fxguide.com/fxfeatured/game-environments-partc/
  - A practitioner survey of how shipped games build wet roads/puddles: layered wetness masks, puddle
    accumulation by height, ripple normal maps, reflective specular tuning.
  - **swish:** A grounded checklist of which wet-environment features read on screen and how studios
    stage them.

- **(Practical) Cyanilux — "Rain Effects Breakdown"** (https://www.cyanilux.com/tutorials/rain-effects-breakdown/)
  and **DeepSpaceBanana — "Rainy Surface Shader Part 1: Ripples"**
  (https://deepspacebanana.github.io/blog/shader/art/unreal%20engine/Rainy-Surface-Shader-Part-1).
  - Concrete shader recipes for animated ripple normal maps (Normal-From-Height), flipbook ripples,
    puddle masks driven by a flood/height variable.
  - **swish:** Drop-in techniques for road ripples and puddle masks layered on the Lagarde wet-BRDF —
    ready to translate to GLSL.

---

## 5. Atmospheric scattering / rain veils / volumetric light shafts

> The night-scene brief §4 covers froxel fog / god-rays in depth. Here, the rain-specific framing.

- **Narasimhan, S. G. & Nayar, S. K. — "Vision and the Atmosphere." IJCV 48(3), 233–254, 2002.**
  - URL: http://www.cs.cmu.edu/~ILIM/publications/PDFs/NN-IJCV02.pdf
  - The standard physically-based model of *attenuation + airlight* through participating media (fog,
    haze, rain): scene radiance is exponentially attenuated by distance while scattered ambient
    "airlight" is added in.
  - **swish:** The math for the rain *veil* — distance-graded attenuation + airlight that makes far
    streetlamps wash out and fade. A few lines in the deferred composite, driven by depth.

- **Mitchell, K. — "Volumetric Light Scattering as a Post-Process." GPU Gems 3, Ch. 13, NVIDIA, 2008.**
  - URL: https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process
  - The classic cheap screen-space "god rays": radial blur of bright pixels away from a light's screen
    position to fake light shafts/halos.
  - **swish:** A near-free way to add *halos around streetlamps and headlight cones* as a post-process
    over the deferred result — MoltenVK-friendly (a fullscreen pass), no froxel volume needed.

- **"Volumetric lighting" (froxel-grid single-scattering survey; anisotropy / phase $g$).**
  - URL: https://en.wikipedia.org/wiki/Volumetric_lighting
  - Summarises the modern real-time approach: a froxel volume accumulating in-scattering per light with
    a phase function (forward-scattering $g>0$ for fog/rain).
  - **swish:** The upgrade path if post-process god-rays aren't enough — a small froxel volume with a
    forward Mie-like phase term gives true volumetric headlight/streetlamp cones in rain. Heavier;
    reach for it only if MoltenVK perf allows.

---

## 6. Photorealistic rain in games / film (course notes, GDC, postmortems)

- **Tatarchuk, N. — "Artist-Directable Real-Time Rain Rendering in City Environments." SIGGRAPH 2006
  Course "Advanced Real-Time Rendering in 3D Graphics and Games," Ch. 3 (ATI).** *[indirect — verify
  vs the repo's existing Tatarchuk PDF before adding]*
  - URL: https://advances.realtimerendering.com/s2006/Chapter3-Artist-Directable_Real-Time_Rain_Rendering_in_City_Environments.pdf
  - The course-notes version of the view-aligned-streak-billboard approach with the Garg–Nayar
    database, plus splashes, misty fog, and light glow, designed for artist control in a city scene.
  - **swish:** The most directly applicable single document — a city-at-night, artist-directable
    streak+splash+glow system that is essentially *swish*'s target feature set.

- **Ikeda, Y. (Sony / Japan Studio) — "Come Rain or Shine: A *Rain* Postmortem." GDC 2014.**
  - URL: https://gdcvault.com/play/1020810/Come-Rain-or-Shine-Rain — write-up:
    https://www.gamedeveloper.com/design/video-come-rain-or-shine----a-i-rain-i-postmortem
  - Production postmortem of the PS3 game *Rain*, where rain is both art-direction and core mechanic.
    Strong on the *aesthetic* of a rainy night and conveying wetness/visibility.
  - **swish:** Art-direction reference for rainy-night mood and readability — tuning streak density,
    contrast, and where the player's eye should land in the cockpit view. *[Vault may require login.]*

- **SIGGRAPH "Advances in Real-Time Rendering in Games" — course archive** (RDR2 sky/fog/volumetrics,
  *Ghost of Tsushima*, inFAMOUS: Second Son postmortem).
  - Index: https://advances.realtimerendering.com/ — S2021 GoT:
    https://advances.realtimerendering.com/s2021/jpatry_advances2021/index.html
  - Shipped-game volumetrics, atmosphere, and wet-weather rendering from Rockstar/Sucker Punch.
  - **swish:** Mine these for production-proven volumetric-fog and night-atmosphere parameters; the
    Sucker Punch (inFAMOUS) and Rockstar (RDR2) talks are the most relevant to wet urban night scenes.

---

## 7. University course slides / lecture notes (participating media, real-time water)

- **UC San Diego CSE 168 — "Volumetric Rendering" (Lecture 15), Spring 2024.**
  - URL: https://cseweb.ucsd.edu/~viscomp/classes/cse168/sp24/lectures/168-lecture15.pdf
  - Participating media: emission/absorption/scattering, the phase function, the volume-rendering
    equation.
  - **swish:** Reference for getting the signs and terms right (extinction vs in-scattering, phase
    function) when implementing the rain veil / volumetric lamp halos.

- **University of Washington ARCH 481 — "Atmospherics: Night, Rain" handout.**
  - URL: http://courses.washington.edu/arch481/00HomePage/4.Miscellaneous/0.Handouts/atmospherics.night%20rain.pdf
  - A course handout repackaging the SIGGRAPH advanced-rendering material specifically on **night +
    rain atmospherics** — a slide-form version of the Tatarchuk/atmosphere content aimed at exactly
    *swish*'s scenario.
  - **swish:** The single most on-topic lecture artifact found.

- **RPI Advanced Computer Graphics — "Volumetric Rendering of Participating Media" (student project
  report, Gemmell & Maicus, S17).**
  - URL: https://www.cs.rpi.edu/~cutler/classes/advancedgraphics/S17/final_projects/alec_evan.pdf
  - A worked implementation report on participating-media rendering from a graduate course.
  - **swish:** A concrete worked example (with pitfalls) of going from the volume-rendering equation to
    code — a sanity check for the froxel/halo path.

---

## Top 8 actionable techniques for *swish*, ranked

1. **Lagarde dry↔wet single-pass BRDF lerp for the road** (§4) — highest ROI, lowest cost: one
   `WetLevel`-driven `lerp` on diffuse/roughness/normal in the existing deferred lighting pass sells
   wet asphalt and the stretched streetlamp highlights that define a rainy night.
2. **Heartfelt-style SDF drops + fog-trail-cutting + normal-driven UV refraction on the windshield**
   (§2) — already *swish*'s reference; lock its behaviour down and validate against the Toadstorm/Unity
   ports.
3. **Light-source-dependent streak shading via the Garg–Nayar photometric model** (§1) — make streak
   brightness/tint depend on the deferred point-light list so streaks ignite near streetlamps.
   (Halder ICCV 2019.)
4. **Wang–Wade double-cone / parallax far-field rain layer** (§3) behind the instanced near-field
   streaks — near-free depth illusion complementing *swish*'s existing parallax layer.
5. **Mitchell post-process volumetric light scattering for streetlamp halos / headlight cones** (§5) —
   fullscreen radial-blur pass, MoltenVK-friendly, no volume needed.
6. **Narasimhan–Nayar attenuation + airlight rain veil** in the deferred composite (§5) — a
   depth-driven few-line addition that fades distant lamps and unifies the atmosphere.
7. **Tariq/NVIDIA view-aligned-billboard + streak-texture-indexed-by-direction contract** for the GPU
   streaks (§3) — adopt the billboard-orientation pattern even using instancing/compute on MoltenVK.
8. **R4 as the end-to-end integration blueprint** and its adaptive near-observer density (§3) — budget
   streaks + splashes + halos together; concentrate simulation around the cockpit camera as LOD.

---

## Flagged / could-not-fully-verify

- **Tatarchuk S2006 Ch. 3** (§6): URL resolves and serves the PDF; content confirmed via ResearchGate,
  but **check it is not the same file already in `References/`** before adding.
- **Garg–Nayar TOG'06, Tariq NVIDIA whitepaper, von Bernuth 2018**: URLs resolve and serve the
  documents; the automated fetch summariser couldn't parse the compressed PDF text, so summaries rely
  on cross-confirmed search metadata.
- **GDC Vault "Come Rain or Shine"** (§6): page confirmed, video likely requires a GDC Vault login.
