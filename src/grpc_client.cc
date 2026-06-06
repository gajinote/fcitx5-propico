#include "grpc_client.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <thread>

GrpcClient::GrpcClient(const std::string &target,
                       fcitx::EventDispatcher *dispatcher)
    : dispatcher_(dispatcher),
      alive_(std::make_shared<std::atomic<bool>>(true)) {
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    stub_ = propico::Propico::NewStub(channel);
}

GrpcClient::~GrpcClient() {
    alive_->store(false, std::memory_order_relaxed);
}

void GrpcClient::searchAsync(
    const std::string &prefix,
    std::function<void(propico::SearchResponse)> callback) {
    std::thread([this, prefix, alive = alive_, cb = std::move(callback)]() {
        propico::SearchRequest req;
        req.set_prefix(prefix);
        req.set_max_candidates(9);

        propico::SearchResponse resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::seconds(3));

        grpc::Status status = stub_->Search(&ctx, req, &resp);

        if (status.ok() && alive->load(std::memory_order_relaxed)) {
            dispatcher_->schedule(
                [cb = std::move(cb), resp = std::move(resp), alive]() {
                    if (alive->load(std::memory_order_relaxed)) {
                        cb(std::move(resp));
                    }
                });
        }
        // サーバー未起動・タイムアウト時はコールバックを呼ばず静かに失敗
    }).detach();
}

void GrpcClient::learnAsync(const std::string &candidate_id,
                             const std::string &prefix) {
    std::thread([this, candidate_id, prefix]() {
        propico::LearnRequest req;
        req.set_candidate_id(candidate_id);
        req.set_prefix(prefix);

        propico::LearnResponse resp;
        grpc::ClientContext ctx;
        ctx.set_deadline(std::chrono::system_clock::now() +
                         std::chrono::seconds(3));

        stub_->Learn(&ctx, req, &resp);
        // 戻り値は無視
    }).detach();
}
