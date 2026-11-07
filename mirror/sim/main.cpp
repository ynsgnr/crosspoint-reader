// mirror_sim: replays a golden transcript through the real Session + ChatRenderer + RefreshPolicy and writes
// the frames the panel would show (PNG, dirty rects outlined in red) plus frames.txt with the refresh plan of each.
// Usage: mirror_sim <transcript.txt> <out_dir> [width height]
#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/Frame.h"
#include "core/RefreshPolicy.h"
#include "sim/Png.h"
#include "sim/SimGfx.h"
#include "sim/Transcript.h"

using namespace mirror;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s transcript.txt out_dir [width height]\n", argv[0]);
    return 2;
  }
  const int W = argc > 4 ? std::atoi(argv[3]) : 480, H = argc > 4 ? std::atoi(argv[4]) : 800;
  std::ifstream f(argv[1]);
  if (!f) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  mkdir(argv[2], 0755);

  sim::SimGfx gfx(W, H);
  RefreshPolicy policy;
  std::ofstream plans(std::string(argv[2]) + "/frames.txt");
  int frame = 0;
  sim::ReplayHooks hooks;
  hooks.fail = [](const std::string& m) { std::fprintf(stderr, "transcript expectation failed: %s\n", m.c_str()); };
  hooks.afterLine = [&](const std::string& line, Session& s) {
    if (!s.active() || (line.rfind("tx ", 0) != 0 && line.rfind("corrupt ", 0) != 0)) return;
    const bool force = s.takeTemplateSwitched() | s.takeResyncHappened();
    const DirtyRects d = renderFrame(*s.active(), gfx, statusBandText(s.phoneStatus()), bandDarkMode(s.phoneStatus()));
    const RefreshPlan p = policy.plan(d, W, H, s.active()->refreshOverrides(), force);
    if (p.kind == RefreshKind::None) return;
    char name[64];
    std::snprintf(name, sizeof name, "/frame_%03d.png", frame);
    sim::writePng(std::string(argv[2]) + name, W, H,
                  gfx.toRgb(p.kind == RefreshKind::Partial ? p.rects : std::vector<Rect>{}));
    plans << "frame_" << frame << " " << (p.kind == RefreshKind::Full ? "FULL" : "PARTIAL");
    for (const Rect& r : p.rects) plans << " [" << r.x << "," << r.y << " " << r.w << "x" << r.h << "]";
    plans << "\n";
    ++frame;
  };
  const bool ok = sim::replay(ss.str(), TemplateRegistry::instance(), hooks);
  std::printf("%d frames written to %s (%s)\n", frame, argv[2], ok ? "transcript OK" : "transcript FAILED");
  return ok ? 0 : 1;
}
