#include "platform/platform_logging.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#ifdef __ANDROID__
#include <android/log.h>

#include <memory>
#include <mutex>
#include <string>
#endif

namespace earth_map::platform {

#ifdef __ANDROID__
namespace {

constexpr char kAndroidLogTag[] = "EarthMap";

int ToAndroidPriority(spdlog::level::level_enum level) {
    switch (level) {
    case spdlog::level::trace:
    case spdlog::level::debug:
        return ANDROID_LOG_DEBUG;
    case spdlog::level::info:
        return ANDROID_LOG_INFO;
    case spdlog::level::warn:
        return ANDROID_LOG_WARN;
    case spdlog::level::err:
        return ANDROID_LOG_ERROR;
    case spdlog::level::critical:
        return ANDROID_LOG_FATAL;
    case spdlog::level::off:
        break;
    case spdlog::level::n_levels:
        break;
    }
    return ANDROID_LOG_DEFAULT;
}

class AndroidLogcatSink final : public spdlog::sinks::base_sink<std::mutex> {
private:
    void sink_it_(const spdlog::details::log_msg& message) override {
        const std::string text(message.payload.data(), message.payload.size());
        __android_log_write(ToAndroidPriority(message.level), kAndroidLogTag, text.c_str());
    }

    void flush_() override {}
};

}  // namespace
#endif

void ConfigurePlatformLogging() {
#ifdef __ANDROID__
    static std::once_flag configured;
    std::call_once(configured, [] {
        auto sink = std::make_shared<AndroidLogcatSink>();
        auto logger = std::make_shared<spdlog::logger>("earth_map", std::move(sink));
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::warn);

        spdlog::set_default_logger(std::move(logger));
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::warn);
        spdlog::info("Android Logcat logging initialized");
    });
#endif
}

}  // namespace earth_map::platform
