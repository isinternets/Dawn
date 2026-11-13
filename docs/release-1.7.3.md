# Dawn 1.7.3

Homecoming bazaar door fix, the Dawn title screen, and loadout editor updates.

## Homecoming

- The bazaar door in Tower North now opens when you reach it after Ikora's Nova Bomb. It no longer waits for every scripted Cabal in the area to die, so Cabal that survive the blast or cannot reach you through the closed door no longer leave it shut.
- Holliday now lands when you clear the bazaar or reach its far end, so a Cabal you cannot reach no longer holds the pickup.
- Preserved mission order, enemy spawning, scenes, dialogue, and the door breach.

## Title screen

- Dawn now has its own title screen: a navy background with the Dawn wordmark and animated medallions around it.
- The Bungie splash and character select art are recoloured in memory as they load, and the bright loading screens are inverted and tinted blue. No Bungie art is shipped.
- Each part can be turned off with `client.custom_bootflow_textures`, `client.invert_splash_screens` and `client.title_filigree` (all on by default).

## Loadout editor

- Added saved loadouts, an armor optimizer, undo, and bulk item actions.
- Supports non-stock subclasses, builds ability rows for custom subclass equips, and lists all subclasses.
- Shows damage types set by weapon plugs, names perk scopes per item, and keeps damage and energy type names consistent.
- The armory no longer limits gear to the character's class or to one Exotic weapon and one Exotic armor piece. Another class's gear equips but does not show in the inventory screen.
- Editor changes wait until a character is picked, the UI is polished, and a font atlas crash is fixed.

## Missions

- 1AU: music sections now follow the retail mission more closely, the Vanguard channel plays its second line, Ikora's farewell replaces Zavala's in the interior corridors, and the foundry objective and line wait for the tube entry.
- Adieu: music cues are adjusted to keep the tone consistent through the level, and a cut canyon line no longer plays.

## Other

- Optional diagnostic traces (`client.stall_trace`, `effect_trace`, `selector_watch`, `audio_trace`, `action_trace`), all off by default.
- The release ZIP now uses forward-slash paths, so it extracts correctly on Linux and macOS.
- Added a security policy (SECURITY.md).

## Credits

Thanks to [di-donna](https://github.com/di-donna) for the **title screen**, [KyleThmpsn](https://github.com/KyleThmpsn) for the **loadout editor** and diagnostics, and [EthanRDoesMC](https://github.com/EthanRDoesMC) for the **1AU and Adieu music** changes.

## Install or update

Download and extract **Dawn-1.7.3.zip**. Close Destiny 2, then run **Update-Dawn.cmd** to keep your existing save and settings. **Install-Dawn.cmd** starts a fresh save. Keep the complete extracted package together.

Requires Destiny 2 build **86657.20.08.23.1800.d2_rc** and its complete packages folder. The game is not included.

The player ZIP excludes performance-capture tools, Python scripts, debug symbols, logs, caches, and personal saves.

## Validation

- Homecoming Debug and Release: 304,432 checks passed in each configuration, including a new full-mission run in which Ikora's Cabal and a bazaar Cabal never die.
- Release: 1AU 1,391 checks, Omega ending 42,458 checks, title screen recolour 20 of 20, and the account settings suite passed. Both title-screen shaders compile and fit their buffers.
- The Adieu suite still checks the music cues from before the Adieu music change and fails there; it has not been updated yet.
- Release x64 DLL built successfully.
- In game: the bazaar door was confirmed to open in a live playthrough of the fix. Holliday's landing and the pickup were not separately confirmed.
