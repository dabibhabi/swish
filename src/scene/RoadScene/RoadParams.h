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

// N2a Queens network free-path ramps, metres (m() at use). Sources: docs/corridor/tuning-notes.md (N2a).
namespace network {
inline constexpr float kRampLaneFt  = 14.0f;  // one-lane ramp (NYSDOT HDM ch. 2, as diamond::kRampWidthFt)
inline constexpr float kShoulderLFt = 4.0f;   // AASHTO Green Book Table 10-2 ramp shoulders: 4 ft left
inline constexpr float kShoulderRFt = 8.0f;   // 8 ft right
inline constexpr float kStep        = 4.0f;   // resample spacing along a ramp
inline constexpr float kSmoothXZ    = 10.0f;  // plan box filter radius (OSM node kinks)
inline constexpr float kSmoothY     = 40.0f;  // height box filter radius, two passes: a 6.5 m layer step -> ~4 %
inline constexpr float kWeld        = 3.0f;   // ramp ends this close share one point
inline constexpr float kLift      = 0.02f;  // ramp asphalt over its road sits this far above it (hides the road paint)
inline constexpr float kHandoffDy = 1.0f;   // a piece is reachable only within this height of the car
inline constexpr float kBankEase  = 30.0f;  // a ramp's borrowed road bank eases to level over this past the pin
inline constexpr float kParapetH  = 0.81f;  // 32 in concrete parapet (NYSDOT standard)
inline constexpr float kParapetW  = 0.30f;
inline constexpr float kDeck      = 1.2f;   // slab + girder depth under an elevated ramp
inline constexpr float kPierEvery = 30.0f;  // pier spacing where the deck is high
inline constexpr float kPierMinH  = 4.0f;   // deck this far above ground gets piers
inline constexpr float kPierW     = 1.4f;
// N3 ramp crossings: vertical clearance 16 ft (NYSDOT Interstate minimum) + kDeck, spread / eased along the ramp.
inline constexpr float kClear = 6.1f, kClearSpread = 90.0f, kClearEase = 60.0f;
inline constexpr float kForkBias = 0.5f;  // a fork sibling takes the car once its centreline is this much nearer
// N3 road structures (CorridorViaduct.h). Metres unless named.
inline constexpr float kViaductMinH = 3.0f;   // profile this far over the DEM ground = viaduct (else embankment)
inline constexpr float kTrenchMinD  = 1.5f;   // this far under it = trench with retaining walls (else a cut slope)
inline constexpr float kMinSpan     = 40.0f;  // shorter structure spans are dropped
inline constexpr float kRoadDeck    = 1.8f;   // slab + girders under a mainline viaduct (fascia depth)
inline constexpr float kColumn      = 1.2f;   // square pier column
inline constexpr float kCapH = 1.3f, kCapD = 1.6f;         // pier cap beam height and depth along the road
inline constexpr float kGirderGap = 2.7f;                  // steel plate girder spacing across the deck
inline constexpr float kGirderD = 1.1f, kGirderW = 0.45f;  // web depth, flange width
inline constexpr float kUnderClear = 6.0f;   // deck over `under`'s pavement within this of its outer edge: steel span
inline constexpr float kGroundEase = 40.0f;  // ground eases from verge level to the DEM over this past a span end
inline constexpr float kCoping     = 0.45f;  // retaining-wall coping width
inline constexpr float kGroundW    = 60.0f;  // ground band past the structure, each side
// N3: LIE exits whose diamond the real junction replaces (17 = the BQE; 16's diamond would sit on its ramps), and
// LIE crossings drawn by a network road instead (name prefix): the BQE viaduct over the LIE. N4/N5 add theirs.
// N5: 27 = the Clearview (26's and 28's diamonds would sit on its C/D and loops), 31 = the Cross Island; the Van
// Wyck viaduct.
inline constexpr int         kJunctionExits[]  = {16, 17, 22, 26, 27, 28, 31};
inline constexpr const char* kOwnedCrossings[] = {"Brooklyn", "Van Wyck"};
// N4: LIE-over crossings whose under-road is a network road (it follows the D4 trench, then eases back to its own
// profile over net_under_blend_m); the D4 bridge keeps its deck and U-walls.
inline constexpr const char* kNetUnder[] = {"Grand Central", "Clearview", "Cross Island"};  // N5: the rungs under it
// N3 guide signs on the network (MUTCD 2E; legends in tools/sign_corridor.py NET_BOARDS, roads.md section 1). Each row:
// a gantry `miles` real miles along travel from the anchor's gore (negative = before it). anchor = an exit ref on
// `road` in direction `westbound` (the record's), or a junction name (its station on `road`). board / board2 =
// sign_atlas::kNetBoards (board2 left of board on the same truss), -1 = the LIE's own panel for the exit number with
// its distance strip. gore = sign_atlas::kNetGores, -1 = none, -2 = the LIE panel's gore sign.
struct GuideSign {
    int         road;
    const char* anchor;
    bool        westbound;
    int         board, board2, gore;
    float       miles;
};
inline constexpr GuideSign kGuideSigns[] = {
    // LIE (road 0): the exit joins the D3 exit list (gantries by its collapse rule, the panel's gore sign)
    {0, "17W-E", false, -1, -1, -2, 0.0f},   // LIE EB: EXIT 17 W-E, Staten Island / Bronx / LaGuardia
    {0, "17W", true, -1, -1, -2, 0.0f},      // LIE WB: EXIT 17 W, Brooklyn
    {1, "lie_bqe", false, 5, -1, -1, 0.3f},  // BQE NB past the LIE: 278 EAST Bronx / Grand Central Pkwy, 3 MILES
    {1, "39", false, 0, -1, 1, -0.5f},
    {1, "40", false, 1, -1, 2, -0.5f},
    {1, "41", false, 2, -1, 3, -0.5f},
    {1, "42", false, 4, 3, 4, -0.15f},  // the Y: 278 WEST GCP W / RFK (left lanes), EXIT 42 GCP E / LaGuardia (right)
    {1, "bqe_gcp", true, 6, -1, -1, 0.4f},  // BQE SB leaving the Y: 278 WEST Brooklyn / Long Island Expwy, 2 MILES
    {1, "41", true, 7, -1, 3, -0.5f},
    {1, "40", true, 8, -1, 2, -0.5f},
    {1, "39W", true, 9, -1, 1, -0.5f},
    {1, "35", true, 10, -1, 0, -0.5f},
    // N4 LIE exit 22 (the D3 panels "22A-E" EB, "22 A-B" WB) and the GCP in parkway style: EXIT 10 to the LIE both
    // ways, EXIT 4 to the BQE westbound, GCP trailblazer confirmation boards past the Y and past exit 22
    {0, "22A-E", false, -1, -1, -2, 0.0f},
    {0, "22A", true, -1, -1, -2, 0.0f},
    {2, "10W-E", false, 11, -1, 7, -0.5f},
    {2, "10E-W", true, 12, -1, 7, -0.5f},
    {2, "4", true, 13, -1, 8, -0.5f},
    {2, "bqe_gcp", false, 14, -1, -1, 0.3f},
    {2, "lie_gcp", true, 15, -1, -1, 0.3f},
    // N5 rungs: LIE exits 27 (Clearview) and 31 (CIP) join the D3 exit list; the Van Wyck and the Clearview in
    // Interstate style, the CIP in parkway style; the GCP's exits 13, 21, 23 (legends: OSM destination tags)
    {0, "27N-S", false, -1, -1, -2, 0.0f},
    {0, "27N-S", true, -1, -1, -2, 0.0f},
    {0, "31S", false, -1, -1, -2, 0.0f},
    {0, "31N-S", true, -1, -1, -2, 0.0f},
    {3, "7", false, 16, -1, 9, -0.5f},        // VW SB: EXIT 7 Union Tpke / Jackie Robinson Pkwy
    {3, "10", true, 17, -1, 7, -0.5f},        // VW NB: EXIT 10 GCP
    {3, "12A", true, 18, -1, 10, -0.5f},      // VW NB: EXIT 12 A-B I-495
    {4, "4E-W", false, 19, -1, 11, -0.5f},    // Clearview: EXIT 4 E-W I-495 both ways
    {4, "4E-W", true, 19, -1, 11, -0.5f},     //
    {4, "1", false, 20, -1, 12, -0.5f},       // Clearview SB: EXIT 1 GCP (the stack)
    {5, "30E", false, 21, -1, 13, -0.5f},     // CIP SB: EXIT 30 E I-495 EAST (no SB access to I-495 west)
    {5, "29E-W", false, 22, -1, 14, -0.5f},   // CIP SB: EXIT 29 E-W GCP
    {5, "29E-W", true, 23, -1, 14, -0.5f},    // CIP NB: EXIT 29 E-W GCP
    {5, "lie_cip", false, 24, -1, -1, 0.3f},  // CIP SB past the LIE: Belt Pkwy / Kennedy Airport
    {2, "13", false, 25, -1, 15, -0.5f},      // GCP EB: EXIT 13 I-678 SOUTH (Kew Gardens)
    {2, "21", false, 26, -1, 16, -0.5f},      // GCP: EXIT 21 I-295 NORTH (the stack), both ways
    {2, "21", true, 26, -1, 16, -0.5f},       //
    {2, "23", false, 27, -1, 17, -0.5f},      // GCP: EXIT 23 Cross Island Pkwy, both ways
    {2, "23", true, 27, -1, 17, -0.5f},       //
};
inline constexpr float kGoreAfter = 100.0f;  // m: gore past an exit record when no network ramp gives it
// N3 junction ramps: the ramp's left edge line is white this far past where it leaves its road (the gore).
inline constexpr float kGoreLen    = 60.0f;
inline constexpr float kUnderBlend = 150.0f;  // N4: m, a road under a LIE bridge eases back to its profile (slider)
// N4: the LIE's bridge over a network road spans it and its C/D roads (exit 22: GCP 4+4 lanes + C/D each side):
// girder deck of at least this half width (m) along the LIE, piers this far either side of the road's median.
inline constexpr float kUnderHalf = 42.0f, kUnderPier = 22.0f;
inline constexpr float kUnderFall = 40.0f;  // m: ramps past the bridge's half width keep less of the sag
inline constexpr float kVergeSink = 2.0f;   // m: a ramp at most this far under a road's grass verge is lifted onto it
inline constexpr float kUnderDig  = 1.4f;   // m: its trench floor this much deeper, so its smoothed C/D ramps clear too
inline constexpr float kHugEase   = 15.0f;  // N5: m, a ramp sunk into a road's shoulder is lifted this close to its pin
inline constexpr int   kClearPasses = 8;    // N5: clearance + reweld rounds (a weld moves the crossings near it)
inline constexpr float kPinPastDy = 4.0f;   // N5: m, a ramp rides its road past half its length only this level with it
// N6 guide signs per road (roads.md): the GCP and the CIP are parkways (NYSDOT: smaller boards on roadside posts),
// the rest Interstates (overhead trusses).
inline constexpr int   kParkways[]  = {2, 5};
inline constexpr float kPostScale   = 0.6f;   // parkway roadside board: 0.6 x the 20 ft overhead panel (12 ft)
inline constexpr float kPostLift    = 2.1f;   // m: board bottom over the ground (7 ft, MUTCD 2A.18)
inline constexpr float kPostOut     = 3.0f;   // m: board's near edge past the shoulder (or the ramp) edge
inline constexpr float kDirBefore   = 60.0f;  // m: exit-direction sign this far before the gore (MUTCD 2E.37)
inline constexpr float kClosedW     = 0.4f;   // EXIT CLOSED plaque width / board width (8 x 5 ft under a 20 ft board)
inline constexpr float kMaxGrade    = 0.08f;  // N6: steepest ramp grade (AASHTO Green Book ch. 10: 6-8 % upper range)
inline constexpr float kGradeSmooth = 12.0f;  // N6: m, box radius rounding the grade breaks (vertical curves)
// N6: where pins, welds and 16 ft clearance leave no room in the compact layout the solve raises one ramp's grade in 2
// % steps; AASHTO allows 2 % more on one-way downgrades. Test cap for those ramps (one LIE x CIP pair excepted).
inline constexpr float kMaxGradeTight = 0.11f;
// N7: a parkway (kParkways, passenger cars only) needs less headroom: NYC parkway structures are posted 10 ft 7 in to
// 11 ft 6 in (photos cip-stone-bridge); 12 ft 6 in + kDeck over a parkway's pavement, and under a road for a ramp to
// or from a parkway (the grade solve only; clear_ramps keeps 16 ft where there is room).
inline constexpr float kParkwayClearFt = 12.5f;
// N7 network ground (CorridorGround.h), metres: DEM blended between roads, carved under and pinned to at-grade
// pavement.
inline constexpr float kGroundFine    = 16.0f;    // fine cell, within kGroundNear of a network road or ramp
inline constexpr float kGroundCoarse  = 96.0f;    // coarse cell everywhere else (out to the far plane)
inline constexpr float kGroundNear    = 220.0f;   // fine grid this far round network pavement
inline constexpr float kGroundMargin  = 2600.0f;  // coarse grid past the network's bbox (camera far plane 2.4 km)
inline constexpr float kGroundSink    = 0.12f;    // under network pavement and verges (hides no paint, no z-fight)
inline constexpr float kGroundLieSink = 0.3f;     // under the LIE's level ground (its D5 far strip sits 0.2 m down)
inline constexpr float kGroundDem     = 0.3f;     // under the DEM (N3 structure ground bands win)
inline constexpr float kGroundDrop    = 1.0f;     // coarse grid (and the fine grid's rim) this much lower
inline constexpr float kGroundCut     = 0.5f;     // cut slope 1V:2H back up from pavement to the DEM
inline constexpr float kGroundFill    = 0.5f;     // fill slope 1V:2H down from at-grade pavement (no floating verge)
inline constexpr float kGroundReach   = 60.0f;    // slopes stop binding this far past the pavement band
inline constexpr float kGroundIdw     = 300.0f;   // DEM blend radius; weight 1 / (d^2 + kGroundSoft^2)
inline constexpr float kGroundSoft    = 30.0f;
// N9 HUD per road id (MUTCD 2D.10 route shields, 2D.13 cardinal directions): Interstate shield ("" = parkway, no
// shield), name, and the signed direction of travel along +s / -s. +s runs config/queens_network.toml from -> to; the
// letter is that real bearing on the route's signed axis (even Interstates and the GCP E-W, the odd ones and the CIP
// N-S), so the BQE north to Astoria is I-278 East. The world frame is the LIE's (it runs along -z), not
// compass-aligned.
struct HudRoad {
    const char* shield;
    const char* name;
    char        up, down;
};
inline constexpr HudRoad kHudRoads[]  = {{"I-495", "Long Island Expy", 'E', 'W'}, {"I-278", "BQE", 'E', 'W'},
                                         {"", "Grand Central Pkwy", 'E', 'W'},    {"I-678", "Van Wyck", 'S', 'N'},
                                         {"I-295", "Clearview", 'S', 'N'},        {"", "Cross Island Pkwy", 'S', 'N'}};
inline constexpr int     kHudRampHops = 8;  // ramp chain followed this far to find where it lands
inline constexpr int     kHudMapEvery = 4;  // minimap ramp lines keep every 4th 4 m point
}  // namespace network

