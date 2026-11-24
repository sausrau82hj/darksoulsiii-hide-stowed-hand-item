# Hide Stowed Hand Item — *Dark Souls III*

Two-hand one weapon and the other hand's sheathed model stops being drawn on your back — weapons, shields,
catalysts, bows, staves. Single-player, offline.

## How it works

`hidestowed.dll` points the sheathed and off-hand model fields of `WEP_ABSORP_POS_PARAM_ST` at a parking dummy
(382, spare 383) that sits out of view. Those dummies have to exist in the character models, so the game has to
read models that carry them.

## Files

| Release asset | Contents |
|---|---|
| `ds3-hshi-v1.0.0.zip` | `hidestowed.dll`, `hidestowed.ini`, docs — 196 KB |
| `ds3-hshi-chr-models-v1.0.0.zip` | 165 patched `chr\*.chrbnd.dcx` — 342 MB |

## Install

1. Make one mod folder holding `hidestowed.dll`, `hidestowed.ini` and a `chr` folder with the patched models —
   from the models pack, or patched by you, see [Patched models](#patched-models).
2. Point the loader at that folder: [me](https://www.nexusmods.com/darksouls3/mods/2410),
   [me2](https://github.com/soulsmods/ModEngine2) or [me3](https://github.com/garyttierney/me3).

Without a loader, copy `hidestowed.dll`, `hidestowed.ini` and `xinput1_3.dll` into the game folder instead.
`xinput1_3.dll` stands in for the system XInput library: it forwards every XInput call to the real DLL, so
controllers keep working, and then loads `hidestowed.dll`. Only one `xinput1_3.dll` fits in a folder, so rename
another mod's copy first if there is one.

## Uninstall

- Remove the mod folder from the loader profile — or, on the game-folder route, delete `hidestowed.dll`,
  `hidestowed.ini` and `xinput1_3.dll`, and put your own `xinput1_3.dll` back.
- Models left patched — strip the dummies instead: `dotnet ds3tae.dll stripdir "<chr>" INPLACE "382,383" "chrbnd"`.
  A stripped bundle is not byte-identical (the DCX is re-compressed); `verify` ending in `verify: CLEAN` is what
  proves it either way.
- Effect off only — `HideLeftHang=0` and `HideRightHang=0` in the ini, applied within a second.

## Configuration

`hidestowed.ini` is re-read every second: edit, save, done. A missing file means all defaults.

| Key | Default | Meaning |
|---|---|---|
| `HideLeftHang` | `1` | hide the sheathed left-hand item (reached by two-handing the **right** weapon) |
| `HideRightHang` | `1` | mirror image: hide the sheathed right-hand item (reached by two-handing the **left** weapon) |
| `HideBothPaired` | `1` | hide the spare model of a one-handed paired weapon, **both hands** |
| `PairedFields` | `1,2,3` | which models of a paired weapon to hide: `1` = blade only (keeps the scabbard), `none` = leave paired weapons alone |
| `LoadCore` | `1` | shell only: `0` = the shell loads no core DLL, so the mod can stay installed but off |
| `ExtraMods` | *(empty)* | shell only: further DLLs to load, comma- or space-separated; when the key is missing it falls back to `dkp_loader.dll` |
| `ExtraModsDelayMs` | `0` | shell only: how long the shell waits before loading `ExtraMods` |
| `Rescan` | `1` | `0` = stop the periodic address-space re-scan (the stutter switch) |
| `IntervalSeconds` | `1` | how often the parameter values are re-asserted |
| `HidePoly` | `382` | parking dummy id (`383` spare) |
| `Probe` | `0` | `1` = observation only, writes nothing to the game |
| `ProbeAll` | `0` | `1` = probe every write it sees instead of stopping at the first |
| `ProbeLimit` | `2000` | how many probe hits to collect |
| `ProbeRearmMs` | `100` | delay before the probe arms again |
| `ScanMapped` | `0` | `1` = also consider MEM_MAPPED regions during the scan |
| `WorldChrManOffset` | `0x4768E78` | `WorldChrMan` offset, only used by the probe path |

The `shell only` keys are read by `xinput1_3.dll` alone, so they do nothing when a loader injects the DLL directly.
Everything from `Rescan` down is diagnostics and tuning: not in the shipped ini, and the log goes to the debugger
(`OutputDebugString`), never to a file. `HideLeftHang=0` restores vanilla for the right-hand two-hand stance only,
`HideRightHang=0` does the same for the left-hand stance, and `HidePairedOffhand` is read when `HideBothPaired` is
absent.

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
cl /nologo /O2 /EHsc /std:c++17 /DNDEBUG /LD src\hidestowed.cpp /Fe:hidestowed.dll /link /INCREMENTAL:NO
cl /nologo /O2 /EHsc /std:c++17 /DNDEBUG /LD src\xinput1_3_proxy.cpp /Fe:xinput1_3.dll /link /DEF:src\xinput1_3_proxy.def /INCREMENTAL:NO
dotnet build validator\validator.csproj
```

`dotnet build` writes `ds3tae.exe` and `ds3tae.dll` into `validator\bin\…` and needs the .NET 10 runtime. The same
assembly is committed in `tools\` as `ds3tae.dll` with its `.deps.json` and `.runtimeconfig.json`, next to the
`SoulsFormats.dll` it links, so `dotnet ds3tae.dll` works without building anything.

### Patched models

Both ways end with a `chr` folder whose `*.chrbnd.dcx` all carry dummies 382 and 383:

- **Prepared pack** — download `ds3-hshi-chr-models-v1.0.0.zip` and use its `chr` folder.
- **Your own models** — copy `ds3tae.dll`, `ds3tae.deps.json`, `ds3tae.runtimeconfig.json` and `SoulsFormats.dll`
  out of `tools\` (or build them as above) and run:

  ```
  dotnet ds3tae.dll parkdir "<chr>" INPLACE "382,383" "0,-1000,0" 0 "chrbnd"
  dotnet ds3tae.dll verify  "<chr>" chrbnd "382,383"
  ```

  The two numbers `verify` prints must match. `parkdir` skips files that already carry the dummies, so it is safe
  to re-run, and a model without them shows black triangles instead of hiding the item.

## Repository layout

```
hidestowed.dll / .ini         mod core and its configuration
xinput1_3.dll                 loader shell (forwards the system XInput)
src\                          C++ sources of both DLLs
validator\                    C# source of ds3tae (patch / verify / strip) + its GPL-3.0 text
tools\ds3tae.dll + .deps.json  the patcher assembly, run it with `dotnet`
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
