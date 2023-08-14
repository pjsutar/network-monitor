#include <network-monitor/timer.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using NetworkMonitor::Timer;
using NetworkMonitor::TimerResults;
using Clock = NetworkMonitor::TimerResults::Clock;

struct TimerMeasurement {
    std::chrono::time_point<Clock> start{};
    std::chrono::time_point<Clock> stop{};

    bool IsComplete() const
    {
        return start.time_since_epoch().count() != 0 &&
            stop.time_since_epoch().count() != 0;
    }

    Clock::duration GetDuration() const
    {
        if (start.time_since_epoch().count() == 0) {
            throw std::runtime_error("Measurement hasn't started yet");
        }
        if (stop.time_since_epoch().count() == 0) {
            throw std::runtime_error("Measurement hasn't stopped yet");
        }
        return std::chrono::duration_cast<Clock::duration>(stop - start);
    }
};

struct TimerMeasurements {
    std::unordered_map<
        std::string,
        std::vector<TimerMeasurement>
    > measurements{};
    std::vector<std::string> order{};

    static inline TimerMeasurements& Get()
    {
        static TimerMeasurements instance{};
        return instance;
    }
};

Timer::Timer(
    const std::string& name
) noexcept
{
    auto& timers{ TimerMeasurements::Get() };
    auto& measurements{ timers.measurements[name] };
    auto& measurement{ measurements.emplace_back(TimerMeasurement {}) };

    // This will be used to identify the specific instance of this named timer.
    name_ = name;
    id_ = measurements.size() - 1;
    if (id_ == 0) {
        timers.order.push_back(name);
    }

    // Start the timer as the very last thing.
    measurement.start = std::chrono::high_resolution_clock::now();
}

Timer::~Timer()
{
    Stop();
}

void Timer::Stop() noexcept
{
    // Stop the timer as the very first thing.
    auto stop{ std::chrono::high_resolution_clock::now() };

    auto& timers{ TimerMeasurements::Get() };
    auto& measurements{ timers.measurements.at(name_) };
    auto& measurement{ measurements[id_] };
    if (!measurement.IsComplete()) {
        measurement.stop = stop;
    }
}

void Timer::ClearAll(
)
{
    auto& timers{ TimerMeasurements::Get() };
    timers.measurements.clear();
    timers.order.clear();
}

TimerResults Timer::GetResults() const
{
    return Timer::GetResults(name_);
}

TimerResults Timer::GetResults(
    const std::string& name
)
{
    auto& timers{ TimerMeasurements::Get() };
    auto measurementsIt{ timers.measurements.find(name) };
    if (measurementsIt == timers.measurements.end()) {
        throw std::runtime_error("Could not find measurement: " + name);
    }
    auto& measurements{ measurementsIt->second };

    if (measurements.size() == 0) {
        return TimerResults{};
    }

    Clock::duration best{ Clock::duration::max() };
    Clock::duration worst{ 0 };
    Clock::duration avg{ 0 };
    size_t nSamples{ measurements.size() };
    for (const auto& measurement : measurements) {
        auto duration{ measurement.GetDuration() };
        avg += duration;
        if (duration > worst) {
            worst = duration;
        }
        if (duration < best) {
            best = duration;
        }
    }
    avg /= nSamples;
    return TimerResults{ best, worst, avg, nSamples };
}

void Timer::PrintReport()
{
    using us = std::chrono::microseconds;

    static auto repeat = [](const char& c, size_t n) -> std::string {
        std::string s{};
        s.reserve(n);
        for (size_t idx{ 0 }; idx < n; ++idx) {
            s.push_back(c);
        }
        return s;
    };
    static const size_t wNameMin{ 30 };
    static const size_t wN{ 10 };
    static const size_t wMeas{ 10 };
    static const auto bN{ repeat('-', wN + 2) };
    static const auto bMeas{ repeat('-', wMeas + 2) };

    auto& timers{ TimerMeasurements::Get() };
    auto nTimers{ timers.measurements.size() };
    spdlog::info("Found {} timers", nTimers);
    size_t wName{ wNameMin };
    for (const auto& name : timers.order) {
        if (name.size() > wName) {
            wName = name.size();
        }
    }
    const auto bName{ repeat('-', wName + 1) };
    if (nTimers > 0) {
        spdlog::info("{:{}s} | {:{}s} | {:{}s} | {:{}s} | {:{}s}",
            "Name", wName,
            "# meas", wN,
            "Best (us)", wMeas,
            "Worst (us)", wMeas,
            "Avg (us)", wMeas
        );
        spdlog::info("{}|{}|{}|{}|{}", bName, bN, bMeas, bMeas, bMeas);
    }
    for (const auto& name : timers.order) {
        auto res{ GetResults(name) };
        spdlog::info("{:{}s} | {:{}d} | {:{}d} | {:{}d} | {:{}d}",
            name, wName,
            res.nSamples, wN,
            TimerResults::As<us>(res.best).count(), wMeas,
            TimerResults::As<us>(res.worst).count(), wMeas,
            TimerResults::As<us>(res.avg).count(), wMeas);
    }
}