//
// Created by DaVonte Carter Vault on 5/15/26.
//

#pragma once

#include "Log.h"

namespace fe {

    class LogInternal {
    public:
        static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return Log::s_CoreLogger; }
    };

} // namespace fe

#define FE_CORE_TRACE(...)    SPDLOG_LOGGER_TRACE(::fe::LogInternal::GetCoreLogger(), __VA_ARGS__)
#define FE_CORE_INFO(...)     SPDLOG_LOGGER_INFO(::fe::LogInternal::GetCoreLogger(), __VA_ARGS__)
#define FE_CORE_WARN(...)     SPDLOG_LOGGER_WARN(::fe::LogInternal::GetCoreLogger(), __VA_ARGS__)
#define FE_CORE_ERROR(...)    SPDLOG_LOGGER_ERROR(::fe::LogInternal::GetCoreLogger(), __VA_ARGS__)

#define FE_CORE_CRITICAL(...)                                                  \
do {                                                                       \
SPDLOG_LOGGER_CRITICAL(::fe::LogInternal::GetCoreLogger(), __VA_ARGS__); \
::fe::detail::FatalTrap(::fe::LogInternal::GetCoreLogger());           \
} while (0)