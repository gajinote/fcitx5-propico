#include "propico_engine.h"
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>

namespace fcitx {

namespace {

// UTF-8 文字列の末尾1コードポイントを削除する
void popLastUtf8Char(std::string &s) {
  if (s.empty()) return;
  size_t i = s.size();
  while (i > 0 && (static_cast<unsigned char>(s[--i]) & 0xC0) == 0x80) {}
  s.erase(i);
}

} // namespace

PropicoEngine::PropicoEngine(Instance *instance)
    : instance_(instance),
      state_factory_([](InputContext &) { return new PropicoState; }) {
  instance_->inputContextManager().registerProperty("propicoState",
                                                    &state_factory_);
}

void PropicoEngine::activate(const InputMethodEntry &,
                             InputContextEvent &) {}

void PropicoEngine::deactivate(const InputMethodEntry &,
                               InputContextEvent &event) {
  auto *ic = event.inputContext();
  auto *state = ic->propertyFor(&state_factory_);
  state->reading.clear();
  state->romaji_kana.reset();
  state->mode = PropicoState::Mode::Idle;
  updatePreedit(ic, *state);
}

void PropicoEngine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
  if (event.isRelease()) return;

  auto *ic = event.inputContext();
  auto *state = ic->propertyFor(&state_factory_);
  const auto &key = event.key();

  if (key.check(FcitxKey_Return)) {
    if (!state->reading.empty() || !state->romaji_kana.pending().empty()) {
      // pending が "n" 単独ならんに変換、それ以外はローマ字のまま追加
      std::string commit = state->reading;
      const auto &p = state->romaji_kana.pending();
      commit += (p == "n") ? "ん" : p;
      ic->commitString(commit);
      state->reading.clear();
      state->romaji_kana.reset();
      state->mode = PropicoState::Mode::Idle;
      updatePreedit(ic, *state);
      event.filterAndAccept();
    }
    return;
  }

  if (key.check(FcitxKey_Escape)) {
    if (!state->reading.empty() || !state->romaji_kana.pending().empty()) {
      state->reading.clear();
      state->romaji_kana.reset();
      state->mode = PropicoState::Mode::Idle;
      updatePreedit(ic, *state);
      event.filterAndAccept();
    }
    return;
  }

  if (key.check(FcitxKey_BackSpace)) {
    if (!state->romaji_kana.pending().empty()) {
      state->romaji_kana.backspace();
      if (state->reading.empty() && state->romaji_kana.pending().empty()) {
        state->mode = PropicoState::Mode::Idle;
      }
      updatePreedit(ic, *state);
      event.filterAndAccept();
    } else if (!state->reading.empty()) {
      popLastUtf8Char(state->reading);
      if (state->reading.empty()) {
        state->mode = PropicoState::Mode::Idle;
      }
      updatePreedit(ic, *state);
      event.filterAndAccept();
    }
    return;
  }

  if (key.isSimple()) {
    const char c = static_cast<char>(key.sym() & 0x7F);
    state->reading += state->romaji_kana.feed(c);
    state->mode = PropicoState::Mode::Composing;
    updatePreedit(ic, *state);
    event.filterAndAccept();
    return;
  }
}

void PropicoEngine::updatePreedit(InputContext *ic, PropicoState &state) {
  Text preedit;
  if (!state.reading.empty()) {
    preedit.append(state.reading, TextFormatFlag::Underline);
  }
  if (!state.romaji_kana.pending().empty()) {
    preedit.append(state.romaji_kana.pending());
  }
  if (!state.reading.empty() || !state.romaji_kana.pending().empty()) {
    preedit.setCursor(static_cast<int>(
        state.reading.size() + state.romaji_kana.pending().size()));
  }
  ic->inputPanel().setClientPreedit(preedit);
  ic->updatePreedit();
}

} // namespace fcitx
