# Timp — Forge installer support

**Date:** 2026-06-06
**Status:** Approved design, pending implementation
**Reference:** Hisashi's `forge.toml` + `installer.ps1` (`D:/Workhammer/hisashi`)

## Goal

Give Timp a Windows installer built with **Forge** (the Go installer toolkit at
`D:/Workhammer/Forge`), mirroring the proven Hisashi setup. Output is a single
`Timp-Setup-<version>.exe`.

## Decisions

| Topic | Choice |
| --- | --- |
| Wizard theme | `console` (fits Timp's retro/Winamp aesthetic) |
| Install scope | Machine-wide — `${PROGRAMFILES}/Timp`; Forge auto-elevates via UAC |
| License step | Include it; create a public-domain `LICENSE.txt` |
| Version | `0.5.0` |
| Settings location | **Always** `%APPDATA%\Timp\config.ini` (via `SDL_GetPrefPath`), regardless of install dir — drops the next-to-exe behavior entirely |

## Why a clean staging step

Timp's `build/` directory is **not** a shippable payload — it holds `.o`
intermediates and a local `config.ini`. The installer script therefore stages a
fresh `dist/win-x64/` containing only the three shippable artifacts
(`timp.exe`, `SDL2.dll`, `skins/`), and `forge.toml` points its single `[[dirs]]`
entry at that staged folder.

## Files

### 1. `Timp/forge.toml` (new)

Installer manifest. Validated against Forge's schema
(`internal/forge/validate.go`): `icon` is optional, `0.5.0` is valid semver,
`console` is a valid theme, `launches` attach only to the `finish` step, `HKLM`
is a valid hive.

```toml
[app]
name    = "Timp"
version = "0.5.0"
id      = "io.timp.timp"

[meta]
publisher = "Şamil Bülbül"
comments  = "Native Winamp-style music player"

[ui]
theme        = "console"
window_title = "Timp Setup"

[install]
default_dir = "${PROGRAMFILES}/Timp"

# Settings live in %APPDATA%\Timp (outside the install dir), so uninstall
# never touches them — nothing to keep here.
[uninstall]
keep = []

[[steps]]
type  = "welcome"
title = "Timp 0.5.0"
body  = "This wizard will install Timp 0.5.0 on your computer.\n\nTimp is a small, native, Winamp-style music player.\n\nClick Next to continue."

[[steps]]
type  = "license"
title = "License"
body  = "Timp is released into the public domain. Please review the terms below."
file  = "LICENSE.txt"

[[steps]]
type  = "folder"
title = "Select Folder"
body  = "Choose where to install Timp 0.5.0."

[[steps]]
type  = "shortcuts"
title = "Select Optional Tasks"
body  = "Check the items you would like the installer to perform."

[[steps]]
type  = "install"
title = "Installing..."
body  = "Copying files to your system."

[[steps]]
type  = "finish"
title = "Installation Complete"
body  = "Timp has been installed successfully.\n\nClick Finish to close the installer."

[[steps.launches]]
kind    = "app"
target  = "${INSTALLDIR}/timp.exe"
label   = "Run Timp after install"
checked = true

# Application payload — staged clean by installer.ps1
[[dirs]]
src = "dist/win-x64"
dst = "${INSTALLDIR}"

[[shortcuts]]
target   = "${INSTALLDIR}/timp.exe"
location = "${DESKTOP}/Timp.lnk"
name     = "Timp"
icon     = "${INSTALLDIR}/timp.exe"
label    = "Create Desktop Shortcut"
optional = true
default  = true

[[shortcuts]]
target   = "${INSTALLDIR}/timp.exe"
location = "${STARTMENU}/Timp/Timp.lnk"
name     = "Timp"
icon     = "${INSTALLDIR}/timp.exe"
label    = "Create Start Menu Entry"
optional = true
default  = true

[[registry]]
hive  = "HKLM"
key   = "Software\\Timp\\Timp"
value = "InstallDir"
data  = "${INSTALLDIR}"

[[registry]]
hive  = "HKLM"
key   = "Software\\Timp\\Timp"
value = "Version"
data  = "0.5.0"
```

No `[app] icon` — Timp ships no `.ico`; shortcuts reference `timp.exe`, so they
get the exe's default icon.

### 2. `Timp/installer.ps1` (new)

Adapted from Hisashi's. Params: `-Rid win-x64`, `-Config Release`,
`-ForgeDir ..\Forge`, `-SkipBuild`. Steps:

1. **Build** — run `build.ps1` unless `-SkipBuild`. (Timp's `build.ps1` takes no
   `-Rid`/`-Config` args, so these params only gate forge/staging, not the gcc
   build.)
2. **Stage** — recreate `dist/win-x64/` from scratch, copying only
   `build/timp.exe`, `build/SDL2.dll`, and `build/skins/`. Fail loudly if any is
   missing (means `build.ps1` didn't run / produce output).
3. **Ensure `forge-gui.exe`** — same build-if-stale logic as Hisashi: require
   `$ForgeDir/uninstall.exe` to exist; (re)build `forge-gui.exe` with
   `go build -tags "desktop,production" -ldflags "-H windowsgui -X main.Version=local-gui" -o forge-gui.exe ./cmd/forge/`
   when missing or older than newest `cmd/forge/**.go`.
4. **Build Setup.exe** — via `Start-Process -Wait -PassThru` (GUI-subsystem
   binary doesn't propagate `$LASTEXITCODE`):
   `forge validate forge.toml` then `forge build --out dist/installer`.
5. **Verify** — locate newest `Timp-Setup-*.exe` in `dist/installer`, confirm PE
   subsystem == GUI (2), print size. Warn if not GUI.

### 3. `Timp/LICENSE.txt` (new)

Public-domain dedication (Unlicense text), matching the README's "All code
written for this project is released into the public domain."

### 4. `Timp/src/config.c` (modify) — always use the user-data dir

Settings always live in the platform user-data dir, which `SDL_GetPrefPath`
returns and creates: `%APPDATA%\Timp\config.ini` on Windows,
`~/.local/share/Timp/config.ini` on Linux, `~/Library/Application Support/Timp/`
on macOS. This keeps the install dir (which may be read-only, e.g. Program
Files) free of writable state, and works identically for admin and standard
users. The previous next-to-exe (`SDL_GetBasePath`) behavior is dropped.

Only `config_path()` changes; `config_load`/`config_save` are untouched since
both already route through it.

```c
// Settings always live in the platform user-data dir (e.g. %APPDATA%\Timp\
// on Windows), which SDL_GetPrefPath creates for us. Independent of where the
// app is installed, so a read-only install dir (Program Files) is fine.
static void config_path(char* out, size_t cap) {
    char* pref = SDL_GetPrefPath("Timp", "Timp");
    if (pref) {
        snprintf(out, cap, "%sconfig.ini", pref);
        SDL_free(pref);
    } else {
        snprintf(out, cap, "config.ini");  // last-resort: cwd
    }
}
```

### 5. `Timp/.gitignore` (modify)

Add `dist/` (installer staging + output). `build/`, `*.exe`, `*.o` already
ignored.

### 6. `Timp/README.md` (modify)

The "Portable" feature bullet and the "Configuration" section currently say
`config.ini` lives next to the executable via `SDL_GetBasePath()`. Update both
to reflect the new location (`%APPDATA%\Timp\config.ini`, or the platform
equivalent via `SDL_GetPrefPath`). Add a short "Install" note pointing at
`installer.ps1` / the Forge-built `Setup.exe`.

## Data flow

```
build.ps1 ──> build/ (dirty: .o, timp.exe, SDL2.dll, skins/, config.ini)
                 │  installer.ps1 stages clean copy
                 ▼
            dist/win-x64/ (timp.exe + SDL2.dll + skins/)
                 │  forge validate + forge build
                 ▼
            dist/installer/Timp-Setup-0.5.0.exe
```

## Verification

- `forge validate forge.toml` passes (run inside `installer.ps1`).
- `installer.ps1` produces `dist/installer/Timp-Setup-0.5.0.exe`, reports
  subsystem **GUI**.
- Manual: run Setup, confirm UAC elevation prompt, install to
  `C:\Program Files\Timp`, Desktop/Start-Menu shortcuts, "Run Timp" launch.
- Manual: change a setting and relaunch — confirm it persists via
  `%APPDATA%\Timp\config.ini` (works for admin and standard users alike, since
  it never touches the install dir).
- `config.c` change compiles cleanly under `build.ps1`.

## Known limitations / non-goals

- **Per-user shortcuts.** Forge's `${DESKTOP}` / `${STARTMENU}` resolve to the
  *current* (post-elevation, admin) user's folders, not All-Users. Acceptable;
  matches Forge's current capability.
- **No embedded exe icon.** Out of scope; would require adding a Windows
  resource (`.rc` + `.ico`) to Timp's gcc build.
- **No version flag added to the C app.** `0.5.0` lives only in `forge.toml`;
  wiring a `--version` into `timp.exe` is out of scope.
- **Settings are no longer portable next-to-exe.** They always live in the
  user-data dir, so copying the build folder to a USB stick no longer carries
  settings with it (per request: settings always in `%APPDATA%`).
- Linux/macOS packaging is unchanged (Forge is Windows-only).
