//
// Created by DaVonte Carter Vault on 5/15/26.
//

#include "../include/Log.h"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cstdlib>
#include <vector>

namespace fe {

std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

void Log::Init() {
    // One pool, shared by both loggers.
    // 8192 slots is plenty for typical engine traffic; one worker thread keeps
    spdlog::init_thread_pool(8192, 1);

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink    = std::make_shared<spdlog::sinks::basic_file_sink_mt>("FragmentEngine.log", true);

    consoleSink->set_pattern("%^[%T.%e] [%n] [%l] [thread %t]: %v%$");
    fileSink->set_pattern("[%Y-%m-%d %T.%e] [%n] [%l] [thread %t] [%s:%#]: %v");

    std::vector<spdlog::sink_ptr> sinks { consoleSink, fileSink };

    s_CoreLogger = std::make_shared<spdlog::async_logger>(
        "FRAGMENT",
        sinks.begin(), sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest);
    s_CoreLogger->set_level(spdlog::level::trace);
    s_CoreLogger->flush_on(spdlog::level::err);
    spdlog::register_logger(s_CoreLogger);

    s_ClientLogger = std::make_shared<spdlog::async_logger>(
        "APP",
        sinks.begin(), sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest);
    s_ClientLogger->set_level(spdlog::level::trace);
    s_ClientLogger->flush_on(spdlog::level::err);
    spdlog::register_logger(s_ClientLogger);
}

void Log::Shutdown() {
    // Drain the queue and join the worker thread before static teardown.
    spdlog::shutdown();
}

namespace detail {

[[noreturn]] void FatalTrap(const std::shared_ptr<spdlog::logger>& logger) {
    // Synchronously drain so the critical line is on disk / on screen
    // before we trap or abort.
    if (logger) logger->flush();
    spdlog::shutdown();   // drain ALL async loggers, just in case

#ifdef NDEBUG
    std::abort();
#else
    FE_DEBUGBREAK();
    // If somehow execution continues after the breakpoint (debugger detached, etc.)
    // make sure we still die.
    std::abort();
#endif
}

} // namespace detail
} // namespace fe