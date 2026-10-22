# M0 Demo: Windows Foundation

From a Visual Studio 2022 x64 developer shell with Qt 6.8 and vcpkg available:

```powershell
$qtRoot = "C:\Qt\6.8.x\msvc2022_64"
cmake --preset windows-debug "-DCMAKE_PREFIX_PATH=$qtRoot"
cmake --build build\windows-debug
$env:PATH = "$qtRoot\bin;$env:PATH"
ctest --preset windows-debug --output-on-failure
cargo test --all
& .\build\windows-debug\bin\ForgeIDE.exe
```

The expected M0 result is the Forge IDE placeholder window and a running `phpintel`
service shown in the status bar. The JSON-RPC handshake, ping, error response,
shutdown, and frame codec are covered by the Rust tests.

The app is a foundation build, not a feature-complete PHP/Laravel IDE. Shell and
docking features begin in M1; editor and PHP analysis capabilities are later milestones.
