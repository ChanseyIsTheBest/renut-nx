// banjo-nx: the boot-time choice between banjo-nx's native renderer and Renut's (banjo_renut_select.h).
#include "renut_engine/gpu/banjo_renut_select.h"
#include "banjo_switch_default.h"  // banjo-nx (2026-11-01): the shipped Switch defaults

#include <string>

#include <rex/cvar.h>
#include <rex/logging/macros.h>

REXCVAR_DEFINE_STRING(banjo_renderer, BANJO_SWITCH_DEFAULT("renut", "native"), "Banjo",
                      "Renderer, chosen at boot. native = banjo-nx's own native renderer (the default; the exact "
                      "reference, with every banjo_* GPU and ring key). renut = Renut's Direct3D-level renderer "
                      "(github.com/masterspike52/reNut): N&B's Direct3D runs natively through plume, with no PM4 and no "
                      "EDRAM tile model; its own renut_* keys apply")
    .allowed({"native", "renut"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace banjo::renut_gpu {

// banjo-nx (2026-10-19, CPU proposal 22): Active() is inline (banjo_renut_select.h), a byte load after the first call.
namespace detail {
int8_t g_active = 0;
bool LatchActive() {
#if BANJO_SWITCH_NO_VULKAN
  // banjo-nx (2026-10-20): a Switch build without the Vulkan driver has only Renut's renderer on deko3d (the native
  // renderer and the emulated GPU need Vulkan).
  static const bool active = [] {
    if (REXCVAR_GET(banjo_renderer) != "renut")
      REXLOG_WARN("banjo_renderer = \"{}\" needs Vulkan, which this build does not have: Renut's renderer on deko3d "
                  "instead", REXCVAR_GET(banjo_renderer));
    return true;
  }();
#else
  // banjo-nx (2026-10-29, title update 3): the native renderer (app/src/me, app/src/native) reads the base game's
  // CDevice layout, which title update 3 changed (4 bytes more from 0x2AC0, 8 from 0x2E30; render/draw.h); it is kept
  // as source only, and Renut's renderer runs whatever banjo_renderer says.
  static const bool active = [] {
    if (REXCVAR_GET(banjo_renderer) != "renut")
      REXLOG_WARN("banjo_renderer = \"{}\": the native renderer reads the base game's Direct3D device layout, not "
                  "title update 3's: Renut's renderer instead", REXCVAR_GET(banjo_renderer));
    return true;
  }();
#endif
  g_active = active ? 1 : -1;
  return active;
}
}  // namespace detail

}  // namespace banjo::renut_gpu
