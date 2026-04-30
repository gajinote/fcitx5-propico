#pragma once

#include <fcitx/addonfactory.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <string>

namespace fcitx {

struct PoboxState : public InputContextProperty {
  enum class Mode { Idle, Composing };
  Mode mode = Mode::Idle;
  std::string buffer;
};

class PoboxEngine : public InputMethodEngineV3 {
public:
  explicit PoboxEngine(Instance *instance);

  void keyEvent(const InputMethodEntry &entry,
                KeyEvent &event) override;
  void activate(const InputMethodEntry &entry,
                InputContextEvent &event) override;
  void deactivate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;

private:
  Instance *instance_;
  FactoryFor<PoboxState> state_factory_;

  void updatePreedit(InputContext *ic, PoboxState &state);
};

class PoboxEngineFactory : public AddonFactory {
public:
  AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx
