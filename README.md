# CrossPoint Reader: personal Xteink X4 Pro fork

This is one person's fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader),
the open-source e-reader firmware. It exists to make reading on a single device, the
**Xteink X4 Pro**, better for its owner. It is public so the work can be read and
reused, not because it is a product.

**Read this first**

- **Not a distribution.** There are no releases, no support, and no promise that it
  works on your device. Install it only if you can recover a bad flash yourself. The
  official project's [web flasher](https://crosspointreader.com/#flash-tools) and
  releases are the right way to run CrossPoint.
- **Do not use the on-device update check.** It still points at the official project's
  releases (`src/network/OtaUpdater.cpp`), so installing from it replaces this fork
  with stock CrossPoint. Update by flashing a build of this fork.
- **Only the X4 Pro is a target.** Other boards still build from shared code where
  upstream does, but they are not tested or tuned here.
- **Hardware testing is limited.** Changes are judged by host tests, allocation and
  operation counts, static RAM, and a clean `x4pro-gh_release` build. Nothing here is
  claimed as validated on a device unless a doc says so.
- **Issues are the owner's task queue.** Outside issues and pull requests may go
  unanswered. For anything that is not specific to this fork, please go to
  [upstream](https://github.com/crosspoint-reader/crosspoint-reader).
- **Not affiliated** with Xteink, the CrossPoint project or the FreeInk SDK.

## What this fork is for

The goals are fast and cheap page turns, less ghosting, and a Library that refreshes
quickly with a large collection of books kept offline on the SD card. In practice:

- **Offline EPUB reading.** Web server upload, WebDAV, OPDS and Calibre wireless
  come from upstream and are left in place, but they are unused and not worked on.
  Books arrive by SD card reader.
- **A Calibre library on the card.** The owner exports about 750 books from Calibre
  ("Save to disk": one folder per book) and copies them over with
  [`scripts/sync-calibre-library.sh`](scripts/sync-calibre-library.sh). Adding,
  removing, renaming and re-exporting files must be dependable, and reading position,
  bookmarks and clippings must survive a re-export.
  See [Library](docs/fork-library.md).
- **Page-turn speed and display quality.** Cheaper page turns, idle prefetch of the
  next page, ghost cleanup and hardened display and storage paths. See
  [Reader](docs/fork-reader.md), [Layout and prefetch](docs/fork-layout.md) and
  [Storage and display](docs/fork-storage-display.md).
- **One reader font.** The built-in [Libron](docs/fork-libron-font.md) serif is
  enforced for book text, shown in menus as "Reader Serif".
- **Reading aids.** Stable page numbers, a seconds-based auto page turn, and Find in
  Book. See [EPUB indexing](docs/fork-epub-indexing.md).

Features the owner does not want, such as time-left estimates or reading statistics,
are listed as declined in [scope](docs/fork/scope.md) and
[CrossInk](docs/fork-crossink.md).

## How it relates to upstream

The fork is a linear series of patches rebased onto upstream's `develop`. Upstream's
design wins any conflict, and patches that upstream replaces are dropped. Some
features are ported by hand from [CrossInk](https://github.com/uxjulia/CrossInk), a
feature source and never a base, with the original authors credited as
`Co-Authored-By`. The SDK comes from a [matching fork](https://github.com/endqwerty/freeink-sdk)
of the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk).

## History is rewritten

`develop` is always upstream's `develop` plus this fork's patches on top. When
upstream moves, the patches are **rebased and force-pushed**, so commit hashes change
often and `develop` never merges with upstream. Do not build on this fork's history:
a clone made earlier will diverge. Pull requests here are squash-merged; to follow
the fork, fetch and reset to `origin/develop` rather than merging. Upstream's history
is never rewritten. The fork's history may also be rewritten to remove sensitive
information.

## License

The code is MIT-licensed, as upstream is: see [LICENSE](LICENSE) (copyright upstream
and this fork). Material the fork adds, such as the Libron font under the SIL Open Font
License, is listed with its license in [NOTICE.md](NOTICE.md). Authors of adapted
code are credited as `Co-Authored-By` in the commit and in the `docs/fork-*.md`
attribution sections.

## Building

```bash
git clone --recurse-submodules --branch develop https://github.com/endqwerty/crosspoint-reader.git
cd crosspoint-reader
pio run -e x4pro-gh_release
```

`scripts/fork-workflow.sh check` runs what CI would (formatting, host tests with and
without sanitizers, the X4 Pro build). GitHub Actions is disabled on this fork, so
that script is the gate. Flash with the official web flasher's "Custom .bin" option or
the `esptool` command below.

## Fork documentation

- [Fork instructions](docs/FORK.md): rules for working in this fork, with topic pages
  under [docs/fork/](docs/fork/).
- Design notes: [reader](docs/fork-reader.md), [library](docs/fork-library.md),
  [layout](docs/fork-layout.md), [EPUB indexing](docs/fork-epub-indexing.md),
  [storage and display](docs/fork-storage-display.md), [Libron font](docs/fork-libron-font.md),
  [tests and upstream imports](docs/fork-maintenance.md).
- Security: [SECURITY.md](SECURITY.md). Licenses and third-party notices: [NOTICE.md](NOTICE.md).

---

# Upstream README

What follows is the upstream project's README. Parts of it (the web installer, the
Developer Edition link, release downloads) describe the official firmware, not this fork.

# CrossPoint Reader

[![Fund contributors](https://img.shields.io/badge/%F0%9F%91%91_Fund_contributors-royalty.dev-BB953A?style=for-the-badge&labelColor=1a1a1a)](https://app.royalty.dev/crosspoint-reader/crosspoint-reader)

CrossPoint is open-source e-reader firmware - community-built, fully hackable, free forever. It's maintained by a growing community of developers and readers who believe your device should do what you want - not what a manufacturer decided for you.

### Now running on:
- **ESP32C3-based** Xteink X4 and X3.
- **ESP32S3-based** Xteink X4Pro and X4Classic, Seeed reTerminal Sticky, M5PaperMono

Check [our Devices page](https://crosspointreader.com/devices) for the full list.

![CrossPoint Reader running on Xteink device](./docs/images/cover.jpg)

> If you're planning to buy an Xteink device, consider purchasing an **X3/X4 Developer Edition** through https://crosspointreader.com. CrossPoint receives a small share of each sale, helping fund development costs.

## What can CrossPoint do?

- **Reader engine**: EPUB 2/3 rendering with embedded-style option, image handling, hyphenation, kerning, adaptive table layouts, native CJK ruby annotations, chapter navigation, footnotes, bookmarks, dictionary lookups ([StarDict](docs/dictionary.md)), go-to-percent, auto page turn, orientation control, focus reading, KOReader progress sync and more.

- **Various formats**: native handling for `.epub`, `.xtc/.xtch`, `.txt`, and `.bmp`.

- **Touch reading**: follow EPUB links and look up words in the dictionary on touch-enabled devices.

- **Screenshots.**

- **Custom fonts**: install your favorite fonts on the SD card.

- **Tilt page turn (X3 and Sticky)**.

- **USB Drive mode (X4Pro)**: access the SD card as USB mass storage.

- **Library workflow**: indexed title/author search, recently-added and alphabetical views, multilingual grouping, folder browser, recent books, and SD-cache management.

- **Wireless workflows**:
  
  - File transfer web UI
  - EPUB Optimizer
  - Web settings UI/API (edit many device settings from browser)
  - WebSocket fast uploads
  - WebDAV handler
  - AP mode (hotspot) and STA mode (join existing Wi-Fi), both with QR helpers
  - Calibre wireless connect flow
  - OPDS browser with saved servers (up to 8), search, pagination, and direct download
  - OTA update checks and installs from GitHub releases

- **Customization**: night mode, multiple themes (Classic, Lyra, Lyra Extended, RoundedRaff), sleep screen modes including transparent overlays, front/side button remapping, status bar controls, power-button behavior, refresh cadence, and more.

- **Localization**: 34 UI languages and counting, including CJK font fallback and RTL support.

### Coming soon:

- More themes.

- Web plugins.

- Bluetooth pageturner.

- Much more! stay tuned.

---

## USB-locked devices (Xteink Unlocker)

Some Xteink units purchased from third-party stores (e.g. AliExpress) ship with USB flashing locked from the factory.
If your device is locked, you will need to use the **Xteink Unlocker** tool available at
https://crosspointreader.com/#unlock-tool before you can flash CrossPoint.

**You do not need this tool if you bought your device directly from xteink.com.** Those units are not locked.

**Not sure if your device is locked?** Power it on, connect the USB-C cable, and try flashing via the web flasher first (see
[Install firmware](#install-firmware) below). If the browser's serial device picker does not show your device, try a different
USB port or browser before assuming the device is locked. Only reach for the unlocker if the device still doesn't appear.

> ### ⚠️ WARNING: READ THIS BEFORE USING THE UNLOCKER ⚠️
> 
> **The only officially supported firmwares in the unlock tool are CrossPoint and CrossInk.**
> 
> Flashing any other firmware on a USB-locked device may **permanently brick the device** or leave it **permanently
> stuck on that firmware with no recovery path**. Once USB flashing is re-locked, your only way back is via OTA, and if
> the firmware you flashed doesn't support OTA, **there is no way out**.

## Install firmware

### Web installer (recommended)

1. Connect your device to your computer via USB-C and wake/unlock the device
2. Go to https://crosspointreader.com/#flash-tools, select your device (X3, X4, Xteink X4Pro, Seeed reTerminal Sticky, or M5PaperMono), and choose an official CrossPoint release.

### Web installer (specific version)

1. Connect your device to your computer via USB-C and wake/unlock the device
2. Download the firmware file for your device from [Releases](https://github.com/crosspoint-reader/crosspoint-reader/releases), or compile yourself.
3. Go to https://crosspointreader.com/#flash-tools, select your device, click "Custom .bin" and upload the firmware file.

### Revert to Official Firmware

To revert to the official firmware, you can also flash the latest official firmware using https://crosspointreader.com/#flash-tools.

### Command line

1. Install [`esptool`](https://github.com/espressif/esptool):

```bash
pip install esptool
```

2. Download the firmware file for your device from the [releases page](https://github.com/crosspoint-reader/crosspoint-reader/releases).
3. Connect your device via USB-C.
4. Find the device port. On Linux, run `dmesg` after connecting. On macOS:

```bash
log stream --predicate 'subsystem == "com.apple.iokit"' --info
```

5. Flash an X3 or X4:

```bash
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

   Flash an Xteink X4Pro, Seeed reTerminal Sticky, or M5PaperMono:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

### Manual

See [Development quick start](#development-quick-start) below.

---

## Custom SD-card fonts

On devices with external RAM enabled in CrossPoint, copy `.ttf`, `.otf`, or `.ttc` files to the SD card and select them as reader fonts. Put one file in `/fonts/` or `/.fonts/`, or put one family's files in a subfolder. See the [SD card font guide](./docs/sd-card-fonts.md) for the folder layout and styles.

On other devices, convert the font to `.cpfont` first. `.cpfont` files also work on devices with external RAM enabled and have better performance. No firmware reflash is needed to add fonts.

To make `.cpfont` files:

1. Go to https://crosspointreader.com/fonts and open the "SD-card font builder" form.
2. Upload up to four styles (regular, bold, italic, bold-italic), set the family name, point sizes, and Unicode range.
3. Download the generated `.cpfont` files.
4. Copy them to your SD card under `/fonts/YourFont/` (or `/.fonts/YourFont/` to hide the folder).
5. Select the font on the device from the font settings.

Conversion runs the firmware repo's `lib/EpdFont/scripts/fontconvert_sdcard.py` script unmodified, so output matches a local host build.

---

## Documentation

- [User Guide](./USER_GUIDE.md)
- [Web server usage](./docs/webserver.md)
- [Web server endpoints](./docs/webserver-endpoints.md)
- [Project scope](./SCOPE.md)
- [Contributing docs](./docs/contributing/README.md)
- [Touch and UI development](./docs/contributing/touch-and-ui.md) - how to build new screens on the FreeInkUI activity bases (UiListActivity and friends), plus build envs for the non-Xteink touch devices

---

## Development quick start

### Prerequisites

- [pioarduino PlatformIO Core](https://github.com/pioarduino/platformio-core) or [VS Code + pioarduino IDE](https://github.com/pioarduino/pioarduino-vscode-ide)
- Python 3.8+
- `clang-format` 21
- USB-C cable supporting data transfer

### Setup

```bash
git clone --recursive https://github.com/crosspoint-reader/crosspoint-reader
cd crosspoint-reader

# if cloned without --recursive:
git submodule update --init --recursive
```

### Nix/NixOS

Nix/NixOS users can enter the development shell with either `nix develop` (flakes) or `nix-shell`:

```bash
nix develop -f nix
# or
nix-shell nix
```

To flash a connected ESP32-C3 device, enable PlatformIO's udev rules in your NixOS configuration:

```nix
services.udev.packages = with pkgs; [ platformio-core.udev ];
```

After rebuilding the system configuration, reconnect the device or reload udev rules.

### Build / flash / monitor

```bash
pio run --target upload
```

### Contributor pre-PR checks

```bash
./bin/clang-format-fix
pio check -e default
pio run -e default
```

### Debugging

After flashing the new features, it’s recommended to capture detailed logs from the serial port.

First, make sure all required Python packages are installed:

```python
python3 -m pip install pyserial colorama matplotlib
```

After that run the script:

```sh
# For Linux
# This was tested on Debian and should work on most Linux systems.
python3 scripts/debugging_monitor.py

# For macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

Minor adjustments may be required for Windows.

---

## Internals

CrossPoint Reader is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the
cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:

```text
.crosspoint/
├── epub_<hash>/         # one directory per book, named by content hash
│   ├── progress.bin     # reading position (chapter, page, etc.)
│   ├── cover.bmp        # generated cover image
│   ├── book.bin         # metadata: title, author, spine, TOC
│   ├── css_rules.cache  # parsed CSS rule cache
│   ├── img_*            # rendered image cache files
│   └── sections/        # per-chapter layout cache
│       ├── 0.bin
│       ├── 1.bin
│       └── ...
├── settings.json        # device settings
├── state.json           # resume/runtime state
└── recent.json          # recent books list
```

Removing `/.crosspoint` clears all cached metadata and forces a full regeneration on next open. Book deletes, overwrites, and moves done through the firmware or web UI clear or re-key matching caches; manual SD-card edits may leave stale cache directories behind.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

---

## Contributing

Contributions are welcome. If you're new to the codebase, start with the [contributing docs](./docs/contributing/README.md). For things to work on, check the [ideas discussion board](https://github.com/crosspoint-reader/crosspoint-reader/discussions/categories/ideas) — leave a comment before starting so we don't duplicate effort.

Everyone here is a volunteer, so please be respectful and patient. For governance and community expectations, see [GOVERNANCE.md](./GOVERNANCE.md).

---

## Community forks

One of the best things about open source is that anyone can take the code in a different direction. If you need something outside CrossPoint's [scope](./SCOPE.md), check out the community forks:

- [CrossInk](https://github.com/uxjulia/CrossInk) — UX focused with minimal reading stats and broader customizations for the reading experience.

- [papyrix-reader](https://github.com/bigbag/papyrix-reader) — Adds FB2 and MD format support. Actively maintained with Arabic script support. Custom themes.

- [inx](https://github.com/obijuankenobiii/inx) — Completely reimagines the user interface with tabbed navigation.

- [Witch(hunt) Reader](https://github.com/jpirnay/witchhunt-reader) — More faithful CSS styling and background work for slightly snappier interaction. Weather information panel. Markdown support.

**Note:** Many of these features will make their way into CrossPoint over time. Each project chooses its own priorities and tradeoffs.

Want to build your own device? Be sure to check out the [de-link](https://github.com/iandchasse/de-link) project or [OnePage Reader](https://github.com/MoveCall/onepage-reader).

---

CrossPoint Reader is **not affiliated with Xteink or any device manufacturer**.
