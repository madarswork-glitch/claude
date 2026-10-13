// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Kaleido Glass
// Author:  madarswork + Claude
// Lineage: original
//
// A rose window that never stops blooming. Space is folded into N mirrored
// wedges around the centre; inside the wedge sits a warped triangular lattice
// of glass panes that flows outward and turns, so new panes are born at the
// heart and swept to the rim. Each pane is one saturated colour from the
// palette, lit brighter at its middle, and held in black lead lines - the
// contrast is the point. A white-hot jewel pulses at the centre.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace KaleidoGlass {
const char* NAME = "Kaleido Glass";
const char* const KNOB_LABELS[4] = {"spin", "mirrors", "zoom", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGB { uint8_t r, g, b; };
constexpr int NPAL = 6;
const RGB PAL[NPAL][5] = {
  {{255, 0, 140}, {0, 210, 255}, {255, 200, 0}, {110, 0, 255}, {0, 255, 90}},     // cathedral neon
  {{255, 30, 0}, {255, 170, 0}, {200, 0, 90}, {255, 240, 120}, {120, 0, 30}},     // ember
  {{0, 80, 255}, {0, 255, 220}, {160, 0, 255}, {230, 250, 255}, {0, 160, 255}},   // glacier
  {{255, 0, 0}, {0, 0, 255}, {255, 220, 0}, {0, 200, 60}, {255, 255, 255}},       // de Stijl+
  {{255, 0, 200}, {120, 255, 0}, {255, 120, 0}, {0, 180, 255}, {200, 0, 255}},    // acid
  {{255, 255, 255}, {255, 0, 60}, {30, 30, 255}, {255, 200, 0}, {0, 255, 200}},   // carnival
};

struct Params {
  float spin = 0.6f;
  int mirrors = 3;     // index into MIRRORS
  float zoom = 1.7f;
  int palette = 0;
};
Params params;
const int MIRRORS[10] = {3, 4, 5, 6, 7, 8, 9, 10, 12, 16};

float* rT = nullptr;     // frame-space radius, in half-min-dimension units
float* aT = nullptr;     // frame-space angle, in turns 0..1
float rot = 0.0f, flow = 0.0f, warpT = 0.0f, pulse = 0.0f;

inline uint8_t u8(float v) {
  return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : (uint8_t)v);
}

void setup() {
  PFMath::buildSinLUT();
  if (!rT) rT = PFMem::allocFloats(FW * FH);
  if (!aT) aT = PFMem::allocFloats(FW * FH);
  if (!rT || !aT) return;
  const float inv = 2.0f / (float)(FW < FH ? FW : FH);
  for (int y = 0; y < FH; y++)
    for (int x = 0; x < FW; x++) {
      const float dx = ((float)x + 0.5f - FW * 0.5f) * inv;
      const float dy = ((float)y + 0.5f - FH * 0.5f) * inv;
      rT[y * FW + x] = sqrtf(dx * dx + dy * dy);
      aT[y * FW + x] = atan2f(dy, dx) * 0.15915494f + 0.5f;
    }
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &params.spin, -2.0f, 2.0f, 4.0f / 48.0f);
  PFParams::applyIndex(input, 1, &params.mirrors, 10);
  PFParams::apply(input, 2, &params.zoom, 0.8f, 4.5f, 3.7f / 48.0f);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);

  rot += dt * params.spin * 0.08f;
  rot -= PFMath::floorF(rot);
  flow += dt * (0.25f + fabsf(params.spin) * 0.2f);
  if (flow > 1024.0f) flow -= 1024.0f;
  warpT += dt * 0.6f;
  if (warpT > 2000.0f * PFMath::TWO_PI_F) warpT -= 2000.0f * PFMath::TWO_PI_F;
  pulse += dt * 2.1f;
  if (pulse > 2000.0f * PFMath::TWO_PI_F) pulse -= 2000.0f * PFMath::TWO_PI_F;
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  if (!rT || !aT) { PFCanvas::clear(); PFCanvas::present(); return; }

  const RGB* pal = PAL[params.palette];
  const float N = (float)MIRRORS[params.mirrors];
  const float zoom = params.zoom;
  const float px = zoom * 2.0f / (float)(FW < FH ? FW : FH);   // lattice units per pixel
  const float lead = px * 0.55f;                               // half-width of the lead line
  const float aa = 1.0f / (px * 1.1f);
  // The lattice itself turns slowly against the wedges: the mandala morphs.
  const float latA = rot * PFMath::TWO_PI_F * -2.3f;
  const float lc = PFMath::fastCos(latA), ls = PFMath::fastSin(latA);
  const float jewel = 0.09f + 0.03f * PFMath::fastSin(pulse);
  const float invJewel = 1.0f / (jewel * 2.2f);

  for (int y = 0; y < FH; y++) {
    for (int x = 0; x < FW; x++) {
      const int i = y * FW + x;
      const float r = rT[i];

      // Fold the angle into one mirrored wedge.
      float w = (aT[i] + rot) * N;
      w -= (float)PFMath::ifloor(w);
      if (w > 0.5f) w = 1.0f - w;
      const float a = w * PFMath::TWO_PI_F / N;
      float qx = r * PFMath::fastCos(a) * zoom;
      float qy = r * PFMath::fastSin(a) * zoom;

      // Turn, flow outward, and bend the lattice so panes come out curved.
      float tx = qx * lc - qy * ls - flow;
      float ty = qx * ls + qy * lc;
      tx += 0.18f * PFMath::fastSin(ty * 1.7f + warpT);
      ty += 0.18f * PFMath::fastSin(tx * 1.4f - warpT * 0.8f);

      // Triangular lattice: skew, split each rhombus into two triangles.
      const float s = tx + ty * 0.57735f;
      const float t = ty * 1.1547f;
      const int is = PFMath::ifloor(s), it = PFMath::ifloor(t);
      const float fs = s - (float)is, ft = t - (float)it;
      const int upper = (fs + ft > 1.0f) ? 1 : 0;
      float e = fs < ft ? fs : ft;
      const float d3 = fabsf(1.0f - fs - ft) * 0.7071f;
      e = e < d3 ? e : d3;
      if (upper) {
        const float e1 = 1.0f - fs, e2 = 1.0f - ft;
        e = e < e1 ? e : e1;
        e = e < e2 ? e : e2;
      }

      // Pane colour from the cell, pane light from the distance to its lead.
      const int h = (int)(PFNoise::cellHash(is * 2 + upper, it) * 255.0f);
      const RGB& c = pal[h % 5];
      float glass = (e - lead) * aa;
      glass = glass < 0.0f ? 0.0f : (glass > 1.0f ? 1.0f : glass);
      const float shade = glass * (0.6f + 0.4f * (e * 4.0f > 1.0f ? 1.0f : e * 4.0f));
      float cr = c.r * shade, cg = c.g * shade, cb = c.b * shade;

      // The jewel: a hard-edged white core with a coloured corona.
      if (r < jewel * 2.2f) {
        float k = 1.0f - r * invJewel;
        k = k * k;
        const RGB& jc = pal[(int)(flow * 0.5f) % 5];
        cr += jc.r * k; cg += jc.g * k; cb += jc.b * k;
        if (r < jewel) { cr = 255.0f; cg = 255.0f; cb = 255.0f; }
      }
      PFCanvas::setPixel(x, y, u8(cr), u8(cg), u8(cb));
    }
  }
  PFCanvas::present();
}
}  // namespace KaleidoGlass

PF_REGISTER_PATTERN(KaleidoGlass)
