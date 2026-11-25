# Artwine Bridge

Artwine Bridge bridges music software for Windows running in Wine to USB hardware in Linux.

Arturia's software (MiniFreak V, Arturia Software Center, and others) doesn't support Linux. While you can run it under Wine, Wine doesn't support passing through USB connected hardware like the MiniFreak to the software.
This has meant that sync, preset transfer and firmware updates haven't worked. Playing over MIDI works, but **Link / Sync** is missing.

The software uses **libusb**, through a `libusb.dll`. On Windows that DLL relies
on WinUSB drivers, which Wine doesn't have, so under Wine the software never
finds the hardware.

Artwine Bridge replaces that `libusb.dll` with one that forwards each call to
the Linux libusb already on your system. The software synth and firmware on the hardware synth is unchanged, but the bridge lets them talk to each other.

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
| MiniFreak V VST/VST3 through yabridge | Should work, not tested yet (see below) |
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
artwine-bridge [--prefix DIR] status | install | uninstall | check | auto on|off | devices
```

- `status` lists every libusb DLL in the prefix: `original`, `bridged`, `outdated` or `broken`.
- `install` bridges them. The originals are kept next to them as `libusb.dll.orig`, and running it again is safe.
- `uninstall` puts the originals back.
- `check` lists libusb functions an app uses that the bridge doesn't export yet.
  Run it after app updates: an app that gets a missing function may crash instead of reporting an error.
- `auto on` adds a systemd user path unit that re-runs `install` when an app
  update replaces a bridged DLL. Arturia's installers do this on every update.
- `devices` shows Arturia USB devices and whether your user can open them.

The default prefix is `$WINEPREFIX`, or `~/.wine` if that isn't set.

## yabridge

[yabridge](https://github.com/robbert-vdh/yabridge) runs Windows VST plugins in
Linux DAWs. The MiniFreak V plugin loads the same
`C:\ProgramData\Arturia\Shared\x64\libusb.dll` as the standalone app, so
bridging the prefix yabridge uses should be enough. Run
`artwine-bridge --prefix <yabridge prefix> install`.

Only one process can hold the synth's sync interface at a time. Close the
standalone app before you open the plugin, as you would on Windows.

## Troubleshooting

Log every USB call an app makes:

```sh
ARTWINE_LOG=/tmp/artwine.log wine "C:\Program Files\Arturia\MiniFreak V\MiniFreak V.exe"
```

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

## License

LGPL-2.1-or-later, like libusb and Wine. Not affiliated with Arturia.
