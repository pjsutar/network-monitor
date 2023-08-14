// This performance.cpp file is used to produce a separate test suite for
// performance tests only.
#define BOOST_TEST_MODULE network-monitor-perf

#include "websocket-client-mock.h"
#include "websocket-server-mock.h"

#include <network-monitor/file-downloader.h>
#include <network-monitor/network-monitor.h>

#include <boost/asio.hpp>
#include <boost/test/unit_test.hpp>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using NetworkMonitor::GetMockSendFrame;
using NetworkMonitor::GetMockStompFrame;
using NetworkMonitor::MockWebSocketClientForStomp;
using NetworkMonitor::MockWebSocketEvent;
using NetworkMonitor::MockWebSocketServerForStomp;
using NetworkMonitor::NetworkMonitorConfig;
using NetworkMonitor::NetworkMonitorError;
using NetworkMonitor::ParseJsonFile;
using NetworkMonitor::Timer;

// This fixture is used to re-initialize all mock properties before a test.
struct NetworkMonitorTestFixture {
    NetworkMonitorTestFixture()
    {
        MockWebSocketClientForStomp::endpoint = "/passengers";
        MockWebSocketClientForStomp::username = "some_username";
        MockWebSocketClientForStomp::password = "some_password_123";
        MockWebSocketClientForStomp::connectEc = {};
        MockWebSocketClientForStomp::sendEc = {};
        MockWebSocketClientForStomp::closeEc = {};
        MockWebSocketClientForStomp::triggerDisconnection = false;
        MockWebSocketClientForStomp::subscriptionMessages = {};

        MockWebSocketServerForStomp::triggerDisconnection = false;
        MockWebSocketServerForStomp::runEc = {};
        MockWebSocketServerForStomp::mockEvents = {};

        Timer::ClearAll();
    }
};

// Helper function to calculate the (n, k) binomial coefficient.
// Taken from: https://stackoverflow.com/a/44719165
constexpr size_t GetBinomialCoeff(size_t n, size_t k) noexcept
{
    return
        (k > n) ? 0 :                 // out of range
        (k == 0 || k == n) ? 1 :                 // edge
        (k == 1 || k == n - 1) ? n :                 // first
        (k + k < n) ?                     // recursive:
        (GetBinomialCoeff(n - 1, k - 1) * n) / k :   // path to k=1   is faster
        (GetBinomialCoeff(n - 1, k) * n) / (n - k);  // path to k=n-1 is faster
}

// Helper function to generate a chain of quiet-route requests between station
// pairs.
std::queue<MockWebSocketEvent> GetQuietRouteRequestMockEvents(
    const std::vector<std::pair<std::string, std::string>>& stationPairs
)
{
    std::queue<MockWebSocketEvent> events{};
    events.push(MockWebSocketEvent{
        "connection0",
        MockWebSocketEvent::Type::kConnect,
        // Succeeds
        });
    events.push(MockWebSocketEvent{
        "connection0",
        MockWebSocketEvent::Type::kMessage,
        {}, // Succeeds
        GetMockStompFrame("localhost")
        });
    for (const auto& [stationA, stationB] : stationPairs) {
        events.push(MockWebSocketEvent{
            "connection0",
            MockWebSocketEvent::Type::kMessage,
            {}, // Succeeds
            GetMockSendFrame("req0", "/quiet-route", nlohmann::json {
                {"start_station_id", stationA},
                {"end_station_id", stationB},
            }.dump())
            });
    }
    return events;
}

// Helper function to generate all possible combinations of n items in k groups.
// Adapted from http://rosettacode.org/wiki/Combinations#C.2B.2B
std::vector<std::vector<size_t>> GetCombinations(size_t n, size_t k)
{
    std::string bitmask(k, 1); // k leading 1's
    bitmask.resize(n, 0); // n-k trailing 0's

    // Print integers and permute bitmask
    size_t nCombs{ GetBinomialCoeff(n, k) };
    std::vector<std::vector<size_t>> combinations(
        nCombs,
        std::vector<size_t>(k, 0)
    );
    size_t nComb{ 0 };
    do {
        size_t kdx{ 0 };
        for (int i = 0; i < n; ++i) { // [0..n-1] integers
            if (bitmask[i]) {
                combinations[nComb][kdx] = i;
                ++kdx;
                if (kdx == k) {
                    break;
                }
            }
        }
        ++nComb;
    } while (std::prev_permutation(bitmask.begin(), bitmask.end()));
    return combinations;
}

BOOST_AUTO_TEST_SUITE(network_monitor);

BOOST_FIXTURE_TEST_SUITE(performance, NetworkMonitorTestFixture);

