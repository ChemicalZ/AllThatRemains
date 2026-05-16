//
// Created by DaVonte Carter Vault on 5/15/26.
//

#pragma once

#include <spdlog/spdlog.h>
#include <memory>

namespace fe {

    class Log {
    public:
        static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

    private:
        friend class Engine;        // only Engine can init
        friend class LogInternal;   // engine-side core logger access

        static void Init();
        static void Shutdown();

        static std::shared_ptr<spdlog::logger> s_CoreLogger;
        static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };

    namespace detail {
        [[noreturn]] void FatalTrap(const std::shared_ptr<spdlog::logger>& logger);
    }

} // namespace fe

// ---- debug trap ----------------------------------------------------------
#if defined(_MSC_VER)
#define FE_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#include <csignal>
#define FE_DEBUGBREAK() std::raise(SIGTRAP)
#else
#include <cstdlib>
#define FE_DEBUGBREAK() std::abort()
#endif

// ---- client macros -------------------------------------------------------
#define FE_TRACE(...)    SPDLOG_LOGGER_TRACE(::fe::Log::GetClientLogger(), __VA_ARGS__)
#define FE_INFO(...)     SPDLOG_LOGGER_INFO(::fe::Log::GetClientLogger(), __VA_ARGS__)
#define FE_WARN(...)     SPDLOG_LOGGER_WARN(::fe::Log::GetClientLogger(), __VA_ARGS__)
#define FE_ERROR(...)    SPDLOG_LOGGER_ERROR(::fe::Log::GetClientLogger(), __VA_ARGS__)

#define FE_CRITICAL(...)                                                       \
do {                                                                       \
SPDLOG_LOGGER_CRITICAL(::fe::Log::GetClientLogger(), __VA_ARGS__);     \
::fe::detail::FatalTrap(::fe::Log::GetClientLogger());                 \
} while (0)