// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: DNA Helix
// Author:  madarswork + Claude
// Lineage: original
//
// A double helix in real 3D, running the long way down the panel. Two
// backbones of glossy spheres wind around the axis with the major and minor
// grooves of the real molecule, joined by base-pair rungs in four vivid
// colours. Every sphere is ray-shaded (diffuse + specular highlight) into a
// z-buffer, so the strands genuinely pass in front of and behind each other;
// perspective makes near spheres swell and far ones shrink and dim, while the
// whole molecule spins, snakes and leans toward you.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace DnaHelix {
const char* NAME = "DNA Helix";
const char* const KNOB_LABELS[4] = {"spin", "twist", "distance", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGBf { float r, g, b; };
constexpr int NPAL = 5;
// backbone 1, backbone 2, then the four bases (A, T, G, C)
const RGBf PAL[NPAL][6] = {
  {{0.0f, 0.35f, 1.0f}, {1.0f, 0.0f, 0.4f}, {1.0f, 0.85f, 0.0f}, {0.0f, 1.0f, 0.4f}, {1.0f, 0.3f, 0.0f}, {0.0f, 0.9f, 1.0f}},
  {{1.0f, 1.0f, 1.0f}, {0.0f, 0.85f, 1.0f}, {1.0f, 0.0f, 0.6f}, {1.0f, 0.9f, 0.0f}, {0.5f, 0.0f, 1.0f}, {0.2f, 1.0f, 0.0f}},
  {{1.0f, 0.3f, 0.0f}, {1.0f, 0.8f, 0.0f}, {1.0f, 0.0f, 0.2f}, {1.0f, 1.0f, 0.6f}, {0.8f, 0.0f, 1.0f}, {1.0f, 0.55f, 0.0f}},
  {{0.0f, 1.0f, 0.4f}, {0.6f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.8f}, {0.0f, 0.8f, 1.0f}, {1.0f, 0.4f, 0.0f}},
  {{0.9f, 0.9f, 1.0f}, {0.3f, 0.3f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.4f, 1.0f}, {1.0f, 0.2f, 0.6f}},
};

struct Params {
  float spin = 1.0f;
  float twist = 0.55f;     // radians of helix turn per unit of axis
  float distance = 6.0f;
  int palette = 0;
};
Params params;

float* zbuf = nullptr;
uint8_t* col = nullptr;
float spin = 0.0f, sway = 0.0f, lean = 0.0f;

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

// Light from upper left, toward the viewer.
constexpr float LX = -0.48f, LY = -0.56f, LZ = 0.67f;
constexpr float HX = -0.24f, HY = -0.28f, HZ = 0.93f;   // half vector (light + view)

// One shaded sphere at screen (sx, sy), projected radius pr, depth z.
void sphere(float sx, float sy, float pr, float z, const RGBf& c, float fog) {
  if (pr < 0.4f) pr = 0.4f;
  const int x0 = PFMath::ifloor(sx - pr - 1.0f), x1 = PFMath::ifloor(sx + pr + 1.0f);
  const int y0 = PFMath::ifloor(sy - pr - 1.0f), y1 = PFMath::ifloor(sy + pr + 1.0f);
  const float ipr = frcp(pr);
  for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < FH; y++) {
    const float dy = ((float)y + 0.5f - sy) * ipr;
    for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < FW; x++) {
      const float dx = ((float)x + 0.5f - sx) * ipr;
      const float d2 = dx * dx + dy * dy;
      if (d2 >= 1.0f + 2.0f * ipr) continue;
      // Edge coverage, in pixels, for an anti-aliased rim.
      const float d = d2 * frsqrt(d2 + 1e-6f);
      float cov = (1.0f - d) * pr + 0.5f;
      if (cov <= 0.0f) continue;
      if (cov > 1.0f) cov = 1.0f;
      const float nz2 = 1.0f - d2;
      const float nz = nz2 > 0.0f ? nz2 * frsqrt(nz2 + 1e-6f) : 0.0f;
      const float depth = z + nz * pr * 0.02f;
      const int i = y * FW + x;
      if (depth < zbuf[i]) continue;
      zbuf[i] = depth;
      float diff = dx * LX + dy * LY + nz * LZ;
      diff = diff < 0.0f ? 0.0f : diff;
      float sp = dx * HX + dy * HY + nz * HZ;
      sp = sp < 0.0f ? 0.0f : sp;
      sp *= sp; sp *= sp; sp *= sp; sp *= sp;   // ^16
      const float k = (0.12f + 0.88f * diff) * fog;
      const float w = sp * 0.9f * fog;
      float r = (c.r * k + w) * 255.0f, g = (c.g * k + w) * 255.0f, b = (c.b * k + w) * 255.0f;
      uint8_t* p = col + i * 3;
      p[0] = (uint8_t)(p[0] + ((r > 255.0f ? 255.0f : r) - p[0]) * cov);
      p[1] = (uint8_t)(p[1] + ((g > 255.0f ? 255.0f : g) - p[1]) * cov);
      p[2] = (uint8_t)(p[2] + ((b > 255.0f ? 255.0f : b) - p[2]) * cov);
    }
  }
}

