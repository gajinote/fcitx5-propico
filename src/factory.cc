#include "pobox_engine.h"
#include <fcitx/addonmanager.h>

namespace fcitx {

AddonInstance *PoboxEngineFactory::create(AddonManager *manager) {
  return new PoboxEngine(manager->instance());
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::PoboxEngineFactory)
