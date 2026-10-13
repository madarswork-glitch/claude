// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Tesseract
// Author:  madarswork + Claude
// Lineage: original
//
// A 4D hypercube turning through the fourth dimension. Its 16 corners rotate
// in the XW and YZ planes at once (a double rotation, so the inner cube
// swallows the outer one and turns inside out), then project 4D -> 3D -> 2D
// with real perspective on both steps. Edges are coloured by their depth in W
// and glow brighter the nearer they are; corners burn white. The camera
// dollies in and out, and a phosphor buffer leaves neon trails behind.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace Tesseract {
const char* NAME = "Tesseract";
const char* const KNOB_LABELS[4] = {"spin", "distance", "trails", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGBf { float r, g, b; };
constexpr int NPAL = 6;
// Colour of the W-near cell, the W-far cell, and the corners.
const RGBf PAL[NPAL][3] = {
  {{0.0f, 0.85f, 1.0f}, {1.0f, 0.0f, 0.6f}, {1.0f, 1.0f, 1.0f}},    // cyan / magenta
  {{1.0f, 0.8f, 0.0f}, {0.45f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.9f}},    // gold / violet
  {{0.3f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.35f}, {1.0f, 1.0f, 1.0f}},    // lime / hot pink
  {{1.0f, 0.35f, 0.0f}, {0.0f, 0.35f, 1.0f}, {1.0f, 0.95f, 0.8f}},  // orange / blue
  {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.9f}, {1.0f, 1.0f, 1.0f}},     // red / aqua
  {{1.0f, 1.0f, 1.0f}, {0.25f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}},    // white / ultraviolet
};

struct Params {
  float spin = 1.0f;
  float distance = 4.2f;
  float trails = 0.6f;
  int palette = 0;
};
Params params;

float* acc = nullptr;      // FW*FH*3 light accumulator (the phosphor)
float a4 = 0.0f, b4 = 0.0f, c4 = 0.0f, yaw = 0.0f, dolly = 0.0f;

int edgeA[32], edgeB[32];
float vx[16], vy[16], vz[16], vw[16];   // projected corner: screen x, y, depth z, rotated w

constexpr int NSTAR = 70;
float starX[NSTAR], starY[NSTAR], starZ[NSTAR];   // background stars drifting toward the camera

inline float frcp(float x) {
  union { float f; uint32_t i; } u = {x};
  u.i = 0x7EF311C3u - u.i;
  float y = u.f;
  y = y * (2.0f - x * y);
  return y * (2.0f - x * y);
}

inline void add(int x, int y, float w, const RGBf& c) {
  if ((unsigned)x >= (unsigned)FW || (unsigned)y >= (unsigned)FH) return;
  float* p = acc + (y * FW + x) * 3;
  p[0] += c.r * w;
  p[1] += c.g * w;
  p[2] += c.b * w;
}

// Bilinear deposit of one sample, plus a four-tap halo for thickness.
inline void splat(float fx, float fy, float core, float halo, const RGBf& c) {
  const int x0 = PFMath::ifloor(fx), y0 = PFMath::ifloor(fy);
  const float ax = fx - x0, ay = fy - y0;
  const float w00 = (1 - ax) * (1 - ay), w10 = ax * (1 - ay);
  const float w01 = (1 - ax) * ay, w11 = ax * ay;
  add(x0, y0, core * w00, c);
  add(x0 + 1, y0, core * w10, c);
  add(x0, y0 + 1, core * w01, c);
  add(x0 + 1, y0 + 1, core * w11, c);
  if (halo > 0.0f) {
    add(x0 - 1, y0, halo * (w00 + w01), c);
    add(x0 + 2, y0, halo * (w10 + w11), c);
    add(x0, y0 - 1, halo * (w00 + w10), c);
    add(x0, y0 + 2, halo * (w01 + w11), c);
  }
}

