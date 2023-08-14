#include <network-monitor/env.h>
#include <network-monitor/network-monitor.h>
#include <network-monitor/websocket-client.h>
#include <network-monitor/websocket-server.h>

#include <chrono>
#include <string>

using NetworkMonitor::BoostWebSocketClient;
using NetworkMonitor::BoostWebSocketServer;
using NetworkMonitor::GetEnvVar;
using NetworkMonitor::NetworkMonitorError;
using NetworkMonitor::NetworkMonitorConfig;

int main()
{
    // Monitor configuration
    NetworkMonitorConfig config{
        GetEnvVar("LTNM_SERVER_URL", "ltnm.learncppthroughprojects.com"),
        GetEnvVar("LTNM_SERVER_PORT", "443"),
        GetEnvVar("LTNM_USERNAME"),
        GetEnvVar("LTNM_PASSWORD"),
        GetEnvVar("LTNM_CACERT_PATH", "cacert.pem"),
        GetEnvVar("LTNM_NETWORK_LAYOUT_FILE_PATH", ""),
        "127.0.0.1", // We use the IP as the server hostname because the client
        // will connect to 127.0.0.1 directly, without host name
        // resolution.
"127.0.0.1",
8042,
0.1,
0.1,
20,
    };

    // Optional run timeout
    // Default: Oms = run indefinitely
    auto timeoutMs{ std::stoi(GetEnvVar("LTNM_TIMEOUT_MS", "0")) };

    // Launch the monitor.
    NetworkMonitor::NetworkMonitor<
        BoostWebSocketClient,
        BoostWebSocketServer
    > monitor{};
    auto error{ monitor.Configure(config) };
    if (error != NetworkMonitorError::kOk) {
        return -1;
    }
    if (timeoutMs == 0) {
        monitor.Run();
    }
    else {
        monitor.Run(std::chrono::milliseconds(timeoutMs));
    }

    // The disconnection of the StompServer client is an acceptable error code.
    // All other truthy error codes are considered failures.
    auto ec{ monitor.GetLastErrorCode() };
    if (ec != NetworkMonitorError::kOk &&
        ec != NetworkMonitorError::kStompServerClientDisconnected
        ) {
        spdlog::error("Last error code: {}", ec);
        return -2;
    }
    return 0;
}