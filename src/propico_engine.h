#pragma once

#include "romaji_kana.h"
#include <fcitx/addonfactory.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <string>

namespace fcitx {

struct PropicoState : public InputContextProperty {
  enum class Mode { Idle, Composing };
  Mode mode = Mode::Idle;
  std::string reading;   // 確定済みひらがな
  RomajiKana romaji_kana;
};

class PropicoEngine : public InputMethodEngineV3 {
public:
  explicit PropicoEngine(Instance *instance);

  void keyEvent(const InputMethodEntry &entry,
                KeyEvent &event) override;
  void activate(const InputMethodEntry &entry,
                InputContextEvent &event) override;
  void deactivate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;

private:
  Instance *instance_;
  FactoryFor<PropicoState> state_factory_;

  void updatePreedit(InputContext *ic, PropicoState &state);
};

class PropicoEngineFactory : public AddonFactory {
public:
  AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx
