<div align="center">

<img src="assets/dawn-logo.svg" width="120" alt="Dawn">

# Dawn

**Missions and a native loadout editor for Destiny 2 build 86657.**

Install a packaged release over an existing game installation using the bundled Dawn installer.

**[Download the installer ZIP — 0.1.5.1](https://github.com/isinternets/Dawn/releases/download/v0.1.5.1/Dawn-0.1.5.1.zip)**

[Release notes and checksum](https://github.com/isinternets/Dawn/releases/tag/v0.1.5.1)

**Preview: [Dawn 1.7 Preview 1 — Adieu, Homecoming, and mission guidance](https://github.com/isinternets/Dawn/releases/tag/v1.7-preview.1)**

</div>

---

Dawn adds authored Lua missions, native gameplay systems, and an in-game loadout studio.
The player release includes the DLL, mission scripts, default settings, vendor rules, and event
presets together. The game itself is not included.

## Requirements

- Windows with **Windows PowerShell 5.1 or newer**.
- An existing Destiny 2 installation with executable version **`86657.20.08.23.1800.d2_rc`**
  and its complete `packages` folder.
- A packaged Dawn release ZIP containing `Install-Dawn.cmd`, `Install-Dawn.ps1`,
  `READ-ME.txt`, `release.json`, and the `payload` folder.

Players do not need Visual Studio, Python, Lua, or a source checkout.

## Getting the installer

### If you want to play

Download **[Dawn-0.1.5.1.zip](https://github.com/isinternets/Dawn/releases/download/v0.1.5.1/Dawn-0.1.5.1.zip)**
from the [GitHub release](https://github.com/isinternets/Dawn/releases/tag/v0.1.5.1). Under
**Assets**, choose that named installer ZIP. GitHub's automatically generated **Source code**
archives contain the source checkout and do not include the installable payload.

Version 0.1.5.1 fixes Gateway's final cannon activation and includes rebindable camera controls, file logging by default, and a standalone uninstaller. The updater keeps existing saves and settings. Read the release notes before installing. Extract the entire installer
ZIP into its own folder. Before running anything, check that the extracted folder contains:

```text
Dawn-<release>/
  Install-Dawn.cmd
  Install-Dawn.ps1
  Update-Dawn.cmd
  Update-Dawn.ps1
  Uninstall-Dawn.cmd
  Uninstall-Dawn.ps1
  READ-ME.txt
  release.json
  payload/
    steam_api64.dll
    Dawn/
      settings.json
      hud.json
      movement.json
      player.json
      scripts/
      event_presets/
      licenses/
      vendor_*.txt
```

- **`Install-Dawn.cmd`** is the file you double-click. It starts the PowerShell installer and
  keeps the window open so you can read its result.
- **`Install-Dawn.ps1`** performs the version checks, installation, backup, and rollback.
- **`Update-Dawn.cmd`** starts **`Update-Dawn.ps1`** to update while keeping existing saves and settings.
- **`release.json`** identifies the release and lists the expected payload files, sizes, and
  hashes. The installer uses it to check that the bundle is complete and unchanged.
- **`payload/`** contains the built DLL and the matching runtime content that will be installed.

Keep these files together. Downloading the `.cmd` or `.ps1` file by itself is not enough to
install Dawn. Extract the ZIP before running the installer; do not run it from inside the archive.

### If you downloaded or cloned this repository

GitHub's **Code → Download ZIP** and `git clone` provide the **source code**. In that checkout,
the installer source lives in [`tools/install/release/`](tools/install/release/):

```text
tools/install/
  New-DawnRelease.ps1
  release/
    Install-Dawn.cmd
    Install-Dawn.ps1
    Update-Dawn.cmd
    Update-Dawn.ps1
    Uninstall-Dawn.cmd
    Uninstall-Dawn.ps1
    READ-ME.txt
    README.md
```

This source folder does not contain `release.json` or `payload/`. Running its
[`Install-Dawn.cmd`](tools/install/release/Install-Dawn.cmd) directly will therefore fail with a
missing `release.json` error. The launcher does not build the DLL or download the missing files.

To turn the source into an installable release, first build the Release DLL, then run
[`tools/install/New-DawnRelease.ps1`](tools/install/New-DawnRelease.ps1) from the repository root.
That packaging script collects the DLL and runtime content, generates `release.json`, and creates
the complete player ZIP. Follow [Building and packaging from source](#building-and-packaging-from-source)
below for the commands and required build tools. Players receiving that finished ZIP do not need
those tools.

The separate `tools/install/Install-Dawn.ps1` is the development installer. The player-release
instructions on this page refer to the installer in `tools/install/release/` after packaging.

## Install or update Dawn

**Use `Update-Dawn.cmd` to keep an existing Dawn save. Use `Install-Dawn.cmd` for a
fresh save.** The updater is included in bundles starting with 0.1.3; earlier
bundles only contain the fresh-install workflow.

1. Extract the entire release ZIP into a new folder, such as a folder under Downloads.
2. Close Destiny 2.
3. Confirm that `release.json` and `payload/` are present, then double-click **`Update-Dawn.cmd`**
   to retain progress, or **`Install-Dawn.cmd`** to start fresh.
4. Enter your existing game folder when prompted: the folder containing **`destiny2.exe`**.
5. Wait for the installer to confirm success and show the backup location.
6. Launch `destiny2.exe` normally. The installer does not launch the game for you.

For example, if the executable is `D:\Games\Destiny 2\destiny2.exe`, enter
`D:\Games\Destiny 2`. Select the game folder itself, rather than its `bin/x64` or `Dawn`
subfolder, the source checkout, or the extracted installer folder.

The installer checks the game version and release file hashes before replacing files. It installs
matching DLLs and runtime content at both locations the game can load from:

```text
<game folder>/
  destiny2.exe
  steam_api64.dll
  Dawn/
  bin/x64/
    steam_api64.dll
    Dawn/
  .dawn/release-backups/
```

The first launch rebuilds caches, so it can take longer than later launches.
An update upgrades older Dawn databases automatically; a fresh installation creates a new one.
Keep the complete release bundle together when updating.

### Saves and settings

The updater keeps existing databases and their companion files, account identity, Dawn preferences,
event selections, and custom files. It refreshes the packaged DLL, mission scripts, vendor rules,
and presets. Packaged files replace older versions with the same names; personal scripts with
other names remain. Previously packaged content that the new release retires is kept in the backup.
Separate root and `bin/x64` profiles remain separate.

The fresh installer uses release defaults and does not carry old personal data into the active
installation. A fresh installation has no characters: the first launch opens the game's character
creator, and every character you create starts the New Light introduction. New characters are built
from the per-class `character_templates` in `Dawn/settings.json` (an older `settings.json` without
them uses the defaults built into the DLL), and you can delete one from the character screen. Existing saves keep their characters. Both workflows back up the previous `Dawn`
folders and DLLs. Existing `Sunrise` and
`Restoration` folders remain intact and are not imported by the release updater.

The fresh installer sets **Windowed Fullscreen** while preserving your existing resolution, render scale,
graphics quality, and key bindings. You can choose another mode later in the game's Video settings.
This changes the installing Windows user's shared Destiny display preferences, so other Destiny 2
installations under that same Windows user also see the mode change. Replacing only a DLL does not
apply this display setting. The updater leaves the native display preferences unchanged.

To update directly from PowerShell in the extracted release folder:

```powershell
.\Update-Dawn.ps1 -GameRoot "D:\Games\Destiny 2"
```

Add `-WhatIf` to preview, or `-Restore` to roll back the most recent installation/update.

### Preview an installation

From PowerShell in the extracted release folder, replace `D:\Dawn` with your game folder:

```powershell
.\Install-Dawn.cmd -GameRoot "D:\Dawn" -WhatIf
```

This validates the package and installation and shows the intended changes without changing your
game files or display preferences. Omit `-WhatIf` to install.

## Uninstall Dawn

Close Destiny 2, then double-click
[`Uninstall-Dawn.cmd`](tools/install/release/Uninstall-Dawn.cmd) and enter the game
folder. Keep `Uninstall-Dawn.ps1` beside the launcher; no release payload is needed.
Newly packaged releases include both files.

The uninstaller **permanently deletes all Dawn saves, settings, caches, and
backups**, including both Dawn runtime folders and the entire `.dawn` directory.
It restores original Steam DLLs when found in your backups before deleting those
backups. If none are available, it reports the missing DLLs to recover from your
original game backup before launching. Original game files and native
graphics/key-binding preferences remain intact. No uninstall backup is kept.

Preview from PowerShell with:

```powershell
.\tools\install\release\Uninstall-Dawn.ps1 -GameRoot "D:\Games\Destiny 2" -WhatIf
```

## Backups and rollback

Each installation keeps its backup under:

```text
<game folder>/.dawn/release-backups/<backup-folder>/
```

To restore the most recent installation, close Destiny 2 and run this from the extracted release
folder using the **same Windows user account** that installed it:

```powershell
.\Install-Dawn.cmd -GameRoot "D:\Dawn" -Restore
```

To select an older backup, add `-BackupPath` with the full path to that backup folder:

```powershell
.\Install-Dawn.cmd -GameRoot "D:\Dawn" -Restore -BackupPath "D:\Dawn\.dawn\release-backups\<backup-folder>"
```

Rollback restores the previous DLLs, Dawn folders, and display preferences. It also keeps the files
it displaces, including progress made after installation, inside the backup's `after-restore`
folder. Keep the backup and those retained files if you need that progress.

If installation fails after replacement begins, the installer attempts to restore the previous
files automatically. If it reports an interrupted installation or incomplete rollback, use the
reported backup path with `-Restore` before trying another installation.

## Loadout studio

Open **Loadout** in the in-game menu to edit your characters and gear. Click an item to edit it in
the side panel. Parts are adapted from [Sundial](#sundial).

- Edit character identity, progression, equipment, subclasses, and inventories.
- Pick a subclass and attunement, plus jump, grenade, and class ability.
- Browse weapons, armor, cosmetics, perks, and materials, with artwork from your packages.
- Give, equip, lock, and randomize items; set power and quantities; edit sockets with wider perk
  choices.
- Drag an armor stat bar to set a target. Letting go rolls the closest spread the game ships.
- Save equipped gear as a **loadout**, with perks, level, and abilities, and equip it in one click.
  Missing pieces are rebuilt, and held pieces get their saved perks back. Loadouts can be renamed,
  reordered, and copied to a character of the same class, and are kept in `Dawn/loadouts.json`.
- **Optimize Armor** works like Sundial's stat adjuster: set minimum stats, and **Adjust Armor**
  refits stat plugs, adds mods or a Masterwork, or swaps in better armor from your characters of
  that class. Locked armor is left alone.
- Ctrl+click inventory items to lock, send, pull, set power on, or remove them together. Search
  covers every character and the account, and takes filters like `is:exotic`, `is:locked`, and
  `power:>1000`.
- **Undo** and **Redo** with Ctrl+Z and Ctrl+Y.

Edits apply to the running game. **Apply Live** is on by default; turn it off to build a draft,
then **Apply** or **Reload** it. The editor checks inventory limits, one Exotic per gear category,
and whether the game changed the account meanwhile. The first apply of a session backs up your
player database to `Dawn/editor-backups`.

Most edits show in game within a moment. The Guardian you're playing may keep its look until you
sign in again, and changing class needs matching armor and a subclass.

## Missions

Bundled mission scripts include:

- Omega — `omega.lua`
- Beyond Infinity — `beyond_infinity.lua`
- The Gateway — `gateway.lua`
- A Deadly Trial — `deadly_trial.lua`
- Deep Storage — `deep_storage.lua`
- Tree of Probabilities — `strike_pact.lua`
- Hijacked — `hijacked.lua`
- New Light — `launchpad.lua`

Eater of Worlds is excluded from this build. Mission scripts load when the game starts; restart
the game after changing them.

## Installation troubleshooting

- **`Cannot find path ...\tools\install\release\release.json`:** you ran the installer source
  from the repository. Use a complete player release ZIP, or build and package the source using
  the instructions below. `release.json` is generated during packaging and is not committed in
  the installer source folder. Creating an empty JSON file will not fix this: the installer also
  needs the exact payload files and their matching manifest.
- **You cannot find `Install-Dawn.cmd`:** in this repository it is under
  [`tools/install/release/`](tools/install/release/). In a packaged player ZIP it is at the top
  level of the extracted folder, alongside `release.json` and `payload/`.
- **The game is still running:** close Destiny 2 and run the installer again. The installer does
  not stop the game automatically.
- **Unsupported game version:** select the folder containing build `86657.20.08.23.1800.d2_rc`.
  The installer checks the executable version before proceeding.
- **A release file is missing or changed:** extract the complete release ZIP into a new folder.
  Keep `release.json` and the entire `payload` folder together with the installer.
- **An interrupted installation needs recovery:** close the game and restore the reported backup
  using the rollback command above, then retry the installation.

## Building and packaging from source

<details>
<summary>Developer and release publisher instructions</summary>

Source builds require **MSBuild 18 Build Tools**, the **v145** C++ toolset, and the Windows SDK
selected by `Dawn/Dawn.vcxproj`. Python 3.11+ is used by the development validation and binding
tools. These requirements do not apply to players installing a release ZIP.

### 1. Build the Release DLL

From PowerShell:

```powershell
git clone --branch production https://github.com/isinternets/Dawn.git dawn
cd dawn
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  Dawn/Dawn.vcxproj /p:Configuration=Release /p:Platform=x64 `
  /p:PreferredToolArchitecture=x64 /p:CL_MPCount=2 /m:2 /v:minimal /nologo
```

Build the project directly; there is no solution file. Keep the x64 compiler host and v145 toolset.
The output is `build/x64/Release/steam_api64.dll`. Builds treat warnings as errors.

### 2. Generate the installer bundle

Run the packaging command from the repository root, where this README lives. After validating
the DLL and current runtime content together, create a player bundle with a
unique release name. For example:

```powershell
.\tools\install\New-DawnRelease.ps1 -Release '0.1.0-example'
```

This creates `build/releases/Dawn-0.1.0-example.zip` and the matching extracted folder,
`build/releases/Dawn-0.1.0-example/`. The generated `release.json`, `payload/`, and installer
launchers all live inside that output folder, not under `tools/install/release/`. Packaging
uses an existing Release DLL; it does not build or playtest it. Existing output folders and ZIPs
are never overwritten. `-DllPath` and `-OutputDirectory` can override the defaults.

For a local installation, use `Install-Dawn.cmd` inside that generated output folder and follow
the installation steps above. Every installation, including a local test, starts a fresh save;
use a separate game installation for release testing.

### 3. Distribute the complete ZIP

The bundle contains the installer, runtime payload, licenses, and a file-hash manifest. It excludes
source code, development tools, symbols, local saves, logs, and caches. Distribute the complete ZIP.
Manifest hashes check file integrity; they do not authenticate the publisher.

Creating this ZIP is a local operation. The packaging script does not upload it to GitHub or
create a GitHub Release. Give players the generated ZIP or attach it as a release asset; sending
them the repository's source ZIP or the installer launcher alone will not provide the payload.

See the [installer documentation](tools/install/release/README.md) for its replacement and rollback
contract, and [installer regression tests](tools/install/tests/release_installer.tests.ps1) for
checks using disposable game folders on Windows PowerShell 5.1 and current PowerShell.

</details>

## Credits

Dawn is possible because of the research, tools, and contributions shared by these projects.
The same acknowledgements appear in the in-game **Credits** tab.

### Sunrise

Thank you to [stanuwu and the Sunrise contributors](https://github.com/stanuwu/Sunrise) for the
foundation, game services, package research, and original runtime that Dawn builds on.

### Sundial

Thank you to [KyleThmpsn and the Sundial contributors](https://github.com/KyleThmpsn/sundial) for
the character and inventory editor, full perk selection, item-artwork research, random loadouts,
and armor-stat tools that informed Dawn's native loadout integration.

Dawn adapts Sundial's localized investment string and icon readers, class-restriction hash lists,
subclass ability displays and attunement selection, five plug-selection scopes, the armor-stat
allocation socket model, and the finished sandbox-perk catalog layout that supplies mod
descriptions and perk liveness. Those adaptations
are distributed under **GPL-3.0-only**. The pinned upstream revision, attribution, and license are
included in [NOTICE.md](Dawn/vendor/sundial/NOTICE.md) and the
[Sundial license](Dawn/vendor/sundial/LICENSE).

Item names and artwork are read from the player's installed game packages. Dawn does not
distribute Destiny artwork, textures, or a manifest database.

---

<div align="center">
<sub>Offline research project. Not affiliated with or endorsed by Bungie.</sub>
</div>
