#pragma once
#include <QString>
#include <QStringList>

namespace forge::platform {

/**
 * @brief OS-abstraction layer.
 *
 * All OS-specific calls MUST go through this layer so that the
 * rest of the codebase has no platform ifdefs. Platform-specific
 * implementations live in platform/src/ with one .cpp per OS.
 */
class Platform {
public:
    Platform() = delete;

    /// User-visible name for the current OS (e.g. "Windows 11")
    static QString osName();

    /// Path where app data / logs are stored.
    /// Windows: %LOCALAPPDATA%/ForgeIDE
    static QString appDataDir();

    /// Locate an executable on PATH or in the given search paths.
    static QString findExecutable(const QString& name,
                                  const QStringList& extraPaths = {});

    /// Open a URL in the default browser (no-op on headless builds).
    static void openUrl(const QString& url);
};

} // namespace forge::platform
