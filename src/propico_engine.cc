#include "propico_engine.h"
#include "grpc_client.h"
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>

namespace fcitx {

namespace {

void popLastUtf8Char(std::string &s) {
  if (s.empty()) return;
  size_t i = s.size();
  while (i > 0 && (static_cast<unsigned char>(s[--i]) & 0xC0) == 0x80) {}
  s.erase(i);
}

class PropicoCandidateWord : public CandidateWord {
public:
  PropicoCandidateWord(PropicoEngine *engine, std::string text,
                       std::string id)
      : CandidateWord(Text(text)), engine_(engine),
        id_(std::move(id)), textStr_(std::move(text)) {}

  void select(InputContext *ic) const override {
    engine_->onCandidateSelected(ic, textStr_, id_);
  }

private:
  PropicoEngine *engine_;
  std::string id_;
  std::string textStr_;
};

} // namespace

PropicoEngine::PropicoEngine(Instance *instance)
    : instance_(instance),
      state_factory_([](InputContext &) { return new PropicoState; }),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
  instance_->inputContextManager().registerProperty("propicoState",
                                                    &state_factory_);
  grpc_client_ = std::make_unique<GrpcClient>(
      "localhost:50051", &instance_->eventDispatcher());
  triggerSync();
}

PropicoEngine::~PropicoEngine() {
  alive_->store(false, std::memory_order_relaxed);
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
  if (!state->candidates.empty()) {
    clearCandidates(ic, *state);
  }
  updatePreedit(ic, *state);
}

