// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Warp Tunnel
// Author:  madarswork + Claude
// Lineage: original
//
// Flying down a curving neon tunnel. Each ring sits at depth z = K / r, so the
// tiles race outward and stretch as they pass you; the far end bends toward
// a vanishing point that wanders, so the tunnel snakes. Checker tiles in the
// palette's colours, white-hot seams, a light ring every eighth band, and
// black fog at the far end.
//
// Every pixel solves for its own depth (a fixed-point step against the
// bending centre) with multiply-only rsqrt / reciprocal - no sqrtf, no
// division, no libm in the pixel loop.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace WarpTunnel {
const char* NAME = "Warp Tunnel";
const char* const KNOB_LABELS[4] = {"speed", "twist", "curve", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGB { uint8_t r, g, b; };
constexpr int NPAL = 6;
// Four tile colours and a seam colour per palette, picked for hard contrast.
const RGB PAL[NPAL][5] = {
  {{255, 0, 150}, {0, 220, 255}, {255, 210, 0}, {120, 0, 255}, {255, 255, 255}},  // neon
  {{255, 20, 0}, {255, 120, 0}, {255, 0, 80}, {255, 220, 30}, {255, 250, 200}},   // fire
  {{80, 255, 0}, {170, 0, 255}, {0, 255, 160}, {255, 0, 200}, {230, 255, 230}},   // toxic
  {{0, 70, 255}, {0, 220, 255}, {90, 0, 255}, {200, 240, 255}, {255, 255, 255}},  // ice
  {{255, 0, 0}, {255, 230, 0}, {0, 255, 60}, {0, 90, 255}, {255, 255, 255}},      // primary
  {{255, 0, 110}, {255, 140, 0}, {0, 200, 255}, {60, 0, 200}, {255, 230, 120}},   // sunset
};

struct Params {
  float speed = 1.3f;
  float twist = 0.5f;
  float curve = 0.6f;
  int palette = 0;
};
Params params;
float travel = 0.0f;   // distance flown, in ring units
float spin = 0.0f;     // segment rotation
float wander = 0.0f;   // vanishing-point clock

inline float frsqrt(float x) {
  union { float f; uint32_t i; } u = {x};
  u.i = 0x5f375a86u - (u.i >> 1);
  float y = u.f;
  y = y * (1.5f - 0.5f * x * y * y);
  return y * (1.5f - 0.5f * x * y * y);
}

inline float frcp(float x) {
  union { float f; uint32_t i; } u = {x};
  u.i = 0x7EF311C3u - u.i;
  float y = u.f;
  y = y * (2.0f - x * y);
  return y * (2.0f - x * y);
}

// atan2 in turns, 0..1, with the division done by frcp.
inline float atanTurns(float y, float x) {
  float ax = fabsf(x), ay = fabsf(y);
  float mx = ax > ay ? ax : ay;
  if (mx < 1e-6f) return 0.0f;
  float mn = ax > ay ? ay : ax;
  float a = mn * frcp(mx);
  float s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0.0f) r = 3.14159274f - r;
  if (y < 0.0f) r = -r;
  return r * 0.15915494f + 0.5f;
}

inline uint8_t u8(float v) {
  return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : (uint8_t)v);
}

