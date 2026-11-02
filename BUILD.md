# Windows x64 build

Use PowerShell 7, Zig 0.15.2, the OBS Studio 32.2.2 source headers, an installed supported OBS runtime, Visual Studio 2022 C++ x64 tools, CMake 3.28+, Qt 6.8+ development libraries compatible with your target OBS, the Windows SDK resource compiler, and Inno Setup 6. These tools and SDKs are not vendored.

1. Run `./build.ps1 -Zig C:/Tools/zig-0.15.2/zig.exe -ObsSdk C:/SDK/obs-studio-32.2.2 -ObsInstall 'C:/Program Files/obs-studio'`. Pass `-ResourceCompiler` when the Windows SDK is elsewhere.
2. Run `./build-ui.ps1 -Qt C:/SDK/obs-deps-qt6-2025-07-11-x64 -ObsSdk C:/SDK/obs-studio-32.2.2 -ObsInstall 'C:/Program Files/obs-studio'`. Pass `-CMake` for your CMake installation.
3. Review and run the tests in `tests/`. Installer tests must use isolated directories/runtimes, never your habitual OBS installation.
4. `./package.ps1 -ISCC 'C:/Tools/Inno Setup 6/ISCC.exe'` packages validated binaries. `-TestBuild` uses a separate QA installer identity.

Packaging intentionally checks fixed binary hashes and validation counts from the official build. A new toolchain or source change requires reviewing the new binaries and test evidence before updating those gates; do not bypass them silently. Generated build outputs and test logs are excluded from Git.

The source archive preserves the official implementation and installer branding. Publication changes documentation and normalizes developer-specific default paths; the released installer/runtime are unchanged. Build scripts may retain historical validation expectations; review the current release reports when rebuilding.
