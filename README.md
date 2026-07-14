# CrossInk Carousel — BookFusion Edition

> CrossInk Carousel is a personal fork of CrossInk that adds a book-cover-centric UI inspired by the Lua fork CrossPoint Flow. As it stands, I always judge a book by its cover. I deserve to see the fruits of my good taste every time I open my X4. The goal is to make book covers the first priority of the navigation experience: on the home screen, in the recent books browser, and on the reading stats screen.

**ONLY TESTED ON XTEINK X4. NOT YET COMPATIBLE WITH X3 MODEL. BACKUP YOUR DEVICE BEFORE FLASHING.**

---

## What's different from CrossInk

### Flow Theme

A new selectable UI theme called "Flow", inheriting from Lyra and overriding only the home-screen book selector with an iPod-style perspective carousel. The currently selected book renders centered at full size, flanked by partial side covers drawn with a 3D fan perspective transform. Up to seven recent books cycle through the carousel. Visual concept ported from CrossPoint Flow.

**Switchable carousel size.** Settings → Display → Carousel Size lets you pick between the original 5-cover layout and a larger 3-cover layout with bigger center and side covers.

### BookFusion Sync

**Native BookFusion integration.** This fork adds first-class BookFusion sync support to CrossInk Carousel. Link your BookFusion account from Settings → BookFusion Sync, and your library, reading progress, and book downloads stay in sync automatically.

**Library browser in File Transfer.** Once your account is linked, a "Bookfusion" entry appears directly in the File Transfer menu — the same place you go to connect Calibre or join a network. Browse and download books without digging through Settings.

**QR auth support.** Log into your BookFusion account by scanning a QR code — no typing required on the device. The auth screen shows both the verification URL as a scannable QR code and the pairing code to enter on your phone.

**Download progress.** Monitor book downloads with a live progress bar.

**Download streaming fix.** BookFusion pre-signed S3 download URLs regularly exceed 2,000 characters, so the library search and download-URL endpoints stream-parse directly from the TLS socket instead of buffering the full JSON response. `WiFiClientSecure::read()` returns `-1` between TLS records even while the connection is still open, which ArduinoJson's stream reader was misreading as end-of-input and used to cause intermittent JSON parse errors — a blocking stream wrapper now waits for real data (or a real close) before handing bytes to the parser, fixing the failures for good.

