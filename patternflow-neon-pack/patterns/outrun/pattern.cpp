// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Outrun
// Author:  madarswork + Claude
// Lineage: original
//
// Synthwave at full throttle. A striped sun sinks behind wireframe mountains
// while a neon grid floor rushes at you in true perspective (z = h / y): the
// cross lines accelerate as they come closer, the long lines converge on the
// vanishing point, and the road curves and the horizon banks as you drive.
// Every line is anti-aliased from its own screen-space footprint, so they stay
// crisp from the horizon to your feet. The sun's reflection shimmers on the
// floor and stars twinkle in the upper sky.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace Outrun {
const char* NAME = "Outrun";
const char* const KNOB_LABELS[4] = {"speed", "sun", "curve", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGBf { float r, g, b; };
struct Look {
  RGBf skyTop, skyLow, sunTop, sunLow, grid, rim, floor, mount;
};
constexpr int NPAL = 5;
const Look PAL[NPAL] = {
  // classic: magenta grid, cyan peaks, gold-to-pink sun
  {{3, 0, 18}, {150, 0, 90}, {255, 225, 0}, {255, 0, 120}, {255, 0, 190}, {0, 225, 255}, {5, 0, 16}, {16, 0, 34}},
  // night drive: cyan everything, white-hot sun
  {{0, 0, 12}, {0, 60, 140}, {255, 255, 255}, {0, 190, 255}, {0, 255, 255}, {255, 0, 200}, {0, 3, 14}, {0, 10, 30}},
  // toxic: acid green on violet
  {{6, 0, 20}, {110, 0, 150}, {220, 255, 0}, {0, 255, 110}, {120, 255, 0}, {255, 0, 255}, {4, 0, 12}, {20, 0, 30}},
  // red line: ember sky, white-gold sun
  {{14, 0, 0}, {170, 20, 0}, {255, 245, 190}, {255, 40, 0}, {255, 50, 0}, {255, 210, 0}, {10, 0, 0}, {30, 2, 0}},
  // vapor: pink sky, aqua grid
  {{20, 0, 50}, {255, 70, 150}, {255, 255, 120}, {255, 50, 200}, {0, 255, 220}, {255, 255, 255}, {12, 0, 30}, {40, 0, 70}},
};

struct Params {
  float speed = 1.5f;
  float sun = 0.36f;
  float curve = 0.6f;
  int palette = 0;
};
Params params;
float travel = 0.0f, drive = 0.0f, stripes = 0.0f, scenery = 0.0f, twinkle = 0.0f;

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

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline uint8_t u8(float v) { return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : (uint8_t)v); }
inline RGBf mix(const RGBf& a, const RGBf& b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// Mountain skyline height (pixels above the horizon) at screen column u.
inline float ridge(float u, float scale) {
  // Two triangle waves at unrelated periods, so peaks never line up evenly,
  // plus a little roughness on the slopes.
  float h = 0.0f;
  float a = u * 0.045f + scenery;
  float f = a - (float)PFMath::ifloor(a);
  float t = 1.0f - fabsf(f * 2.0f - 1.0f);
  h += t * t * 0.75f;                                      // big peaks
  a = u * 0.113f - scenery * 1.7f + 0.37f;
  f = a - (float)PFMath::ifloor(a);
  h += (1.0f - fabsf(f * 2.0f - 1.0f)) * 0.3f;             // foothills
  h += 0.06f * PFMath::fastSin(u * 0.9f + scenery * 5.0f);
  return h * scale - scale * 0.12f;
}

void setup() {
  PFMath::buildSinLUT();
}

void update(float dt, const InputFrame& input) {
  PFParams::apply(input, 0, &params.speed, 0.0f, 4.0f, 4.0f / 48.0f);
  PFParams::apply(input, 1, &params.sun, 0.2f, 0.7f, 0.5f / 48.0f);
  PFParams::apply(input, 2, &params.curve, 0.0f, 1.0f, 1.0f / 48.0f);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);

  const float W = 2000.0f * PFMath::TWO_PI_F;
  travel += dt * params.speed * 3.0f;
  if (travel > 4096.0f) travel -= 4096.0f;
  drive += dt * (0.25f + params.speed * 0.05f);
  if (drive > W) drive -= W;
  stripes += dt * 0.12f;
  stripes -= PFMath::floorF(stripes);
  scenery += dt * params.curve * 0.03f * PFMath::fastSin(drive);
  if (scenery > 1000.0f) scenery -= 1000.0f;
  if (scenery < -1000.0f) scenery += 1000.0f;
  twinkle += dt * 3.0f;
  if (twinkle > W) twinkle -= W;
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  const Look& L = PAL[params.palette];

  const float cx = FW * 0.5f;
  const float hy = FH * (PF_PORTRAIT ? 0.56f : 0.63f);        // horizon
  const float shortS = (float)(FW < FH ? FW : FH);
  const float sunR = shortS * params.sun;
  const float sunCy = -sunR * 0.45f;                           // centre, above horizon
  const float invHaloW = 1.0f / (sunR * 0.6f);                 // halo reaches 1.6 sun radii
  const float invSunD = 1.0f / (2.0f * sunR);
  const float mScale = shortS * 0.3f;

  // The road swings left and right, the horizon banks with it.
  const float steer = params.curve * PFMath::fastSin(drive);
  const float bank = steer * 0.12f;
  const float cb = PFMath::fastCos(bank), sb = PFMath::fastSin(bank);
  const float camH = shortS * 0.35f;                           // camera height: z = camH / v
  const float invFloor = 1.0f / (FH - hy);
  const float invHy = 1.0f / hy;
  const float invCamH = 1.0f / camH;

  for (int y = 0; y < FH; y++) {
    for (int x = 0; x < FW; x++) {
      // Screen -> banked view coordinates: u across, v below the horizon.
      const float px = (float)x + 0.5f - cx, py = (float)y + 0.5f - hy;
      const float u = px * cb + py * sb;
      const float v = -px * sb + py * cb;
      RGBf c;

      if (v < 0.0f) {
        // ── Sky ──
        const float up = clamp01(-v * invHy);
        c = mix(L.skyLow, L.skyTop, up < 0.5f ? up * 2.0f * (1.0f - up * 0.5f) : 0.75f + (up - 0.5f) * 0.5f);
        // Twinkling stars in the upper sky.
        if (up > 0.35f) {
          const uint32_t gx = (uint32_t)PFMath::ifloor((u + 400.0f) * 0.5f);
          const uint32_t gy = (uint32_t)PFMath::ifloor((v + 400.0f) * 0.5f);
          uint32_t hh = gx * 0x8da6b343u ^ gy * 0xd8163841u;
          hh ^= hh >> 13; hh *= 0x5bd1e995u; hh ^= hh >> 15;
          const float h = (float)(hh & 0xffff) * (1.0f / 65536.0f);
          if (h > 0.93f) {
            const float tw = 0.5f + 0.5f * PFMath::fastSin(twinkle + h * 60.0f);
            const float k = (h - 0.93f) * 14.0f * tw * up;
            c.r += 255.0f * k; c.g += 255.0f * k; c.b += 255.0f * k;
          }
        }
        // Sun and its halo.
        const float dy = v - sunCy;
        const float d2 = u * u + dy * dy;
        const float halo = sunR * 1.6f;
        if (d2 < halo * halo) {
          const float d = d2 * frsqrt(d2 + 1e-4f);
          if (d < sunR) {
            const float sv = clamp01((dy + sunR) * invSunD);   // 0 top .. 1 bottom
            RGBf sc = mix(L.sunTop, L.sunLow, sv * sv * (3.0f - 2.0f * sv));
            // Classic cut stripes, widening downward and sliding down.
            float edge = clamp01(sunR - d);               // 1-px soft rim
            if (sv > 0.3f) {
              const float band = (sv - stripes * 0.16f) * 6.2f;
              const float fb = band - (float)PFMath::ifloor(band);
              const float gap = (sv - 0.3f) * 1.5f;
              float open = clamp01((fb - (1.0f - gap)) * sunR * 0.35f + 0.5f);
              edge *= 1.0f - open;
            }
            c = mix(c, sc, edge);
          } else {
            const float k = 1.0f - (d - sunR) * invHaloW;
            const float g = k * k * 0.45f;
            c.r += L.sunLow.r * g; c.g += L.sunLow.g * g; c.b += L.sunLow.b * g;
          }
        }
        // Wireframe mountains in front of the sun.
        const float mh = ridge(u, mScale);
        if (-v < mh) {
          const float depth = -v * frcp(mh + 0.01f);       // 0 at base .. 1 at the ridge
          c = L.mount;
          // Horizontal contour lines that sharpen toward the peaks.
          const float cl = (-v) * 0.5f;
          const float fl = cl - (float)PFMath::ifloor(cl);
          const float line = clamp01(1.0f - fabsf(fl - 0.5f) * 4.0f) * depth * 0.6f;
          c = mix(c, L.rim, line);
          // Bright rim along the skyline.
          const float rim = clamp01(1.2f - (mh + v));
          c = mix(c, L.rim, rim);
        }
      } else {
        // ── Floor ──
        const float vv = v + 0.35f;
        const float iv = frcp(vv);
        const float z = camH * iv;                          // depth along the road
        const float dzPx = z * iv;                          // depth per pixel row
        // Road curve: far rows shift sideways in proportion to z^2.
        const float xw = (u - steer * z * z * 0.35f) * z * invCamH * 1.6f;
        const float dxPx = z * invCamH * 1.6f;

        const float zz = z * 0.5f + travel;
        float fz = zz - (float)PFMath::ifloor(zz);
        fz = fz < 0.5f ? fz : 1.0f - fz;
        float lineZ = clamp01(1.0f - fz * frcp(dzPx * 0.5f * 1.1f + 0.02f));

        float fx = xw - (float)PFMath::ifloor(xw);
        fx = fx < 0.5f ? fx : 1.0f - fx;
        float lineX = clamp01(1.0f - fx * frcp(dxPx * 1.1f + 0.01f));

        // Where lines crowd closer than a few pixels apart they would only
        // alias, so each family fades by its own density.
        lineZ *= clamp01(1.5f - dzPx * 0.5f * 4.0f);
        lineX *= clamp01(1.4f - dxPx * 4.0f);
        float line = lineZ > lineX ? lineZ : lineX;
        // Fade into haze at the horizon so the lines never moire.
        float haze = clamp01((v - 0.5f) * 3.5f * invFloor);
        haze = haze * (2.0f - haze);
        line *= haze;

        c = mix(L.floor, L.skyLow, clamp01(1.0f - v * 0.25f) * 0.6f);
        // The sun's reflection: a shimmering column under the sun.
        const float rw = sunR * (0.55f + 0.25f * PFMath::fastSin(v * 0.9f - travel * 2.0f));
        if (fabsf(u) < rw) {
          const float k = (1.0f - fabsf(u) * frcp(rw)) * clamp01(1.0f - v * invFloor) * 0.35f;
          c.r += L.sunLow.r * k; c.g += L.sunLow.g * k; c.b += L.sunLow.b * k;
        }
        c = mix(c, L.grid, line);
        // Horizon line: thin and white-hot.
        const float hl = clamp01(1.0f - v * 0.9f);
        c.r += 200.0f * hl; c.g += 120.0f * hl; c.b += 200.0f * hl;
      }
      PFCanvas::setPixel(x, y, u8(c.r), u8(c.g), u8(c.b));
    }
  }
  PFCanvas::present();
}
}  // namespace Outrun

PF_REGISTER_PATTERN(Outrun)
