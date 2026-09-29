# Hardware: PiSugar Whisplay HAT

- Source: <https://docs.pisugar.com/docs/product-wiki/whisplay/overview>
- Driver repo: <https://github.com/PiSugar/Whisplay>

The Whisplay HAT is a multifunctional expansion board for Raspberry Pi Zero. It integrates a display, microphones, speaker, button and LEDs, turning the Pi into a portable interactive device.

## Specifications

| Item | Value |
|---|---|
| Power supply voltage | 5V |
| Logic voltage | 3.3V |
| Audio codec chip | ES8389 |
| Audio control interface | I2C |
| Audio data interface | I2S |
| Speaker | 8Ω 1W |
| Screen size | 1.69 inch (30mm × 37mm) |
| Screen resolution | 240 × 280, RGB |
| Screen controller chip | ST7789 |
| Screen interface | 4-wire SPI |

## Pinout

Physical header pin numbers (40-pin header) as given by the wiki. The BCM column is **not** in the wiki. It is derived from the standard Raspberry Pi 40-pin header layout, so verify it against the schematic before relying on it.

| Function | Physical pin | BCM GPIO (derived) | Notes |
|---|---|---|---|
| 5V | P2, P4 | — | Power |
| GND | GND | — | |
| I2CDAT (SDA) | P3 | GPIO2 | Audio codec control |
| I2CCLK (SCL) | P5 | GPIO3 | Audio codec control |
| SPI_RST | P7 | GPIO4 | LCD reset |
| KEY (button) | P11 | GPIO17 | Physical button |
| SPI_DC | P13 | GPIO27 | LCD data/command |
| LCDCTRL | P15 | GPIO22 | LCD control (likely backlight) |
| LED3 | P16 | GPIO23 | |
| LED2 | P18 | GPIO24 | |
| SPI_MOSI | P19 | GPIO10 | LCD data |
| LED1 | P22 | GPIO25 | |
| SPI_CLK | P23 | GPIO11 | LCD clock |
| SPI_CS | P24 | GPIO8 (CE0) | LCD chip select |
| I2SBCLK | P12 | GPIO18 | I2S bit clock |
| I2SWS (LRCLK) | P35 | GPIO19 | I2S word select |
| I2SDIN | P38 | GPIO20 | I2S data in (microphones) |
| I2SDOUT | P40 | GPIO21 | I2S data out (speaker) |

I2SBCLK on P12 is confirmed by the user; the wiki itself does not list it.

## Supported platforms

- Raspberry Pi boards with a 40-pin header, running official Raspberry Pi OS
- Radxa ZERO 3W (RK3566, Debian 12 Bookworm)
- Radxa Cubie A7Z (Allwinner A733, Debian 11 Bullseye). The physical button is unsafe on this board because of a circuit incompatibility.

## External speaker

The board accepts an external speaker on a PH2.0 connector. It must be a single-channel (mono) speaker.

## Driver setup (Raspberry Pi)

Use the official Raspberry Pi OS. The driver supports both the ES8389 and WM8960 codecs; this board uses the ES8389.

### Install

The unified installer detects the platform automatically:

```bash
git clone https://github.com/PiSugar/Whisplay.git --depth 1
cd Whisplay
sudo bash install_driver.sh
sudo reboot
```

To install for Raspberry Pi only, run `script/install_raspberry_pi.sh` instead.

What the installer does:

- Enables the I2C bus, the I2S bus (audio codec) and the SPI bus (LCD).
- Uses the bundled sound card driver in `audio/whisplay-soundcard/`.
- On a Pi whose HAT EEPROM is programmed, the `whisplay-soundcard` overlay is loaded from the EEPROM. The installer removes any legacy manual `dtoverlay=whisplay-soundcard` line from the config.

### Verify audio

```bash
aplay /usr/share/sounds/alsa/Front_Center.wav   # playback
arecord -d 5 test.wav                            # record 5 s
alsamixer                                        # adjust levels
```

### Test LCD, button and LED

```bash
cd example
pip install -r requirements.txt --break-system-packages
bash run_test.sh
```

Python entry point: `from runtime.whisplay import Whisplay` (see `runtime/whisplay.py` for the LCD, button and LED helper classes and `example/test.py` for a quick test).

The daemon `whisplay_daemon.py` can manage these components centrally and exposes a Unix socket API at `/tmp/whisplay-daemon.sock`.

### Troubleshooting