BOOST_AUTO_TEST_CASE(passenger_events)
{
    NetworkMonitorConfig config{
        "ltnm.learncppthroughprojects.com",
        "443",
        "some_username",
        "some_password_123",
        TESTS_CACERT_PEM,
        TESTS_NETWORK_LAYOUT_JSON,
        "localhost",
        "127.0.0.1",
        8042,
        0.1,
        0.1,
        50,
    };

    // Setup the mock.
    auto events = ParseJsonFile(
        std::filesystem::path(TEST_DATA) / "passenger_events.json"
    ).get<std::vector<nlohmann::json>>();
    std::vector<std::string> messages{};
    messages.reserve(events.size());
    for (const auto& event : events) {
        messages.emplace_back(event.dump());
    }
    MockWebSocketClientForStomp::subscriptionMessages = std::move(messages);

    // We need to set a timeout otherwise the network monitor will run forever.
    NetworkMonitor::NetworkMonitor<
        MockWebSocketClientForStomp,
        MockWebSocketServerForStomp
    > monitor{};
    auto ec{ monitor.Configure(config) };
    BOOST_REQUIRE_EQUAL(ec, NetworkMonitorError::kOk);
    monitor.Run(std::chrono::milliseconds(9000));

    // When we arrive here, the Run() function ran out of things to do.
    BOOST_REQUIRE_EQUAL(monitor.GetLastErrorCode(), NetworkMonitorError::kOk);
    Timer::PrintReport();

    // TODO: Turn this into a performance non-regression test.
}

BOOST_AUTO_TEST_CASE(quiet_route_slow)
{
    NetworkMonitorConfig config{
        "ltnm.learncppthroughprojects.com",
        "443",
        "some_username",
        "some_password_123",
        TESTS_CACERT_PEM,
        TESTS_NETWORK_LAYOUT_JSON,
        "localhost",
        "127.0.0.1",
        8042,
        0.1,
        0.1,
        50,
    };

    // Setup the mock.
    MockWebSocketServerForStomp::mockEvents = GetQuietRouteRequestMockEvents({
        {"station_211", "station_119"},
        });

    // We need to set a timeout otherwise the network monitor will run forever.
    NetworkMonitor::NetworkMonitor<
        MockWebSocketClientForStomp,
        MockWebSocketServerForStomp
    > monitor{};
    auto ec{ monitor.Configure(config) };
    BOOST_REQUIRE_EQUAL(ec, NetworkMonitorError::kOk);
    monitor.Run(std::chrono::milliseconds(15000));

    // When we arrive here, the Run() function ran out of things to do.
    BOOST_REQUIRE_EQUAL(monitor.GetLastErrorCode(), NetworkMonitorError::kOk);
    Timer::PrintReport();

    // TODO: Turn this into a performance non-regression test.
}

BOOST_AUTO_TEST_CASE(quiet_route_avg)
{
    NetworkMonitorConfig config{
        "ltnm.learncppthroughprojects.com",
        "443",
        "some_username",
        "some_password_123",
        TESTS_CACERT_PEM,
        TESTS_NETWORK_LAYOUT_JSON,
        "localhost",
        "127.0.0.1",
        8042,
        0.1,
        0.1,
        50,
    };

    // Setup the mock.
    // We first calculate all possible station pairs combinations, but then
    // only test a randomized subset. The cutoff will be driven by the
    // NetworkMonitor::Run() timeout.
    auto combinations{ GetCombinations(426, 2) }; // We have 426 stations.
    auto rng{ std::default_random_engine {} };
    std::shuffle(combinations.begin(), combinations.end(), rng);
    std::vector<std::pair<std::string, std::string>> stationPairs{};
    auto getStationName = [](int value, size_t size) {
        std::ostringstream oss;
        oss << "station_" << std::setw(size) << std::setfill('0') << value;
        return oss.str();
    };
    for (const auto& combination : combinations) {
        auto stationA{ getStationName(combination[0], 3) };
        auto stationB{ getStationName(combination[1], 3) };
        stationPairs.push_back({ stationA, stationB });
    }
    spdlog::info("Requesting {} quiet-route requests", stationPairs.size());
    MockWebSocketServerForStomp::mockEvents = GetQuietRouteRequestMockEvents(
        stationPairs
    );

    // We need to set a timeout otherwise the network monitor will run forever.
    NetworkMonitor::NetworkMonitor<
        MockWebSocketClientForStomp,
        MockWebSocketServerForStomp
    > monitor{};
    auto ec{ monitor.Configure(config) };
    BOOST_REQUIRE_EQUAL(ec, NetworkMonitorError::kOk);
    monitor.Run(std::chrono::minutes(1));

    // When we arrive here, the Run() function ran out of things to do.
    BOOST_REQUIRE_EQUAL(monitor.GetLastErrorCode(), NetworkMonitorError::kOk);
    Timer::PrintReport();

    // TODO: Turn this into a performance non-regression test.
}

BOOST_AUTO_TEST_SUITE_END(); // performance

BOOST_AUTO_TEST_SUITE_END(); // network_monitor