// N8 network landmarks (debug; docs/queens-network-reference/landmarks.md, tuning notes N8). Placed from the network's
// LMRK block (config/queens_network.toml `landmarks`). Metres unless named; "(to check)" sizes are picks from photos.
namespace netmarks {
enum class Kind : unsigned char { Gas, Arch, Astoria, Jamaica, Fence, Runway, Terminal, Citi, Ashe, Kew, Throgs };
// #1 GCP median service area, drawn left of the eastbound lanes (both reference photos are eastbound, station on the
// left; the compact GCP has an NJ median, no room for it in the middle). Canopy about 30 x 10 m, 4 pump islands.
inline constexpr float kGasSide = -1.0f, kGasOut = 24.0f;  // canopy centre this far past the outer edge
inline constexpr float kCanopyL = 30.0f, kCanopyW = 10.0f, kCanopyH = 4.9f, kFascia = 1.2f;  // 16 ft clearance
inline constexpr float kKioskL = 9.0f, kKioskW = 6.0f, kKioskH = 3.2f, kKioskRoof = 2.6f, kKioskBack = 14.0f;
inline constexpr float kPriceUp = 30.0f, kPriceH = 6.5f, kPriceW = 2.4f, kPriceTall = 3.0f;  // pole sign upstream
inline constexpr float kPriceNits    = 400.0f;    // LED price digits, night-dimmed level (slider lm_sign_nits)
inline constexpr float kCanopyNits   = 1500.0f;   // recessed canopy luminaires (lit with the street lamps)
inline constexpr float kCanopyLumens = 40000.0f;  // per spot, two spots (about 8 LED canopy fixtures)
// #2 stone arch: the D4 stone kit (emit_arch) on the GCP's faces over the CIP; wing walls run on past the piers.
inline constexpr float kArchWing = 25.0f, kArchRing = 0.75f;
inline constexpr float kHaunchFt = 10.5f;  // posted 10 ft 6 in at the haunches (photo): spring at least this high
// #3 Astoria Blvd station (BMT, rebuilt 2019-2020): along 31st St over the GCP, platform and mezzanine levels.
inline constexpr float kStaLen = 150.0f, kStaW = 14.0f, kStaRise = 7.0f, kStaRoof = 3.6f, kStaCol = 15.0f;
inline constexpr float kMezzLen = 60.0f, kMezzH = 3.0f;
inline constexpr float kSnap = 60.0f;  // an underpass landmark snaps to the nearest OSM crossing this close
// #4 Jamaica Yard bridge: two riveted plate-girder spans on granite abutments and a centre pier, 12 ft 7 in posted.
inline constexpr float kYardClearFt = 12.583f, kYardGirder = 2.4f, kYardPier = 1.6f, kYardAbut = 2.5f;
inline constexpr float kYardReplace = 40.0f;  // yard-lead crossings this close are this one bridge (drawn here)
inline constexpr float kStiffEvery  = 2.0f;   // web stiffeners (the riveted panels in the photo)
// #5 LaGuardia: white concrete wall + chain-link past the GCP's north edge; runway 45.7 m (OSM), edge lights.
inline constexpr float kFenceOut = 30.0f, kFenceWall = 1.2f, kFenceH = 2.4f, kFencePost = 3.0f;
inline constexpr float kRunwayW = 45.7f, kRunwayLight = 60.0f, kRunwayNits = 6000.0f;
inline constexpr float kTermL = 400.0f, kTermD = 70.0f, kTermH = 20.0f, kTowerH = 70.0f, kCabR = 9.0f;
// #6 Citi Field (41,922 seats, about 260 x 230 m) and Arthur Ashe Stadium (about 190 m across). Semi-axes, heights.
inline constexpr float kCitiA = 130.0f, kCitiB = 112.0f, kCitiH = 28.0f, kCitiTop = 38.0f, kCitiMast = 58.0f;
inline constexpr float kAsheR = 95.0f, kAsheH = 37.0f, kAsheRoof = 8.0f;
inline constexpr float kMastNits = 8000.0f;  // stadium light banks (lit with the street lamps)
inline constexpr float kSightRun = 350.0f;   // tree-free fan to a stadium / the terminal from this far along (D6)
// #7 Kew Gardens (2010-2022 rebuild): light-blue steel plate girders under the elevated ramps.
inline constexpr float kKewGirder = 1.6f;
// #8 Throgs Neck Bridge (WP-TNB): towers 326 ft (99 m) over the islands, deck 142 ft (43 m) over the water, side
// spans 555 ft; drawn at kSkyDist and scaled like the D5 skyline (the bridge is 5 km past the far plane).
inline constexpr float kTnbTower = 99.0f, kTnbDeck = 43.0f, kTnbSide = 169.0f, kTnbHalfW = 14.0f, kTnbLeg = 6.0f;
inline constexpr float kTnbSight =
    1500.0f;  // tree-free line from each nearby wall toward it (towers clear trees past it)
inline constexpr float kTnbDraw = 1000.0f;  // drawn this far out (the scene haze at kSkyDist washed a grey tower out)
}  // namespace netmarks

