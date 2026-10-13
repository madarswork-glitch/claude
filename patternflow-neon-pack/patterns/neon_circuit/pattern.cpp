// SPDX-License-Identifier: CC-BY-SA-4.0
// Pattern: Neon Circuit
// Author:  madarswork + Claude
// Lineage: original (Truchet tiles, after Sebastien Truchet, 1704)
//
// A maze of neon tubes that keeps rewiring itself. Every tile holds two
// quarter-circle arcs; together they join into closed loops and long winding
// paths. Tiles keep spinning a quarter turn - you see each one rotate - and
// every spin cuts some loops and splices others. After each change every path
// is walked end to end, so colour runs along it as one continuous neon
// gradient and the light pulses travel the whole circuit in one direction -
// through the tiles, round the bends, and across the spliced joints.
#include "pf_module.h"

#ifndef PF_PORTRAIT
#define PF_PORTRAIT 1
#endif

namespace NeonCircuit {
const char* NAME = "Neon Circuit";
const char* const KNOB_LABELS[4] = {"flow", "tile", "rewire", "palette"};
constexpr bool ABSOLUTE_READY = true;

constexpr int FW = PF_PORTRAIT ? 64 : 128;
constexpr int FH = PF_PORTRAIT ? 128 : 64;

struct RGB { uint8_t r, g, b; };
constexpr int NPAL = 6;
constexpr int NC = 5;
const RGB PAL[NPAL][NC] = {
  {{255, 0, 150}, {0, 220, 255}, {255, 210, 0}, {130, 0, 255}, {0, 255, 100}},   // arcade
  {{255, 30, 0}, {255, 150, 0}, {255, 0, 90}, {255, 230, 60}, {200, 0, 255}},    // ember
  {{0, 90, 255}, {0, 255, 230}, {170, 0, 255}, {120, 200, 255}, {0, 255, 120}},  // deep sea
  {{255, 0, 0}, {0, 255, 0}, {0, 60, 255}, {255, 255, 0}, {255, 0, 255}},        // RGB
  {{0, 255, 60}, {0, 160, 40}, {180, 255, 0}, {0, 255, 180}, {255, 255, 255}},   // terminal
  {{255, 255, 255}, {255, 0, 100}, {0, 200, 255}, {255, 160, 0}, {140, 0, 255}},   // stage
};

constexpr int SIZES[4] = {8, 12, 16, 22};
constexpr int MAXC = FW / 8 + 2, MAXR = FH / 8 + 2;
constexpr int MAXT = MAXC * MAXR;
constexpr int MAXN = (MAXR + 1) * MAXC + (MAXC + 1) * MAXR;

struct Params {
  float flow = 1.0f;
  int tile = 2;
  float rewire = 1.0f;     // tile flips per second, per 100 tiles
  int palette = 0;
};
Params params;

int cols = 0, rows = 0, size = 0, offX = 0, offY = 0;
uint8_t orient[MAXT];
float anim[MAXT];          // 0 idle, else 0..1 progress of a quarter turn
// Per arc (0: the one touching the top edge, 1: the bottom edge): where it sits
// along its path, which way the path runs through it, and the path's hue.
float arcPos[MAXT][2];
int8_t arcDir[MAXT][2];
float arcHue[MAXT][2];
int16_t adj[MAXN][2];
uint8_t seen[MAXT * 2];
float pulse = 0.0f, flipDebt = 0.0f;

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

inline uint8_t u8(float v) { return v <= 0.0f ? 0 : (v >= 255.0f ? 255 : (uint8_t)v); }

// ── Path walking ────────────────────────────────────────────────────────────
// Nodes are tile-edge midpoints; each arc joins two of them. Every node has at
// most two arcs, so the network is a set of simple paths (ending at the panel
// border) and closed loops. Walk each one, numbering its arcs in order.
inline int nodeTop(int c, int r) { return r * cols + c; }
inline int nodeLeft(int c, int r) { return (rows + 1) * cols + r * (cols + 1) + c; }

// Endpoints of arc a: e0 is its top / bottom edge node (where "along" = 0),
// e1 its side node (along = 1).
inline void ends(int a, int& e0, int& e1) {
  const int t = a >> 1, which = a & 1;
  const int c = t % cols, r = t / cols;
  const bool o = orient[t] != 0;
  if (which == 0) { e0 = nodeTop(c, r); e1 = o ? nodeLeft(c + 1, r) : nodeLeft(c, r); }
  else { e0 = nodeTop(c, r + 1); e1 = o ? nodeLeft(c, r) : nodeLeft(c + 1, r); }
}

void walk(int node, int arc, float hue) {
  float pos = 0.0f;
  while (arc >= 0 && !seen[arc]) {
    seen[arc] = 1;
    int e0, e1;
    ends(arc, e0, e1);
    const int t = arc >> 1, w = arc & 1;
    arcPos[t][w] = pos;
    arcHue[t][w] = hue;
    int next;
    if (e0 == node) { arcDir[t][w] = 1; next = e1; }
    else { arcDir[t][w] = -1; next = e0; }
    pos += 1.0f;
    node = next;
    arc = adj[node][0] == arc ? adj[node][1] : adj[node][0];
  }
}

void traceLoops() {
  const int nn = (rows + 1) * cols + (cols + 1) * rows;
  const int na = cols * rows * 2;
  for (int i = 0; i < nn; i++) adj[i][0] = adj[i][1] = -1;
  for (int a = 0; a < na; a++) {
    seen[a] = 0;
    int e0, e1;
    ends(a, e0, e1);
    adj[e0][adj[e0][0] < 0 ? 0 : 1] = (int16_t)a;
    adj[e1][adj[e1][0] < 0 ? 0 : 1] = (int16_t)a;
  }
  // Open paths first, from their border ends; then whatever is left is loops.
  // A path's hue is hashed from where it starts, so paths a spin did not
  // touch keep their colours.
  for (int n = 0; n < nn; n++)
    if (adj[n][0] >= 0 && adj[n][1] < 0 && !seen[adj[n][0]])
      walk(n, adj[n][0], (float)(((uint32_t)n * 2654435761u) >> 20) * (1.0f / 4096.0f));
  for (int a = 0; a < na; a++)
    if (!seen[a]) {
      int e0, e1;
      ends(a, e0, e1);
      walk(e0, a, (float)(((uint32_t)a * 2246822519u) >> 20) * (1.0f / 4096.0f));
    }
}

// Distance from (px, py) - tile-centred - to the nearer of the tile's two
// arcs, which arc that is, and how far along it (0 at its top/bottom edge
// end, 1 at its side end).
inline void arcGeom(int o, float px, float py, float h, float& e, int& which, float& along) {
  const float sx = o ? 1.0f : -1.0f;
  const float ax = px - sx * h, ay = py + h;     // from the top corner
  const float bx = px + sx * h, by = py - h;     // from the bottom corner
  const float da2 = ax * ax + ay * ay + 1e-4f, db2 = bx * bx + by * by + 1e-4f;
  const float ea = fabsf(da2 * frsqrt(da2) - h), eb = fabsf(db2 * frsqrt(db2) - h);
  which = ea < eb ? 0 : 1;
  e = which ? eb : ea;
  const float qx = fabsf(which ? bx : ax), qy = fabsf(which ? by : ay);
  along = qy * frcp(qx + qy + 1e-4f);
}

// A tile at rest always looks the same, so its geometry is a lookup.
constexpr int MAXS = 22;
float tabE[2][MAXS * MAXS], tabA[2][MAXS * MAXS];
uint8_t tabW[2][MAXS * MAXS];

void layout() {
  size = SIZES[params.tile];
  const float h = size * 0.5f;
  for (int o = 0; o < 2; o++)
    for (int ly = 0; ly < size; ly++)
      for (int lx = 0; lx < size; lx++) {
        float e, along;
        int which;
        arcGeom(o, lx + 0.5f - h, ly + 0.5f - h, h, e, which, along);
        tabE[o][ly * size + lx] = e;
        tabA[o][ly * size + lx] = along;
        tabW[o][ly * size + lx] = (uint8_t)which;
      }
  cols = (FW + size - 1) / size + 1;
  rows = (FH + size - 1) / size + 1;
  if (cols > MAXC) cols = MAXC;
  if (rows > MAXR) rows = MAXR;
  offX = (cols * size - FW) / 2;
  offY = (rows * size - FH) / 2;
  for (int i = 0; i < cols * rows; i++) {
    orient[i] = (uint8_t)random(2);
    anim[i] = 0.0f;
  }
  traceLoops();
}

void setup() {
  PFMath::buildSinLUT();
  if (size == 0) layout();
}

void update(float dt, const InputFrame& input) {
  const int oldTile = params.tile;
  PFParams::apply(input, 0, &params.flow, 0.0f, 4.0f, 4.0f / 48.0f);
  PFParams::applyIndex(input, 1, &params.tile, 4);
  PFParams::apply(input, 2, &params.rewire, 0.0f, 6.0f, 6.0f / 48.0f);
  PFParams::applyIndex(input, 3, &params.palette, NPAL);
  if (params.tile != oldTile || size == 0) layout();

  pulse += dt * params.flow;
  if (pulse > 1000.0f) pulse -= 1000.0f;

  // Advance the spinning tiles; a finished quarter turn becomes the other
  // orientation (identical picture), and the loops are re-traced.
  bool changed = false;
  const int n = cols * rows;
  for (int i = 0; i < n; i++) {
    if (anim[i] <= 0.0f) continue;
    anim[i] += dt * 2.6f;
    if (anim[i] >= 1.0f) {
      anim[i] = 0.0f;
      orient[i] ^= 1;
      changed = true;
    }
  }
  if (changed) traceLoops();

  // Start new spins at the requested rate.
  flipDebt += dt * params.rewire * (float)n * 0.01f;
  int guard = 8;
  while (flipDebt >= 1.0f && guard-- > 0) {
    flipDebt -= 1.0f;
    const int t = (int)random(n);
    if (anim[t] <= 0.0f) anim[t] = 0.001f;
  }
  if (flipDebt > 4.0f) flipDebt = 4.0f;
}

void draw() {
  PFCanvas::setFrame(FW, FH);
  const RGB* pal = PAL[params.palette];
  const float S = (float)size, h = S * 0.5f;
  const float tube = S * 0.11f + 0.45f;        // half-width of the lit tube
  const float itube = 1.0f / tube;
  const float glowW = tube * 2.4f;
  const float pulseK = 0.34f;                  // pulses per arc along a path
  const float hueDrift = pulse * 0.01f;

  const float iglow = 1.0f / (glowW - tube);

  for (int y = 0; y < FH; y++) {
    const int gy = y + offY;
    const int r = gy / size;
    const int ly = gy - r * size;
    int c = offX / size;
    int lx = offX - c * size;
    for (int x = 0; x < FW; x++, lx++) {
      if (lx == size) { lx = 0; c++; }
      const int t = r * cols + c;
      float e, along;
      int which;
      if (anim[t] <= 0.0f) {
        const int k = ly * size + lx;
        e = tabE[orient[t]][k];
        if (e > glowW) { PFCanvas::setPixel(x, y, 0, 0, 0); continue; }
        along = tabA[orient[t]][k];
        which = tabW[orient[t]][k];
      } else {
        // A spinning tile: look up the unrotated picture, eased in and out.
        float a = anim[t];
        a = a * a * (3.0f - 2.0f * a);
        const float ang = -a * HALF_PI;
        const float ca = PFMath::fastCos(ang), sa = PFMath::fastSin(ang);
        const float px0 = (float)lx + 0.5f - h, py0 = (float)ly + 0.5f - h;
        const float px = px0 * ca - py0 * sa, py = px0 * sa + py0 * ca;
        // Outside the turning square: the gap the spin opens is black.
        if (fabsf(px) > h || fabsf(py) > h) { PFCanvas::setPixel(x, y, 0, 0, 0); continue; }
        arcGeom(orient[t], px, py, h, e, which, along);
        if (e > glowW) { PFCanvas::setPixel(x, y, 0, 0, 0); continue; }
      }

      // Position along the whole path.
      const float pos = arcPos[t][which] + (arcDir[t][which] > 0 ? along : 1.0f - along);
      float ph = pos * pulseK - pulse;
      ph -= PFMath::floorF(ph);
      float bump = 1.0f - fabsf(ph - 0.5f) * 3.0f;
      bump = bump > 0.0f ? bump * bump : 0.0f;

      // Colour runs along the path through the palette.
      float hp = (arcHue[t][which] + pos * 0.04f + hueDrift) * NC;
      hp -= NC * PFMath::floorF(hp * (1.0f / NC));
      const int hi = (int)hp;
      // Hold each colour, then switch over a short stretch: a long blend
      // between two neons passes through grey.
      float hf = (hp - (float)hi - 0.82f) * (1.0f / 0.18f);
      hf = hf < 0.0f ? 0.0f : hf;
      const RGB& c0 = pal[hi % NC];
      const RGB& c1 = pal[(hi + 1) % NC];
      const float cr = c0.r + (c1.r - c0.r) * hf, cg = c0.g + (c1.g - c0.g) * hf,
                  cb = c0.b + (c1.b - c0.b) * hf;
      float lit;
      if (e < tube) {
        lit = 0.55f + 0.45f * (1.0f - e * itube);
      } else {
        const float k = 1.0f - (e - tube) * iglow;
        lit = 0.35f * k * k;
      }
      lit *= 0.45f + 0.55f * bump;
      // White-hot core only where a pulse passes, so the tubes stay saturated.
      float core = 1.0f - e * itube * 2.6f;
      core = core > 0.0f ? core * bump * bump * 230.0f : 0.0f;
      PFCanvas::setPixel(x, y, u8(cr * lit + core), u8(cg * lit + core), u8(cb * lit + core));
    }
  }
  PFCanvas::present();
}
}  // namespace NeonCircuit

PF_REGISTER_PATTERN(NeonCircuit)