void setup() {
  PFMath::buildSinLUT();
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &params.speed, 0.0f, 4.0f, 4.0f / 48.0f);
  PFParams::apply(input, 1, &params.twist, -2.0f, 2.0f, 4.0f / 48.0f);
  PFParams::apply(input, 2, &params.curve, 0.0f, 1.0f, 1.0f / 48.0f);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);

  travel += dt * params.speed * 2.2f;
  if (travel > 4096.0f) travel -= 4096.0f;   // multiple of every period used below
  spin += dt * (0.15f + params.speed * 0.05f);
  if (spin > 1024.0f) spin -= 1024.0f;
  wander += dt * (0.35f + params.speed * 0.12f);
  if (wander > 2000.0f * PFMath::TWO_PI_F) wander -= 2000.0f * PFMath::TWO_PI_F;
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  const RGB* pal = PAL[params.palette];

  const float minDim = (float)(FW < FH ? FW : FH);
  const float K = minDim * 0.55f;            // depth constant: z = K / r
  const float SEG = 12.0f;                   // tiles around the ring (even: checker closes)
  const float RINGS = 2.0f;                  // rings per unit of depth

  // Near centre sways a little; the vanishing point wanders a lot.
  const float cx0 = FW * 0.5f + PFMath::fastSin(wander * 0.61f) * FW * 0.05f;
  const float cy0 = FH * 0.5f + PFMath::fastCos(wander * 0.47f) * FH * 0.05f;
  const float vpx = params.curve * PFMath::fastSin(wander) * FW * 0.42f;
  const float vpy = params.curve * PFMath::fastSin(wander * 0.73f + 1.3f) * FH * 0.36f;
  // The glow drifts through the palette, one colour every eight rings.
  const float gph = travel * 0.125f;
  const int gi = PFMath::ifloor(gph);
  const float gf = gph - (float)gi;
  const RGB& g0 = pal[gi & 3];
  const RGB& g1 = pal[(gi + 1) & 3];
  const RGB glowC = {(uint8_t)(g0.r + (g1.r - g0.r) * gf), (uint8_t)(g0.g + (g1.g - g0.g) * gf),
                     (uint8_t)(g0.b + (g1.b - g0.b) * gf)};
  const float glowInv = 1.0f / (minDim * 0.40f);
  const float invRK = 1.0f / (RINGS * K * 1.2f);
  const float invSG = 1.0f / (SEG * 0.159155f * 1.2f);

  for (int y = 0; y < FH; y++) {
    const float dy0 = (float)y + 0.5f - cy0;
    for (int x = 0; x < FW; x++) {
      const float dx0 = (float)x + 0.5f - cx0;

      // Depth with the bend: a ring at depth z is centred at c0 + vp * s(z),
      // s easing from 0 (near) to 1 (deep). One fixed-point step settles it.
      float d2 = dx0 * dx0 + dy0 * dy0 + 0.01f;
      float t = K * frsqrt(d2) * 0.28f;
      t = t > 1.0f ? 1.0f : t;
      const float s = t * (2.0f - t);
      const float dx = dx0 - vpx * s, dy = dy0 - vpy * s;
      d2 = dx * dx + dy * dy + 0.01f;
      const float inv = frsqrt(d2);
      const float z = K * inv;
      const float r = d2 * inv;

      // Light at the end of the tunnel: a palette-coloured glow around the far end.
      float glow = 1.0f - r * glowInv;
      glow = glow > 0.0f ? glow * glow * glow * glow : 0.0f;

      // Fog by distance, and by footprint: once a tile is smaller than a few
      // pixels it would only alias, so it dissolves into the glow instead.
      const float duPx = RINGS * z * inv;              // rings per pixel
      const float dvPx = SEG * 0.159155f * inv;        // segments per pixel
      const float lod = duPx > dvPx ? duPx : dvPx;
      float fog = 1.0f - (lod - 0.22f) * 3.2f;
      const float core = glow * glow * 255.0f;   // white-hot centre
      if (fog <= 0.0f) {
        PFCanvas::setPixel(x, y, u8(glowC.r * glow + core), u8(glowC.g * glow + core),
                           u8(glowC.b * glow + core));
        continue;
      }
      if (fog > 1.0f) fog = 1.0f;
      fog *= fog;

      const float u = z * RINGS + travel;
      const float v = atanTurns(dy, dx) * SEG + z * params.twist + spin;
      const int iu = PFMath::ifloor(u);
      const int iv = PFMath::ifloor(v);
      const float fu = u - (float)iu;
      const float fv = v - (float)iv;

      // 1-px anti-aliased seams, sized from the same footprint.
      // 1 / duPx = d2 / (RINGS K) and 1 / dvPx = r / (SEG / 2pi): no division.
      const float eu = (fu < 1.0f - fu ? fu : 1.0f - fu) * d2 * invRK;
      const float ev = (fv < 1.0f - fv ? fv : 1.0f - fv) * r * invSG;
      float seam = 1.0f - (eu < ev ? eu : ev);
      if (seam < 0.0f) seam = 0.0f;
      seam *= seam;

      float cr = 0, cg = 0, cb = 0;
      const RGB& c = pal[iu & 3];
      if (((iu + iv) & 1) == 0) {
        // Lit tile: brighter at the edge facing you, so the ring reads as a surface.
        const float lit = 0.3f + 0.7f * fu * fu;
        cr = c.r * lit; cg = c.g * lit; cb = c.b * lit;
      }
      if ((iu & 7) == 0 && fu > 0.7f) {
        // A thin strip light every eighth ring - the speed cue.
        cr = pal[4].r; cg = pal[4].g; cb = pal[4].b;
      }
      const RGB& e = pal[4];
      cr += (e.r - cr) * seam;
      cg += (e.g - cg) * seam;
      cb += (e.b - cb) * seam;
      cr = cr * fog + (glowC.r * glow + core) * (1.0f - fog);
      cg = cg * fog + (glowC.g * glow + core) * (1.0f - fog);
      cb = cb * fog + (glowC.b * glow + core) * (1.0f - fog);

      PFCanvas::setPixel(x, y, u8(cr), u8(cg), u8(cb));
    }
  }
  PFCanvas::present();
}
}  // namespace WarpTunnel

PF_REGISTER_PATTERN(WarpTunnel)
