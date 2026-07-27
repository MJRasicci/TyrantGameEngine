#pragma once

#include <chrono>
#include <expected>
#include <string>

namespace TGE::Internal
{
    enum class DesktopEventErrorCode
    {
        InvalidState,
        PumpStartFailed
    };

    struct DesktopEventError
    {
        DesktopEventErrorCode code {
            DesktopEventErrorCode::PumpStartFailed
        };
        std::string message;

        bool operator==(const DesktopEventError&) const = default;
    };

    using DesktopEventResult =
        std::expected<void, DesktopEventError>;

    /**
     * @brief Backend-owned mechanism behind one desktop event queue.
     *
     * Start, PumpEvents, and Stop are invoked serially on the thread driving
     * DesktopEventRuntime::Run. Wake is the sole operation callable from any
     * thread and must promptly interrupt a blocking PumpEvents call.
     * Start is invoked once. After a successful Start, PumpEvents is invoked
     * zero or more times and Stop is invoked exactly once. A failed Start must
     * unwind any partial backend initialization and is not followed by Stop.
     *
     * Concrete implementations translate and route their own native events.
     * This interface intentionally carries no backend event, window handle, or
     * input-device representation.
     */
    class IDesktopEventPump
    {
    public:
        virtual ~IDesktopEventPump() = default;

        /**
         * @brief Initialize the backend queue on the event runner.
         */
        [[nodiscard]] virtual DesktopEventResult Start() = 0;

        /**
         * @brief Interrupt PumpEvents from any thread.
         */
        virtual void Wake() noexcept = 0;

        /**
         * @brief Dispatch at most one backend event within maxWait.
         *
         * Returning after one native event lets DesktopEventRuntime drain
         * semantic work posted by that event before dispatching the next
         * native event. This preserves shared-queue ordering across adapters
         * such as windowing and input.
         */
        virtual void PumpEvents(
            std::chrono::milliseconds maxWait) noexcept = 0;

        /**
         * @brief Release successfully started backend state on the runner.
         */
        virtual void Stop() noexcept = 0;
    };
}
