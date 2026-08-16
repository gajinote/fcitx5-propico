#pragma once

#include "propico.grpc.pb.h"
#include <fcitx-utils/eventdispatcher.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

class GrpcClient {
public:
    GrpcClient(const std::string &target, fcitx::EventDispatcher *dispatcher);
    ~GrpcClient();

    void searchAsync(const std::string &prefix,
                     std::function<void(propico::SearchResponse)> callback);
    void learnAsync(const std::string &candidate_id, const std::string &prefix);
    void syncAsync(int64_t last_sync_timestamp,
                   std::function<void(propico::SyncResponse)> callback);

private:
    std::shared_ptr<propico::Propico::Stub> stub_;
    fcitx::EventDispatcher *dispatcher_;
    std::shared_ptr<std::atomic<bool>> alive_;
};
