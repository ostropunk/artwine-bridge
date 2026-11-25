# Artwine Bridge

Artwine Bridge bridges music software for Windows running in Wine to USB hardware in Linux.

Arturia's software (MiniFreak V, Arturia Software Center, and others) doesn't support Linux. While you can run it under Wine, Wine doesn't support passing through USB connected hardware like the MiniFreak to the software.
This has meant that sync, preset transfer and firmware updates haven't worked. Playing over MIDI works, but **Link / Sync** is missing.

The software uses **libusb**, through a `libusb.dll`. On Windows that DLL relies
on WinUSB drivers, which Wine doesn't have, so under Wine the software never
finds the hardware.

Artwine Bridge replaces that `libusb.dll` with one that forwards each call to
the Linux libusb already on your system. The software synth and firmware on the hardware synth is unchanged, but the bridge lets them talk to each other.

> **Unofficial and unsupported.** Use it at your own risk. Artwine Bridge isn't affiliated with or
> endorsed by Arturia, Wine or anyone else. See [Disclaimer](#disclaimer).

```
MiniFreak V.exe ──► libusb.dll (Artwine, Windows side)
                        │  Wine unix call
                        ▼
                    artwine.so (Linux side) ──► libusb-1.0 ──► USB ──► MiniFreak
```

## Status

| App | Status |
| --- | --- |
| MiniFreak V 5.1.1 standalone ↔ MiniFreak firmware 5.0 | **Sync works** |
| Arturia Software Center 2.13 agent | Loads, sees devices |
| MiniFreak V VST3 in REAPER through [vstbridge](https://github.com/rations/vstbridge) 0.0.5 | **Sync works** |
| MiniFreak V VST3 through yabridge 5.1.1 | The bridge works, but the plugin window ignores mouse clicks on Wine 9.22 and newer (see below) |
| MIDI Control Center (BeatStep Pro and other controllers) | **Not bridgeable**: libusb is built into the program, and it finds devices through Windows device APIs that Wine doesn't fill in |
| Firmware update (DFU) | **Untested.** Don't flash firmware through the bridge yet |

## Requirements

- Wine 9 or newer, 64-bit prefix (tested with wine-staging 11.22)
- libusb 1.0.30 or newer (needs `libusb_get_device_string`)
- To build: `mingw-w64-gcc`, `gcc`, `make`, `pkg-config`

## Install

```sh
make
sudo make install            # or build the PKGBUILD in packaging/arch
sudo udevadm control --reload
```

Unplug and replug the device so the udev rule gives your user access, then:

```sh
artwine-bridge devices       # ok 1c75:0602 MiniFreak
artwine-bridge install       # bridges every 64-bit libusb.dll in ~/.wine
```

Restart the Arturia app.

## Usage

```
artwine-bridge [--prefix DIR] [--no-auto] status | install | uninstall | check | auto [on|off] | devices
```

- `status` lists every libusb DLL in the prefix: `original`, `bridged`, `outdated` or `broken`.
- `install` bridges them. The originals are kept next to them as `libusb.dll.orig`, and running it again is safe.
  On systems with systemd it also turns on `auto` (see below), unless you pass `--no-auto` or have run `auto off`.
- `uninstall` puts the originals back.
- `check` lists libusb functions an app uses that the bridge doesn't export yet.
  Run it after app updates: an app that gets a missing function may crash instead of reporting an error.
- `auto` watches the bridged DLLs with a systemd user path unit and re-runs `install` when an app
  update replaces one. Arturia's installers do this on every update. It's on by default.
  `auto off` turns it off and is remembered, so later installs leave it off; `auto on` turns it back on,
  and `auto` alone shows the current setting. `uninstall` removes the units.
- `devices` shows Arturia USB devices and whether your user can open them.

The default prefix is `$WINEPREFIX`, or `~/.wine` if that isn't set.

## yabridge and vstbridge

[yabridge](https://github.com/robbert-vdh/yabridge) runs Windows VST plugins in
Linux DAWs. The MiniFreak V plugin loads the same
`C:\ProgramData\Arturia\Shared\x64\libusb.dll` as the standalone app, so
bridging the prefix yabridge uses should be enough. Run
`artwine-bridge --prefix <yabridge prefix> install`.

Only one process can hold the synth's sync interface at a time. Close the
standalone app before you open the plugin, as you would on Windows.

Known issue: with yabridge 5.1.1 on Wine 9.22 or newer, plugin windows draw but
don't respond to the mouse ([yabridge#432](https://github.com/robbert-vdh/yabridge/issues/432)).
This isn't caused by Artwine Bridge. [vstbridge](https://github.com/rations/vstbridge), a yabridge
fork, fixes it, and MiniFreak V links and syncs from inside REAPER with vstbridge 0.0.5 built from
source. REAPER lists each VST3 filename once, so don't keep yabridge's and vstbridge's copies of
the same plugin in `~/.vst3` at the same time.

## Troubleshooting

Log every USB call an app makes:

```sh
ARTWINE_LOG=/tmp/artwine.log wine "C:\Program Files\Arturia\MiniFreak V\MiniFreak V.exe"
```

Only log while debugging. A linked MiniFreak makes about 1,000 USB calls a second, and logging each
one slows the app enough to notice: for example, the first on-screen key press after a preset change
gets lost.

- `open ... LIBUSB_ERROR_ACCESS`: the udev rule isn't active. Run `artwine-bridge devices`, then replug.
- App crashes on start after an update: run `artwine-bridge check` and open an issue with its output.
- "Unsupported sample rate" in MiniFreak V: this isn't a bridge problem. Wine can't
  open multichannel interfaces in some layouts (for example 4.0 surround, or
  14-channel inputs). Choose a stereo device, or create one in PipeWire.

## Safety

- The bridge never detaches the kernel's audio or MIDI driver, so the device's
  normal MIDI port keeps working while the app uses its own interface.
- Re-selecting the USB configuration that's already active is skipped, because
  on Linux that would fail or reset the device.

## How it works

`libusb.dll` is a small Windows DLL built with MinGW. On first use it loads
`artwine.so` from its own folder through Wine's unix-library interface
(`__wine_load_unix_lib`), then forwards each call with one argument block.
Both halves run in the same 64-bit process, so device handles and descriptor
pointers pass through unchanged. Only Windows' 32-bit `timeval` and `long` are
converted. Hotplug callbacks are queued on the Linux side and run on the
app's own thread the next time it handles libusb events.

## Disclaimer

Artwine Bridge is unofficial, unsupported software, provided as is, without warranty of any kind.
It's unlikely to damage your hardware or your computer, but it could harm either or both: it talks
directly to USB devices and replaces files inside your Wine prefix. You install and use it at your
own risk. The author takes no responsibility for any damage or loss it may cause.

Artwine Bridge isn't affiliated with, supported or endorsed by Arturia, the Wine project, libusb,
yabridge, vstbridge or any other third party, and the author doesn't endorse or represent any of
them. Arturia, MiniFreak, MiniFreak V, BeatStep Pro, REAPER and other product names are trademarks
of their owners, used here only to say what the software works with. The author claims no rights to
them or to any third party's copyrighted work.

Don't take problems to Arturia's support while the bridge is installed. Run
`artwine-bridge uninstall` first and check whether the problem is still there.

## Acknowledgements

- [libusb](https://libusb.info), which does the real USB work on the Linux side and which Arturia's apps already use.
- [Wine](https://www.winehq.org), whose unix-library interface makes the bridge possible.
- [yabridge](https://github.com/robbert-vdh/yabridge) by Robbert van der Helm and its fork
  [vstbridge](https://github.com/rations/vstbridge), which run Windows plugins in Linux DAWs.
- Arturia, for the MiniFreak and MiniFreak V. (Thanks only, not an affiliation.)

## License

Artwine Bridge's own code is LGPL-2.1-or-later (see `LICENSE`), like libusb and Wine.

- The repository holds no Arturia code, files or firmware. The bridge changes files in your own Wine
  prefix only, and keeps the originals.
- The `libusb.dll` it replaces is the stock open-source libusb (1.0.30) that Arturia ships under
  LGPL-2.1-or-later. That license explicitly lets users replace the library with a compatible one.
- The built `libusb.dll` includes Wine's `winecrt0` startup code (LGPL-2.1-or-later) and the
  MinGW-w64 runtime (permissive licenses). If you distribute built binaries, offer the source with
  them, as the LGPL requires.
