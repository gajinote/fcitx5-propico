#pragma once

#include "romaji_kana.h"
#include <fcitx/addonfactory.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GrpcClient;

namespace fcitx {

struct CandidateData {
    std::string id;
    std::string text;
    std::string reading;
};

struct PropicoState : public InputContextProperty {
    enum class Mode { Idle, Composing, Selecting };
    Mode mode = Mode::Idle;
    std::string reading;
    RomajiKana romaji_kana;
    std::vector<CandidateData> candidates;
};

class PropicoEngine : public InputMethodEngineV3 {
public:
    explicit PropicoEngine(Instance *instance);
    ~PropicoEngine();

    void keyEvent(const InputMethodEntry &entry,
                  KeyEvent &event) override;
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;

    void onCandidateSelected(InputContext *ic, const std::string &text,
                             const std::string &id);

private:
    Instance *instance_;
    FactoryFor<PropicoState> state_factory_;
    std::unique_ptr<GrpcClient> grpc_client_;
    std::shared_ptr<std::atomic<bool>> alive_;
    int64_t sync_timestamp_ = 0;

    void updatePreedit(InputContext *ic, PropicoState &state);
    void showCandidates(InputContext *ic, PropicoState &state);
    void clearCandidates(InputContext *ic, PropicoState &state);
    void triggerSync();
};

class PropicoEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override;
};

} // namespace fcitx
