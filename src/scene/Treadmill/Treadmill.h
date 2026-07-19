#pragma once

#include "../SceneTypes.h"  // Ribbon, LightDesc

#include <vector>

struct GLFWwindow;

namespace swish {

class Renderer;
class CarEntity;

// ── Treadmill ─────────────────────────────────────────────────────────
// The endless-road illusion, extracted from App::run(). Owns all state that
// makes a finite authored intro + one canonical chunk read as an infinite road:
//
//  • Origin rebase — keeps the car's render-frame Z small (float32 precision) by
//    shifting the world back toward 0 in whole chunk lengths. renderZ = trueZ +
//    origin_shift(); the periodic tile looks identical after a rebase, so it's
//    invisible. Intro lamp point-lights are shifted with it so they stay on their
//    posts.
//  • Active chunk window — the set of canonical-chunk instances around the car.
//  • Sparse elevated interchanges (~1.5 mi apart) around the car.
//  • Interchange ribbon-follower — when the car enters a ramp it "attaches" to a
//    graded ribbon and its position/heading/pitch are driven by that curve until
//    a junction/exit (guided, not on-rails: you still steer within the ribbon).
//
// The scene-setup step feeds it the authored intro length, lamp lights, and the
// interchange ribbons; then update() runs once per frame.
class Treadmill {
public:
    Treadmill();
    ~Treadmill();

    // Called from scene setup once the road is generated.
    void set_intro(float introLen, std::vector<LightDesc> sceneLights);
    void set_ribbons(std::vector<Ribbon> ribbons);

    // Per-frame update: rebases the origin, refreshes the chunk/interchange
    // windows, feeds drivable bounds to the car, and advances the ribbon-follower.
    // Mutates the car's transform/bounds and pushes chunk/interchange/light data
    // to the renderer — the same side effects the inline code in run() had.
    void update(CarEntity& car, Renderer& renderer, GLFWwindow* window, float deltaTime, bool debugEdit);

    // renderZ = trueZ + origin_shift(). Exposed for callers that need to convert.
    double origin_shift() const { return m_originShift; }
    float  intro_len() const { return m_introLen; }

private:
    // m_introLen = authored road length (WU); tiled with one canonical chunk past
    // its far end. m_originShift = accumulated origin-rebase (WU, double), kept a
    // whole number of chunk lengths so the periodic tile looks identical.
    float  m_introLen    = 0.0f;
    double m_originShift = 0.0;
    // Intro lamp point-lights, shifted into the render frame on each rebase.
    std::vector<LightDesc> m_sceneLights;

    // Interchange ribbon-follower state. m_ixRibbons are interchange-local;
    // m_ixTrueZ is the attached instance's true Z (→ render offset = m_ixTrueZ +
    // m_originShift, survives rebases).
    std::vector<Ribbon> m_ixRibbons;
    int                 m_carRibbon = -1;  // -1 = free driving
    float               m_ribbonS   = 0.0f;
    float               m_ribbonT   = 0.0f;
    double              m_ixTrueZ   = 0.0;
};

}  // namespace swish
