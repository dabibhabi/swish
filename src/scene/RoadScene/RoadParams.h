#pragma once

// ── RoadParams.h ─────────────────────────────────────────────────────
// Purpose   : single source of truth for the LIE cross-section constants that
//             RoadGeometry (physics/drivable bounds) and RoadScene (mesh
//             generators, incl. generate_exit_ramp) both need, so the two can
//             no longer drift. Values are moved VERBATIM from the former
//             RoadGeometry.h mirror constants; tests/test_road_params.cpp pins
//             them against config/road.toml -> road.bin.
// Units     : *_ft members are FEET (convert with util::units::ft() / kWuPerFt);
//             everything else is world units (WU) or a dimensionless fraction.
// Complexity: O(1), constexpr, no allocation.
// Precision : callers keep the legacy expression form (`kXxxFt * kFt`), so
//             float results are bit-identical to the pre-refactor constants.
// Example   : const float ramp_w = swish::road::kExit.width_ft * util::units::kWuPerFt;
// See also  : ../../utils/units/Units.h, RoadGeometry/RoadGeometry.h
// Test      : tests/test_road_params.cpp

#include <algorithm>

namespace swish::road {

// Mainline cross-section values that config/road.toml bakes into road.bin.
struct CrossSectionParams {
    float barrier_ft     = 2.67f;  // [barrier] width_ft
    float clearance_ft   = 3.0f;   // gap between barrier and first EB lane (layout offset)
    float lane_ft        = 13.0f;  // [road] lane_width_ft
    float lanes          = 4.0f;   // [road] lane_count (kept float: used in float arithmetic)
    float shoulder_eb_ft = 10.0f;  // [road] shoulder_width_eb_ft
    float marking_y      = 5.0f;   // [markings] y_offset (WU)
};
inline constexpr CrossSectionParams kLie{};

// Crown (cross-slope) shared by RoadConfig::kCrownSlope, road_surface_y and the marking generators.
inline constexpr float kCrownSlope = 0.02f;

// Shipped road puddle coverage [0,1]. ONE C++ source: WetGrip.h (physics puddle film) reads it directly, and
// tests/test_debug_params_io.cpp pins the src/debug/Tunables.def `puddleCoverage` default AND
// shaders/lighting.frag's SP_PUDDLE_COVERAGE literal against it (the registry stays a literal: the .def is
// X-macro-included by renderer code and sits in the debug layer, so it cannot cheaply include scene headers).
inline constexpr float kPuddleCoverage = 0.501f;
static_assert(kPuddleCoverage == 0.501f, "shipped puddle coverage must stay bit-identical to the GLSL literal");

// EB frontage ("marginal") road: ~55 ft berm past the mainline edge, two 12 ft lanes,
// lifted above the grass. Offsets are from the EB road edge, in feet.
struct ServiceRoadParams {
    float inner_ft = 50.0f;
    float outer_ft = 92.0f;
    float y        = 6.0f;  // WU lift above grass (avoids reverse-Z coplanar fighting)
};
inline constexpr ServiceRoadParams kService{};

// Intro EB exit -> N Marginal. Z: z_far = -introLen; the ramp is fully separated at
// end_frac * z_far; diverge/decel lengths are real-world feet.
struct ExitRampParams {
    float end_frac         = 0.38f;  // earlier in the intro so the gantry is easy to reach
    float diverge_ft       = 671.5f;
    float decel_ft         = 328.5f;
    float gantry_lead_ft   = 80.0f;  // EXIT ONLY gantry upstream of diverge
    float offset_ft        = 40.0f;
    float width_ft         = 14.0f;
    float marginal_gap_ft  = 10.0f;
    float marginal_lane_ft = 12.0f;
    float connect_ft       = 200.0f;
    float asphalt_y        = 6.0f;  // WU lift (same idea as endless service y)
};
inline constexpr ExitRampParams kExit{};

// Dashed-lane cycle (WU). Chunks snap the cycle to an exact divisor of the chunk length so
// markings tile with no phase jump at seams, keeping the dash:gap ratio. Pure: nothing mutated.
struct DashPattern {
    float length;
    float gap;
};
constexpr DashPattern snap_dash_to_chunk(DashPattern in, float chunk_len) noexcept {
    const float desired_cycle = in.length + in.gap;  // ~12192 WU
    const int   n             = std::max(1, static_cast<int>(chunk_len / desired_cycle + 0.5f));
    const float cycle         = chunk_len / static_cast<float>(n);  // exact divisor of chunk_len
    const float ratio         = in.length / desired_cycle;          // preserve dash:gap
    const float length        = cycle * ratio;
    return {length, cycle - length};
}

// C2b diamond dimensions in metres (m() at use). Sources: docs/corridor/tuning-notes.md (C2b).
namespace diamond {
inline constexpr float kCurbW = 0.15f, kCurbH = 0.15f;  // 6 in curb: top width and reveal
inline constexpr float kGutter      = 0.30f;            // pavement edge to edge line
inline constexpr float kLine        = 0.10f;            // MUTCD 4 in long lines
inline constexpr float kStopBar     = 0.61f;            // MUTCD 24 in stop bar
inline constexpr float kGoreLine    = 0.20f;            // 8 in channelizing line at a gore
inline constexpr float kRampWidthFt = 14.0f;            // one-lane ramp
inline constexpr float kJoin        = 60.0f;            // ramp fully on the service road this far from the crossroad
inline constexpr float kRampMin = 300.0f, kRampMax = 500.0f;
inline constexpr float kUnpairedLead = 400.0f;  // crossroad past a lone diverge
inline constexpr float kMinSpacing   = 900.0f;  // closer crossroads are dropped (diamonds would overlap)
inline constexpr float kStubPad      = 150.0f;  // service-road stub past the ramps where OSM has none
inline constexpr float kDeck         = 1.2f;    // slab + girders, D4 restyles
inline constexpr float kClearFt      = 16.0f;   // AASHTO vertical clearance over a freeway
inline constexpr float kLiftWu       = 3.0f;    // WU: overlay on a coplanar surface, below the 5 WU paint
inline constexpr float kDeadEnd      = 3.0f;    // C3: lateral snap longer than this = a side road ended; stop instead
// C3a cross-street stub: crest over its last stretch hides the end from road level
inline constexpr float kStubFade  = 45.0f;  // rise starts this far before the end
inline constexpr float kStubCrest = 15.0f;  // crest this far before the end
inline constexpr float kCrestH    = 1.8f;   // crest above level ground
inline constexpr float kStubDrop  = 2.5f;   // end this far below level ground (behind the crest)
inline constexpr float kApronHalf = 50.0f;  // grass either side of the stub, along the mainline
inline constexpr float kSidewalk  = 1.52f;  // 5 ft concrete walk behind the curb
// C3a span-wire signal (NYSDOT Region 10 practice, docs/endless-road.md C3a)
inline constexpr float kPoleH   = 9.0f;   // strain pole
inline constexpr float kSpanH   = 7.9f;   // wire attach height
inline constexpr float kSpanSag = 0.6f;   // mid-span sag
inline constexpr float kHeadW   = 0.36f;  // housing width, 12 in lenses
inline constexpr float kSection = 0.36f;  // one section; 3 per head
inline constexpr float kHeadD   = 0.25f;
inline constexpr float kLensR   = 0.15f;
inline constexpr float kVisor   = 0.24f;
inline constexpr float kAmberS  = 4.0f;  // ITE change interval at 35-40 mph
inline constexpr float kAllRedS = 1.0f;
// C3a HOV lane (exits 32 to 64, median lane both ways; MUTCD 3D.01/3D.02)
inline constexpr int   kHovFromExit = 32, kHovToExit = 64;
inline constexpr float kHovBuffer        = 0.61f;    // 2 ft between the buffer lines' centres
inline constexpr float kHovLine          = 0.20f;    // MUTCD wide line, 8 in
inline constexpr float kDiamondWFt       = 2.5f;     // MUTCD minimum width
inline constexpr float kDiamondLFt       = 12.0f;    // MUTCD minimum length
inline constexpr float kDiamondLineFt    = 0.5f;     // MUTCD minimum line width
inline constexpr float kDiamondSpacingFt = 1000.0f;  // MUTCD 3D.01 freeway spacing
inline constexpr float kHovZoneFt        = 1000.0f;  // access zone length (fixed pattern, no data)
inline constexpr float kHovZoneLeadFt    = 1000.0f;  // zone ends this far before each exit's diverge
}  // namespace diamond

// C4 route ends (debug; docs/endless-road.md C4). Metres unless noted.
namespace ends {
// Queens-Midtown Tunnel, Queens end. Portal = end of OSM tunnel way 658498546 (-73.95407 40.74145), which the
// 2026-11-20 bake puts 80 m before the EB exit 13 diverge. Posted clearance 12 ft 1 in (OSM maxheight).
inline constexpr float kPortalLead   = 80.0f;  // portal this far before the EB exit 13 diverge
inline constexpr float kCeilH        = 4.3f;   // road to ceiling: 12 ft 1 in posted plus the luminaires
inline constexpr float kWalkW        = 0.75f;  // safety walk on both sides of a tube
inline constexpr float kWalkH        = 0.30f;  // walk curb reveal
inline constexpr float kCurbClear    = 0.65f;  // car centre kept this far (plus the lane inset) off a walk curb
inline constexpr float kTileRepeat   = 300.0f / 246.0f;  // texture square (8 x 16 subway tiles), 246 per 300 m tile
inline constexpr float kCourse       = 0.0762f;          // one 3 in tile course: stripes are whole course pairs
inline constexpr float kBandLo       = 14.0f * kCourse;  // blue stripe starts this far above the walk
inline constexpr float kBandH        = 4.0f * kCourse;   // blue stripe, then the gold one above it
inline constexpr float kGoldH        = 2.0f * kCourse;
inline constexpr float kLightSpacing = 6.0f;                  // one spot light per this along the tube
inline constexpr float kLightStart   = 30.0f;                 // dark lead-in at the closed west end of the tube
inline constexpr float kLightK       = 4000.0f;               // LED tunnel luminaire CCT
inline constexpr float kStripW = 0.30f, kStripInset = 0.45f;  // luminaire row width; centre this far off the wall
inline constexpr float kStripNits = 1500.0f;  // lens luminance; ~9 kcd/m^2 is physical, cut for the wide bloom
// CIE 88 zones: threshold boost entering the tube (WB), transition decays to interior, exit boost leaving (EB).
inline constexpr float kThreshLen = 50.0f, kThreshBoost = 10.0f, kTransLen = 100.0f;
inline constexpr float kExitLen = 60.0f, kExitBoost = 5.0f;
inline constexpr float kRoofChunk   = 25.0f;  // roof volume (sky occlusion) per this much tube
inline constexpr float kTunnelEvMin = 4.5f;  // EV100 floor in the tube: the eye adapts to an interior of about 5 cd/m^2
// Portal face, embankment and plaza
inline constexpr float kHeadH = 9.0f, kHeadT = 1.0f;  // headwall above the road, thickness
inline constexpr float kPortalOut  = 6.0f;            // headwall past the outer tube wall
inline constexpr float kCapLen     = 80.0f;           // grassed cover over the tube behind the headwall
inline constexpr float kWingLen    = 18.0f;           // wing walls retain a 2:1 slope down to grade
inline constexpr float kPlazaLanes = 1.5f;            // extra paved width each side, in lanes (old toll plaza)
inline constexpr float kPlazaIn = 60.0f, kPlazaHold = 60.0f, kPlazaOut = 150.0f;
inline constexpr float kPadS = 130.0f, kPadLen = 40.0f;  // U-turn opening in the plaza median (fictional)
inline constexpr float kOpenPad = 250.0f;                // diamonds and service roads start this far past the portal
// Riverhead: the expressway ends in a single-lane roundabout (fictional; real LIE becomes CR 58 at a signal)
inline constexpr float kRingRo = 24.0f, kRingRi = 17.0f;  // 48 m inscribed diameter (FHWA single-lane range)
inline constexpr float kRingBack   = 10.0f;               // far edge this far before the last station
inline constexpr float kSplitter   = 15.0f;               // raised splitter island before the ring
inline constexpr float kTaperFt    = 540.0f;              // MUTCD lane-drop taper L = W x S (12 ft x 45 mph)
inline constexpr float kTaperGap   = 20.0f;               // one lane this long before the splitter
inline constexpr float kRingLiftWu = 10.0f;               // WU: ring overlays the approach pavement
inline constexpr float kApronW = 2.5f, kApronH = 0.08f;   // mountable truck apron round the island
inline constexpr float kIslandH    = 0.25f;
inline constexpr float kBarrierGap = 2.0f;    // outer barrier this far past the ring edge
inline constexpr float kGrassPast  = 120.0f;  // grass beyond the last station
}  // namespace ends

// D2 edges (debug; docs/endless-road.md D2). Metres unless noted; sources in docs/corridor/tuning-notes.md.
namespace edges {
// New Jersey shape, 32 in: 3 in vertical reveal, 55 deg face to 13 in, 84 deg face to the top (AASHTO RDG 6.5).
// Runs: 10 / tan 55 = 7.0 in, 19 / tan 84 = 2.0 in, so a 24 in base carries a 6 in top.
inline constexpr float kNjRevealIn = 3.0f, kNjBreakIn = 13.0f, kNjHeightIn = 32.0f;
inline constexpr float kNjBaseIn = 24.0f, kNjLowerRunIn = 7.0f, kNjUpperRunIn = 2.0f;
inline constexpr float kPedestalLen = 1.5f, kPedestalHalf = 0.30f;  // widened block under a median lamp pole
// W-beam (AASHTO M180), MGS: 31 in to the rail top, 12.25 in deep beam, 6 ft 3 in post spacing, 8 in blockouts.
inline constexpr float kRailTop = 0.787f, kRailDepth = 0.311f, kRailCrest = 0.083f;
inline constexpr float kPostSpacingFt = 6.25f, kPost = 0.152f, kBlockout = 0.203f;
inline constexpr float kRailOffset = 0.60f;                     // rail face this far past the shoulder edge (2 ft)
inline constexpr float kRailEnd    = 7.62f;                     // turned-down end: the last 25 ft drop to the ground
inline constexpr float kShieldUp = 45.0f, kShieldDown = 15.0f;  // run past a pier, upstream / downstream
inline constexpr float kPierOffset = 2.0f;  // crossroad pier centre past the shoulder edge (deflection room)
// Sound wall: precast panels between concrete posts on a low base (photo exit-33); 4.5 m inside FHWA's 10-17 ft.
inline constexpr float kWallPost = 6.1f, kWallPostW = 0.45f, kWallBase = 0.6f, kWallT = 0.20f;
inline constexpr float kWallBack   = 1.2f;  // wall this far in from the strip top (the service road's curb)
inline constexpr float kWallGapMin = 400.0f, kWallMargin = 40.0f;  // between diamonds; clear of ramps
inline constexpr float kBrickTile = 1.083f;                        // Bricks054 texture: 16 modular courses of 2 2/3 in
// Roadside ditch: 1V:6H foreslope, flat bottom, rounded backslope to the strip top.
inline constexpr float kDitch = 0.45f, kDitchFlat = 0.6f, kForeslope = 6.0f;
// Wide grass median east of exit 64 ([WP]); cable barrier down the middle (3 cables, S3x5.7 posts at 15 ft).
inline constexpr int   kWideFromExit = 64;
inline constexpr float kWideLead = 1000.0f, kWideEase = 500.0f,
                       kWideTail    = 600.0f;  // past exit 64; ease; before the drop
inline constexpr float kCableH[3]   = {0.53f, 0.64f, 0.76f};
inline constexpr float kCablePostFt = 15.0f, kCableMinHalf = 2.0f;  // cable starts once the median is this wide
}  // namespace edges

// D3 signs (debug; docs/endless-road.md D3). Metres unless noted. Panel heights follow the atlas pixel aspect
// (scene/Corridor/SignAtlas.h), so only widths live here.
namespace signs {
inline constexpr float kPanelW    = 6.10f;  // advance guide panel, 20 ft (tab and distance strip scale with it)
inline constexpr float kClearFt   = 17.0f;  // MUTCD 2A.18: overhead sign clearance over lanes and shoulders
inline constexpr float kOneMileFt = 5280.0f;
inline constexpr float kQuarterMi = 0.25f, kHalfMi = 0.5f, kOneMi = 1.0f;  // advance distances, real miles
inline constexpr float kAfterGore = 100.0f;  // an advance gantry stands at least this far past the previous gore
inline constexpr float kDeckClear = 30.0f;   // and this far off a crossroad deck edge
inline constexpr float kDeckHide  = 150.0f;  // past a deck (in its dip) the deck hides a gantry from afar
inline constexpr float kLampClear = 2.5f;    // median post moved off a lamp pole
// Box truss (galvanised): square section, chords and lacing pipes, bay length; sign plane in front of it.
inline constexpr float kTrussSide = 1.37f, kChord = 0.114f, kLacing = 0.06f, kBay = 1.37f;
inline constexpr float kSignOff = 0.30f;                    // sign face this far upstream of the truss front chords
inline constexpr float kColumn = 0.32f, kColumnGap = 1.2f;  // twin pipe columns (12.75 in OD), centres along s
inline constexpr float kFooting = 0.9f, kFootingH = 0.3f;   // concrete pedestal under each column, above grade
inline constexpr float kPostOut   = 2.0f;                   // outer column past the shoulder edge
inline constexpr float kStiffener = 0.3048f;                // back of an extruded panel: one rib per foot
// Gore sign (MUTCD E5-1a) on a breakaway post pair; mile markers (D10 enhanced reference location sign).
inline constexpr float kGoreW = 1.6f, kGoreLift = 2.1f, kGoreGap = 4.0f;  // gap: ramp inner edge past the shoulder
inline constexpr float kMarkerW = 0.46f, kMarkerLift = 1.2f, kMarkerOut = 1.0f, kMarkerStepMi = 0.2f;
inline constexpr float kSheen = 0.45f;  // sheeting roughness multiplier (glass-bead retroreflective face)
}  // namespace signs

// D4 overpasses (debug; docs/endless-road.md D4). Metres unless noted; sources in docs/corridor/tuning-notes.md (D4).
namespace bridges {
inline constexpr float kUnderClearFt = 14.5f;  // NYSDOT Bridge Manual: local road under a structure (unverified)
inline constexpr float kSlab = 0.23f, kHaunch = 0.05f;  // 9 in cast-in-place deck on a 2 in haunch
inline constexpr float kGirderD  = 1.2f;                // girder depth, about span / 25 for the 20-30 m spans here
inline constexpr float kSpacing  = 2.6f;                // girder spacing (8.5 ft)
inline constexpr float kOverhang = 0.9f;                // deck past the fascia girder centre
inline constexpr float kFlangeW = 0.40f, kFlangeT = 0.04f, kWebT = 0.02f;  // painted plate girder (web thickened)
inline constexpr float kStiffStep = 3.0f;                                  // fascia web stiffeners
inline constexpr float kBeamTop = 1.07f, kBeamBot = 0.66f, kBeamWeb = 0.18f, kBeamFl = 0.15f;  // precast I-beam
inline constexpr float kBearingW = 0.6f, kBearingL = 0.45f, kBearingH = 0.12f;  // laminated elastomeric pad
inline constexpr float kCapH = 1.07f, kCapW = 1.2f;                             // pier cap / abutment seat
inline constexpr float kColR = 0.457f, kColStep = 6.0f;  // 36 in round columns of a multi-column bent
inline constexpr float kWallPier     = 0.9f;             // wall pier thickness (concrete style)
inline constexpr float kToeClear     = 1.5f;             // spill slope toe past a service road's outer curb
inline constexpr float kClearZone    = 9.0f;  // AASHTO 30 ft clear zone past the shoulder when no service road
inline constexpr float kSpill        = 2.0f;  // spill-through slope 1V:2H, concrete slope paving under the deck
inline constexpr float kApproachFlat = 10.0f, kApproachLen = 120.0f;  // level, then down to grade (about 5 % mean)
inline constexpr float kFBreakIn = 10.0f;                    // F-shape parapet: NJ runs with the slope break at 10 in
inline constexpr float kFenceH = 2.44f, kFencePost = 3.05f;  // 8 ft chain-link frame on the parapet (mesh omitted)
inline constexpr float kWalk = 1.52f;                        // 5 ft walk behind the curb on a street bridge
inline constexpr float kDrip = 0.025f;                       // drip notch under the overhang
// Stone-faced parkway arch (reference: Cross Island Pkwy photo): rise per span, spandrel over the crown.
inline constexpr float kArchRise = 0.15f, kArchRiseMax = 3.0f, kSpandrel = 0.75f, kStoneParapet = 1.0f;
inline constexpr float kStoneTile = 2.4f;  // Bricks066 texture square
inline constexpr float kStonePier = 1.5f, kWing = 6.0f;
// LIRR through-girder: floor, girders above it, ballast deck
inline constexpr float kFloorD = 0.9f, kThroughH = 2.4f, kThroughStiff = 1.6f;
inline constexpr float kTrack = 5.5f, kGauge = 1.435f, kTieStep = 0.61f, kTieL = 2.6f, kRailApproach = 80.0f;
// LIE over a road: under-road at the clearance, back to grade over this; trench walls (U-wall) either side
inline constexpr float kLieFlat = 8.0f, kLieClimb = 85.0f, kTrenchWall = 0.6f;
inline constexpr float kLieCover = 0.08f;  // 3 in asphalt overlay: slab top under the mainline pavement
inline constexpr float kMergePad = 20.0f;  // structures this close (footprints) are one; the wider stays
}  // namespace bridges

// ── D5 district kits, street lamps, skyline (debug; metres; docs/lie-reference/README.md "By district") ──
namespace dressing {
enum class Lamp : unsigned char { None, MedianTwin, Shoebox, Cobra };
enum class Bldg : unsigned char { None, House, Apartment, Office, Tower, Warehouse, Barn };
// Roadside from the first record of `fromExit` to the next kit. trees = chance a kCell square holds a tree; species
// weights oak, maple, pitch pine, scrub oak; two building types, each a chance per lot; floors for the multi-storeys.
struct Kit {
    int   fromExit;
    float trees;
    float species[4];
    Bldg  b1;
    float rate1;
    Bldg  b2;
    float rate2;
    int   floorsMin, floorsMax;
    Lamp  lamp;
    float graves  = 0.0f;  // D6: headstone field (chance per kGrave cell), 0 = none
    float srTrees = 0.0f;  // D6: street-tree row along the service roads' outer curb, spacing (m), 0 = none
};
// Columns: exit, trees, species {oak, maple, pitch pine, scrub}, building 1 + rate, building 2 + rate, floors, lamp.
using B                      = Bldg;
using L                      = Lamp;
inline constexpr Kit kKits[] = {
    // Queens: LIC towers, Maspeth industry, LeFrak City / Rego Park, Flushing Meadows, Bayside, Alley Pond woods
    {13, 0.08f, {0.5f, 0.5f, 0.0f, 0.0f}, B::Tower, 0.55f, B::Apartment, 0.25f, 30, 60, L::MedianTwin},
    // D6: Calvary Cemetery both sides from exit 17 (the EB 17 record bakes before WB 15), Maspeth industry from 18
    {17, 0.05f, {0.5f, 0.5f, 0.0f, 0.0f}, B::None, 0.0f, B::None, 0.0f, 0, 0, L::MedianTwin, 0.85f},
    {18, 0.08f, {0.5f, 0.5f, 0.0f, 0.0f}, B::Warehouse, 0.6f, B::Apartment, 0.1f, 4, 6, L::MedianTwin},
    {19, 0.12f, {0.4f, 0.6f, 0.0f, 0.0f}, B::Apartment, 0.6f, B::House, 0.2f, 14, 18, L::MedianTwin},
    {22, 0.45f, {0.5f, 0.5f, 0.0f, 0.0f}, B::None, 0.0f, B::None, 0.0f, 0, 0, L::MedianTwin},
    {24, 0.25f, {0.5f, 0.5f, 0.0f, 0.0f}, B::House, 0.6f, B::Apartment, 0.12f, 3, 6, L::MedianTwin},
    {30, 0.60f, {0.6f, 0.4f, 0.0f, 0.0f}, B::None, 0.0f, B::None, 0.0f, 0, 0, L::MedianTwin},
    // Nassau: Lake Success office parks, suburbs behind walls (shoebox heads), drop-lens cobras east of 40
    {32, 0.35f, {0.5f, 0.5f, 0.0f, 0.0f}, B::Office, 0.45f, B::House, 0.2f, 3, 5, L::Shoebox},
    {34, 0.40f, {0.5f, 0.5f, 0.0f, 0.0f}, B::House, 0.7f, B::None, 0.0f, 0, 0, L::Shoebox},
    {40, 0.40f, {0.5f, 0.5f, 0.0f, 0.0f}, B::House, 0.6f, B::Office, 0.08f, 2, 4, L::Cobra},
    // Suffolk: Melville offices, west Suffolk mixed woods, pine barrens (no lamps east of 64 [NYCR]), Riverhead farms
    {48, 0.30f, {0.5f, 0.4f, 0.1f, 0.0f}, B::Office, 0.6f, B::House, 0.1f, 4, 8, L::Cobra, 0.0f, 12.0f},
    {51, 0.45f, {0.35f, 0.35f, 0.25f, 0.05f}, B::House, 0.35f, B::None, 0.0f, 0, 0, L::Cobra},
    {62, 0.55f, {0.35f, 0.1f, 0.45f, 0.1f}, B::House, 0.25f, B::None, 0.0f, 0, 0, L::Cobra},  // D6: pine-oak mix
    {65, 0.85f, {0.15f, 0.0f, 0.5f, 0.35f}, B::None, 0.0f, B::None, 0.0f, 0, 0, L::None},
    {71, 0.15f, {0.4f, 0.0f, 0.5f, 0.1f}, B::Barn, 0.3f, B::None, 0.0f, 0, 0, L::None},
};
// Per building type: footprint (along the road, across), storey height, atlas row (256 px bands of the 2048 atlas),
// texture repeat width and band height (tools/gen_dressing.py), lot pitch along and across.
struct BldgSpec {
    float len, dep, floorH;
    int   row;
    float texW, bandH, lotS, lotT;
};
inline constexpr BldgSpec kBldg[] = {
    {0, 0, 0, 0, 1, 1, 1, 1},                              // None
    {12.0f, 8.0f, 3.0f, 2, 12.0f, 6.0f, 24.0f, 30.0f},     // House: two storeys, hip roof
    {54.0f, 16.0f, 3.0f, 3, 72.0f, 9.0f, 80.0f, 45.0f},    // Apartment brick slab
    {64.0f, 32.0f, 4.0f, 4, 64.0f, 8.0f, 110.0f, 70.0f},   // Office, ribbon windows
    {32.0f, 28.0f, 3.2f, 5, 102.4f, 12.8f, 95.0f, 75.0f},  // Glass tower
    {64.0f, 40.0f, 8.0f, 6, 64.0f, 8.0f, 90.0f, 60.0f},    // Warehouse, one tall storey
    {24.0f, 12.0f, 5.0f, 7, 24.0f, 5.0f, 260.0f, 110.0f},  // Barn, red siding, hip roof
};
inline constexpr float kHouseRise = 2.5f, kBarnRise = 4.0f, kEave = 0.4f;  // hip roof rise, overhang
inline constexpr float kCell      = 10.0f;                                 // tree grid
inline constexpr float kTreeIn    = 12.0f;  // nearest tree past the shoulder edge (AASHTO 30 ft clear zone + margin)
inline constexpr float kBldgIn    = 28.0f;  // nearest building past the shoulder edge
inline constexpr float kPathClear = 5.0f, kBldgPathClear = 14.0f;  // past a ramp / service road edge
inline constexpr float kTreeH[4][2]   = {{14.0f, 22.0f}, {12.0f, 19.0f}, {13.0f, 20.0f}, {3.0f, 5.5f}};
inline constexpr float kTreeAspect[4] = {0.95f, 0.85f, 0.75f, 1.7f};  // card width / height
inline constexpr float kSink          = 0.3f;    // trees and buildings sink this far into level ground
inline constexpr float kFarSink       = 0.2f;    // far ground below level ground: aprons and stubs win the depth test
inline constexpr float kFarCurve      = 0.8f;    // far edge at most this fraction of the curve radius (no fold-over)
inline constexpr float kCrossPad      = 10.0f;   // trees and buildings this far off a crossroad apron / bridge deck
inline constexpr float kApproach      = 150.0f;  // overpass approach / trench run past its abutment, kept clear
// Lamps: Queens median twin arms keep the L6.1 60 m grid (D3 gantry posts dodge it); outer singles elsewhere.
inline constexpr float kPoleH           = 12.2f;   // 40 ft mounting height
inline constexpr float kShoulderSpacing = 65.0f;   // Nassau / west Suffolk right-shoulder poles (reference: 60-70 m)
inline constexpr float kQueensOuter     = 120.0f;  // Queens outer-edge singles between the median rows
inline constexpr float kPoleOut         = 2.0f;    // pole past the shoulder edge, behind the W-beam
inline constexpr float kArmReach        = 4.5f;    // upswept single arm over the shoulder
inline constexpr float kSignClear       = 4.0f;    // pole off a gantry column, gore sign or mile marker
// Skyline card (texture scale, tools/gen_dressing.py): Midtown this far past the tube's Manhattan end, drawn at
// kSkyDist and scaled by kSkyDist / R, so its bearing and angular size are right at any range R (no parallax error).
inline constexpr float kSkyW = 4500.0f, kSkyH = 560.0f, kSkyBack = 1300.0f, kSkyDist = 2000.0f, kSkyMaxR = 16000.0f;
// D6 headstones: one chance per cell (along x across), field this deep past the tree line; upright tablet, family
// die, obelisk (heights), shares of the last two
inline constexpr float kGraveS = 3.0f, kGraveT = 2.4f, kGraveDeep = 90.0f;
inline constexpr float kTablet[3] = {0.75f, 0.9f, 0.2f}, kDie[3] = {1.6f, 1.1f, 0.6f}, kObelisk[2] = {0.6f, 2.6f};
inline constexpr float kDieShare = 0.15f, kObeliskShare = 0.03f;
inline constexpr float kSrTreeOut = 3.0f;  // street-tree row past the service road's outer curb
}  // namespace dressing

// ── D6 landmarks (debug; docs/endless-road.md D6, docs/lie-reference/README.md "Landmark notes"). Metres. ──
namespace landmarks {
// The 12 landmarks' exits (13 is the tunnel, 73 the end): a diamond too close to a plain neighbour wins the slot.
inline constexpr int kExits[] = {17, 22, 31, 33, 39, 44, 49, 62, 68};  // not 53: its diamond would swallow the D4 arch
enum class Prop : unsigned char { Unisphere, Pavilion, VentTower };
// One-off structure at the mean station of `exit`'s records + `along` (baked), `lat` signed (+ = right of EB).
// Offsets measured on OSM: nearest LIE way point to the landmark, distance and side kept real.
// `clear`: tree-free wedge out to the spot, this half-width (along s) at it, widening to clear + kSightBase at the
// road, so drivers within about kSightBase of its station see it over the park lawns.
struct Spot {
    int   exit;
    float along, lat;
    Prop  kind;
    float clear = 0.0f;
};
inline constexpr Spot kSpots[] = {
    {22, 0.0f, -547.0f, Prop::Unisphere, 40.0f},  // 40.7462 -73.8448, 547 m north of the LIE, over the park mall
    {22, -100.0f, -307.0f, Prop::Pavilion},       // NYS Pavilion towers 40.7437 -73.8447, 307 m
    {13, -360.0f, 24.0f, Prop::VentTower},        // Queens vent building in Borden Ave (2nd-5th St), over the EB tube
};
// Unisphere (1964): 120 ft stainless globe, 140 ft overall, axis tilted 23.5 deg, three orbit rings.
inline constexpr float kGlobeR = 18.3f, kGlobeTop = 43.0f, kGlobeTilt = 23.5f, kGlobeRib = 0.5f, kRingR = 1.18f;
inline constexpr int   kMeridians = 20, kParallels = 11;
// NYS Pavilion (1964): observation towers 60 / 150 / 226 ft, Tent of Tomorrow ring 100 ft high (elliptic).
inline constexpr float kTowerH[3] = {18.3f, 45.7f, 68.9f}, kTowerR = 3.0f, kDeckR[3] = {8.0f, 9.5f, 11.0f};
inline constexpr float kTentA = 50.0f, kTentB = 38.0f, kTentH = 30.5f;
// Queens vent building: 100 ft orange-brick Art Deco tower; footprint a pick. Borden Ave pad at street level.
inline constexpr float kVentL = 36.0f, kVentW = 20.0f, kVentH = 30.5f, kVentStep = 6.0f, kVentInset = 3.0f;
inline constexpr float kSightBase = 350.0f;
inline constexpr float kPadPast   = 60.0f;  // street pad runs this far past the building (from the portal's cap)
// Crossroad deck style per exit (D4 BridgeStyle: 1 concrete, 2 stone): Van Wyck concrete, Cross Island stone-faced
// parkway bridge, NY-135 freeway concrete.
struct Style {
    int exit, style;
};
inline constexpr Style kStyles[] = {{22, 1}, {31, 2}, {44, 1}};
// Roadside boards (tools/sign_corridor.py LANDMARK_BOARDS, sign_atlas::kLandmarkBoards[cell]) at the first record
// of `exit` minus `before` (baked m; 343 = 1/2 real mile), facing `side`'s traffic on its right shoulder.
struct Board {
    int   exit, side;
    float before;
    int   cell;
    float w, h, lift;
};
inline constexpr Board kBoards[] = {
    {33, +1, 250.0f, 0, 3.6f, 1.26f, 2.1f},  // NASSAU COUNTY LINE (city line just west of Lakeville Rd)
    {33, -1, 250.0f, 1, 3.6f, 1.26f, 2.1f},  // QUEENS COUNTY LINE
    {32, +1, 343.0f, 2, 3.0f, 2.4f, 2.1f},   // HOV LANE BEGINS LEFT LANE 1/2 MILE (photo exit-33)
    {33, +1, 200.0f, 3, 3.0f, 2.1f, 2.1f},   // Great Neck / Manhasset EXIT 33 (photo exit-33)
    {62, +1, 600.0f, 4, 4.9f, 2.94f, 2.4f},  // ATTRACTIONS AT EXIT 62N (photo exit-62, 2000)
};
inline constexpr float kBoardOut = 2.5f;  // board centre past the shoulder edge (or a ramp's outer edge)
}  // namespace landmarks

static_assert(kExit.diverge_ft + kExit.decel_ft == 1000.0f, "exit ramp spans 1000 ft");
static_assert(kExit.end_frac > 0.0f && kExit.end_frac < 1.0f);

}  // namespace swish::road