```bash
journalctl -u whisplay-daemon.service -f   # daemon logs
tail -f ~/.whisplay-daemon/daemon-app.log  # apps with use_daemon_default_log: true
```

## Memory optimization (Pi Zero 2 W, 512 MB)

Goal: keep used memory (`free -m`, "used" column, not buff/cache) under 100 MB. Page cache is reclaimable and does not count. The number to watch besides "used" is "available".

System: Raspberry Pi OS Lite 64-bit, Debian 13 (trixie), kernel 6.18.50+rpt-rpi-v8, fresh image flashed with Raspberry Pi Imager.

### Applied

Back up first:

```bash
sudo cp /boot/firmware/config.txt  /boot/firmware/config.txt.bak
sudo cp /boot/firmware/cmdline.txt /boot/firmware/cmdline.txt.bak
```

1. Disable services that are not needed (units that do not exist are skipped):

   ```bash
   sudo systemctl disable --now bluetooth avahi-daemon avahi-daemon.socket udisks2 cron \
     rpi-eeprom-update apt-daily.timer apt-daily-upgrade.timer man-db.timer \
     dpkg-db-backup.timer e2scrub_all.timer NetworkManager-wait-online \
     getty@tty1 serial-getty@ttyS0 keyboard-setup console-setup
   ```

   On the trixie image `ModemManager`, `hciuart` and `triggerhappy` are not installed. On the older bookworm image also disable `ModemManager`, `hciuart`, `triggerhappy` and `triggerhappy.socket`.

2. Keep the journal in RAM with a 4 MB cap, in `/etc/systemd/journald.conf.d/lite.conf`:

   ```
   [Journal]
   Storage=volatile
   RuntimeMaxUse=4M
   ```

3. Edit `/boot/firmware/config.txt`:

   - Comment out `dtoverlay=vc4-kms-v3d` (no HDMI output after this; the LCD is on SPI).
   - `camera_auto_detect=0`, `display_auto_detect=0`.
   - `dtparam=audio=off` (turns off the on-board audio, so the HAT's ES8389 is the only sound card).
   - Add at the end, under `[all]`: `gpu_mem=16` and `dtoverlay=disable-bt`.

4. In `/boot/firmware/cmdline.txt`, remove `console=tty1` (leave the other options untouched). Then reboot.

### Result (measured)

| State | Total | Used | Buff/cache | Available |
|---|---|---|---|---|
| Fresh trixie image | 415 MB | 150 MB | 211 MB | 264 MB |
| After steps 1 to 4 (after reboot) | 462 MB | **144 MB** | 128 MB | 318 MB |
| Later, with build tools, LVGL and DRM modules loaded | 462 MB | 148 MB | 294 MB | 313 MB |

- Total went up by 47 MB because `gpu_mem=16` and the smaller CMA pool (256 MB to 64 MB) return memory to Linux.
- "Used" only dropped by 6 MB: the services that were disabled were small, and some were not present on the trixie image at all. **The 100 MB goal is not reached.**
- No failed services after the change. Swap is zram (about 460 MB) and is used a little under memory pressure.

The largest processes after optimization (RSS): NetworkManager about 20 MB, the two `systemd` instances about 26 MB together (one is the SSH login session's `systemd --user`), `wpa_supplicant` 12 MB, `systemd-udevd` 10 MB, `systemd-journald` 8 MB, `sshd` sessions 7 to 10 MB each.

### Not done yet (estimates, not verified)

| Option | Expected saving | Risk |
|---|---|---|
| Replace NetworkManager with `systemd-networkd` + `wpa_supplicant` | about 15 to 20 MB | Wrong Wi-Fi config cuts the remote connection. Write and test the config before switching. |
| Stop `systemd-timesyncd` | a few MB | Clock no longer synced. |
| Keep SSH sessions few and short (each `sshd` plus `systemd --user` is about 15 to 20 MB) | about 12 MB | None. |

Together these would put used memory around 100 to 110 MB, so getting strictly under 100 MB is tight on this system. Stopping `systemd-logind` or `systemd-udevd` would save more but breaks login sessions and device handling, so it is not recommended.

Also: the LCD program itself (LVGL + spidev, see [DISPLAY.md](DISPLAY.md)) needs memory on top of this. Measured with the Coney demo running: RSS about 2.5 MB with the status bar (2.3 MB without), while the whole system was at about 144 MB used and 318 MB available.

## Other resources (via PiSugar CDN, linked from the wiki)

- ES8389 schematic and technical manual
- 3D model (.step)
- Screen technical manual and source code
