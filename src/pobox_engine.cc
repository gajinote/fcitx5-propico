#include "pobox_engine.h"
#include <fcitx-utils/keysymgen.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>

namespace fcitx {

PoboxEngine::PoboxEngine(Instance *instance)
    : instance_(instance),
      state_factory_([](InputContext &) { return new PoboxState; }) {
  instance_->inputContextManager().registerProperty("poboxState",
                                                    &state_factory_);
}

void PoboxEngine::activate(const InputMethodEntry &,
                           InputContextEvent &) {}

void PoboxEngine::deactivate(const InputMethodEntry &,
                             InputContextEvent &event) {
  auto *ic = event.inputContext();
  auto *state = ic->propertyFor(&state_factory_);
  state->buffer.clear();
  state->mode = PoboxState::Mode::Idle;
  updatePreedit(ic, *state);
}

void PoboxEngine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
  if (event.isRelease()) return;

  auto *ic = event.inputContext();
  auto *state = ic->propertyFor(&state_factory_);
  const auto &key = event.key();

  if (key.check(FcitxKey_Return)) {
    if (!state->buffer.empty()) {
      ic->commitString(state->buffer);
      state->buffer.clear();
      state->mode = PoboxState::Mode::Idle;
      updatePreedit(ic, *state);
      event.filterAndAccept();
    }
    return;
  }

  if (key.check(FcitxKey_Escape)) {
    if (!state->buffer.empty()) {
      state->buffer.clear();
      state->mode = PoboxState::Mode::Idle;
      updatePreedit(ic, *state);
      event.filterAndAccept();
    }
    return;
  }

  if (key.check(FcitxKey_BackSpace)) {
    if (!state->buffer.empty()) {
      state->buffer.pop_back();
      if (state->buffer.empty()) {
        state->mode = PoboxState::Mode::Idle;
      }
      updatePreedit(ic, *state);
      event.filterAndAccept();
    }
    return;
  }

  if (key.isSimple()) {
    state->buffer += Key::keySymToUTF8(key.sym());
    state->mode = PoboxState::Mode::Composing;
    updatePreedit(ic, *state);
    event.filterAndAccept();
    return;
  }
}

void PoboxEngine::updatePreedit(InputContext *ic, PoboxState &state) {
  Text preedit(state.buffer);
  if (!state.buffer.empty()) {
    preedit.setCursor(static_cast<int>(state.buffer.size()));
  }
  ic->inputPanel().setClientPreedit(preedit);
  ic->updatePreedit();
}

} // namespace fcitx
