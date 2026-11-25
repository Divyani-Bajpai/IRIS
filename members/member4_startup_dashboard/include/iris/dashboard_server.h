#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <winsock2.h>

namespace iris {

enum class DashboardSessionState { Idle, Starting, Collecting, Complete };

// Small loopback-only HTTP server for the local IRIS report.
class DashboardServer {
public:
    explicit DashboardServer(std::string databasePath,
                             unsigned short port = 8765);
    ~DashboardServer();
    DashboardServer(const DashboardServer&) = delete;
    DashboardServer& operator=(const DashboardServer&) = delete;

    bool start(std::string& error);
    void stop();
    void setSessionState(DashboardSessionState state);
    bool consumeCollectionRequest();
    unsigned short port() const { return port_; }

private:
    void serve();
    void handleClient(SOCKET client);
    std::string databasePath_;
    unsigned short port_;
    SOCKET listenSocket_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::atomic<DashboardSessionState> sessionState_{
        DashboardSessionState::Idle};
    std::atomic<bool> collectionRequested_{false};
    std::atomic<long long> sessionStartedEpoch_{0};
    std::thread worker_;
    bool winsockStarted_ = false;
};

}  // namespace iris
