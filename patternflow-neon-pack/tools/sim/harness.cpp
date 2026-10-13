// Native simulator for Patternflow .pfm pattern sources.
//
// Compiled together with a pattern.cpp (which includes the real abi/pf_module.h
// and exports pf_module_entry), it plays the host's part: owns the panel
// framebuffer, hands out memory, feeds InputFrames, and records every
// present() to a raw RGB file for tools/preview.py.
//
//   sim --frames 240 --fps 30 --out clip.rgb [--abs 0=500 --abs 3=900]
//       [--sweep] [--turn 1=+2] [--time]
//
//   --abs  i=v    hold knob i on the absolute bus at v (0..1000) all clip
//   --sweep       sweep every knob 0 -> 1000 -> 0, staggered (range test)
//   --turn i=d    turn knob i by d detents every frame (delta path test)
//   --time        print mean draw() time in microseconds
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pf_abi.h"

static uint8_t g_fb[PF_PANEL_W * PF_PANEL_H * 3];
static FILE* g_out = nullptr;
static uint32_t g_ms = 0;
static uint32_t g_rng = 0x9E3779B9u;
static float* g_r = nullptr;
static float* g_theta = nullptr;
static long g_presents = 0;
static long g_nonfinite = 0;

static void h_present() {
  g_presents++;
  if (g_out) fwrite(g_fb, 1, sizeof(g_fb), g_out);
}
static void h_clear() { memset(g_fb, 0, sizeof(g_fb)); }
static void* h_alloc(size_t n) { return calloc(1, n); }
static void h_log(const char* m) { fputs(m, stderr); }
static void h_vlogf(const char* f, va_list ap) { vfprintf(stderr, f, ap); }
static uint32_t h_millis() { return g_ms; }
static uint32_t h_rand32() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static void build_tables() {
  if (g_r) return;
  const int N = PF_PANEL_W * PF_PANEL_H;
  g_r = (float*)calloc(N, sizeof(float));
  g_theta = (float*)calloc(N, sizeof(float));
  const float hw = PF_PANEL_W * 0.5f, hh = PF_PANEL_H * 0.5f, inv = 1.0f / PF_PANEL_H;
  for (int y = 0; y < PF_PANEL_H; y++)
    for (int x = 0; x < PF_PANEL_W; x++) {
      float nx = (x - hw) * inv, ny = (y - hh) * inv;
      g_r[y * PF_PANEL_W + x] = sqrtf(nx * nx + ny * ny);
      g_theta[y * PF_PANEL_W + x] = atan2f(ny, nx);
    }
}
static const float* h_tr() { build_tables(); return g_r; }
static const float* h_tt() { build_tables(); return g_theta; }

int main(int argc, char** argv) {
  int frames = 240;
  float fps = 30.0f;
  std::string out;
  bool sweep = false, timing = false;
  int absv[4] = {-1, -1, -1, -1};
  int turn[4] = {0, 0, 0, 0};
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() { return std::string(i + 1 < argc ? argv[++i] : ""); };
    if (a == "--frames") frames = atoi(next().c_str());
    else if (a == "--fps") fps = (float)atof(next().c_str());
    else if (a == "--out") out = next();
    else if (a == "--sweep") sweep = true;
    else if (a == "--time") timing = true;
    else if (a == "--abs" || a == "--turn") {
      std::string kv = next();
      size_t eq = kv.find('=');
      int k = atoi(kv.substr(0, eq).c_str());
      int v = atoi(kv.substr(eq + 1).c_str());
      if (k >= 0 && k < 4) (a == "--abs" ? absv : turn)[k] = v;
    }
  }
  if (!out.empty()) {
    g_out = fopen(out.c_str(), "wb");
    if (!g_out) { perror("open"); return 1; }
  }

  static PFHostAPI api = {};
  api.abi_version = PF_ABI_VERSION;
  api.panel_w = PF_PANEL_W;
  api.panel_h = PF_PANEL_H;
  api.framebuffer = g_fb;
  api.present = h_present;
  api.clear = h_clear;
  api.alloc = h_alloc;
  api.log = h_log;
  api.vlogf = h_vlogf;
  api.millis = h_millis;
  api.rand32 = h_rand32;
  api.tables_r = h_tr;
  api.tables_theta = h_tt;

  const PFPatternModule* m = pf_module_entry(&api);
  if (!m) { fprintf(stderr, "module refused to load\n"); return 1; }
  fprintf(stderr, "loaded \"%s\" knobs: %s | %s | %s | %s\n", m->name,
          m->knob_labels[0], m->knob_labels[1], m->knob_labels[2], m->knob_labels[3]);
  m->setup();

  const float dt = 1.0f / fps;
  double drawUs = 0;
  for (int f = 0; f < frames; f++) {
    PFInputFrame in = {};
    in.now = g_ms;
    for (int k = 0; k < 4; k++) {
      in.knobDeltas[k] = turn[k];
      if (absv[k] >= 0) {
        in.paramAbsoluteActive[k] = true;
        in.paramAbsolute[k] = (uint16_t)absv[k];
      }
      if (sweep) {
        // Each knob gets its own window of the clip, sweeping 0 -> 1000 -> 0.
        float u = (float)f / (float)frames;
        float lo = k * 0.25f, hi = lo + 0.25f;
        if (u >= lo && u < hi) {
          float p = (u - lo) / 0.25f;
          float tri = p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f;
          in.paramAbsoluteActive[k] = true;
          in.paramAbsolute[k] = (uint16_t)(tri * 1000.0f);
        }
      }
    }
    m->update(dt, &in);
    auto t0 = std::chrono::steady_clock::now();
    m->draw();
    auto t1 = std::chrono::steady_clock::now();
    drawUs += std::chrono::duration<double, std::micro>(t1 - t0).count();
    g_ms += (uint32_t)(dt * 1000.0f);
  }
  if (g_out) fclose(g_out);
  if (g_presents != frames)
    fprintf(stderr, "WARNING: %ld presents for %d frames\n", g_presents, frames);
  if (timing) printf("%s: %.0f us/frame (native)\n", m->name, drawUs / frames);
  (void)g_nonfinite;
  return 0;
}
