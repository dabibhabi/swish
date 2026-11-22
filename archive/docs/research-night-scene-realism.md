# Night-Driving Scene Visual Realism — Research Brief

> **AI-assisted research artifact.** This brief was compiled with AI (Claude) doing web search and URL
> verification, then reviewed for inclusion. Citations and URLs were cross-checked; items confirmed
> only indirectly (paywalls, bot-blocks, cert quirks) are flagged inline and in the Verification
> Summary. Treat as a curated starting point, not a substitute for reading the primary sources.

> Scope: Everything that sells a rainy **night** highway scene in *swish* **except the rain particles themselves** (covered by [`research-rain-rendering.md`](research-rain-rendering.md)). Target engine: deferred Vulkan renderer (G-buffer + lighting pass), point-light streetlamps, PBR-ish materials, MoltenVK on macOS, config-baked procedural road (lanes, lamps, signs, exit ramp), cockpit camera, Long Island Expressway (I-495).
>
> Each entry gives a full citation, a verified stable URL, a short technique summary, and a concrete *"How this applies to swish"* note. Verification flags (paywalls, bot-blocks, cert issues) are called out per source and consolidated at the end. URLs were checked programmatically; where an automated fetch was blocked but the resource is genuinely live, that is stated explicitly.

---

## 1. Night/Headlight & Streetlight Rendering

