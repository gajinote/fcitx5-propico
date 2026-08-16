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
  std::thread([stub = stub_, dispatcher = dispatcher_, prefix,
               alive = alive_, cb = std::move(callback)]() {
    propico::SearchRequest req;
    req.set_prefix(prefix);
    req.set_max_candidates(9);

    propico::SearchResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(3));

    grpc::Status status = stub->Search(&ctx, req, &resp);

    if (status.ok() && alive->load(std::memory_order_relaxed)) {
      dispatcher->schedule(
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
  std::thread([stub = stub_, candidate_id, prefix]() {
    propico::LearnRequest req;
    req.set_candidate_id(candidate_id);
    req.set_prefix(prefix);

    propico::LearnResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(3));

    stub->Learn(&ctx, req, &resp);
    // 戻り値は無視
  }).detach();
}

void GrpcClient::syncAsync(
    int64_t last_sync_timestamp,
    std::function<void(propico::SyncResponse)> callback) {
  std::thread([stub = stub_, dispatcher = dispatcher_, last_sync_timestamp,
               alive = alive_, cb = std::move(callback)]() {
    propico::SyncRequest req;
    req.set_last_sync_timestamp(last_sync_timestamp);

    propico::SyncResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() +
                     std::chrono::seconds(5));

    grpc::Status status = stub->Sync(&ctx, req, &resp);

    if (status.ok() && alive->load(std::memory_order_relaxed)) {
      dispatcher->schedule(
          [cb = std::move(cb), resp = std::move(resp), alive]() {
            if (alive->load(std::memory_order_relaxed)) {
              cb(std::move(resp));
            }
          });
    }
  }).detach();
}
