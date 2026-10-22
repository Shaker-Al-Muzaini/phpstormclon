# Forge IDE Plugin SDK

## Overview

Forge IDE plugins are native shared libraries (DLLs on Windows) that extend the IDE
through a stable C ABI. This document describes how to write, package, and distribute
a Forge IDE plugin.

**ABI Version:** 1<br>
**SDK Header:** `sdk/include/forge/sdk/IdeContext.h`

---

## Plugin Manifest (`plugin.json`)

Every plugin must include a `plugin.json` at its root:

```json
{
  "id": "com.example.myplugin",
  "name": "My Plugin",
  "version": "1.0.0",
  "vendor": "Example Corp",
  "description": "Does something useful.",
  "sinceBuild": "0.1.0",
  "untilBuild": "1.0.0",
  "dependencies": [
    { "id": "forge.core", "version": ">=0.1.0" }
  ],
  "entryLibrary": "myplugin.dll",
  "extensions": [
    { "point": "forge.action", "id": "myAction", "displayName": "My Action" }
  ],
  "permissions": ["fs.read"],
  "signature": ""
}
```

### Fields

| Field | Required | Description |
|-------|----------|-------------|
| `id` | Yes | Reverse-DNS unique identifier |
| `version` | Yes | Semantic version |
| `sinceBuild` | Yes | Minimum Forge IDE version |
| `untilBuild` | Yes | Maximum Forge IDE version (inclusive) |
| `entryLibrary` | Yes | DLL filename relative to the plugin directory |
| `permissions` | Yes | Declared capabilities (see below) |
| `signature` | Marketplace | Base64 signature over the manifest |

### Permissions

| Permission | Grants |
|-----------|--------|
| `fs.read` | Read files in the project |
| `fs.write` | Write files in the project |
| `process.spawn` | Spawn child processes |
| `network.localhost` | HTTP to localhost (e.g. Ollama) |
| `network.internet` | Outbound internet requests |
| `ui` | Register tool windows, actions, and UI elements |

---

## Entry Points

The plugin DLL must export these C functions:

```c
// Called once after the DLL is loaded. Return 0 on success.
int forge_plugin_init(ForgeIdeContext* ctx);

// Called when the plugin is disabled or unloaded. Return 0 on success.
int forge_plugin_shutdown(ForgeIdeContext* ctx);

// Return the ABI version this plugin was compiled against.
// Must return FORGE_PLUGIN_ABI_VERSION (currently 1).
int forge_plugin_abi_version(void);
```

---

## Extension Points

### forge.action

Register an IDE action callable from menus, keyboard shortcuts, and Search Everywhere.

```cpp
forge_register_action(ctx, "com.example.myAction", "My Action", myHandler, userData);
```

### forge.toolWindow (M1+)

Register a tool window panel.

### forge.completionContributor (M5+)

Add completion items for a file type.

### forge.annotator (M5+)

Add diagnostics/annotations to editor documents.

### forge.fileType (M2+)

Register a new file type with icon and default associations.

---

## C++ Wrapper (Recommended)

Use the header-only C++ wrapper for a cleaner API:

```cpp
#include <forge/sdk/IdeContext.h>

extern "C" int forge_plugin_init(ForgeIdeContext* rawCtx) {
    try {
        forge::sdk::IdeContext ctx(rawCtx);
        ctx.log(1, "My plugin initializing.");
        // Register actions, extension points, etc.
        return 0;
    } catch (const std::exception& e) {
        return -1;
    }
}

extern "C" int forge_plugin_abi_version(void) {
    return FORGE_PLUGIN_ABI_VERSION;
}

extern "C" int forge_plugin_shutdown(ForgeIdeContext*) {
    return 0;
}
```

---

## Safety Model

- Plugins run **in-process** with full privileges (by design, for performance).
- Marketplace plugins must be digitally signed; unsigned local plugins require user confirmation.
- Permissions are declared in the manifest and shown to the user at install time.
- If a plugin crashes during `forge_plugin_init`, the crash guard disables it and shows a notification on next start.
- Enable Safe Mode (`--safe-mode` flag) to launch the IDE with all plugins disabled.

---

## Plugin Template Repository

A starter template is available at: `https://github.com/forge-ide/plugin-template`

It includes:
- `plugin.json` scaffold
- CMakeLists.txt linking to the Forge IDE SDK
- Example action registration
- CI workflow for building and signing

---

## First-Party Plugin Examples

Study these plugins (all in `plugins/`) as reference implementations:

| Plugin | Extension Points Used |
|--------|-----------------------|
| `laravel/` | completionContributor, annotator, toolWindow, action |
| `blade/` | fileType, completionContributor |
| `pest-phpunit/` | runConfiguration, action, toolWindow |
| `composer/` | toolWindow, action |
| `docker/` | runConfiguration, toolWindow |

All first-party plugins use only the public SDK — no internal APIs.
