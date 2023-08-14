#include <network-monitor/timer.h>

#include <boost/test/unit_test.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#include <chrono>
#include <thread>

using NetworkMonitor::Timer;
using NetworkMonitor::TimerResults;
using Clock = NetworkMonitor::TimerResults::Clock;

using namespace std::chrono_literals;

using ms = std::chrono::milliseconds;
using us = std::chrono::microseconds;
using ns = std::chrono::nanoseconds;

BOOST_AUTO_TEST_SUITE(network_monitor);

BOOST_AUTO_TEST_SUITE(timer);

BOOST_AUTO_TEST_CASE(no_results)
{
    try {
        auto res{ Timer::GetResults("no_results") };
        BOOST_CHECK(false);
    }
    catch (...) {
    }
}

BOOST_AUTO_TEST_CASE(results_before_stop)
{
    Timer t1{ "results_before_stop" };
    try {
        auto res{ Timer::GetResults("results_before_stop") };
        BOOST_CHECK(false);
    }
    catch (...) {
    }
}

BOOST_AUTO_TEST_CASE(results_after_stop)
{
    Timer t1{ "results_after_stop" };
    t1.Stop();
    auto res{ Timer::GetResults("results_after_stop") };
}

BOOST_AUTO_TEST_CASE(stop_on_dtor)
{
    {
        Timer t1{ "stop_on_dtor" };
    }
    auto res{ Timer::GetResults("stop_on_dtor") };
}

BOOST_AUTO_TEST_CASE(double_stop)
{
    Timer t1{ "double_stop" };
    t1.Stop();
    auto res1{ t1.GetResults() };
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t1.Stop();
    auto res2{ t1.GetResults() };
    BOOST_CHECK_EQUAL(res1.nSamples, res2.nSamples);
    BOOST_CHECK_EQUAL(TimerResults::As<ns>(res1.best).count(),
        TimerResults::As<ns>(res2.best).count());
}

BOOST_AUTO_TEST_CASE(empty)
{
    auto start{ std::chrono::high_resolution_clock::now() };
    Timer t1{ "empty" };
    t1.Stop();
    auto stop{ std::chrono::high_resolution_clock::now() };
    auto res{ Timer::GetResults("empty") };
    BOOST_CHECK_EQUAL(res.nSamples, 1);

    // An ideal empty timer should measure 0s, but there is some overhead in
    // reality. We also report the overall overhead introduced by a timer.
    spdlog::info("Empty timer: {}ns",
        TimerResults::As<ns>(res.worst).count());
    auto overhead{ std::chrono::duration_cast<Clock::duration>(stop - start) };
    spdlog::info("Timer overhead: {}ns",
        TimerResults::As<ns>(overhead).count());
    BOOST_CHECK(TimerResults::As<ns>(res.best).count() > 0);
    BOOST_CHECK(TimerResults::As<ns>(res.worst).count() > 0);
    BOOST_CHECK(TimerResults::As<ns>(res.avg).count() > 0);
}

BOOST_AUTO_TEST_CASE(measurement)
{
    Timer t1{ "measurement" };
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    t1.Stop();
    auto res{ Timer::GetResults("measurement") };
    BOOST_CHECK_EQUAL(res.nSamples, 1);
    BOOST_CHECK(res.avg == res.best);
    BOOST_CHECK(res.avg == res.worst);
}

BOOST_AUTO_TEST_CASE(can_nest_diff_name)
{
    Timer t1{ "can_nest_diff_name_1" };
    {
        Timer t2{ "can_nest_diff_name_2" };
        t2.Stop();
    }
    t1.Stop();
}

BOOST_AUTO_TEST_CASE(can_nest_same_name)
{
    {
        Timer t1{ "can_nest_same_name" };
        {
            Timer t2{ "can_nest_same_name" };
            t2.Stop();
        }
        t1.Stop();
    }
    {
        Timer t1{ "can_nest_same_name" };
        {
            Timer t2{ "can_nest_same_name" };
            t1.Stop(); // !!!
            t2.Stop();
        }
    }
}

BOOST_AUTO_TEST_CASE(repeated_measurements)
{
    size_t nTimes{ 10 };
    for (size_t idx{ 0 }; idx < nTimes; ++idx) {
        Timer t1{ "repeated_measurements" };
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    auto res{ Timer::GetResults("repeated_measurements") };
    BOOST_CHECK_EQUAL(res.nSamples, nTimes);

    spdlog::info("Repeated measurements: {}ms (best), {}ms (worst), {}ms (avg)",
        TimerResults::As<ms>(res.best).count(),
        TimerResults::As<ms>(res.worst).count(),
        TimerResults::As<ms>(res.avg).count());
    BOOST_CHECK(res.best <= res.worst);
    BOOST_CHECK(res.best <= res.avg);
    BOOST_CHECK(res.avg <= res.worst);

    Timer::PrintReport();
}

BOOST_AUTO_TEST_CASE(measurement_macro)
{
    // This is a no-op if the Timer code is not compiled in.
    TIMER_START(measurement_macro);
    TIMER_STOP(measurement_macro);

#if defined(NETWORK_MONITOR_TIMER) && NETWORK_MONITOR_TIMER == 1
    spdlog::info("NETWORK_MONITOR_EXE is defined");
    auto res{ Timer::GetResults("measurement_macro") };
    BOOST_CHECK_EQUAL(res.nSamples, 1);
    BOOST_CHECK(res.avg == res.best);
    BOOST_CHECK(res.avg == res.worst);
#else
    spdlog::info("NETWORK_MONITOR_EXE is not defined");
    try {
        auto res{ Timer::GetResults("measurement_macro") };
        BOOST_CHECK(false);
    }
    catch (...) {
    }
#endif
}

BOOST_AUTO_TEST_SUITE_END(); // timer

BOOST_AUTO_TEST_SUITE_END(); // network_monitor