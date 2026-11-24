# Hide Stowed Hand Item — *Dark Souls III*

Two-hand one weapon and the other hand's sheathed model stops being drawn on your back — weapons, shields,
catalysts, bows, staves. Single-player, offline.

## How it works

`HideSheathedLeft.dll` points the sheathed and off-hand model fields of `WEP_ABSORP_POS_PARAM_ST` at a parking
dummy (382, spare 383) that sits out of view. Those dummies have to exist in the character models, so either install
the prepared models or add them to your own with `ds3tae`.

## Files

| Release asset | Contents |
|---|---|
| `ds3-hshi-v1.0.0.zip` | `HideSheathedLeft.dll`, `HideSheathedLeft.ini`, docs — 196 KB |
| `ds3-hshi-chr-models-v1.0.0.zip` | 165 patched `chr\*.chrbnd.dcx` — 342 MB |
| `ds3-hshi-ds3tae-v1.0.0.zip` | the patcher: .NET 10 assembly, run `dotnet ds3tae.dll <command>` |

## Install

The game must be closed. `HideSheathedLeft.dll` and `HideSheathedLeft.ini` belong together and either route ends with
the game reading patched character models.

### Route 1 — unpacked game

1. Unpack the game with UXM: **Unpack**, then **Patch**, so the game reads loose files instead of its archives.
2. Get patched models into `Game\chr` — either copy the `chr` folder from `ds3-hshi-chr-models-v1.0.0.zip` over it
   (back up `Game\chr` first), or patch your own copies:

   ```
   dotnet ds3tae.dll parkdir "<chr>" INPLACE "382,383" "0,-1000,0" 0 "chrbnd"
   dotnet ds3tae.dll verify  "<chr>" chrbnd "382,383"
   ```

   The two numbers printed by `verify` must match.
