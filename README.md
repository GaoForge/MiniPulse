# MiniPulse (C++17 / Win32)

Windows 11 native taskbar-tray resource monitor. MIT licensed source example.

## Features
- CPU load % refreshed once per second (dynamic tray icon)
- Live CPU core clocks from the hardware helper: current core average, current fastest core, and highest sampled core clock in this session. Taskbar second line combines fastest-core GHz and CPU temperature.
- CPU detail separates live clocks from CPU-reported base/maximum specifications and marks clocks >101% of base as above-base (turbo range). Intel base may fall back to the explicit '@ ...GHz' brand-string specification when CPUID leaf 16h is empty. No CPU-model lookup table is used.
- RAM load %, used/total GiB
- Physical Ethernet and Wi-Fi download/upload speed
- Physical disk read/write throughput (all disks combined)
- Taskbar strip with CPU, RAM, disk and network groups, larger bold text and a translucent contrast backing
- Compact 360 x 36 DIP taskbar layout (320 DIP minimum); no decorative icons or padded label columns
- Higher-contrast 12px CPU/RAM text and temperature/available capacity; RAM second line shows available physical memory
- RAM hover details include physical usage/capacity, system cache and system commit used/limit; temperature remains in details when supported
- Disk hover lists physical disk instances with drive letters and individual read/write rates; strip still shows all disks combined
- Temperature status distinguishes missing providers, permission errors, missing sensors, read errors and stale data
- Dark per-group hover detail cards with live metrics and up to 45 samples of history; cards do not steal focus
- Detail typography uses 12px or larger labels, 18–22px supporting metrics and a neutral charcoal/white palette with a single subdued blue chart accent. RAM shows available capacity next to utilization, with commit/cache and concise sensor status below.
- Hover cards use one alpha-composited rounded surface with an aligned border and soft shadow; no hard window-region clipping or decorative LIVE badge. Section titles, card radii and labels are consistent.
- Hover-only per-section details; clicking the strip or detail card does not open an overview
- Each taskbar group has centered text and symmetric spacing around crisp separators
- Automatic taskbar positioning with button avoidance, DPI scaling, and Explorer recovery
- Tray menu can switch between embedded, above-taskbar, and hidden widget placements
- Independent CPU/RAM temperature helper using LibreHardwareMonitor 0.9.6 + PawnIO 2.2; existing WMI providers remain a fallback
- Dark, rounded hover cards with sparklines
- Left/right-click tray icon for its menu; right-click strip/details for the same menu
- Native UI needs no Qt, Python or Electron; hardware helper uses Windows .NET Framework 4.7.2+ and bundled dependencies
- MiniPulse requests administrator privileges on launch and starts the hardware helper automatically; the helper inherits elevation and requires PawnIO. No Explorer injection is used.
- Optional guided installer supports an all-users installation and startup at sign-in for the current administrator account.

## Build on Windows
### Portable toolchain (used for the packaged release)
The build tools are stored locally in `.tools` (LLVM-MinGW and portable Inno Setup).
Run `build.bat` to build `MiniPulse.exe`, or double-click `package.bat` to rebuild the
application and produce `dist/MiniPulse-Setup-1.6.3-x64.exe`.
The packaged build targets Windows 11 x64 and statically links the C++ libraries.
Hardware dependencies and original notices are in `sensors/`; see `sensors/THIRD-PARTY.txt` for exact versions, hashes and source links. Build the helper with the Windows .NET Framework compiler (build scripts do this automatically).
Release tool versions: LLVM-MinGW 20261006 and Inno Setup 6.4.3 (`.tools/inno6`).
The Simplified Chinese installer translation is included in `assets/ChineseSimplified.isl`.

### MSVC alternative
Install the **Desktop development with C++** workload in Visual Studio 2022 or a compatible MSVC Build Tools installation.
Open **x64 Native Tools Command Prompt for VS 2022** and run `build.bat`.
Run `MiniPulse.exe`. Use taskbar settings to promote the icon from tray overflow if necessary.

You can also compile using:

```bat
build.bat
```

