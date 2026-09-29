# AGENTS.md

Instructions for coding agents working in this repository. [CONTRIB.md](CONTRIB.md) has the same rules for people, and [README.md](README.md) describes the project.

## Project

Coney: C11 program, LVGL v9.6.0 (git submodule in `third_party/lvgl`), draws on the Whisplay HAT (ST7789, 240x280) through spidev. It runs as the systemd service `coney` on a Raspberry Pi Zero 2 W.

## Working with the Pi

- The program only runs on the Pi, so build and run there. Keep a copy of the sources on the Pi (for example `~/Coney`) and sync changed files to it before building. Sync only what changed; `third_party/lvgl` is large.
- Changing only `src/` rebuilds in seconds. Changing `lv_conf.h` rebuilds all of LVGL (about 6 minutes), so batch such changes.
- A running `coney` service holds the display and `/dev/spidev0.0`. After a build, `sudo systemctl restart coney` picks up the new binary.
- You cannot see the screen. To check the layout, render the UI into a memory buffer with a display that has a flush callback and no hardware, and look at the picture.
- Do not put passwords or other secrets in the repository, in commit messages or in documentation.

## Coding requirements

These are taken from the existing code. New code should look like it was written by the same person.

Language and build

- C11 (`CMAKE_C_STANDARD 11`), built with `-Wall -Wextra`. The build has no warnings, keep it that way.
- Source files, comments, identifiers, error messages and commit messages are in English. `docs/DISPLAY.md` is the one Chinese document. The only Chinese in the code is text that is shown on the screen.

Formatting

- 4 spaces for indentation, no tabs in C files. The `Makefile` uses tabs, as it must.
- A function's opening brace goes on its own line. Braces of `if`, `for`, `while` and `switch` stay on the same line. A short single statement after `if` may go without braces (`if (!disp) return 1;`).
- Include guards are `FILE_NAME_H`.
- `snake_case` for everything. Public functions carry the module prefix (`whisplay_`, `sysinfo_`). Everything used in one file only is `static`.

Structure

- Hardware access (SPI, GPIO) stays in `src/whisplay_display.c`. `src/sysinfo.c` only reads `/proc`, `/sys` and network interfaces and does not depend on LVGL. `src/main.c` builds the UI.
- File-local state that has to be shared between callbacks goes into one `static struct`, as in `whisplay_display.c`.
- Errors: print to stderr with a module prefix (`perror("whisplay: spidev")`), release resources through a single `fail:` label, return `NULL` or `false`. Reading `/proc` and `/sys` must not crash when a file is missing.

Comments

- Block comments `/* ... */`. Comment why, not what: a hardware quirk, a measured number, a limit of a library. Keep them short and leave obvious code without a comment.
- A comment that cites a measurement or a limit points to `docs/`, or says where the number comes from.

LVGL

- `lv_label_set_text_fmt` has no `%f` here (`LV_USE_FLOAT` is off), it prints a literal `f`. Format floats from integers.
- A label with a fixed width wraps text that is wider. Use `LV_SIZE_CONTENT` when the text length varies.
- Only fonts enabled in `lv_conf.h` can be used. Enabling one rebuilds all of LVGL (about 6 minutes on the Pi), so add all the fonts you need in one change.
- The 8 px font has no CJK glyphs. Use `lv_font_source_han_sans_sc_14_cjk` for Chinese.
- The panel corners are rounded. Keep text a few pixels away from the edges.

Documentation

- Change the README and `docs/` in the same commit as the behavior they describe. Numbers in the docs come from measurements on the device, not from estimates.

## Commits

- Write commit messages in English.
- Subject line: imperative mood, no trailing period, about 72 characters at most (`Add make deploy and a systemd unit`).
- Body (optional, after a blank line): say what changed and why, wrapped at about 72 columns.
- One logical change per commit. A change to code and the documentation for it belong together.
- Commits made with Claude end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Never commit `.env` (it holds the SSH password for the Pi) or `build/`. Both are in `.gitignore`.
