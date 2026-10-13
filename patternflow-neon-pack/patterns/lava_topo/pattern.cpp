// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Lava Topo
// Author:  madarswork + Claude
// Lineage: original
//
// Metaballs drawn as a neon contour map. Several blobs swing along their own
// Lissajous orbits; their combined field is sliced into hard-edged bands, each
// band a different saturated colour, separated by crisp black contour lines,
// with the bands flowing outward like heat rising. Where blobs meet, the
// contours pinch and merge; at their hearts the field burns white. Contour
// lines stay 1 px wide everywhere because their width comes from the field's
// own per-pixel gradient (measured against the previous pixel and row).
// The field is summed on a half-resolution grid and interpolated.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace LavaTopo {
const char* NAME = "Lava Topo";
const char* const KNOB_LABELS[4] = {"speed", "blobs", "bands", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGB { uint8_t r, g, b; };
constexpr int NPAL = 6;
constexpr int NC = 5;
// Five band colours cycling outward, and the colour a blob's heart burns.
const RGB PAL[NPAL][NC + 1] = {
  {{90, 0, 255}, {255, 0, 160}, {255, 60, 0}, {255, 190, 0}, {0, 230, 255}, {255, 255, 160}},   // lava neon
  {{0, 40, 255}, {0, 200, 255}, {0, 255, 140}, {200, 255, 0}, {120, 0, 255}, {255, 255, 255}},  // thermal cold
  {{255, 0, 60}, {255, 120, 0}, {255, 230, 0}, {200, 0, 255}, {255, 0, 200}, {255, 250, 220}},  // magma
  {{255, 0, 255}, {0, 255, 255}, {255, 255, 0}, {255, 0, 90}, {0, 90, 255}, {255, 255, 255}},   // CMY strobe
  {{0, 120, 60}, {0, 255, 120}, {180, 255, 0}, {255, 0, 120}, {255, 120, 0}, {255, 255, 255}},  // jungle
  {{40, 0, 160}, {140, 0, 255}, {255, 0, 200}, {0, 220, 255}, {255, 120, 255}, {255, 255, 255}}, // ultraviolet
};

struct Params {
  float speed = 1.0f;
  int blobs = 3;          // index: 3..8 blobs
  float bands = 2.2f;
  int palette = 0;
};
Params params;

constexpr int MAXB = 8;
float bx[MAXB], by[MAXB], br2[MAXB];
float clock = 0.0f, flow = 0.0f;
float prevRow[FW];
// The field is smooth, so it is evaluated on every other pixel and
// interpolated: a quarter of the blob sums for the same picture.
constexpr int GW = FW / 2 + 1, GH = FH / 2 + 1;
float grid[GW * GH];

// Orbit shape for each blob: frequencies and phases, chosen to never repeat soon.
const float FX[MAXB] = {0.31f, 0.47f, 0.23f, 0.53f, 0.37f, 0.29f, 0.61f, 0.43f};
const float FY[MAXB] = {0.43f, 0.29f, 0.51f, 0.37f, 0.59f, 0.41f, 0.33f, 0.27f};
const float PX[MAXB] = {0.0f, 1.7f, 3.1f, 4.4f, 2.3f, 5.1f, 0.9f, 3.8f};
const float PY[MAXB] = {1.1f, 0.3f, 2.6f, 5.0f, 3.9f, 1.8f, 4.6f, 0.6f};

inline float frcp(float x) {
  union { float f; uint32_t i; } u = {x};
  u.i = 0x7EF311C3u - u.i;
  float y = u.f;
  y = y * (2.0f - x * y);
  return y * (2.0f - x * y);
}

inline float frsqrt(float x) {
  union { float f; uint32_t i; } u = {x};
  u.i = 0x5f375a86u - (u.i >> 1);
  float y = u.f;
  return y * (1.5f - 0.5f * x * y * y);
}

inline uint8_t u8(float v) { return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : (uint8_t)v); }

