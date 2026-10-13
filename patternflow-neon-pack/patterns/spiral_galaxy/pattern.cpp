// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Spiral Galaxy
// Author:  madarswork + Claude
// Lineage: original
//
// A barred-spiral galaxy of 1300 stars, built in 3D and seen through a real
// perspective camera. Two logarithmic arms of hot blue stars studded with pink
// star-forming knots wrap a golden bulge; the disk turns, nods and rolls, so
// you see it face-on one moment and as a blazing edge-on blade the next.
// The bulge is a per-pixel glow; every star is a sub-pixel splat into a
// light buffer that keeps a whisper of the last frame for shimmer.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace SpiralGalaxy {
const char* NAME = "Spiral Galaxy";
const char* const KNOB_LABELS[4] = {"spin", "tilt", "distance", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGBf { float r, g, b; };
constexpr int NPAL = 6;
// core, inner arm, outer arm, star-forming knot
const RGBf PAL[NPAL][4] = {
  {{1.0f, 0.7f, 0.2f}, {0.45f, 0.75f, 1.0f}, {0.05f, 0.3f, 1.0f}, {1.0f, 0.05f, 0.5f}},   // hubble
  {{1.0f, 0.1f, 0.6f}, {1.0f, 0.4f, 0.9f}, {0.0f, 0.85f, 1.0f}, {1.0f, 0.9f, 0.0f}},      // synthwave
  {{1.0f, 0.85f, 0.2f}, {0.6f, 1.0f, 0.3f}, {0.0f, 0.8f, 0.45f}, {0.75f, 0.0f, 1.0f}},    // emerald
  {{1.0f, 0.95f, 0.6f}, {1.0f, 0.45f, 0.0f}, {1.0f, 0.0f, 0.1f}, {0.2f, 0.4f, 1.0f}},     // inferno
  {{0.8f, 0.95f, 1.0f}, {0.4f, 0.7f, 1.0f}, {0.35f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.85f}},    // ice
  {{1.0f, 1.0f, 1.0f}, {1.0f, 0.9f, 0.0f}, {1.0f, 0.0f, 0.4f}, {0.0f, 0.9f, 1.0f}},       // rainbow (by radius)
};

struct Params {
  float spin = 1.0f;
  float tilt = 0.75f;      // radians from face-on
  float distance = 3.0f;
  int palette = 0;
};
Params params;

constexpr int NS = 1300;
float sr[NS], sa[NS], sh[NS];     // radius, angle, height (galaxy frame)
uint8_t skind[NS];                // 0 bulge, 1 arm, 2 knot
float sb[NS];                     // brightness
float* acc = nullptr;
float rot = 0.0f, nod = 0.0f;

inline float frand() { return (float)random(0, 100000) * 0.00001f; }
inline float gauss() { return (frand() + frand() + frand() - 1.5f) * 1.4f; }

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

inline void splat(float fx, float fy, float k, const RGBf& c) {
  const int x0 = PFMath::ifloor(fx), y0 = PFMath::ifloor(fy);
  const float ax = fx - x0, ay = fy - y0;
  add(x0, y0, k * (1 - ax) * (1 - ay), c);
  add(x0 + 1, y0, k * ax * (1 - ay), c);
  add(x0, y0 + 1, k * (1 - ax) * ay, c);
  add(x0 + 1, y0 + 1, k * ax * ay, c);
}

void setup() {
  PFMath::buildSinLUT();
  if (!acc) acc = (float*)PFMem::alloc(sizeof(float) * FW * FH * 3);
  for (int i = 0; i < NS; i++) {
    const float u = frand();
    if (i < NS / 6) {
      // Bulge: a squashed ball, with a short bar.
      float r = 0.22f * frand() * frand() + 0.01f;
      const float a = frand() * PFMath::TWO_PI_F;
      if ((i & 3) == 0) r = 0.3f * frand();   // bar stars, along angle 0 / pi
      sr[i] = r;
      sa[i] = ((i & 3) == 0) ? ((i & 4) ? PI : 0.0f) + gauss() * 0.15f : a;
      sh[i] = gauss() * 0.06f * (1.0f - r * 2.0f);
      skind[i] = 0;
      sb[i] = 0.5f + 0.5f * frand();
    } else {
      // Arms: logarithmic spirals from the bar ends, scatter widening outward.
      const float r = 0.18f + 0.82f * powf(u, 0.8f);
      const int arm = i & 1;
      const float wind = 2.6f;
      sr[i] = r;
      sa[i] = arm * PI + logf(r / 0.18f) * wind + gauss() * (0.22f + 0.25f * r);
      sh[i] = gauss() * 0.025f;
      const bool knot = frand() < 0.09f;
      skind[i] = knot ? 2 : 1;
      sb[i] = knot ? 1.0f + 0.5f * frand() : 0.35f + 0.8f * frand() * frand();
    }
  }
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &params.spin, 0.0f, 3.0f, 3.0f / 48.0f);
  PFParams::apply(input, 1, &params.tilt, 0.0f, 1.5f, 1.5f / 48.0f);
  PFParams::apply(input, 2, &params.distance, 1.6f, 6.0f, 4.4f / 48.0f);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);

  const float W = 2000.0f * PFMath::TWO_PI_F;
  rot += dt * params.spin * 0.35f;
  if (rot > W) rot -= W;
  nod += dt * 0.21f;
  if (nod > W) nod -= W;
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  if (!acc) { PFCanvas::clear(); PFCanvas::present(); return; }
  const int N = FW * FH * 3;
  for (int i = 0; i < N; i++) acc[i] *= 0.35f;

  const RGBf* pal = PAL[params.palette];
  const bool rainbow = params.palette == NPAL - 1;

  // Camera: tilt from face-on (with a gentle nod), and a slow roll. In
  // portrait the disk is rolled a quarter turn so it spans the long side.
  // Elevation of the camera above the disk plane: tilt 0 looks straight down.
  const float el = HALF_PI - params.tilt - 0.18f * PFMath::fastSin(nod);
  const float ct = PFMath::fastCos(el), st = PFMath::fastSin(el);
  const float roll = (PF_PORTRAIT ? HALF_PI : 0.0f) + 0.22f * PFMath::fastSin(nod * 0.61f + 1.0f);
  const float cr = PFMath::fastCos(roll), sr2 = PFMath::fastSin(roll);
  // Distance 3 frames the whole disk: the long axis on the long side, the
  // tilted short axis on the short side. Nearer than that, it overflows.
  const float D = params.distance;
  const float longS = (float)(FW > FH ? FW : FH), shortS = (float)(FW < FH ? FW : FH);
  const float act = fabsf(st) < 0.25f ? 0.25f : fabsf(st);   // how open the disk looks
  float fit = longS * 0.44f;
  if (shortS * 0.44f / act < fit) fit = shortS * 0.44f / act;
  const float F = fit * 3.0f;
  const float cx = FW * 0.5f, cy = FH * 0.5f;

  // Fewer pixels for the same stars when the disk is small: dim each star so
  // the arms stay coloured instead of piling up into white.
  float dens = (F / D) * (1.0f / 50.0f) * (0.3f + 0.7f * act);
  dens = dens < 0.4f ? 0.4f : (dens > 1.4f ? 1.4f : dens);

  for (int i = 0; i < NS; i++) {
    const float a = sa[i] + rot * (skind[i] == 0 ? 1.15f : 1.0f);
    float x = sr[i] * PFMath::fastCos(a);
    float z = sr[i] * PFMath::fastSin(a);
    float y = sh[i];
    // Tilt about the x axis.
    const float y2 = y * ct - z * st;
    const float z2 = y * st + z * ct;
    // Roll in the image plane.
    const float x3 = x * cr - y2 * sr2;
    const float y3 = x * sr2 + y2 * cr;
    const float p = F * frcp(D - z2);
    const float near = 1.0f + z2 * 0.6f;

    RGBf c;
    if (rainbow) {
      // Radius sweeps the rainbow: core gold through to violet rim.
      uint8_t r8, g8, b8;
      PFColor::hsvToRgb(0.12f + sr[i] * 0.75f, skind[i] == 2 ? 0.3f : 1.0f, 1.0f, r8, g8, b8);
      c = {r8 / 255.0f, g8 / 255.0f, b8 / 255.0f};
    } else if (skind[i] == 0) {
      c = pal[0];
    } else if (skind[i] == 2) {
      c = pal[3];
    } else {
      const float m = sr[i] > 1.0f ? 1.0f : sr[i];
      c = {pal[1].r + (pal[2].r - pal[1].r) * m, pal[1].g + (pal[2].g - pal[1].g) * m,
           pal[1].b + (pal[2].b - pal[1].b) * m};
    }
    splat(cx + x3 * p, cy + y3 * p, sb[i] * 0.75f * near * (p / F) * D * dens, c);
  }

  // Bulge glow, flattened by the tilt, computed per pixel.
  const float pc = F / D;
  const float gx = 0.075f * pc, gy = 0.075f * pc * (0.4f + 0.6f * fabsf(st));
  const float igx = 1.0f / (gx * gx), igy = 1.0f / (gy * gy);
  const RGBf core = pal[0];

  for (int y = 0; y < FH; y++) {
    const float* row = acc + y * FW * 3;
    const float dy = (float)y + 0.5f - cy;
    for (int x = 0; x < FW; x++) {
      const float dx = (float)x + 0.5f - cx;
      // Undo the roll so the glow ellipse follows the disk.
      const float ux = dx * cr + dy * sr2, uy = -dx * sr2 + dy * cr;
      const float q = ux * ux * igx + uy * uy * igy;
      const float g = frcp(1.0f + q * q) * 1.3f;
      const float* p = row + x * 3;
      const float r = p[0] + core.r * g, gg = p[1] + core.g * g, b = p[2] + core.b * g;
      const float over = (r + gg + b) * 0.33f - 0.9f;
      const float bleed = over > 0.0f ? over * 160.0f : 0.0f;
      const float R = r * 255.0f + bleed, G = gg * 255.0f + bleed, B = b * 255.0f + bleed;
      PFCanvas::setPixel(x, y, R > 255.0f ? 255 : (uint8_t)R, G > 255.0f ? 255 : (uint8_t)G,
                         B > 255.0f ? 255 : (uint8_t)B);
    }
  }
  PFCanvas::present();
}
}  // namespace SpiralGalaxy

PF_REGISTER_PATTERN(SpiralGalaxy)