3. Copy `HideSheathedLeft.dll`, `HideSheathedLeft.ini` and `xinput1_3.dll` into `Game\`, then start the game.

`xinput1_3.dll` is a stand-in for the system XInput library: the game loads it from its own folder, it forwards every
XInput call to the real DLL (controllers keep working) and then loads `HideSheathedLeft.dll`. It has to sit in
`Game\` — anywhere else the game never loads it — and only one `xinput1_3.dll` fits in a folder, so rename another
mod's copy first if there is one. Skip it entirely and use a loader instead, as in route 2.

### Route 2 — Mod Engine

A loader does both jobs: it serves the `chr` folder as an override and it loads the DLL, so nothing in the game folder
changes and the game needs no unpacking.

1. Make one mod folder holding `HideSheathedLeft.dll`, `HideSheathedLeft.ini` and a `chr` folder with the patched
   models (from the models pack, or patched by you as in route 1 step 2).
2. Point the loader at that folder: [me](https://www.nexusmods.com/darksouls3/mods/2410),
   [me2](https://github.com/soulsmods/ModEngine2) or [me3](https://github.com/garyttierney/me3).

## Uninstall

- **Route 2** — remove the mod folder from the loader profile.
- **Route 1** — delete `HideSheathedLeft.dll` and `.ini`, put your own `xinput1_3.dll` back, then restore `Game\chr`
  from your backup, or delete the loose `chr\*.chrbnd.dcx` files and let the game read its archives.
- **Patched models left behind** — strip the dummies instead:

  ```
  dotnet ds3tae.dll stripdir "<chr>" INPLACE "382,383" "chrbnd"
  ```

  A stripped bundle is structurally identical but not byte-identical (the DCX is re-compressed); `verify` ending in
  `verify: CLEAN` is what proves it either way.
- **Effect off only** — `HideLeftHang=0` and `HideRightHang=0` in the ini, applied within a second.

## Configuration

`HideSheathedLeft.ini` is re-read every second: edit, save, done. A missing file means all defaults.

### Behaviour

| Key | Default | Meaning |
|---|---|---|
| `HideLeftHang` | `1` | hide the sheathed left-hand item (reached by two-handing the **right** weapon) |
| `HideRightHang` | `1` | mirror image: hide the sheathed right-hand item (reached by two-handing the **left** weapon) |
| `HideBothPaired` | `1` | hide the spare model of a one-handed paired weapon, **both hands** |
| `PairedFields` | `1,2,3` | which models of a paired weapon to hide: `1` = blade only (keeps the scabbard), `none` = leave paired weapons alone |

`HideLeftHang=0` restores vanilla for the right-hand two-hand stance only; `HideRightHang=0` does the same for the
left-hand stance. The older key `HidePairedOffhand` is still read when `HideBothPaired` is absent.

### Loader shell

Only read by `xinput1_3.dll`; irrelevant when a mod loader injects the DLL directly.

| Key | Default | Meaning |
|---|---|---|
| `LoadCore` | `1` | `0` = the shell loads no core DLL, so the mod can stay installed but off |
| `ExtraMods` | *(empty)* | further DLLs for the shell to load as well, comma- or space-separated; when the key is missing entirely it falls back to `dkp_loader.dll` |
| `ExtraModsDelayMs` | `0` | how long the shell waits before loading `ExtraMods` |

### Diagnostics and tuning

Not in the shipped ini — add them only while investigating something or chasing a stutter. Log output goes to the
debugger (`OutputDebugString`), never to a file.

| Key | Default | Meaning |
|---|---|---|
| `Rescan` | `1` | `0` = stop the periodic address-space re-scan (the stutter switch) |
| `IntervalSeconds` | `1` | how often the parameter values are re-asserted |
| `HidePoly` | `382` | parking dummy id (`383` spare) |
| `Probe` | `0` | `1` = observation only, writes nothing to the game |
| `ProbeAll` | `0` | `1` = probe every write it sees instead of stopping at the first |
| `ProbeLimit` | `2000` | how many probe hits to collect |
| `ProbeRearmMs` | `100` | delay before the probe arms again |
| `ScanMapped` | `0` | `1` = also consider MEM_MAPPED regions during the scan |
| `WorldChrManOffset` | `0x4768E78` | `WorldChrMan` offset, only used by the probe path |

## Known limitations

- **NPCs hide the item too.** The parameter table is shared, so an NPC in the same stance simply does not draw its
  sheathed item. Player-only behaviour would mean patching the engine's attach resolver.
- **Other model mods.** After another mod replaces something in `chr\`, run `parkdir` again; patched files are
  skipped. The prepared model pack cannot merge with such mods at all.
- **Black triangles or flicker** mean a model is missing the dummy — run `verify`.
- **Online.** Only local memory and local model files change, but online play has a server-side save check: stay
  offline.

## Build

From a VS x64 developer prompt, in the repository root:

```
cl /nologo /O2 /EHsc /std:c++17 /DNDEBUG /LD src\HideSheathedLeft.cpp /Fe:HideSheathedLeft.dll /link /INCREMENTAL:NO
cl /nologo /O2 /EHsc /std:c++17 /DNDEBUG /LD src\xinput1_3_proxy.cpp /Fe:xinput1_3.dll /link /DEF:src\xinput1_3_proxy.def /INCREMENTAL:NO
dotnet build validator\validator.csproj
```

`dotnet build` writes `ds3tae.exe` and `ds3tae.dll` into `validator\bin\…`. Only the assembly is committed:
`tools\ds3tae.dll` with its `.deps.json` and `.runtimeconfig.json` is the build that ships as
`ds3-hshi-ds3tae-v1.0.0.zip`, so a rebuild means copying those over it — no executable lives in the repository.

## Repository layout

```
HideSheathedLeft.dll / .ini   mod core and its configuration
xinput1_3.dll                 loader shell (forwards the system XInput)
src\                          C++ sources of both DLLs
validator\                    C# source of ds3tae (patch / verify / strip) + its GPL-3.0 text
tools\ds3tae.dll + .deps.json  the patcher assembly, same build as the release asset (run it with `dotnet`)
tools\SoulsFormats.dll        the reference the validator build needs
docs\MANUAL-INSTALL.txt       longer walkthrough with troubleshooting
LICENSE, THIRD-PARTY-NOTICES  licenses, and what is not MIT
```

## License

- Mod DLLs, `src\`, `docs\` and this README: MIT — see [`LICENSE`](LICENSE).
- `validator\` and `tools\ds3tae.*`: GPL-3.0 — see [`validator/LICENSE`](validator/LICENSE). `ds3tae` links
  [SoulsFormats](https://github.com/JKAnderson/SoulsFormats), which is GPL-3.0.
- The models pack contains modified FromSoftware assets, single-player use only. Details in
  [`THIRD-PARTY-NOTICES.txt`](THIRD-PARTY-NOTICES.txt).

Not affiliated with FromSoftware or Bandai Namco.
