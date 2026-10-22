#pragma once

/**
 * @file IdeContext.h
 * @brief Public Plugin SDK — IdeContext interface.
 *
 * This is the primary entry point for plugins.
 * ABI version: 1  (check abiVersion() before using)
 *
 * Forge IDE Plugin SDK — Apache-2.0
 */

#ifdef __cplusplus
extern "C" {
#endif

#define FORGE_PLUGIN_ABI_VERSION 1

/** Opaque context handle passed to plugins. */
typedef struct ForgeIdeContext ForgeIdeContext;

/** Get the ABI version of this context. Plugin must refuse if < FORGE_PLUGIN_ABI_VERSION. */
int forge_context_abi_version(ForgeIdeContext* ctx);

/** Get the IDE version string (e.g. "0.1.0"). */
const char* forge_context_ide_version(ForgeIdeContext* ctx);

/** Register an action (returns 0 on success). */
int forge_register_action(ForgeIdeContext* ctx,
                          const char* id,
                          const char* display_name,
                          void (*handler)(void* userdata),
                          void* userdata);

/** Log a message (level: 0=debug, 1=info, 2=warn, 3=error). */
void forge_log(ForgeIdeContext* ctx, int level, const char* message);

#ifdef __cplusplus
} // extern "C"

// ============================================================
// C++ thin wrapper (header-only)
// ============================================================
#include <string>
#include <functional>
#include <stdexcept>

namespace forge::sdk {

class IdeContext {
public:
    explicit IdeContext(ForgeIdeContext* raw) : m_ctx(raw) {
        if (!raw) throw std::invalid_argument("null ForgeIdeContext");
        if (forge_context_abi_version(raw) < FORGE_PLUGIN_ABI_VERSION) {
            throw std::runtime_error(
                "Incompatible Forge IDE ABI. Recompile the plugin against the current SDK.");
        }
    }

    std::string ideVersion() const {
        return forge_context_ide_version(m_ctx);
    }

    void log(int level, const std::string& msg) const {
        forge_log(m_ctx, level, msg.c_str());
    }

    ForgeIdeContext* raw() const { return m_ctx; }

private:
    ForgeIdeContext* m_ctx;
};

} // namespace forge::sdk
#endif // __cplusplus