void PropicoEngine::keyEvent(const InputMethodEntry &, KeyEvent &event) {
  if (event.isRelease()) return;

  auto *ic = event.inputContext();
  auto *state = ic->propertyFor(&state_factory_);
  const auto &key = event.key();

  // === SELECTING モード ===
  if (state->mode == PropicoState::Mode::Selecting) {
    if (key.check(FcitxKey_Escape)) {
      state->mode = PropicoState::Mode::Composing;
      clearCandidates(ic, *state);
      event.filterAndAccept();
      return;
    }
    if (key.check(FcitxKey_Return) && !state->candidates.empty()) {
      auto cl = ic->inputPanel().candidateList();
      if (cl && cl->size() > 0) {
        int idx = cl->cursorIndex() >= 0 ? cl->cursorIndex() : 0;
        cl->candidate(idx).select(ic);
      }
      event.filterAndAccept();
      return;
    }
    if (key.check(FcitxKey_Up) || key.check(FcitxKey_Left)) {
      auto cl = ic->inputPanel().candidateList();
      if (cl) {
        if (auto *cur = cl->toCursorMovable()) {
          cur->prevCandidate();
          ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
      }
      event.filterAndAccept();
      return;
    }
    if (key.check(FcitxKey_Down) || key.check(FcitxKey_Right)) {
      auto cl = ic->inputPanel().candidateList();
      if (cl) {
        if (auto *cur = cl->toCursorMovable()) {
          cur->nextCandidate();
          ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
      }
      event.filterAndAccept();
      return;
    }
    if (key.isSimple()) {
      const char c = static_cast<char>(key.sym() & 0x7F);
      if (c >= '1' && c <= '9') {
        auto cl = ic->inputPanel().candidateList();
        int localIdx = c - '1';
        if (cl && localIdx < cl->size()) {
          cl->candidate(localIdx).select(ic);
        }
        event.filterAndAccept();
        return;
      }
    }
    if (key.check(FcitxKey_space)) {
      auto cl = ic->inputPanel().candidateList();
      if (cl) {
        if (auto *p = cl->toPageable(); p && p->hasNext()) {
          p->next();
          ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
      }
      event.filterAndAccept();
      return;
    }
    event.filterAndAccept();
    return;
  }

  // === Return ===
  if (key.check(FcitxKey_Return)) {
    if (!state->reading.empty() || !state->romaji_kana.pending().empty()) {
      std::string commit = state->reading;
      const auto &p = state->romaji_kana.pending();
      if (p == "n") commit += "ん";
      // "n" 以外の未確定ローマ字は不完全なため破棄する（生ASCIIをコミットしない）
      if (!commit.empty()) {
        ic->commitString(commit);
      }
      state->reading.clear();
      state->romaji_kana.reset();
      state->mode = PropicoState::Mode::Idle;
      updatePreedit(ic, *state);
      event.filterAndAccept();
    }
    return;
  }

  // === Escape ===
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

  // === BackSpace ===
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

  // === Space → gRPC Search ===
  if (key.check(FcitxKey_space)) {
    if (state->mode == PropicoState::Mode::Composing) {
      const auto &p = state->romaji_kana.pending();
      if (p == "n") state->reading += "ん";
      // "n" 以外の未確定ローマ字は不完全なため破棄する
      state->romaji_kana.reset();

      if (!state->reading.empty()) {
        state->mode = PropicoState::Mode::Selecting;
        updatePreedit(ic, *state);

        std::string prefix = state->reading;
        auto alive = alive_;
        uint64_t generation = ++state->search_generation;
        grpc_client_->searchAsync(
            prefix,
            [this, alive, icRef = ic->watch(), generation](
                propico::SearchResponse resp) {
              // GrpcClient の dispatcher_->schedule により
              // すでに fcitx5 main スレッド上で呼ばれる
              if (!alive->load(std::memory_order_relaxed)) return;
              if (!icRef.isValid()) return;
              auto *ic2 = icRef.get();
              auto *st = ic2->propertyFor(&state_factory_);
              if (st->mode != PropicoState::Mode::Selecting) return;
              // 検索中に別の検索が発行されていたら破棄する
              if (st->search_generation != generation) return;
              st->candidates.clear();
              for (const auto &c : resp.candidates()) {
                st->candidates.push_back(
                    {c.id(), c.text(), c.reading()});
              }
              if (!st->candidates.empty()) {
                showCandidates(ic2, *st);
              } else {
                // 候補なしは COMPOSING に戻す
                st->mode = PropicoState::Mode::Composing;
                updatePreedit(ic2, *st);
              }
            });
        event.filterAndAccept();
      } else {
        // 未確定ローマ字を破棄しただけの場合も Space を消費し、
        // 半角スペースがアプリ側に漏れるのを防ぐ
        state->mode = PropicoState::Mode::Idle;
        updatePreedit(ic, *state);
        event.filterAndAccept();
      }
    }
    return;
  }

  // === 英数字 ===
  if (key.isSimple()) {
    const char c = static_cast<char>(key.sym() & 0x7F);
    state->reading += state->romaji_kana.feed(c);
    state->mode = PropicoState::Mode::Composing;
    updatePreedit(ic, *state);
    event.filterAndAccept();
    return;
  }
}

void PropicoEngine::onCandidateSelected(InputContext *ic,
                                         const std::string &text,
                                         const std::string &id) {
  auto *state = ic->propertyFor(&state_factory_);
  ic->commitString(text);
  grpc_client_->learnAsync(id, state->reading);
  state->reading.clear();
  state->romaji_kana.reset();
  state->mode = PropicoState::Mode::Idle;
  clearCandidates(ic, *state);
  updatePreedit(ic, *state);
  triggerSync();
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

void PropicoEngine::showCandidates(InputContext *ic, PropicoState &state) {
  auto candidateList = std::make_unique<CommonCandidateList>();
  candidateList->setLayoutHint(CandidateLayoutHint::Vertical);
  candidateList->setPageSize(9);
  for (const auto &c : state.candidates) {
    candidateList->append<PropicoCandidateWord>(this, c.text, c.id);
  }
  candidateList->setCursorIndex(0);
  ic->inputPanel().setCandidateList(std::move(candidateList));
  ic->updateUserInterface(UserInterfaceComponent::InputPanel);
  ic->updatePreedit();
}

void PropicoEngine::clearCandidates(InputContext *ic, PropicoState &state) {
  state.candidates.clear();
  ic->inputPanel().setCandidateList(nullptr);
  ic->updateUserInterface(UserInterfaceComponent::InputPanel);
  ic->updatePreedit();
}

void PropicoEngine::triggerSync() {
  auto alive = alive_;
  grpc_client_->syncAsync(
      sync_timestamp_,
      [this, alive](propico::SyncResponse resp) {
        if (!alive->load(std::memory_order_relaxed)) return;
        sync_timestamp_ = resp.server_timestamp();
      });
}

} // namespace fcitx
