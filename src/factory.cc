#include "propico_engine.h"
#include <fcitx/addonmanager.h>

namespace fcitx {

AddonInstance *PropicoEngineFactory::create(AddonManager *manager) {
  return new PropicoEngine(manager->instance());
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::PropicoEngineFactory)
