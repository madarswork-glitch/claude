// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Op Art Moire
// Author:  madarswork + Claude
// Lineage: original (after Bridget Riley and Victor Vasarely)
//
// Interference you can feel in your eyes. Rings radiate from emitters that
// drift around the panel and gratings turn slowly across them; where two
// fields meet they XOR, and the moire fringes - shapes that exist in neither
// field - swim across the panel. Two binary channels pick one of four hard
// colours (black is always one of them), and every edge is anti-aliased to
// exactly one pixel so the fringes stay clean instead of crawling.
//
// Four modes: rings x rings over a turning grating; three ring sets; two
// gratings crossing at a slight angle over rings; a spiral against rings.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace OpArtMoire {
const char* NAME = "Op Art Moire";
const char* const KNOB_LABELS[4] = {"speed", "scale", "mode", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGBf { float r, g, b; };
constexpr int NPAL = 6;
// [both on, A only, B only, neither]
const RGBf PAL[NPAL][4] = {
  {{255, 255, 255}, {255, 0, 150}, {0, 220, 255}, {0, 0, 0}},     // riley neon
  {{255, 255, 255}, {255, 255, 255}, {0, 0, 0}, {0, 0, 0}},       // pure black / white (B shifts phase only)
  {{255, 230, 0}, {255, 0, 0}, {0, 60, 255}, {0, 0, 0}},          // bauhaus
  {{180, 255, 0}, {140, 0, 255}, {255, 255, 255}, {0, 0, 0}},     // acid
  {{255, 120, 0}, {255, 0, 90}, {255, 220, 0}, {20, 0, 60}},      // heat
  {{0, 255, 200}, {0, 90, 255}, {255, 0, 255}, {0, 0, 0}},        // aqua
};

struct Params {
  float speed = 1.0f;
  float scale = 0.55f;    // ring frequency, radians per pixel
  int mode = 0;
  int palette = 0;
};
Params params;
float clock = 0.0f, phase = 0.0f, turn = 0.0f;

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

inline float atan2f_fast(float y, float x) {
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
  return r;
}

// A sine wave squeezed into a square wave whose edge is one pixel wide.
// slope = the wave's phase change per pixel.
inline float sq(float ph, float invSlope) {
  float v = PFMath::fastSin(ph) * invSlope;
  return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

inline uint8_t u8(float v) { return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : (uint8_t)v); }

void setup() {
  PFMath::buildSinLUT();
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &params.speed, 0.0f, 3.0f, 3.0f / 48.0f);
  PFParams::apply(input, 1, &params.scale, 0.25f, 1.1f, 0.85f / 48.0f);
  PFParams::applyIndex(input, 2, &params.mode, 4);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);
  const float W = 2000.0f * PFMath::TWO_PI_F;
  clock += dt * params.speed * 0.35f;
  if (clock > W) clock -= W;
  phase += dt * params.speed * 3.0f;
  if (phase > W) phase -= W;
  turn += dt * params.speed * 0.09f;
  if (turn > W) turn -= W;
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  const RGBf* pal = PAL[params.palette];
  const float f = params.scale;
  const float inv = 1.7f / f;          // one-pixel edges for a wave of slope f
  const int mode = params.mode;

  // Emitters on slow Lissajous orbits.
  const float e1x = FW * (0.5f + 0.32f * PFMath::fastSin(clock * 1.3f));
  const float e1y = FH * (0.5f + 0.30f * PFMath::fastSin(clock * 0.9f + 1.0f));
  const float e2x = FW * (0.5f + 0.32f * PFMath::fastSin(clock * 0.7f + 2.5f));
  const float e2y = FH * (0.5f + 0.30f * PFMath::fastSin(clock * 1.1f + 4.0f));
  const float e3x = FW * (0.5f + 0.30f * PFMath::fastSin(clock * 1.7f + 4.4f));
  const float e3y = FH * (0.5f + 0.28f * PFMath::fastSin(clock * 0.6f + 0.3f));
  // Gratings: one turning, one turning a hair faster.
  const float g1c = PFMath::fastCos(turn), g1s = PFMath::fastSin(turn);
  const float g2c = PFMath::fastCos(turn * 1.08f + 0.12f), g2s = PFMath::fastSin(turn * 1.08f + 0.12f);
  const float fg = f * 0.85f;
  const float invG = 1.7f / fg;

  for (int y = 0; y < FH; y++) {
    const float py = (float)y + 0.5f;
    for (int x = 0; x < FW; x++) {
      const float px = (float)x + 0.5f;
      float dx = px - e1x, dy = py - e1y;
      float d2 = dx * dx + dy * dy + 0.01f;
      const float d1 = d2 * frsqrt(d2);
      dx = px - e2x; dy = py - e2y;
      d2 = dx * dx + dy * dy + 0.01f;
      const float dB = d2 * frsqrt(d2);

      float A, B;   // two channels, each -1..1
      if (mode == 0) {
        A = sq(d1 * f - phase, inv) * sq(dB * f - phase * 0.8f, inv);
        // A broad turning grating swaps the colour pair in wide bands.
        B = sq(((px - FW * 0.5f) * g1c + (py - FH * 0.5f) * g1s) * fg * 0.35f, invG / 0.35f);
      } else if (mode == 1) {
        dx = px - e3x; dy = py - e3y;
        d2 = dx * dx + dy * dy + 0.01f;
        const float d3 = d2 * frsqrt(d2);
        A = sq(d1 * f - phase, inv) * sq(dB * f + phase * 0.7f, inv);
        B = sq(d3 * f * 0.6f - phase * 0.5f, inv / 0.6f);
      } else if (mode == 2) {
        const float ux = px - FW * 0.5f, uy = py - FH * 0.5f;
        A = sq((ux * g1c + uy * g1s) * fg, invG) * sq((ux * g2c + uy * g2s) * fg, invG);
        B = sq(d1 * f * 0.5f - phase * 0.6f, inv * 2.0f);
      } else {
        // Spiral arms around emitter 1, rings around emitter 2.
        const float ang = atan2f_fast(py - e1y, px - e1x);
        const float arms = 6.0f;
        // Phase slope of the spiral: radial f plus angular arms / r.
        const float slope = f + arms * frcp(d1 + 1.0f);
        A = sq(ang * arms + d1 * f - phase, 1.7f * frcp(slope));
        B = sq(dB * f * 0.7f + phase * 0.5f, inv / 0.7f);
      }

      // A product of soft edges sags toward 0 along every edge; push it back
      // out so the fringes stay hard black-or-colour, not grey.
      A *= 2.2f; A = A < -1.0f ? -1.0f : (A > 1.0f ? 1.0f : A);
      B *= 1.6f; B = B < -1.0f ? -1.0f : (B > 1.0f ? 1.0f : B);
      const float a = (A + 1.0f) * 0.5f, b = (B + 1.0f) * 0.5f;
      // Bilinear pick between the four colours: hard where a and b are 0/1.
      const RGBf& c00 = pal[3];
      const RGBf& c01 = pal[2];
      const RGBf& c10 = pal[1];
      const RGBf& c11 = pal[0];
      float r0 = c00.r + (c01.r - c00.r) * b, g0 = c00.g + (c01.g - c00.g) * b, b0 = c00.b + (c01.b - c00.b) * b;
      float r1 = c10.r + (c11.r - c10.r) * b, g1 = c10.g + (c11.g - c10.g) * b, b1 = c10.b + (c11.b - c10.b) * b;
      if (params.palette == 1) {
        // Black and white: B inverts A instead of choosing a colour.
        float ab = A * B * 1.8f;
        ab = ab < -1.0f ? -1.0f : (ab > 1.0f ? 1.0f : ab);
        const float v = (ab + 1.0f) * 0.5f * 255.0f;
        PFCanvas::setPixel(x, y, u8(v), u8(v), u8(v));
        continue;
      }
      PFCanvas::setPixel(x, y, u8(r0 + (r1 - r0) * a), u8(g0 + (g1 - g0) * a), u8(b0 + (b1 - b0) * a));
    }
  }
  PFCanvas::present();
}
}  // namespace OpArtMoire

PF_REGISTER_PATTERN(OpArtMoire)
