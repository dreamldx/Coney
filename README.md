# Coney

LVGL UI for a Raspberry Pi Zero 2 W with the PiSugar Whisplay HAT (ST7789, 240×280, SPI).

The display goes through LVGL's `lv_st7789` driver on top of spidev, not through the kernel `panel-mipi-dbi` (DRM) driver. On this board that measured about 82 to 86 fps for full-screen updates against 43 to 46 fps for the kernel path, and small updates are much cheaper. The measurements and the reason are in [`docs/DISPLAY.md`](docs/DISPLAY.md).

Current state: a demo (title, an animated bar and a system status bar) that runs as a systemd service. The display output has been checked on the device.

## Quick start

On the Pi, after [setting it up](#set-up-the-pi):

```bash
git clone --recurse-submodules https://github.com/dreamldx/Coney.git
cd Coney
make deploy      # build, install the systemd unit, enable it and start it
```

To just try it without installing anything: `make && sudo ./build/coney`.

## Repository layout

| Path | Purpose |
|---|---|
| `src/whisplay_display.[ch]` | Whisplay display driver: spidev for SPI, `/dev/gpiomem` for DC, reset and backlight |
| `src/main.c` | Demo: title, an animated bar and the status bar |
| `src/sysinfo.[ch]` | Reads CPU load, memory use, SoC temperature and the IP address from `/proc` and `/sys` |
| `lv_conf.h` | Minimal LVGL configuration |
| `third_party/lvgl` | LVGL v9.6.0, git submodule |
| `Makefile` | `make`, `make deploy`, `make undeploy`: wrapper around CMake and the Pi deployment |
| `deploy/pi/coney.service.in` | systemd unit template, rendered by `make deploy` |
| `CONTRIB.md`, `AGENTS.md` | Coding requirements and commit rules, for people and for coding agents |
| `docs/DISPLAY.md` | Why this display path, measurements, panel initialization (Chinese) |
| `docs/HARDWARE.md` | Whisplay HAT pinout and Pi system notes, including memory |
| `docs/examples/` | The benchmark program behind the numbers in `DISPLAY.md` |

## Pins (BCM)

| Signal | GPIO | Header pin |
|---|---|---|
| Reset | 4 | P7 |
| Data/command | 27 | P13 |
| Backlight (active low) | 22 | P15 |
| SPI0 MOSI, SCLK, CE0 | 10, 11, 8 | P19, P23, P24 |

## Status bar

Drawn with the 8 px Montserrat font and refreshed once a second.

| Position | Content | Source |
|---|---|---|
| Top, left | `CPU 42%` | `/proc/stat`, difference between two readings (the first one shows 0%) |
| Top, middle | `MEM 34%` | `/proc/meminfo`, (`MemTotal` - `MemAvailable`) / `MemTotal` |
| Top, right | `53.2C` | `/sys/class/thermal/thermal_zone0/temp`, `--C` if it cannot be read |
| Bottom, centered | `192.168.86.204` | First IPv4 address that is not loopback or link-local (169.254.x.x), `N/A` if there is none |

Positions are set in `build_status_bar()` in `src/main.c`. The panel corners are rounded, so the text stays about 4 to 14 px away from the edges. `LV_FONT_SOURCE_HAN_SANS_SC_14_CJK` is enabled in `lv_conf.h` for CJK text; the demo uses it for the "你好" label under the animated bar.

Things to know when changing it:

- `lv_label_set_text_fmt` does not support `%f` here (`LV_USE_FLOAT` is off), it prints a literal `f`. The temperature is therefore formatted from integers.
- A label with a fixed width wraps its text when the text is wider. "MEM" did not fit in 20 px at this size, so the name labels use `LV_SIZE_CONTENT`.
- Adding a font to `lv_conf.h` rebuilds all of LVGL, about 6 minutes on the Pi. Layout changes in `src/` rebuild in seconds.

## Set up the Pi

1. Enable SPI in `/boot/firmware/config.txt`. Do not load a display overlay such as `mipi-dbi-spi`, it takes the same SPI chip select.

   ```
   dtparam=spi=on
   ```

2. Raise spidev's per-transfer limit from 4 KB to 64 KB: append `spidev.bufsiz=65536` to the single line in `/boot/firmware/cmdline.txt` (keep it one line).

   - A full frame (134,400 bytes) then takes 3 SPI transfers instead of 33.
   - It has to be a kernel parameter. spidev is loaded early at boot because of the device tree node, so `options spidev bufsiz=...` in `/etc/modprobe.d` is not read.
   - It is optional: without it the program falls back to 4 KB transfers and full-screen updates are a little slower.

3. Reboot, then check:

   ```bash
   ls /dev/spidev0.0
   cat /sys/module/spidev/parameters/bufsiz     # 65536
   ```

4. Install the build tools: `sudo apt install cmake gcc make`.

## Build and run

```bash
cmake -S . -B build
cmake --build build -j3      # or: make
sudo ./build/coney
```

Cloned without `--recurse-submodules`: `git submodule update --init --depth 1`.

The first build compiles LVGL and takes about 6 minutes on a Zero 2 W (3 jobs). Changing `lv_conf.h` rebuilds all of LVGL; changing only files in `src/` takes seconds.

`Ctrl+C` turns the backlight off and exits. It is run with `sudo` because that is how it was tested; whether `/dev/gpiomem` and `/dev/spidev0.0` are usable by a normal user (the `gpio` and `spi` groups) has not been checked.

## Run at boot (systemd)

```bash
make deploy
```

This builds, renders `deploy/pi/coney.service.in` into `build/deploy/coney.service`, creates the run directory `/opt/coney/prod`, installs the unit as `/etc/systemd/system/coney.service`, reloads systemd, enables the unit and restarts it (needs `sudo`). The run directory has to exist before the first start, because systemd changes into the unit's working directory before it runs any `ExecStartPre`.

On every start the unit, running as root, first copies `build/coney` to `/opt/coney/prod/coney` and then runs that copy. Rebuilding therefore never touches the running binary; the new build is picked up on the next start (`sudo systemctl restart coney`, `make deploy`, or a reboot). If `build/coney` is missing, the copy already in prod is used.

Override the run directory with `make deploy PROD_DIR=/some/path`. Template placeholders are `@BUILD_BIN@` and `@PROD_DIR@`; add more `sed` substitutions in the `Makefile` if a new template needs them.

```bash
systemctl status coney            # state
journalctl -u coney -f            # logs
sudo systemctl stop coney         # free the display, e.g. before running a benchmark
make undeploy                     # disable and remove the unit
```

The service holds `/dev/spidev0.0` and the LCD pins, so stop it before running another program that drives the display. `make undeploy` leaves `/opt/coney/prod` in place.

## Notes

- Orientation: MADCTL `0xC0` (mirror X and Y) and a Y offset of 20 lines for the rounded corners.
- The panel has no tearing-effect (TE) line on the header, so there is no vsync. Some tearing during fast motion is expected.
- Render mode is partial with a 70-line buffer. Buffer height barely changes full-screen speed (28 to 280 lines gave 79 to 86 fps).
- Memory: the demo with the status bar uses about 2.5 MB resident. LVGL's memory pool is 256 KB (`LV_MEM_SIZE`), plus the 33 KB render buffer. CPU use is about 1.5%.