**OTA updates from this fork.** The "Check for Updates" feature in Settings now checks [this fork's releases page](https://github.com/tardigradegit/CrossInk-Carousel-Bookfusion/releases) so your device will always receive BookFusion-enabled firmware.

> See [BOOKFUSION_INTEGRATION.md](https://github.com/tardigradegit/CrossInk-Carousel-Bookfusion/blob/main/BOOKFUSION_INTEGRATION.md) for full setup instructions.

### Recent Books Grid

Replaced the plain list of book titles with a 3x3 grid of cover thumbnails, paginated when more than nine books exist. Page indicator dots sit at the bottom. Thumbnails are generated on demand the first time a page is viewed — loading will be much faster on subsequent views. Visual concept ported from CrossPoint Flow.

The Books folder in the File Browser has the same 3x3 cover-grid view, toggled with a long-press on HOME. Covers generate from a lightweight OPF-only parse, so grid thumbnails appear even for books you've never opened yet.

### Reading Stats Redesign

Reworked the Reading Stats screen into a multi-book browseable layout. The current book's cover renders prominently at the top. Below the cover sits the book title and stats for the current book, followed by stats for all books. Cycle through every recent book that has stats, with peek covers at the screen edges showing the previous and next books, each marked by a chevron. Confirm opens the currently displayed book in the reader.

---

## Additional Features

- **New reader fonts:** ChareInk, Lexend Deca, and Bitter
- **Unicode emoji and miscellaneous symbols support** (a limited subset)
- **Adjusted font sizes:** Teensy (8pt), Tiny (10pt), Small (12pt), Medium (14pt), Large (16pt), Extra Large (18pt), Huge (20pt)
- **Strikethrough support**
- **Thicker underlines** for better visibility
- **`<hr>` section break support**
- **"Redaction" style rendering** support
- **Improved table support** with simple markup
- **Bookmarks** — add and manage bookmarks
- **Front button remapping** that only applies in the reader
- **Bionic Reading** and **Guide Dots** as optional reader modes
- **Force Paragraph Indents** for books that render as one giant wall of text
- **Pinned sleep image** — long-press the menu button in the file browser to favorite an image. It displays whenever your sleep setting is set to Custom or Cover + Custom (when no cover is available)
- **Extended in-reader control remapping** for side buttons, short power button clicks, and long-press menu actions
- **Mark as Finished** — mark a book as finished from the in-book menu. A pop-up also appears once 99% of the book is reached. Tracks total books read.
- **Move finished books to "Read" folder** — Settings > System > Move finished books to Read folder
- **In-book reader options menu** — adjust font, size, line spacing, margins, alignment, etc. without leaving the book
- **Customizable Auto Page Turn Interval** (5-120 seconds)
- **Recent Books 3x3 grid view**
- **Device simulator** for development
- **Vietnamese language support**

---

## Reader Fonts

The default fonts have been replaced with ChareInk, Lexend Deca, and Bitter — chosen specifically to improve reading fluency and e-ink performance. These typefaces feature uniform stroke weights and open geometries, allowing the X4 to render crisp, high-contrast text while significantly reducing ghosting and artifacts.

- **ChareInk** — A cult favorite among the e-reading community for over a decade, based on the Charis typeface. Specially designed for long-form reading.
- **Lexend Deca** — A research-backed sans-serif designed to improve reading fluency, engineered around the theory that reading difficulties are often a design problem (visual crowding) rather than a cognitive one.
- **Bitter** — A contemporary slab serif for text, specially designed for comfortable reading on digital screens. The consistent stroke weight renders particularly well on e-ink. The medium weight was chosen for improved rendering on the X4.

The UI now uses **Inter** as the display font for improved readability at smaller sizes.

### Emoji & Misc. Glyphs

Support for a limited set of Unicode Emoticons and Miscellaneous Symbols using Noto Emoji and Noto Sans Symbols.

---

## Build Variants

There are 3 build variants due to build size constraints: `tiny`, `xlarge`, and `no_emoji`.

### tiny
*My preferred build.*
- Emoji & Misc. Symbols support
- Font sizes: Teensy (8pt), Tiny (10pt), Small (12pt), Medium (14pt), Large (16pt)

### xlarge
- Emoji & Misc. Symbols support
- Font sizes: Medium (14pt), Large (16pt), Extra Large (18pt)
- *(Teensy, Tiny, and Small removed to fit emoji within build size)*

### no_emoji
- No Emoji & Misc. Symbols support
- Font sizes: Tiny (10pt), Small (12pt), Medium (14pt), Large (16pt), Extra Large (18pt)

---

## Custom Button Actions

The Controls menu in Settings has been updated with expanded remapping options:

| Control | Options |
|---|---|
| Power Button Short-press | New options added |
| Power Button Long-press | New |
| Front Buttons | Remap (global) / Remap (reader only) |
| Long-press Menu Action | New |
| Side Buttons | Layout / Long-press Chapter Skip / Long-press Action |

**Side Button Long Press Action** — Use the side buttons to change font size. Hold ~2 seconds: Up = increase, Down = decrease. Default = Chapter Skip.

Available actions for Power / Menu button short/long-press:

`Ignore` `Sleep` `Page Turn` `Refresh Screen` `Change Font` `Guide Dots` `Bionic Reading` `Toggle Bookmark` `Sync Progress` `Mark as Finished` `Reading Stats` `Take Screenshot` `Auto Page Turn` `File Transfer` `Tilt Page Turn`

---

## Reading Stats

Per-book stats are tracked automatically and displayed in two places:

**In-book menu -> Reading Stats:**
- Total reading time
- Number of sessions
- Pages turned
- Average session time
- All-time stats including total books read

**Home screen book card (Lyra theme only):**
- Total reading time
- Average session time

**Finished books / Read folder:**
- Mark a book as finished from the in-book menu
- A pop-up appears at 99% progress
- Enable "Move finished books to Read folder" in Settings > System to auto-sort
- Marking books as finished enables the total "Books Read" stat

---

## Installing

### Web (recommended)

1. Download the `carousel-firmware-*.bin` file for your preferred build variant from the [releases page](https://github.com/tardigradegit/CrossInk-Carousel-Bookfusion/releases)
2. Connect your Xteink X4 via USB-C and wake/unlock the device
3. Go to https://crosspointreader.com/#flash-tools and choose your device
4. Select **Custom .bin** from the options
5. Choose the `.bin` file you downloaded and click **Flash**

To revert to official firmware, flash from https://crosspointreader.com/#flash-tools.

### Command Line (macOS / Linux)

1. Install esptool:
   ```
   pip3 install esptool
   ```

2. Download the `carousel-firmware-*.bin` from the [releases page](https://github.com/tardigradegit/CrossInk-Carousel-Bookfusion/releases)

3. Connect your Xteink X4 via USB-C and note the device port:
   - **Linux:** `dmesg | grep tty` after connecting
   - **macOS:** `ls /dev/cu.*` before and after — the new entry is your device

4. Flash:
   ```bash
   # Linux
   esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/carousel-firmware-tiny-v1.4.2.bin

   # macOS
   esptool.py --chip esp32c3 --port /dev/cu.usbmodem2101 --baud 921600 write_flash 0x10000 /path/to/carousel-firmware-tiny-v1.4.2.bin
   ```
   *(Swap the filename for your chosen variant and version.)*

> Windows users should use the Web installer instead.

---

## Development

### Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/index.html) (`pio`) or VS Code + PlatformIO IDE
- Python 3.8+
- USB-C cable for flashing the ESP32-C3
- Xteink X4

### Checking out the code

```bash
git clone --recursive https://github.com/tardigradegit/CrossInk-Carousel-Bookfusion

# Or, if you've already cloned without --recursive:
git submodule update --init --recursive
```

### Flashing your device

```bash
# Replace tiny with xlarge or no_emoji for a different build variant
pio run -e tiny --target upload
```

### Device Simulator

A simulator renders the e-ink display in an SDL2 window for development without flashing every time.

**Platform support:** Currently configured for macOS (Apple Silicon). Intel Mac users need to remove `-arch arm64` and adjust Homebrew paths to `/usr/local`. Linux requires the same path changes plus a replacement for `lib/simulator_mock/src/MD5Builder.h`. Windows is not natively supported — use WSL and follow the Linux instructions.

**Prerequisites:** SDL2 must be installed.

```bash
# macOS
brew install sdl2

# Linux (Debian/Ubuntu)
sudo apt install libsdl2-dev
```

**Setup:** Place EPUB books in `./fs_/books/` (maps to `/books/` on the device SD card).

**Build and run:**
```bash
pio run -e simulator
.pio/build/simulator/program
```

**Keyboard controls:**

| Key | Action |
|---|---|
| Up / Down | Page back / forward (side buttons) |
| Left / Right | Left / right front buttons |
| Return | Confirm / Select |
| Escape | Back |
| P | Power |

> On first open of an ebook, an "Indexing..." popup appears while the section cache builds in `.crosspoint/`. Delete `./fs_/.crosspoint/` to clear stale caches after a code change.

### Debugging

```bash
python3 -m pip install pyserial colorama matplotlib

# Linux
python3 scripts/debugging_monitor.py

# macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

---

## Internals

The firmware aggressively caches data to the SD card to minimise RAM usage. The ESP32-C3 has ~380KB of usable RAM, so memory management is critical.

### Data Caching

```
.crosspoint/
├── epub_12471232/
│   ├── progress.bin
│   ├── stats.bin
│   ├── cover.bmp
│   ├── book.bin
│   └── sections/
│       ├── 0.bin
│       ├── 1.bin
│       └── ...
└── epub_189013891/
```

Deleting `.crosspoint/` clears the entire cache. The cache is not automatically cleared when a book is deleted, and moving a book file creates a new cache directory, resetting reading progress.
