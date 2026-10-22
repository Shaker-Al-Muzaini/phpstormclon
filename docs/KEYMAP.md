# Forge IDE — Keymap Reference

All actions and their default key bindings. Keymaps are defined in
`resources/keymaps/default.json` (PhpStorm-compatible) and `resources/keymaps/vscode.json`.

## File

| Action | Default | VS Code |
|--------|---------|---------|
| New File | `Ctrl+N` | `Ctrl+N` |
| Open File | `Ctrl+O` | `Ctrl+O` |
| Save | `Ctrl+S` | `Ctrl+S` |
| Save All | `Ctrl+Shift+S` | `Ctrl+Shift+S` |
| Close Tab | `Ctrl+F4` | `Ctrl+F4` |
| Quit | `Alt+F4` | `Alt+F4` |

## Edit

| Action | Default | VS Code |
|--------|---------|---------|
| Undo | `Ctrl+Z` | `Ctrl+Z` |
| Redo | `Ctrl+Shift+Z` | `Ctrl+Shift+Z` |
| Cut | `Ctrl+X` | `Ctrl+X` |
| Copy | `Ctrl+C` | `Ctrl+C` |
| Paste | `Ctrl+V` | `Ctrl+V` |
| Select All | `Ctrl+A` | `Ctrl+A` |
| Duplicate Line | `Ctrl+D` | `Ctrl+Shift+D` |
| Delete Line | `Ctrl+Y` | `Ctrl+Shift+K` |
| Toggle Comment | `Ctrl+/` | `Ctrl+/` |
| Toggle Block Comment | `Ctrl+Shift+/` | — |
| Move Line Up | `Alt+Shift+Up` | `Alt+Up` |
| Move Line Down | `Alt+Shift+Down` | `Alt+Down` |
| Extend Selection | `Ctrl+W` | — |
| Shrink Selection | `Ctrl+Shift+W` | — |

## Navigate

| Action | Default | VS Code |
|--------|---------|---------|
| Search Everywhere | `Shift Shift` | `Ctrl+Shift+P` |
| Go to File | `Ctrl+Shift+N` | `Ctrl+P` |
| Go to Class | `Ctrl+N` | — |
| Go to Symbol | `Ctrl+Alt+Shift+N` | `Ctrl+Shift+O` |
| Recent Files | `Ctrl+E` | — |
| Recent Locations | `Ctrl+Shift+E` | — |
| Go to Declaration | `Ctrl+B` | `F12` |
| Go to Implementation | `Ctrl+Alt+B` | — |
| Find Usages | `Alt+F7` | `Shift+F12` |
| Find in Files | `Ctrl+Shift+F` | `Ctrl+Shift+F` |
| Replace in Files | `Ctrl+Shift+R` | `Ctrl+Shift+H` |
| Local Find | `Ctrl+F` | `Ctrl+F` |
| Local Replace | `Ctrl+R` | `Ctrl+H` |

## Code Intelligence

| Action | Default | VS Code |
|--------|---------|---------|
| Completion | `Ctrl+Space` | `Ctrl+Space` |
| Smart Completion | `Ctrl+Shift+Space` | `Ctrl+Shift+Space` |
| Parameter Info | `Ctrl+P` | `Ctrl+Shift+Space` |
| Quick Documentation | `Ctrl+Q` | — |
| Quick Definition | `Ctrl+Shift+I` | — |
| Show Intentions | `Alt+Enter` | `Ctrl+.` |
| Reformat Code | `Ctrl+Alt+L` | `Alt+Shift+F` |
| Optimize Imports | `Ctrl+Alt+O` | — |

## Refactor

| Action | Default | VS Code |
|--------|---------|---------|
| Rename | `Shift+F6` | `F2` |
| Extract Method | `Ctrl+Alt+M` | — |
| Extract Variable | `Ctrl+Alt+V` | — |
| Extract Constant | `Ctrl+Alt+C` | — |
| Inline | `Ctrl+Alt+N` | — |

## Run / Debug

| Action | Default | VS Code |
|--------|---------|---------|
| Run | `Shift+F10` | `Ctrl+F5` |
| Debug | `Shift+F9` | `F5` |
| Stop | `Ctrl+F2` | — |
| Step Over | `F8` | `F10` |
| Step Into | `F7` | `F11` |
| Step Out | `Shift+F8` | `Shift+F11` |
| Toggle Breakpoint | `Ctrl+F8` | `F9` |

## Version Control

| Action | Default | VS Code |
|--------|---------|---------|
| Commit | `Ctrl+K` | — |
| Push | `Ctrl+Shift+K` | — |
| Update (Pull) | `Ctrl+T` | — |

## Windows / Tools

| Action | Default | VS Code |
|--------|---------|---------|
| Settings | `Ctrl+Alt+S` | `Ctrl+,` |
| Project Tree | `Alt+1` | — |
| Terminal | `Alt+F12` | `` Ctrl+` `` |
| Problems | `Alt+6` | — |
| Structure | `Alt+7` | — |

---

## Customization

Edit `resources/keymaps/default.json` or create a new keymap file. Key names follow
Qt::QKeySequence notation. The keymap format:

```json
{
  "$schema": "forge://keymap/v1",
  "name": "My Custom Keymap",
  "extends": "default",
  "bindings": {
    "forge.file.save": "Ctrl+S"
  }
}
```

Place your custom keymap in `~/.forgeide/keymaps/` and select it in Settings → Keymap.
