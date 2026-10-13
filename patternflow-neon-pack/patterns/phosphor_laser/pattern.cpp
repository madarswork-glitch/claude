// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Phosphor Laser
// Author:  madarswork + Claude
// Lineage: original
//
// A laser show on an oscilloscope screen. A 3D Lissajous knot - three sine
// waves at whole-number ratios - is traced in perspective, colour flowing
// along its length and brighter where it comes toward you. Its phases drift,
// so the knot keeps folding into new shapes, while the whole thing tumbles.
// A white-hot comet runs the curve like the beam itself, and the phosphor
// holds what it drew, so every change leaves a fading ghost of the last shape.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace PhosphorLaser {
const char* NAME = "Phosphor Laser";
const char* const KNOB_LABELS[4] = {"morph", "figure", "glow", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGBf { float r, g, b; };
constexpr int NPAL = 6;
// Three colours cycling along the curve, and the comet.
const RGBf PAL[NPAL][4] = {
  {{1.0f, 0.0f, 0.55f}, {0.0f, 0.8f, 1.0f}, {0.5f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},   // laser show
  {{0.0f, 1.0f, 0.2f}, {0.0f, 0.5f, 0.1f}, {0.6f, 1.0f, 0.0f}, {0.8f, 1.0f, 0.8f}},    // oscilloscope green
  {{1.0f, 0.1f, 0.0f}, {1.0f, 0.6f, 0.0f}, {1.0f, 0.0f, 0.3f}, {1.0f, 1.0f, 0.6f}},    // fire
  {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.3f, 1.0f}, {1.0f, 1.0f, 1.0f}},    // RGB
  {{0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},    // CMY
  {{0.3f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.8f}, {0.0f, 0.4f, 1.0f}, {0.6f, 1.0f, 1.0f}},    // ultraviolet
};

// Frequency ratios for x, y, z: each makes a different closed knot.
constexpr int NFIG = 8;
const float FIG[NFIG][3] = {
  {3, 2, 1}, {5, 4, 3}, {3, 4, 7}, {2, 3, 5}, {5, 6, 4}, {1, 3, 2}, {7, 6, 5}, {4, 5, 2},
};

struct Params {
  float morph = 0.8f;
  int figure = 0;
  float glow = 0.8f;
  int palette = 0;
};
Params params;

float* acc = nullptr;
float ph = 0.0f, yaw = 0.0f, pitch = 0.0f, comet = 0.0f, flow = 0.0f;

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
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &params.morph, 0.0f, 3.0f, 3.0f / 48.0f);
  PFParams::applyIndex(input, 1, &params.figure, NFIG);
  PFParams::apply(input, 2, &params.glow, 0.0f, 0.95f, 0.95f / 48.0f);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);
  const float W = 2000.0f * PFMath::TWO_PI_F;
  ph += dt * params.morph * 0.45f;
  if (ph > W) ph -= W;
  yaw += dt * (0.25f + params.morph * 0.1f);
  if (yaw > W) yaw -= W;
  pitch += dt * 0.17f;
  if (pitch > W) pitch -= W;
  comet += dt * 0.35f;
  comet -= PFMath::floorF(comet);
  flow += dt * 0.2f;
  flow -= PFMath::floorF(flow);
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  if (!acc) { PFCanvas::clear(); PFCanvas::present(); return; }

  const int N = FW * FH * 3;
  const float decay = params.glow;
  if (decay <= 0.001f) memset(acc, 0, sizeof(float) * N);
  else for (int i = 0; i < N; i++) acc[i] *= decay;

  const RGBf* pal = PAL[params.palette];
  const float* fr = FIG[params.figure];
  const float cy0 = PFMath::fastCos(yaw), sy0 = PFMath::fastSin(yaw);
  const float tp = 0.5f * PFMath::fastSin(pitch);
  const float cp = PFMath::fastCos(tp), sp = PFMath::fastSin(tp);
  // Sized so the nearest point (perspective x1.45) still lands on the panel.
  const float sxS = FW * (PF_PORTRAIT ? 0.31f : 0.29f);
  const float syS = FH * (PF_PORTRAIT ? 0.29f : 0.31f);
  const float D = 3.2f;
  const float cx = FW * 0.5f, cyc = FH * 0.5f;
  const float deposit = 0.30f * (1.0f - decay * 0.75f);

  constexpr int SAMPLES = 720;
  float lastX = 0, lastY = 0, lastZ = 0;
  for (int i = 0; i <= SAMPLES; i++) {
    const float u = (float)i * (1.0f / SAMPLES);
    const float a = u * PFMath::TWO_PI_F;
    float x = PFMath::fastSin(a * fr[0] + ph);
    float y = PFMath::fastSin(a * fr[1] + ph * 0.61f + 1.3f);
    float z = PFMath::fastSin(a * fr[2] + ph * 0.37f + 0.4f);
    // Tumble: yaw about the vertical, then a nodding pitch.
    float t = x * cy0 - z * sy0; z = x * sy0 + z * cy0; x = t;
    t = y * cp - z * sp; z = y * sp + z * cp; y = t;
    const float p = D * frcp(D - z);
    const float X = cx + x * sxS * p, Y = cyc + y * syS * p;

    if (i > 0) {
      // Fill the gap from the previous sample so the line never breaks.
      const float dx = X - lastX, dy = Y - lastY;
      const float len = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
      int steps = (int)(len * 1.6f) + 1;
      if (steps > 40) steps = 40;
      const float inv = 1.0f / (float)steps;
      // Colour flows along the curve through the palette's three stops.
      float cpos = (u + flow) * 3.0f;
      cpos -= 3.0f * (float)PFMath::ifloor(cpos * (1.0f / 3.0f));
      const int ci = (int)cpos;
      const float cf = cpos - (float)ci;
      const RGBf& c0 = pal[ci % 3];
      const RGBf& c1 = pal[(ci + 1) % 3];
      const RGBf c = {c0.r + (c1.r - c0.r) * cf, c0.g + (c1.g - c0.g) * cf, c0.b + (c1.b - c0.b) * cf};
      // Comet: the beam head and a tail behind it.
      float behind = comet - u;
      behind -= PFMath::floorF(behind);
      float head = 1.0f - behind * 7.0f;
      head = head > 0.0f ? head * head : 0.0f;
      float near = 0.55f + z * 0.45f;
      near = near < 0.12f ? 0.12f : near;
      for (int s = 0; s < steps; s++) {
        const float k = s * inv;
        const float zz = lastZ + (z - lastZ) * k;
        const float nk = 0.55f + zz * 0.45f;
        const float core = deposit * (nk < 0.12f ? 0.12f : nk);
        splat(lastX + dx * k, lastY + dy * k, core, core * 0.35f * near, c);
        if (head > 0.0f) splat(lastX + dx * k, lastY + dy * k, head * 0.5f, head * 0.2f, pal[3]);
      }
    }
    lastX = X; lastY = Y; lastZ = z;
  }

  for (int y = 0; y < FH; y++) {
    const float* row = acc + y * FW * 3;
    for (int x = 0; x < FW; x++) {
      const float* p = row + x * 3;
      const float over = (p[0] + p[1] + p[2]) * 0.33f - 1.0f;
      const float bleed = over > 0.0f ? over * 110.0f : 0.0f;
      const float r = p[0] * 255.0f + bleed, g = p[1] * 255.0f + bleed, b = p[2] * 255.0f + bleed;
      PFCanvas::setPixel(x, y, r > 255.0f ? 255 : (uint8_t)r, g > 255.0f ? 255 : (uint8_t)g,
                         b > 255.0f ? 255 : (uint8_t)b);
    }
  }
  PFCanvas::present();
}
}  // namespace PhosphorLaser

PF_REGISTER_PATTERN(PhosphorLaser)