void setup() {
  PFMath::buildSinLUT();
  if (!acc) acc = (float*)PFMem::alloc(sizeof(float) * FW * FH * 3);
  for (int i = 0; i < NSTAR; i++) {
    starX[i] = (float)random(-1000, 1000) * 0.001f;
    starY[i] = (float)random(-1000, 1000) * 0.001f;
    starZ[i] = (float)random(50, 1000) * 0.001f;
  }
  int n = 0;
  for (int i = 0; i < 16; i++)
    for (int bit = 0; bit < 4; bit++) {
      const int j = i ^ (1 << bit);
      if (j > i) { edgeA[n] = i; edgeB[n] = j; n++; }
    }
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &params.spin, 0.0f, 3.0f, 3.0f / 48.0f);
  PFParams::apply(input, 1, &params.distance, 2.6f, 10.0f, 7.4f / 48.0f);
  PFParams::apply(input, 2, &params.trails, 0.0f, 0.93f, 0.93f / 48.0f);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);

  const float s = dt * params.spin;
  const float W = 2000.0f * PFMath::TWO_PI_F;
  a4 += s * 0.70f; if (a4 > W) a4 -= W;
  b4 += s * 0.43f; if (b4 > W) b4 -= W;
  c4 += s * 0.23f; if (c4 > W) c4 -= W;
  yaw += s * 0.31f; if (yaw > W) yaw -= W;
  dolly += dt * 0.35f; if (dolly > W) dolly -= W;

  for (int i = 0; i < NSTAR; i++) {
    starZ[i] -= dt * (0.04f + params.spin * 0.05f);
    if (starZ[i] < 0.05f) {
      starZ[i] += 0.95f;
      starX[i] = (float)random(-1000, 1000) * 0.001f;
      starY[i] = (float)random(-1000, 1000) * 0.001f;
    }
  }
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  if (!acc) { PFCanvas::clear(); PFCanvas::present(); return; }

  // Phosphor decay: at trails = 0 the buffer starts clean every frame.
  const float decay = params.trails;
  const int N = FW * FH * 3;
  if (decay <= 0.001f) memset(acc, 0, sizeof(float) * N);
  else for (int i = 0; i < N; i++) acc[i] *= decay;

  const float ca = PFMath::fastCos(a4), sa = PFMath::fastSin(a4);
  const float cb = PFMath::fastCos(b4), sb = PFMath::fastSin(b4);
  const float cc = PFMath::fastCos(c4), sc = PFMath::fastSin(c4);
  const float cy = PFMath::fastCos(yaw), sy = PFMath::fastSin(yaw);
  const float tilt = 0.42f, ct = cosf(tilt), st = sinf(tilt);

  const float minDim = (float)(FW < FH ? FW : FH);
  const float Dz = params.distance * (1.0f + 0.12f * PFMath::fastSin(dolly));
  const float F = minDim * 1.5f;
  const float cxs = FW * 0.5f, cys = FH * 0.5f;

  for (int i = 0; i < 16; i++) {
    float x = (i & 1) ? 1.0f : -1.0f;
    float y = (i & 2) ? 1.0f : -1.0f;
    float z = (i & 4) ? 1.0f : -1.0f;
    float w = (i & 8) ? 1.0f : -1.0f;
    // Double rotation: XW and YZ together, then a slower ZW.
    float t = x * ca - w * sa; w = x * sa + w * ca; x = t;
    t = y * cb - z * sb; z = y * sb + z * cb; y = t;
    t = z * cc - w * sc; w = z * sc + w * cc; z = t;
    // 4D -> 3D perspective from w = 3.
    const float k = 3.0f * frcp(3.0f - w) * 0.55f;
    x *= k; y *= k; z *= k;
    // View: yaw around the vertical, a fixed tilt toward the camera.
    t = x * cy - z * sy; z = x * sy + z * cy; x = t;
    t = y * ct - z * st; z = y * st + z * ct; y = t;
    // 3D -> 2D perspective.
    const float p = F * frcp(Dz - z);
    vx[i] = cxs + x * p;
    vy[i] = cys + y * p;
    vz[i] = z;
    vw[i] = w;
  }

  const RGBf* pal = PAL[params.palette];
  const float deposit = 0.42f * (1.0f - decay * 0.65f);

  // Stars first, faint: they give the black some depth without stealing contrast.
  for (int i = 0; i < NSTAR; i++) {
    const float iz = frcp(starZ[i]);
    const float px = cxs + starX[i] * iz * FW * 0.12f;
    const float py = cys + starY[i] * iz * FH * 0.12f;
    const float b = (1.0f - starZ[i]) * (1.0f - starZ[i]) * 0.5f * (1.0f - decay * 0.7f);
    splat(px, py, b, 0.0f, pal[i & 1]);
  }

  for (int e = 0; e < 32; e++) {
    const int A = edgeA[e], B = edgeB[e];
    const float dx = vx[B] - vx[A], dy = vy[B] - vy[A];
    const float len = fabsf(dx) > fabsf(dy) ? fabsf(dx) + 0.4f * fabsf(dy) : fabsf(dy) + 0.4f * fabsf(dx);
    int steps = (int)(len * 2.0f) + 1;
    if (steps > 400) steps = 400;
    const float inv = 1.0f / (float)steps;
    for (int sIdx = 0; sIdx <= steps; sIdx++) {
      const float u = sIdx * inv;
      const float w = vw[A] + (vw[B] - vw[A]) * u;
      const float z = vz[A] + (vz[B] - vz[A]) * u;
      // Colour by position in W (inner cell vs outer cell), brightness by nearness.
      float m = (w + 1.0f) * 0.5f;
      m = m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
      m = m * m * (3.0f - 2.0f * m);
      RGBf c = {pal[0].r + (pal[1].r - pal[0].r) * m, pal[0].g + (pal[1].g - pal[0].g) * m,
                pal[0].b + (pal[1].b - pal[0].b) * m};
      float near = 0.5f + z * 0.45f;
      near = near < 0.15f ? 0.15f : (near > 1.2f ? 1.2f : near);
      splat(vx[A] + dx * u, vy[A] + dy * u, deposit * near, deposit * 0.32f * near * near, c);
    }
  }
  for (int i = 0; i < 16; i++) {
    float near = 0.6f + vz[i] * 0.5f;
    near = near < 0.2f ? 0.2f : near;
    splat(vx[i], vy[i], 1.4f * near, 0.5f * near, pal[2]);
  }

  for (int y = 0; y < FH; y++) {
    const float* row = acc + y * FW * 3;
    for (int x = 0; x < FW; x++) {
      const float* p = row + x * 3;
      // Overexposure bleeds into white, like a real light trace.
      const float over = (p[0] + p[1] + p[2]) * 0.33f - 1.0f;
      const float bleed = over > 0.0f ? over * 120.0f : 0.0f;
      float r = p[0] * 255.0f + bleed, g = p[1] * 255.0f + bleed, b = p[2] * 255.0f + bleed;
      PFCanvas::setPixel(x, y, r > 255.0f ? 255 : (uint8_t)r, g > 255.0f ? 255 : (uint8_t)g,
                         b > 255.0f ? 255 : (uint8_t)b);
    }
  }
  PFCanvas::present();
}
}  // namespace Tesseract

PF_REGISTER_PATTERN(Tesseract)
