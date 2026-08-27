# TaskbarMon Agent Guide

## Scope

TaskbarMon is a native Windows/MFC taskbar monitor derived from TrafficMonitor. Keep changes focused on the taskbar host, sampling core, configuration, rendering, and release tooling.

## Build and validation

- Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/ValidateProject.ps1` before handing off changes.
- Release and Release (lite) must build with warnings treated as errors for Win32, x64, and ARM64EC.
- Use Visual Studio 2022 v143, MFC, and the Windows 10 SDK. CI is the authoritative ARM64EC build environment.
- Do not commit `Bin/`, intermediate files, `artifacts/`, `.binlog`, or runtime diagnostics.

## Safety boundaries

- Do not re-enable upstream TrafficMonitor update feeds. Update support stays disabled until TaskbarMon has a project-owned HTTPS manifest, signature/hash verification, rollback protection, and CI rejection tests.
- Do not load `LibreHardwareMonitorLib.dll` into the UI process. Temperature monitoring stays disabled until a low-privilege helper process and authenticated, validated, time-bounded IPC protocol are implemented and reviewed.
- Explorer taskbar mutation must remain conservative: identify only known layouts, snapshot every changed property, verify restoration, and fall back to a floating bar when anything is uncertain.
- Never add UI updates from worker threads. Publish immutable snapshots and marshal changes to the UI thread.

## Editing and review

- Preserve existing source encoding and line endings, especially legacy Chinese MFC files.
- Prefer existing core/UI abstractions over new global state. Keep network and hardware I/O off the UI thread.
- Add focused validation for security-sensitive behavior and update project documentation when release assumptions change.
- Treat `docs/TASKBAR_HOST_VALIDATION.md` as a required manual test plan. Static checks and compilation do not replace real Explorer validation.

## GitHub workflow

- `.github/workflows/main.yml` validates project structure, builds all six Release combinations, checks the runtime payload, and uploads build evidence.
- Review the supply-chain constraints in `docs/SUPPLY_CHAIN.md` before changing dependencies, GitHub Actions, DLL loading, or update logic.