#version 450

// ── Depth-only (shadow map) fragment shader ───────────────────────────
// The shadow pass writes depth only; there is no color attachment. An empty
// fragment shader is provided anyway for MoltenVK compatibility: MoltenVK
// (and the Metal backend) expect a complete vertex+fragment stage pair, and
// omitting the fragment stage can trip pipeline creation. Depth is written
// automatically from gl_Position by fixed-function; main() does nothing.

void main() {}
