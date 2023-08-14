#ifndef NETWORK_MONITOR_TIMER_H
#define NETWORK_MONITOR_TIMER_H

#include <chrono>
#include <memory>
#include <string>

namespace NetworkMonitor {

    /*! \brief Results for a named timer.
     */
    struct TimerResults {
        /*! \brief Clock used to record the measurements.
         */
        using Clock = std::chrono::high_resolution_clock;

        /*! \brief Best execution time measured.
         */
        Clock::duration best{};

        /*! \brief Worst execution time measured.
         */
        Clock::duration worst{};

        /*! \brief Average execution time measured.
         */
        Clock::duration avg{};

        /*! \brief Number of measurements.
         */
        size_t nSamples{ 0 };

        /*! \brief Represent a timer measurement as a specific time unit.
         *
         * \param duration  Time unit
         */
        template <typename Rep>
        static Rep As(
            const Clock::duration& duration
        )
        {
            return std::chrono::duration_cast<Rep>(duration);
        }
    };

    /*! \brief Timer class
     *
     * An indidual instance of this class represents a single named timer.
     *
     * We also provide static members to get results for any named timer.
     */
    class Timer {
    public:
        /*! \brief The default constructor is deleted.
         */
        Timer() = delete;

        /*! \brief Construct and start a named timer.
         *
         * \param name  Can only contain characters that would be valid as variable
         *              names: [a-zA-Z0-9_].
         */
        Timer(
            const std::string& name
        ) noexcept;

        /*! \brief The copy constructor is deleted.
         */
        Timer(
            const Timer&
        ) = delete;

        /*! \brief The move constructor is deleted.
         */
        Timer(
            Timer&&
        ) = delete;

        /*! \brief The copy assignment operator is deleted.
         */
        Timer& operator=(
            const Timer&
            ) = delete;

        /*! \brief The move assignment operator is deleted.
         */
        Timer& operator=(
            Timer&&
            ) = delete;

        /*! \brief Destructor
         *
         * The timer is stopped on destruction.
         */
        ~Timer();

        /*! \brief Stop the timer.
         *
         * If the timer had already been stopped, this called results in a no-op.
         * The previously stopped measurement is unaffected.
         */
        void Stop() noexcept;

        /*! \brief Get the measurements for the current named timer instance.
         */
        TimerResults GetResults() const;

        /*! \brief Get the measurements for a given named timer.
         */
        static TimerResults GetResults(
            const std::string& name
        );

        /*! \brief Clear all timers measurements.
         */
        static void ClearAll();

        /*! \brief Print all timer measurements in a human-readable form.
         *
         * This method uses the logger to print all timer measurements.
         */
        static void PrintReport();

    private:
        std::string name_;
        size_t id_;
    };

#if defined(NETWORK_MONITOR_TIMER) && NETWORK_MONITOR_TIMER == 1

    // Stringification helper macros
#define __TIMER_NAME(name) #name
#define _TIMER_NAME(name) __TIMER_NAME(name)
#define __TIMER_VAR_NAME(name) _timer##name
#define _TIMER_VAR_NAME(name) __TIMER_VAR_NAME(name)

/*! \brief Construct and start a named timer.
 *
 * This macro results in a no-op if \a NETWORK_MONITOR_TIMER is not defined.
 *
 * \param name  Can only contain characters that would be valid as variable
 *              names: [a-zA-Z0-9_].
 */
#define TIMER_START(name) Timer _TIMER_VAR_NAME(name) {_TIMER_NAME(name)};

 /*! \brief Stop the timer.
  *
  * This macro results in a no-op if \a NETWORK_MONITOR_TIMER is not defined.
  *
  * If the timer had already been stopped, this called results in a no-op.
  * The previously stopped measurement is unaffected.
  */
#define TIMER_STOP(name) _TIMER_VAR_NAME(name).Stop();

#else

    // no-op
#define TIMER_START(name)
#define TIMER_STOP(name)

#endif

} // namespace NetworkMonitor

#endif // NETWORK_MONITOR_TIMER_H