## Design / limitations
- Core clocks are LibreHardwareMonitor clock-sensor samples, not effective frequency or a Task Manager average. The observed peak is only the maximum of roughly one-second samples during this run and can miss brief peaks. Specifications are read only on supported Intel CPUs (leaf 16h or explicit brand-string nominal base); missing specifications never stand in for live clocks. Hardware clocks require the same administrator-authorized helper as temperatures. The helper protocol is now v2 (24 bytes); distribute the UI and helper together.
- The embedded widget is a per-pixel-alpha layered child window of Explorer's primary taskbar; no code is injected into Explorer. A Windows compatibility/DPI manifest is embedded in the executable. It uses accessible button rectangles to select the rightmost free gap before the tray. This is not a reserved taskbar slot and is sensitive to Explorer layout changes.
- If layout discovery fails or no gap fits the compact widget, the embedded widget hides instead of covering buttons. The tray menu remains available to select the above-taskbar placement.
- The widget follows the primary taskbar. Secondary taskbars and vertical taskbars are not supported in this release.
- GPU monitoring is not included. The helper polls CPU and memory hardware only, without exposing hardware controls. Missing temperatures show a dash. DIMM temperature still depends on hardware support; installing a driver cannot add a nonexistent sensor.
- CPU temperature prefers the CPU package/Tctl sensor, with the hottest reported CPU sensor as fallback. RAM temperature uses only sensors explicitly identified as memory/DRAM/DIMM/DDR. ACPI thermal zones are not used as replacements.
- Network counter aggregation includes active hardware Ethernet/Wi-Fi only to reduce VPN/TUN duplication. Virtual interfaces and some drivers' NIC classifications may be omitted.
- Values can differ from Task Manager due to sampler timing and counter definitions.
- Popup has a fixed pixel layout as a first prototype; DPI scaling and accessibility require more work.
- Resource usage target is not a benchmark; measure Private Working Set/CPU on target Windows system before making claims.
- The code has not been security audited. Runtime and visual verification details are recorded in `build/verification.txt` for the packaged release.

## Guided installation
Run `dist/MiniPulse-Setup-1.6.3-x64.exe`.
The default is an all-users installation, requiring administrator approval.
The destination page is always shown; you can change the default Program Files path.
The installer creates a shared Start menu shortcut and an uninstall entry.
The shared desktop shortcut and startup at sign-in are optional and unchecked on a fresh installation. The tray menu controls startup after installation. MiniPulse uses a Windows scheduled task bound to the signed-in administrator account, so an enabled task can launch without a UAC prompt at sign-in. Launching MiniPulse directly still requests administrator approval.
Scheduled startup requires a protected local folder, such as the default Program Files installation folder. Use the task's original MiniPulse copy to turn startup off before switching directory or moving a portable copy. An in-place upgrade keeps the existing startup setting.
After installation, the wizard can start MiniPulse with administrator privileges.
Use the tray icon's right-click menu to exit before upgrading or uninstalling.
Distribute the setup EXE, or the complete portable ZIP including the `sensors` folder and notices;
the `.tools` directory is for developers and is not included in the installer.
Widget placement is saved per user in `%LOCALAPPDATA%\MiniPulse\settings.ini`.

The default hardware-temperature option opens the original PawnIO installer after setup; use its signed official edition. MiniPulse now requests administrator privileges when launched and starts the helper automatically with inherited permissions. Confirm the Windows UAC prompt; right-clicking Run as administrator is no longer needed. Canceling UAC cancels launch. The tray menu can retry hardware sampling. Silent installs do not install PawnIO or launch MiniPulse. PawnIO is shared software and is not removed with MiniPulse; uninstall it separately in Windows Settings if no other software needs it.

## Development verification
`tests/TaskbarLayoutTests.cpp` covers taskbar space allocation without overlapping buttons.
`tests/TemperatureSelectionTests.cpp` covers sensor selection and missing/invalid readings.
`tests/WidgetHitTestTests.cpp` covers hover regions at full/compact widths and different DPI scales.
For a timed smoke test, run `MiniPulse.exe --diagnostics=build/runtime.json --exit-after=10`.
Diagnostics contain only metrics and widget geometry; diagnostics are disabled by default.
Developer visual checks can use `--preview-hover=cpu` (or `ram`, `disk`, `net`) and `--render-preview=build/ui-preview.png`. These synthesize hover messages for the application's own widget and export the actual renderer with live samples; the output is a rendering preview, not a desktop screenshot.
`--capture-widget=build/taskbar.png` captures the widget's own bounds from the actual desktop after initial sampling. This distinguishes compositor visibility from merely having a visible HWND. Diagnostics also verify the input target at the center of all four hover groups.
`--capture-hover=build/hover.png` together with `--preview-hover=cpu` (or `ram`, `disk`, `net`) captures the displayed hover window including its transparent rounded corners and shadow from actual desktop pixels.
The latest release verification record is in `build/verification.txt`, and release file hashes are in `dist/SHA256SUMS.txt`.

## License
MiniPulse source: MIT. Bundled components retain their own licenses; see `sensors/THIRD-PARTY.txt` and the accompanying license files.
