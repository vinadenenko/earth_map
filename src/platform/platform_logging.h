#pragma once

namespace earth_map::platform {

/**
 * Installs Earth Map's platform log sink exactly once.
 *
 * On Android, Earth Map logs must be forwarded to Logcat explicitly; the
 * default spdlog stdout/stderr sink is not a dependable Logcat transport for
 * a Qt application process.
 */
void ConfigurePlatformLogging();

}  // namespace earth_map::platform