void setup() {
  PFMath::buildSinLUT();
  if (!zbuf) zbuf = PFMem::allocFloats(FW * FH);
  if (!col) col = (uint8_t*)PFMem::alloc(FW * FH * 3);
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &params.spin, -3.0f, 3.0f, 6.0f / 48.0f);
  PFParams::apply(input, 1, &params.twist, 0.25f, 1.1f, 0.85f / 48.0f);
  PFParams::apply(input, 2, &params.distance, 3.5f, 12.0f, 8.5f / 48.0f);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);
  const float W = 2000.0f * PFMath::TWO_PI_F;
  spin += dt * params.spin * 1.2f;
  if (spin > W) spin -= W;
  if (spin < -W) spin += W;
  sway += dt * 0.8f;
  if (sway > W) sway -= W;
  lean += dt * 0.23f;
  if (lean > W) lean -= W;
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  if (!zbuf || !col) { PFCanvas::clear(); PFCanvas::present(); return; }
  for (int i = 0; i < FW * FH; i++) zbuf[i] = -1e9f;
  memset(col, 0, FW * FH * 3);

  const RGBf* pal = PAL[params.palette];
  const float longS = (float)(FW > FH ? FW : FH), shortS = (float)(FW < FH ? FW : FH);
  const float D = params.distance;
  const float F = shortS * 0.24f * 6.0f;      // helix (radius 1) spans ~half the short side at D = 6
  const float pMid = F / D;
  const float cx = FW * 0.5f, cy = FH * 0.5f;
  const float tilt = 0.32f * PFMath::fastSin(lean);
  const float ct = PFMath::fastCos(tilt), st = PFMath::fastSin(tilt);
  const float sMax = longS * 0.5f / pMid * 1.25f + 1.0f;
  const float step = 0.62f;                   // axis spacing of base pairs
  const float groove = 2.4f;                  // strand offset: major / minor groove
  const float ballR = 0.24f, rungR = 0.13f;

  // Project one point of the molecule (axis s, helix-plane b / c) and shade a
  // sphere there. Everything goes through the z-buffer, so order is free.
  auto put = [&](float s, float b, float c, float r, const RGBf& colr) {
    b += 0.35f * PFMath::fastSin(s * 0.22f + sway);       // the axis snakes sideways
    const float a2 = s * ct - c * st;                       // and leans toward you
    const float c2 = s * st + c * ct;
    const float p = F * frcp(D - c2);
    float sx, sy;
    if (PF_PORTRAIT) { sx = cx + b * p; sy = cy + a2 * p; }
    else { sx = cx + a2 * p; sy = cy + b * p; }
    float fog = p / pMid;
    fog = fog * fog;
    fog = fog > 1.3f ? 1.3f : (fog < 0.15f ? 0.15f : fog);
    sphere(sx, sy, r * p, c2, colr, fog * 0.9f);
  };

  const int n = (int)(sMax / step);
  const float tw = params.twist * 2.0f;
  for (int k = -n; k <= n; k++) {
    // Backbones: three overlapping spheres per base pair read as a smooth tube.
    for (int sub = 0; sub < 3; sub++) {
      const float s = (k + sub * (1.0f / 3.0f)) * step;
      const float phi = s * tw + spin;
      put(s, PFMath::fastCos(phi), PFMath::fastSin(phi), ballR, pal[0]);
      put(s, PFMath::fastCos(phi + groove), PFMath::fastSin(phi + groove), ballR, pal[1]);
    }
    // The rung: a base pair, A-T or G-C picked per rung, each half its own colour.
    const float s = k * step;
    const float phi = s * tw + spin;
    const float b0 = PFMath::fastCos(phi), c0 = PFMath::fastSin(phi);
    const float b1 = PFMath::fastCos(phi + groove), c1 = PFMath::fastSin(phi + groove);
    const int pairGC = (int)(((uint32_t)k * 2654435761u >> 7) & 1u);
    const int flip = (int)(((uint32_t)k * 2246822519u >> 9) & 1u);
    for (int j = 0; j < 5; j++) {
      const float u = (j + 0.5f) * 0.2f;
      const int base = pairGC * 2 + (((u < 0.5f) ? 1 : 0) ^ flip);
      put(s, (b0 + (b1 - b0) * u) * 0.92f, (c0 + (c1 - c0) * u) * 0.92f, rungR, pal[2 + base]);
    }
  }

  for (int y = 0; y < FH; y++)
    for (int x = 0; x < FW; x++) {
      const uint8_t* p = col + (y * FW + x) * 3;
      PFCanvas::setPixel(x, y, p[0], p[1], p[2]);
    }
  PFCanvas::present();
}
}  // namespace DnaHelix

PF_REGISTER_PATTERN(DnaHelix)
