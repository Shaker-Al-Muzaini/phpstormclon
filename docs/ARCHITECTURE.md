# Forge IDE — Architecture

## Component Overview

```mermaid
graph TB
    subgraph UI_Process ["UI Process (C++/Qt6)"]
        APP["app/\nmain.cpp\nMainWindow"]
        CORE["core/\nServiceSupervisor\nRpcClient\nActionManager\nSettings"]
        UI_LIB["ui/\nEditorWidget\nGutter\nTreeViews\nPopups"]
        PLATFORM["platform/\nPlatform abstraction\n(paths, processes, dialogs)"]
        SDK["sdk/\nPlugin SDK (C ABI)"]
    end

    subgraph Services ["Rust Services (separate processes)"]
        PHPINTEL["phpintel\nPHP parsing + indexing\nCompletion, diagnostics\nNavigation, refactoring"]
        VCS["vcs\ngit2 bindings\nStatus, commit, log\nMerge, diff, blame"]
        DBGP["dbgp\nXdebug DBGp client\nDAP-style facade"]
        DBTOOLS["dbtools\nsqlx\nMySQL/PG/SQLite\nSchema browser"]
        TERM["term\nConPTY / portable-pty\nTerminal emulation"]
        AIGW["ai-gateway\nOllama / llama.cpp\nLocal AI, no telemetry"]
    end

    subgraph Plugins ["First-party Plugins (DLLs)"]
        LARAVEL["laravel/"]
        BLADE["blade/"]
        PEST["pest-phpunit/"]
        COMPOSER["composer/"]
        DOCKER["docker/"]
    end

    APP --> CORE
    APP --> UI_LIB
    APP --> PLATFORM
    CORE -->|"JSON-RPC 2.0\nstdio/named pipe"| PHPINTEL
    CORE -->|"JSON-RPC 2.0"| VCS
    CORE -->|"JSON-RPC 2.0"| DBGP
    CORE -->|"JSON-RPC 2.0"| DBTOOLS
    CORE -->|"JSON-RPC 2.0"| TERM
    CORE -->|"JSON-RPC 2.0"| AIGW
    SDK --> Plugins
    Plugins --> CORE
```

## Process Model

```
┌────────────────────────────────────────────────┐
│  ForgeIDE.exe (Qt6 / C++20)                            │
│                                                        │
│  ┌──────────────────────────────────────────┐         │
│  │  ServiceSupervisor                       │         │
│  │  - starts/stops/restarts services        │         │
│  │  - exponential back-off on crash          │         │
│  │  - status shown in status bar             │         │
│  └──────────────────────────────────────────┘         │
└────────────────────────────────────────────────┘
          |stdio/pipe|   |stdio/pipe|   ...
  ┌──────────┐   ┌─────────┐
  │phpintel  │   │vcs      │  ...
  │(Rust)    │   │(Rust)   │
  └──────────┘   └─────────┘
```

## RPC Protocol

All services communicate over JSON-RPC 2.0 using LSP 3.17 framing:

```
Content-Length: <n>\r\n
\r\n
<UTF-8 JSON body, n bytes>
```

Custom methods use the `forge/` namespace (e.g. `forge/index`, `forge/completions`).
Standard LSP methods are used where they exist (e.g. `textDocument/completion`).

## File Layout

See [Section 4 of the master spec](../README.md#repository-layout).

## Data Flow: Completion Request

```mermaid
sequenceDiagram
    participant Editor as Editor Widget (C++)
    participant Core as RpcClient (C++)
    participant Intel as phpintel (Rust)

    Editor->>Core: requestCompletion(file, line, col)
    Core->>Intel: textDocument/completion {uri, position}
    Intel-->>Core: CompletionList []
    Core-->>Editor: completionResults
    Editor->>Editor: showCompletionPopup()
```

## Plugin Architecture

```mermaid
graph LR
    subgraph Core
        EP[ExtensionPointRegistry]
        IC[IdeContext]
    end
    subgraph Plugin DLL
        MF[plugin.json manifest]
        EP2[Extension registrations]
    end
    EP <--> EP2
    IC --> Plugin DLL
```

Plugins are native DLLs loaded via the stable C ABI in `sdk/`. They receive an
`IdeContext` giving access to the project model, editor, actions, settings, and
RPC. The plugin manifest declares permissions; unsigned plugins require user
confirmation.