- **Olsson, O., Billeter, M., & Assarsson, U. — "Clustered Deferred and Forward Shading." Proc. ACM SIGGRAPH/Eurographics High-Performance Graphics (HPG '12), 2012. DOI 10.2312/EGGH/HPG12/087-096.**
  - URL (verified, author-hosted Chalmers preprint, 7.2 MB PDF): https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf — canonical HPG copy: https://www.highperformancegraphics.org/previous/www_2012/media/Papers/HPG2012_Papers_Olsson.pdf
  - View samples are grouped into 3D **clusters** in view space (a froxel grid binning by screen tile × exponential depth slice), then a per-cluster light list is built so each shaded sample iterates only the lights whose bounding volumes intersect its cluster. Because clusters separate lights along depth as well as screen XY, this bounds shading cost under huge light counts far better than flat tiled deferred, and works for both deferred and forward variants.
  - **swish:** A lit LIE highway has dozens-to-hundreds of streetlamp point lights plus headlights spread across a large depth range down the road — exactly the case where flat tiled culling over-counts. A clustered (froxel) light-assignment compute pass before the Vulkan lighting pass keeps per-pixel light loops short; this is the recommended many-lights architecture for the scene.

- **Harada, T., McKee, J., & Yang, J. C. — "Forward+: Bringing Deferred Lighting to the Next Level." Eurographics 2012 Short Papers, 2012.**
  - URL (verified, author-hosted, 7.3 MB PDF): https://takahiroharada.wordpress.com/wp-content/uploads/2015/04/forward_plus.pdf
  - Forward+ (tiled forward) adds a GPU compute light-culling pass that, per screen tile, produces a list of lights overlapping that tile; the forward shading pass then reads only its tile's light list. This keeps deferred-style many-light scalability while removing deferred's material/lighting-model restrictions and fat G-buffer bandwidth, and it natively supports MSAA and transparency.
  - **swish:** The counterpoint architecture to the current deferred pass — relevant for transparent/blended elements that do not fit deferred cleanly (windshield glass, rain streaks, light halos). A Forward+ tile light list lets wet glass and particle layers be lit by the same streetlamp set without a second lighting model.

- **Lagarde, S., & de Rousiers, C. — "Moving Frostbite to Physically Based Rendering 3.0." SIGGRAPH 2014 Course: *Physically Based Shading in Theory and Practice* (course notes v3), 2014.**
  - URL (verified; hosts v3 course-notes PDF, slides, Mathematica notebooks): https://seblagarde.wordpress.com/2015/07/14/siggraph-2014-moving-frostbite-to-physically-based-rendering/
  - A production reference defining physically based light units (luminous power in lumens, illuminance in lux, luminous intensity in candela, luminance in cd/m² = nits) and how to drive point/spot/area lights from them. §4.5 covers **photometric (IES) lights** — sampling an IES candela profile to modulate intensity by direction — and specifies a **range-windowed inverse-square attenuation** that smoothly drives falloff to zero at a light's max range so culling bounds stay tight.
  - **swish:** Gives a correct lighting unit system so a streetlamp is authored in candela/lumens and headlights as an IES cone. The windowed falloff gives each LIE streetlamp a finite bounding sphere for the clustered/tiled cull above, avoiding the never-quite-zero tail of pure $1/d^2$:
  $$\text{atten}(d) = \frac{1}{d^2}\cdot\left(\operatorname{saturate}\!\left(1-\left(\tfrac{d}{r_{\max}}\right)^4\right)\right)^2,\qquad E = \frac{I(\theta,\phi)}{d^2}$$
  where $I(\theta,\phi)$ comes from the IES candela table.

- **Jimenez, J. — "Next Generation Post Processing in Call of Duty: Advanced Warfare." SIGGRAPH 2014 Course: *Advances in Real-Time Rendering in Games*, 2014.**
  - URL (verified; hosts slides): https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/ — course index: https://advances.realtimerendering.com/s2014/
  - Describes a stable, high-quality **bloom** built as a downsample/upsample mip pyramid: the HDR buffer is progressively downsampled with a 13-tap weighted (Kawase-/Karis-style partial-average) filter to suppress fireflies, then progressively upsampled with tent filters and additively combined, giving a wide, temporally stable glow at low cost. (Also covers temporally stable shadows, DOF, motion blur, lens dirt — see §3 and §6.)
  - **swish:** The bloom/glare pipeline for emissive streetlamps, headlight halos, and wet-road specular highlights. Firefly suppression is critical because bright point lamps against a dark sky are precisely what flickers; implement as a compute/blit mip-pyramid post pass on the HDR target *before* tone mapping.

---

## 2. Wet Asphalt at Night — Road BRDF, Puddles, Reflections

- **Nakamae, E., Kaneda, K., Okamoto, T., & Nishita, T. — "A Lighting Model Aiming at Drive Simulators." Computer Graphics (Proc. ACM SIGGRAPH '90), 24(4):395–404, 1990. DOI 10.1145/97879.97922.**
  - URL (dblp bibliographic record, verified): https://dblp.org/rec/conf/siggraph/NakamaeKON90.html — *flag:* ACM DL page returns HTTP 403 to bots (paywalled, reachable in browser).
  - The original wet-road-at-night reference. It introduces a road-surface reflection model that accounts for weather (dry vs wet): a thin water film raises the specular component and suppresses diffuse, plus a model for the elongated **streaks of light** from streetlamps reflected on the road incorporating refraction/diffraction. Source of the long-cited empirical rule of attenuating asphalt diffuse albedo and boosting specular when wet.
  - **swish:** Foundational citation for the target look — point-light streetlamps smearing into vertical specular streaks on wet asphalt at night. Justifies the wet-road material parameterization in the G-buffer (lower roughness + darkened albedo for wet road tiles); the elongated reflection streaks are exactly the perceptual cue the lighting pass should reproduce.

- **Lagarde, S. — "Water drop 3a / 3b — Physically Based Wet Surfaces." Personal blog, Mar 19 & Apr 14, 2013.**
  - URLs (both verified): 3a https://seblagarde.wordpress.com/2013/03/19/water-drop-3a-physically-based-wet-surfaces/ · 3b https://seblagarde.wordpress.com/2013/04/14/water-drop-3b-physically-based-wet-surfaces/
  - 3a covers the physics: a water film darkens a surface via total internal reflection within the layer and by water filling pores (IOR 1.33 reduces the air–substrate index gap, increasing forward scatter and subsurface absorption); surveys the Weidlich–Wilkie layered BRDF and Mérillou/Hnat porous BRDF. 3b distills these into a cheap production approximation driven by a per-texel **porosity** map and a global **WetLevel**: darken diffuse and push roughness toward smooth/specular, scaled by porosity.
  - **swish:** The directly-implementable wet-asphalt recipe for the deferred pipeline. Bake a porosity/wetness factor for the road and modulate albedo + roughness written into the G-buffer (asphalt porous → strong effect; painted lane lines/metal → little), so the same streetlamp point lights produce sharp specular highlights on the now-smoother wet road.

- **Stachowiak, T. — "Stochastic Screen-Space Reflections." SIGGRAPH 2015 Course: *Advances in Real-Time Rendering in Games* (EA/Frostbite), 2015.**
  - URL (verified; course index lists slides + video): https://advances.realtimerendering.com/s2015/ — Frostbite mirror: https://www.ea.com/frostbite/news/stochastic-screen-space-reflections
  - Glossy screen-space reflections in real time via Monte Carlo importance sampling of the microfacet BRDF: ray-trace at half resolution, reuse neighboring pixels' rays, reconstruct full-res with spatial + temporal filtering to denoise. Reproduces specular elongation and contact hardening across spatially-varying roughness; shipped in Mirror's Edge Catalyst and Need for Speed.
  - **swish:** The wet road is glossy-but-not-mirror — exactly this technique's target. SSR over the G-buffer lets wet asphalt reflect on-screen streetlamps and the car's own lit surfaces with roughness-aware blur; temporal/spatial reuse keeps cost low (important under MoltenVK). Primary reflection method; fall back where rays leave the screen.

- **McGuire, M., & Mara, M. — "Efficient GPU Screen-Space Ray Tracing." Journal of Computer Graphics Techniques (JCGT), 3(4):73–85, 2014.**
  - URLs (verified): landing https://jcgt.org/published/0003/04/04/ · PDF https://jcgt.org/published/0003/04/04/paper.pdf
  - The de-facto reference screen-space ray-march: adapts perspective-correct DDA line rasterization to march reflection rays in screen space at fixed cost per pixel, with multi-depth-layer support for robustness against thin geometry and missed hits. The tracing primitive underneath most production SSR.
  - **swish:** The concrete ray-march to implement for the SSR pass producing streetlamp reflections on wet road. Pair the McGuire/Mara tracer ("find the hit") with Stachowiak's stochastic BRDF sampling + filtering ("make it glossy and clean") inside the deferred lighting/post pass.

> **Planar reflection note:** No single canonical peer-reviewed "planar road reflection" paper of equal standing was found. The standard approach (mirror the camera across the road plane and re-render to an RTT) is textbook material — see *Real-Time Rendering*, 4th ed., and engine docs — rather than a discrete citable paper. For swish's flat road plane it is worth keeping as the cheaper, artifact-free fallback to SSR, but it is flagged as lacking a strong standalone academic source.

---

## 3. Tone Mapping / Exposure / HDR for Night Scenes

- **Reinhard, E., Stark, M., Shirley, P., & Ferwerda, J. — "Photographic Tone Reproduction for Digital Images." Proc. SIGGRAPH 2002, ACM TOG 21(3):267–276, 2002. (Univ. of Utah Tech. Report UUCS-02-001.)**
  - URL (verified; *flag:* redirects to `www-old` host): https://www-old.cs.utah.edu/docs/techreports/2002/pdf/UUCS-02-001.pdf
  - Maps HDR scene luminance to display range using the photographic Zone System: scale to a "key value" relative to log-average luminance, pass through a global operator $L_d = L/(1+L)$ (optionally with a white point), then optionally refine with a local dodge-and-burn operator preserving local contrast.
  - **swish:** The log-average key gives a simple, robust auto-exposure anchor for a very dark night scene; $L/(1+L)$ is the cheapest baseline tone map to compress bright headlights/lamps without blowing out — a good fallback/comparison operator before committing to a filmic curve.

- **Hable, J. (Naughty Dog) — "Uncharted 2: HDR Lighting." GDC 2010.**
  - URL (verified): https://gdcvault.com/play/1012351/Uncharted-2-HDR — companion write-up: https://filmicworlds.com/blog/filmic-tonemapping-operators/
  - Introduces the "Hable/Uncharted 2" filmic tone curve — a parameterized rational function (toe + linear midsection + shoulder) fit to film response, applied in linear-space lighting with a separate white-scale normalization. Crisp blacks, gently rolled-off highlights; avoids plain Reinhard's washed-out grays while keeping saturated darks.
  - **swish:** The filmic toe deepens shadows and keeps the unlit highway genuinely dark while letting headlights/lamps roll off smoothly into bloom rather than hard-clamping — a strong default tone-map operator for the HDR→LDR resolve.

- **Narkowicz, K. — "ACES Filmic Tone Mapping Curve." Technical blog, 2016.**
  - URL (verified): https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/ — companion: https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
  - A cheap rational-function fit to the ACES RRT+ODT output transform, $(x(ax+b))/(x(cx+d)+e)$ with $a{=}2.51,\,b{=}0.03,\,c{=}2.43,\,d{=}0.59,\,e{=}0.14$, reproducing the ACES filmic look (contrasty, slightly desaturated highlights) in one ALU-light pass. Slightly oversaturates very bright colors; bakes in exposure such that input 1 → ~0.8 output.
  - **swish:** A drop-in single-instruction tone-map for the MoltenVK resolve matching the modern UE4-style look; built-in highlight desaturation helps keep over-bright headlights from clipping to pure white. *Caveat:* practitioner blog, not peer-reviewed — cite as a practitioner reference with the formal ACES system as the authority.

- **Jensen, H. W., Premože, S., Shirley, P., Thompson, W., Ferwerda, J., & Stark, M. — "Night Rendering." Univ. of Utah Tech. Report UUCS-00-016, Aug 2000.**
  - URL (verified mirror, co-author Ferwerda's site): https://jamesferwerda.com/wp-content/uploads/2015/06/t04_jensen00_tr.pdf — *flag:* original UCSD page (http://graphics.ucsd.edu/~henrik/papers/night/) has an incomplete SSL cert chain; ResearchGate copy 403s.
  - A physically based pipeline for night scenes: moonlight/starlight/airglow illumination plus a perceptually-based night tone-reproduction operator reproducing low-light vision — loss of acuity, loss of color (rod-dominated scotopic vision), and the Purkinje blue-shift — with adaptation-driven scaling so a daytime display conveys "night."
  - **swish:** The canonical reference for making swish *read* as night rather than just dim. Apply mesopic desaturation + a subtle blue-shift to low-luminance regions (the road between lamps) while the lit lamp/headlight cones stay color-correct.

- **Kirk, A. G., & O'Brien, J. F. — "Perceptually Based Tone Mapping for Low-Light Conditions." ACM TOG 30(4) (Proc. SIGGRAPH 2011), Art. 42, 2011. (UC Berkeley EECS-2011-91.)**
  - URLs: tech-report listing https://www2.eecs.berkeley.edu/Pubs/TechRpts/2011/EECS-2011-91.html · DBLP https://dblp.org/rec/journals/tog/KirkO11.html · ACM DL https://dl.acm.org/doi/10.1145/1964921.1964937 — *flag:* the `graphics.berkeley.edu` project page has a cert-hostname mismatch; use these alternates.
  - Models the Purkinje color shift of mesopic/scotopic vision by deriving the rod contribution to perceived color and blending it into the cone (XYZ) response as light levels drop, producing loss of saturation and a shift toward blue in dark regions — efficient enough for interactive/real-time use.
  - **swish:** A more recent, real-time-oriented complement to "Night Rendering" — a concrete per-pixel rod/cone blend implementable directly in the tone-map shader so dark stretches of the LIE desaturate and cool toward blue automatically, layered on top of the chosen filmic/ACES curve.

---

## 4. Atmospheric Realism — Fog, Volumetric Light, God Rays

- **Wronski, B. — "Volumetric Fog: Unified Compute Shader-Based Solution to Atmospheric Scattering." SIGGRAPH 2014 Course: *Advances in Real-Time Rendering* (Ubisoft Montreal / Assassin's Creed IV), 2014.**
  - URL (verified; original WordPress link 302s to canonical, valid 5.5 MB PDF): https://bartwronski.com/wp-content/uploads/2014/08/bwronski_volumetric_fog_siggraph2014.pdf — course page: https://advances.realtimerendering.com/s2014/
  - Introduces the **"froxel"** (frustum-aligned voxel) volumetric fog technique. A camera-frustum-shaped 3D texture stores per-voxel scattering/extinction; a compute shader injects per-pixel in-scattered lighting (shadowed point/spot lights + a phase function) into each froxel, then a second pass ray-marches/accumulates along view rays (depth-slice prefix sum) to produce per-froxel integrated scattering + transmittance applied as a single texture lookup. Fully per-pixel-lit fog at constant cost regardless of light count.
  - **swish:** The most directly applicable architecture — build a view-aligned froxel volume and inject in-scattering from each streetlamp point light and the headlight cones, so haze visibly thickens in lamp cones and headlight beams; the froxel texture composites cheaply into the deferred lighting pass on MoltenVK without per-light ray marching.

- **Hillaire, S. — "Physically Based and Unified Volumetric Rendering in Frostbite." SIGGRAPH 2015 Course: *Advances in Real-Time Rendering* (EA/Frostbite), 2015.**
  - URL (verified, official EA page): https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite — course listing (full PPTX): https://advances.realtimerendering.com/s2015/
  - Extends the froxel approach into a physically based, unified framework: media described with real scattering/absorption coefficients + a phase function; multiple volumetric sources (height fog, local fog volumes, particles, light shafts) voxelized into one shared volume; a single integration pass produces consistent scattering and transmittance. Adds temporal reprojection/jittered sampling to denoise the low-res froxel grid, plus volumetric shadowing.
  - **swish:** Gives the physically grounded parameterization + temporal-reprojection denoising needed to keep a low-res night froxel volume cheap yet stable on macOS, and lets streetlamp halos, ground mist, and headlight scatter live in one unified volume so wet-road haze and lamp glow stay energy-consistent rather than hand-tuned per effect.

- **Mitchell, K. — "Volumetric Light Scattering as a Post-Process." GPU Gems 3, Ch. 13, ed. H. Nguyen, Addison-Wesley/NVIDIA, 2007.**
  - URL (verified): https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process
  - A cheap screen-space approximation of crepuscular rays ("god rays"). For each pixel, march a fixed number of samples toward the light's screen-space position, summing an occlusion/brightness buffer with per-step exponential decay/weight/exposure constants, then additively blend the radial-blur result. No precomputation; supports arbitrary scene complexity; only works when the light is on/near screen.
  - **swish:** Lightweight layer for the dominant on-screen light — a streetlamp directly ahead or oncoming headlights — producing radial god-ray streaks through haze at near-zero cost, complementing (not replacing) the froxel fog for hero lights. *Caveat:* gate to in-view lights; it breaks when the lamp leaves the frustum.

- **Tóth, B., & Umenhoffer, T. — "Real-time Volumetric Lighting in Participating Media." Eurographics 2009 Short Papers, 2009. DOI 10.2312/egs.20091048.**
  - URL: https://diglib.eg.org/handle/10.2312/egs.20091048.057-060 — *flag:* EG diglib returns 403/connection-refused to bots (not a dead link); corroborated by Semantic Scholar (paper id d401ec88…) and the DOI.
  - A GPU single-scattering algorithm that ray-marches view rays through participating media, sampling the shadow map at each step to test light visibility and accumulating in-scattered radiance with a phase function. Uses interleaved/dithered sampling (low per-pixel ray counts + a smoothing/bilateral pass) to keep frame rates high while suppressing banding — a single fragment-shader post-process.
  - **swish:** The shadow-map-tested ray-march is the textbook recipe for true volumetric **light shafts behind occluders** (the car body, sign posts, overpasses cutting headlight beams); interleaved sampling is how to afford a few shaft samples per pixel for hero streetlamps without tanking the MoltenVK frame budget.

> **Phase-function note (cross-cutting):** All froxel/ray-march methods above rely on the **Henyey–Greenstein** phase function $p(\theta)=\frac{1}{4\pi}\frac{1-g^2}{(1+g^2-2g\cos\theta)^{3/2}}$ to bias scattering forward (small positive $g \approx 0.2\!-\!0.6$). This is what makes lamp/headlight haze bloom brightest when you look *toward* the light. Defined in Wronski (2014) and Hillaire (2015); no separate source needed.

---

## 5. Procedural Road & Highway Environment Realism

- **Dosovitskiy, A., Ros, G., Codevilla, F., López, A., & Koltun, V. — "CARLA: An Open Urban Driving Simulator." Proc. 1st Conf. on Robot Learning (CoRL 2017), PMLR vol. 78, 2017.**
  - URL (verified): https://proceedings.mlr.press/v78/dosovitskiy17a.html — PDF mirror: https://vladlen.info/papers/carla.pdf
  - An open-source urban driving simulator on Unreal Engine, shipping hand-authored assets and configurable sensor suites + environmental conditions (weather, time of day). Road networks are authored/imported via **ASAM OpenDRIVE** (e.g. via RoadRunner), giving a lane-level logical description the renderer turns into drivable geometry. The canonical reference for how a driving sim structures a navigable, semantically-labeled road scene from a standardized network description.
  - **swish:** Adopt CARLA's separation of *logical road description* (lanes, lamps, signs, exit ramp as a config-baked data layer) from *rendered geometry* — bake the 4 km I-495 layout once into a lane-centric network, then generate mesh/decals/props from it.

- **ASAM e.V. — ASAM OpenDRIVE (road-network description standard). Spec v1.8.0 (2023).**
  - URL (verified): https://www.asam.net/standards/detail/opendrive/ — online spec: https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/
  - An XML format (`.xodr`) describing lane-level road networks via **linear referencing**: every road has a center reference line (lines, arcs, spirals/clothoids, polynomials), and lanes, lane markings, signals, and roadside objects are positioned relative to it via $(s, t)$ coordinates. Lanes carry width profiles, types, and `roadmark` sub-elements; signals carry type + placement. The de-facto interchange standard used by CARLA, RoadRunner, and most AV simulators.
  - **swish:** Use OpenDRIVE's reference-line + $(s,t)$ parameterization as the mental model (or literal config schema) for baking the highway — clothoid/arc segments for the gentle I-495 curves and the exit ramp, with lane widths, dashed/solid marks, and lamp/sign positions all expressed as offsets along arc-length $s$, so one 1-D parameter drives the whole 4 km layout.

- **Galin, E., Peytavie, A., Maréchal, N., & Guérin, É. — "Procedural Generation of Roads." Computer Graphics Forum 29(2):429–438 (Eurographics 2010). DOI 10.1111/j.1467-8659.2009.01612.x.**
  - URL (author-hosted PDF, verified, 9 MB): https://perso.liris.cnrs.fr/egalin/Articles/2010-roads.pdf — *flag:* Wiley DOI paywalled (402); EG diglib handle unreachable at fetch time (transient).
  - Generates a road trajectory as a **weighted anisotropic shortest path** minimizing a cost function over terrain (penalizing slope above a max grade, curvature, and obstacles), then realizes it geometrically by excavating the terrain and instancing parameterized road models (surface, embankments, bridges/tunnels). Cleanly separates path-finding from geometry generation.
  - **swish:** Even though swish's highway is config-baked rather than solved, Galin's cost-function framing justifies the curve constraints — bound curvature and grade so the baked I-495 spline reads as a real highway — and the "excavate + instance parameterized models" pattern is how to sweep the road cross-section (lanes + shoulder + guardrail) along the spline.

- **Parish, Y. I. H., & Müller, P. — "Procedural Modeling of Cities." SIGGRAPH 2001, pp. 301–308, 2001.**
  - URL (verified, ACM DL): https://dl.acm.org/doi/abs/10.1145/383259.383292 — SIGGRAPH history page: https://history.siggraph.org/learning/procedural-modeling-of-cities-by-parish-and-muller/
  - The foundational "CityEngine" paper: an extended L-system grows a road/street network from input image maps (population density, land/water, elevation), then subdivides enclosed blocks into lots and generates buildings. Key insight: *the street network is the primary structure* from which the environment derives — establishing the now-standard network-first pipeline.
  - **swish:** Borrow "network drives everything" in miniature — treat the baked highway centerline as the single source of truth and procedurally derive lane stripes, lamp spacing, sign placement, and the exit-ramp split from it, rather than hand-placing each prop over 4 km.

- **Wronski, B. — "Fixing Screen-Space Deferred Decals." Technical blog, Mar 12, 2015.**
  - URL (verified): https://bartwronski.com/2015/03/12/fixing-screen-space-deferred-decals/ — originating production talk: Pope Kim, "Screen Space Decals in Warhammer 40K: Space Marine," SIGGRAPH 2012.
  - Deferred/screen-space decals project a textured box onto existing geometry by reconstructing world position from the depth buffer, transforming into decal space, and sampling — no mesh prep or material permutations, ideal for tire marks/cracks/road wear. Wronski diagnoses the **edge artifact**: pixels straddling depth discontinuities corrupt screen-space derivatives used for mip selection, causing blurry borders, and evaluates fixes (manual mip-level from depth+normals, normal-based fading to reject back-faces).
  - **swish:** Since swish already runs a deferred G-buffer pipeline, project tire-streak, oil-stain, and lane-wear decals as boxes against the G-buffer depth — and apply the manual mip-level + normal-fade fixes so wet-road decals don't smear along lane edges or bleed onto the guardrail at the grazing night-driving camera angle.

- **Akenine-Möller, T., Haines, E., Hoffman, N., Pesce, A., Iwanicki, M., & Hillaire, S. — *Real-Time Rendering*, 4th ed. Ch. 19 (Acceleration Algorithms / Level of Detail). A K Peters/CRC, 2018. ISBN 978-1138627000.**
  - URL (verified publisher page): https://www.routledge.com/Real-Time-Rendering-Fourth-Edition/Akenine-Moller-Haines-Hoffman/p/book/9781138627000 — *flag:* realtimerendering.com resource site 403s bots but is live in-browser.
  - The standard reference for discrete vs continuous LOD, LOD selection metrics (projected screen coverage / distance), and **geomorphing** (interpolating vertices across LOD transitions to remove popping), plus impostors/billboards and culling — the building blocks for cheaply streaming a long world.
  - **swish:** For a 4 km highway with lamps, signs, and guardrail posts marching to the horizon, use distance-based discrete LOD with geomorphing/alpha-fade on lamp/sign meshes and switch distant posts to billboard impostors, selected by projected screen coverage — dense road up close without paying for thousands of far props at night.

---

## 6. Motion & Camera Realism

- **McGuire, M., Hennessy, P., Bukowski, M., & Osman, B. — "A Reconstruction Filter for Plausible Motion Blur." Proc. ACM SIGGRAPH Symp. on Interactive 3D Graphics and Games (I3D 2012), pp. 135–142. DOI 10.1145/2159616.2159639.**
  - URL (verified): https://casual-effects.com/research/McGuire2012Blur/McGuire12Blur.pdf — record: https://dblp.org/rec/conf/si3d/McGuireHBO12.html
  - The canonical real-time motion-blur reconstruction filter: a 2D full-screen post-process over a color buffer + screen-space per-pixel velocity buffer. It tile-classifies the maximum velocity in each neighborhood, then jitter-samples along velocity vectors with depth- and velocity-aware weighting to plausibly reconstruct blur from both moving objects and a moving camera. Cost is independent of geometric complexity.
  - **swish:** The core algorithm for the cockpit's *camera* motion blur (forward velocity streaking the road, guardrails, oncoming traffic) — drive it from a reprojection-based velocity buffer so static geometry blurs with ego-motion, the single strongest per-frame speed cue at night when only headlights and signs are lit.

- **Jimenez, J. — "Next Generation Post Processing in Call of Duty: Advanced Warfare." SIGGRAPH 2014 Course: *Advances in Real-Time Rendering*, 2014.** *(also cited in §1 for bloom)*
  - URL (verified; slides downloadable): https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
  - A production playbook for a unified, temporally-stable post chain in a 16.6 ms budget: "scatter-as-you-gather" motion blur respecting transparency, a DOF that carries color *and* alpha for correct foreground bleeding, pyramidal bloom, and lens-dirt/lens-flare layering for bright highlights.
  - **swish:** The integration blueprint for the whole post stack — chain motion blur → DOF → bloom coherently, and multiply a lens-dirt mask against the bloom buffer so oncoming headlights and streetlamps smear realistic grime/streaks across the virtual lens, reinforcing the wet, glare-heavy night look.

- **Garcia, K. (Frostbite/EA) — "Circular Separable Convolution Depth of Field." GDC 2018 (short SIGGRAPH 2017 Talks abstract, DOI 10.1145/3084363.3085022).**
  - URL (verified, Frostbite page with slides): https://www.ea.com/frostbite/news/circular-separable-convolution-depth-of-field — GDC Vault: https://www.gdcvault.com/play/1025372/Circular-Separable-Convolution-Depth-of
  - A separable, frequency-domain bokeh DOF: approximate the circular blur kernel with complex-valued (phasor) component filters, factoring a 2D disc convolution into a horizontal then vertical pass over complex buffers — accurate circular bokeh at large radii with low bandwidth. *Citation note:* author is **Kleber Garcia**; full talk is GDC 2018 (the prompt's "Abadie/Garcia SIGGRAPH 2017" conflates a shorter SIGGRAPH Talks abstract).
  - **swish:** Cheap, true-circular bokeh on the cockpit scene — keep the dash/wheel in focus while distant headlights and wet-road specular highlights bloom into clean circular bokeh discs (the cinematic night-driving look), far cheaper than scatter/sprite bokeh on MoltenVK.

- **Sharan, L., Neo, Z. H., Mitchell, K., & Hodgins, J. — "Simulated Motion Blur Does Not Improve Player Experience in Racing Game." Proc. Motion in Games (MIG '13), ACM, 2013. DOI 10.1145/2522628.2522653.**
  - URL (verified, official Disney Research Studios page): https://studios.disneyresearch.com/2013/11/06/simulated-motion-blur-does-not-improve-player-experience-in-racing-game/
  - A controlled user study on *Split/Second: Velocity* measuring lap times and subjective experience (enjoyment, *perceived speed*) with motion blur on vs off. Players reliably *detected* the blur, but neither performance nor perceived speed nor enjoyment changed measurably.
  - **swish:** A design caution — don't rely on motion blur alone to "sell" speed. Budget speed cues across FOV, camera shake, peripheral streaking, and rain-streak velocity rather than over-cranking blur (detectable but not experientially decisive, and it hurts readability of headlights/signs at night).

- **Steinrücken, M. ("BigWings"/The Art of Code) — "Heartfelt" (screen-space raindrops-on-glass shader). Shadertoy, 2017.**
  - URL: https://www.shadertoy.com/view/ltffzl — *flag:* Shadertoy 403s headless fetchers (JS-gated); the shader is community-canonical and live in-browser. Verified line-by-line writeup of the same technique: https://greentec.github.io/rain-drops-en/
  - The reference screen-space raindrop post-process: procedurally generates static + animated falling drops on a glass/lens plane, uses each drop's local surface gradient as a UV-refraction offset to warp the underlying scene, carves clearing trails through a fogged-glass layer where drops slide, composited as a full-screen pass.
  - **swish:** The model for raindrops *on the camera lens* (distinct from the windshield-rain pass) — a final-pass overlay refracting the night scene with smeared headlight bokeh inside each drop, for the visceral "shot from inside the car" feel. Distinguish from windshield rain: lens drops are *not* cleared by the wipers and sit at screen-space, not world-space, depth.

---

## 7. University Course Slides & Lecture Notes

- **Ramamoorthi, R. — CSE 167: Computer Graphics, "Shadow Volumes and Deferred Rendering" (Lecture 16). UC San Diego, Winter 2018.**
  - URL (verified, live 1.5 MB PDF; image-heavy so auto-text-extraction fails but the file downloads cleanly): https://cseweb.ucsd.edu/classes/wi18/cse167-a/lec16.pdf
  - Walks through shadow volumes, then the deferred pipeline: render scene attributes (position/normal/material) into a G-buffer, then compute lighting in a separate screen-space pass so cost scales with screen resolution rather than geometry × lights.
  - **swish:** The cleanest classroom-level statement of exactly swish's architecture — G-buffer fill then deferred lighting pass — useful for sanity-checking attachment layout and the geometry/lighting split.

- **Cozzi, P. / Kider, J. (lineage) — CIS 565: GPU Programming and Architecture, "Deferred and Forward+ Rendering" unit. University of Pennsylvania, Fall 2022.**
  - URL (verified syllabus; individual decks are OneDrive/GitHub-gated so the syllabus is the stable anchor): https://cis565-fall-2022.github.io/syllabus/
  - The advanced-rendering section enumerates the full many-light progression — multi-pass forward, deferred, **tile-based deferred, Forward+, clustered shading** — paired with WebGL demos and a project implementing Forward+ and clustered-deferred.
  - **swish:** Directly maps to the many-streetlamp problem — clustered/tiled light culling is how to avoid every fragment iterating every point light, the key scalability technique for a night scene with many lights.

- **Rotenberg, S. — CSE 168: Rendering Algorithms, "Volumetric Scattering" (Lecture 14). UC San Diego, Spring 2017.**
  - URL (verified, live ~960 KB PDF; image-based slides): https://cseweb.ucsd.edu/classes/sp17/cse168-a/CSE168_14_Volumetric.pdf
  - Covers volumetric/participating-media scattering — absorption, in/out-scattering, phase functions — and references precomputed atmospheric scattering and sky-dome radiance models.
  - **swish:** The phase-function and in-scattering framing is the physical basis for streetlamp light shafts/halos through rain-fog and for tinting the night sky — the atmospheric effects sit on this theory.

- **CSE 168: Computer Graphics II — Rendering, "Volumetric Path Tracing / Participating Media" (Lecture 15). UC San Diego, Spring 2024.**
  - URL (verified, live 4.3 MB PDF; image-based): https://cseweb.ucsd.edu/~viscomp/classes/cse168/sp24/lectures/168-lecture15.pdf
  - A more modern treatment of participating media and volumetric scattering integration than the 2017 deck, from the same UCSD rendering-course lineage.
  - **swish:** A current reference for the scattering integral and transmittance governing fog/haze and volumetric light around streetlamps — useful when deriving the in-scattering accumulation baked into the lighting/atmospheric pass.

> **Considered but not used (verification bar):** TU Wien "Algorithms for Real-Time Graphics" (186.192) teaches deferred + PBR but slides are gated behind TUWEL (no public PDF). Cornell CS 5625 "Deferred shading and postprocessing" (Marschner, 2016) PDF is live but exceeded the fetch size limit and its index returned empty — a strong likely-good alternate if a fifth is wanted. Industry SIGGRAPH course notes (selfshadow.com PBS; Olsson/Billeter/Assarsson clustered-shading SIGGRAPH 2015 course) are excellent and verifiable but are conference, not university, materials.

---

## Top 8 Actionable Techniques for swish — Ranked by Visual-Impact-per-Effort

| # | Technique | Source(s) | Why it's high-leverage for a rainy night LIE | Rough effort |
|---|-----------|-----------|-----------------------------------------------|--------------|
| 1 | **HDR mip-pyramid bloom with firefly suppression** | Jimenez 2014 (§1) | Bright lamps/headlights against a dark sky *demand* glow; this is the single biggest "it looks like night" win and is a self-contained post pass before tone mapping. | Low–Med |
| 2 | **Filmic tone map (Hable or ACES) + log-avg auto-exposure** | Hable 2010, Narkowicz 2016, Reinhard 2002 (§3) | Without a filmic curve the dark road washes out or clips; the toe deepens blacks and rolls highlights into the bloom. One shader, huge mood payoff. | Low |
| 3 | **Wet-asphalt BRDF: darken albedo + drop roughness via porosity/WetLevel** | Lagarde 2013, Nakamae 1990 (§2) | Turns the road from "dim grey" into "wet and reflective" purely by modulating G-buffer channels — reuses existing point lights to produce specular streaks. | Low–Med |
| 4 | **Froxel volumetric fog with per-light in-scattering** | Wronski 2014, Hillaire 2015 (§4) | Haze in every lamp cone + headlight beam is the defining rainy-night atmosphere; froxels give it at constant cost independent of light count. | Med–High |
| 5 | **Clustered (froxel) light culling for many streetlamps** | Olsson et al. 2012; CIS 565 (§1, §7) | Enables dozens-to-hundreds of lamps + headlights down a 4 km road without per-fragment light explosion — the scalability backbone everything else leans on. | Med–High |
| 6 | **Screen-space reflections on the wet road** | Stachowiak 2015, McGuire & Mara 2014 (§2) | Reflected streetlamps/headlights on wet asphalt are the iconic cue; SSR over the existing G-buffer with roughness-aware blur reuses the wet-road material from #3. | Med |
| 7 | **Camera motion blur from a velocity buffer (+ measured speed-cue budget)** | McGuire et al. 2012; Sharan et al. 2013 (§6) | Strongest per-frame speed cue, but the Disney study warns not to over-rely on it — pair with FOV/shake/streak cues; the velocity buffer also feeds SSR/TAA. | Med |
| 8 | **IES-profiled / range-windowed point lights in physical units** | Lagarde & de Rousiers 2014 (§1) | Authoring lamps in candela with a finite range gives correct falloff *and* the tight bounding spheres clustered culling (#5) needs — quality + performance from one change. | Low–Med |

**Honorable mentions (lower priority):** screen-space god rays for the single hero on-screen light (Mitchell 2007) — cheap but gated to in-view lights; deferred road-wear/tire decals (Wronski 2015) for surface detail; mesopic desaturation + Purkinje blue-shift (Jensen 2000, Kirk & O'Brien 2011) to make dark stretches genuinely "read" as night; circular-bokeh DOF (Garcia 2018) for cinematic headlight discs; lens raindrops as a final overlay (Steinrücken 2017), distinct from the windshield pass.

---

## Verification Summary

- **Fully verified, resolve cleanly:** Olsson (Chalmers + HPG), Harada, Lagarde PBR notes + wet-surface 3a/3b, Stachowiak (advances + EA), McGuire & Mara (JCGT), Wronski volumetric fog + decals, Hillaire (EA), Mitchell (NVIDIA), Reinhard (`www-old` Utah), Hable (GDC Vault + filmicworlds), Narkowicz, Jensen "Night Rendering" (Ferwerda mirror), CARLA (PMLR + vladlen), ASAM OpenDRIVE, Galin (author PDF), Parish & Müller (ACM DL), McGuire 2012 (casual-effects + dblp), Jimenez (iryoku), Garcia (EA + GDC Vault), Sharan (Disney Studios), all four UCSD/UPenn course pages.
- **Live but bot-blocked / cert-quirked (content reachable in a normal browser — use the noted alternate):** ACM DL pages (Nakamae, Sharan) 403; EG diglib (Tóth & Umenhoffer, Galin handle, Olsson mirror) 403/refused; UCSD "Night Rendering" page incomplete SSL chain; Berkeley `graphics.berkeley.edu` Kirk & O'Brien cert-hostname mismatch; realtimerendering.com 403; Shadertoy "Heartfelt" JS-gated 403.
- **Not peer-reviewed (flagged as practitioner references):** Narkowicz ACES fit (cite the formal ACES system as authority); Lagarde, Wronski, Hable, Garcia, Stachowiak, Hillaire, Jimenez, Mitchell are industry course notes / GDC talks / engine writeups rather than refereed papers.
- **Could not verify a strong standalone source for:** planar road reflections as a discrete academic paper (it is textbook material in *Real-Time Rendering* 4th ed. — listed as the cheaper fallback to SSR).