void setup() {
  PFMath::buildSinLUT();
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &params.speed, 0.0f, 4.0f, 4.0f / 48.0f);
  PFParams::applyIndex(input, 1, &params.blobs, 6);
  PFParams::apply(input, 2, &params.bands, 0.8f, 4.0f, 3.2f / 48.0f);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);
  clock += dt * params.speed;
  if (clock > 2000.0f * PFMath::TWO_PI_F) clock -= 2000.0f * PFMath::TWO_PI_F;
  flow += dt * (0.25f + params.speed * 0.35f);
  if (flow > 500.0f) flow -= 500.0f;   // a multiple of NC
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  const RGB* pal = PAL[params.palette];
  const int nb = 3 + params.blobs;
  const float minDim = (float)(FW < FH ? FW : FH);

  for (int i = 0; i < nb; i++) {
    bx[i] = FW * 0.5f + FW * 0.36f * PFMath::fastSin(clock * FX[i] * 2.0f + PX[i]);
    by[i] = FH * 0.5f + FH * 0.36f * PFMath::fastSin(clock * FY[i] * 2.0f + PY[i]);
    const float r = minDim * (0.12f + 0.035f * PFMath::fastSin(clock * 1.3f + i * 1.7f));
    br2[i] = r * r;
  }
  const float dens = params.bands;

  // 1/sqrt(field) is an effective distance (in blob radii): slicing it gives
  // evenly spaced contours, like a real topographic map. Near one blob it is a
  // cone in x and y, which interpolates almost exactly.
  for (int j = 0; j < GH; j++) {
    const float fy = (float)(j * 2) + 0.5f;
    for (int i = 0; i < GW; i++) {
      const float fx = (float)(i * 2) + 0.5f;
      float F = 0.0f;
      for (int b = 0; b < nb; b++) {
        const float dx = fx - bx[b], dy = fy - by[b];
        F += br2[b] * frcp(dx * dx + dy * dy + 1.0f);
      }
      grid[j * GW + i] = frsqrt(F + 1e-6f);
    }
  }

  for (int y = 0; y < FH; y++) {
    float prevX = 0.0f;
    const float* g0 = grid + (y >> 1) * GW;
    const float* g1 = g0 + GW;
    const float wy = (y & 1) ? 0.5f : 0.0f;
    for (int x = 0; x < FW; x++) {
      const int i = x >> 1;
      const float wx = (x & 1) ? 0.5f : 0.0f;
      const float top = g0[i] + (g0[i + 1] - g0[i]) * wx;
      const float bot = g1[i] + (g1[i + 1] - g1[i]) * wx;
      const float dEff = top + (bot - top) * wy;
      const float level = (2.6f - dEff) * dens;
      const float band = level + flow;

      // The field's change per pixel, from the neighbours already computed.
      const float up = y > 0 ? prevRow[x] : level;
      const float left = x > 0 ? prevX : level;
      float grad = fabsf(level - left) + fabsf(level - up);
      prevRow[x] = level;
      prevX = level;
      if (grad < 0.02f) grad = 0.02f;

      const int k = PFMath::ifloor(band);
      const float f = band - (float)k;
      // Contour line: distance to the band edge in pixels.
      float edge = (f < 1.0f - f ? f : 1.0f - f) * frcp(grad);
      float line = 1.0f - (edge - 0.35f) * 1.6f;
      line = line < 0.0f ? 0.0f : (line > 1.0f ? 1.0f : line);
      // Bands too dense to draw dissolve rather than alias.
      const float crowd = grad > 0.45f ? (grad - 0.45f) * 2.5f : 0.0f;
      line *= crowd > 1.0f ? 0.0f : 1.0f - crowd;

      // Colour by band, brighter toward each band's inner edge, dark far out.
      int ci = k % NC;
      if (ci < 0) ci += NC;
      const RGB& c = pal[ci];
      float lit = 0.45f + 0.55f * f;
      float outer = (2.6f - dEff) * 1.8f;
      outer = outer < 0.0f ? 0.0f : (outer > 1.0f ? 1.0f : outer);
      lit *= outer * outer;
      float cr = c.r * lit, cg = c.g * lit, cb = c.b * lit;

      // White-hot cores.
      if (dEff < 0.62f) {
        float h = (0.62f - dEff) * 7.0f;
        h = h > 1.0f ? 1.0f : h;
        const RGB& hc = pal[NC];
        cr += (hc.r - cr) * h; cg += (hc.g - cg) * h; cb += (hc.b - cb) * h;
        line *= 1.0f - h;
      }
      const float keep = 1.0f - line;
      PFCanvas::setPixel(x, y, u8(cr * keep), u8(cg * keep), u8(cb * keep));
    }
  }
  PFCanvas::present();
}
}  // namespace LavaTopo

PF_REGISTER_PATTERN(LavaTopo)
