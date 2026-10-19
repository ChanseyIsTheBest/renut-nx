// banjo-nx: the boot-time choice between banjo-nx's native renderer and Renut's (banjo_renut_select.h).
#include "renut_engine/gpu/banjo_renut_select.h"

#include <string>

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(banjo_renderer, "native", "Banjo",
                      "Renderer, chosen at boot. native = banjo-nx's own native renderer (the default; the exact "
                      "reference, with every banjo_* GPU and ring key). renut = Renut's Direct3D-level renderer "
                      "(github.com/masterspike52/reNut): N&B's Direct3D runs natively through plume, with no PM4 and no "
                      "EDRAM tile model; its own renut_* keys apply")
    .allowed({"native", "renut"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace banjo::renut_gpu {

bool Active() {
  static const bool active = REXCVAR_GET(banjo_renderer) == "renut";
  return active;
}

}  // namespace banjo::renut_gpu