// N2b work zones (debug; docs/queens-network-reference/workzone.md). Feet / inches where MUTCD gives them, else m.
namespace workzone {
inline constexpr float kSpeedMph = 50.0f;  // S: pre-construction posted speed (GCP / parkways, OSM)
inline constexpr float kSignAFt = 1000.0f, kSignBFt = 1500.0f, kSignCFt = 2640.0f;  // MUTCD Table 6B-1 freeway
// MUTCD Table 6B-2 longitudinal buffer (ft) at 45, 50, ... 70 mph
inline constexpr float kBufferFt[6] = {360.0f, 425.0f, 495.0f, 570.0f, 645.0f, 730.0f};
inline constexpr float kNySpaceFt   = 40.0f;  // NYSDOT 619: channelizers in the work space
inline constexpr float kDownFt      = 20.0f;  // MUTCD 6B.08: downstream taper device spacing
inline constexpr float kGapLen      = 60.0f;  // median U-turn gap (crossover), m
inline constexpr float kGapClear    = 25.0f;  // gap ends this far before the hold
inline constexpr float kFaceBack    = 8.0f;   // barrier face past the hold (car nose + cushion)
inline constexpr float kTaperMinK   = 0.1f;   // a squeezed transition keeps at least this share of L and buffer
inline constexpr float kRampClear   = 30.0f;  // transition starts at least this far past the nearest junction
inline constexpr float kDrumHIn = 36.0f, kDrumDIn = 23.0f, kBandIn = 6.0f;  // MUTCD 6K.06, typical product
inline constexpr float kConeHIn = 28.0f, kConeBaseIn = 14.0f;               // MUTCD 6K.03 freeway cone
inline constexpr float kBarricadeW = 2.44f, kBarricadeH = 1.52f;            // Type III, 8 ft rails, 5 ft (NYSDOT)
inline constexpr float kRailIn = 8.0f;
inline constexpr float kTcbHIn = 32.0f, kTcbBaseIn = 24.0f, kTcbSegFt = 20.0f, kTcbEndIn = 6.0f;  // NYSDOT 619-01
inline constexpr float kBoardWIn = 96.0f, kBoardHIn = 48.0f, kBoardLiftFt = 7.0f;                 // Type C arrow board
inline constexpr float kSignLiftFt = 7.0f;                  // urban sign mounting height (MUTCD 6F.02)
inline constexpr float kBarrelD = 0.91f, kBarrelH = 0.95f;  // sand-filled crash cushion barrel
inline constexpr float kWarnFpm  = 60.0f;                   // Type A warning light, MUTCD 6L.07 55-75 flashes / min
inline constexpr float kLampNits = 9000.0f;                 // arrow-board amber lamp (bright enough to read at 1 mile)
inline constexpr float kExitGore = 60.0f;                   // EXIT CLOSED zone this far past a ramp's gore
inline constexpr float kExitHold = 9.0f;                    // N6: m, the car stops this far before its barricades
}  // namespace workzone

static_assert(kExit.diverge_ft + kExit.decel_ft == 1000.0f, "exit ramp spans 1000 ft");
static_assert(kExit.end_frac > 0.0f && kExit.end_frac < 1.0f);

}  // namespace swish::road
