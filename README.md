# ratslap-g305x

A tiny, no-bloat CLI that configures the **Logitech G305 X LIGHTSPEED**
mouse on Linux: button bindings, DPI levels, report rate, profile
activation, and on-board memory backup/restore.

One C99 binary, zero dependencies (only glibc). No GUI, no daemon, no
libratbag. Settings are written into the mouse's **on-board memory**, so
they keep working on any machine, with no software running.

The CLI is modeled on the classic [`krayon/ratslap`][ratslap] (the
single-binary GPL tool for the G300s); the protocol layer follows
[`sabam54/OpenGMice`][opengmice] (GPL-3.0), the only open implementation
verified against a real format-7 G305 X. This is **not** the old G300
register protocol — it is HID++ 2.0 with the on-board profiles feature
(`0x8100`), profile format 7.

[ratslap]: https://github.com/krayon/ratslap
[opengmice]: https://github.com/sabam54/OpenGMice

## Commands

```
ratslap-g305x                 # print the active profile (default, no args)
ratslap-g305x list            # scan /dev/hidraw* for compatible mice
ratslap-g305x info            # device, battery, all profiles

ratslap-g305x backup FILE     # save all on-board memory sectors (text file)
ratslap-g305x restore FILE    # write a backup back to the mouse

ratslap-g305x button N BINDING    # set button N (1-6) of the active profile
ratslap-g305x gbutton N BINDING   # set G-shift button N
ratslap-g305x dpi LEVEL X [Y]      # set DPI level (1-5)
ratslap-g305x dpi --live X [Y]     # live preview (not saved)
ratslap-g305x rate HZ              # 125 | 250 | 500 | 1000 (wireless + wired)
ratslap-g305x activate N           # make profile slot N active
ratslap-g305x reset [N]            # copy the factory ROM profile over slot N

ratslap-g305x --listkeys       # every binding token
ratslap-g305x -d G305X info    # pick a mouse by name/PID fragment
```

Bindings (`--listkeys` prints them all): mouse buttons `left right middle
back forward`, keys and modifier combos (`ctrl+shift+z`, `super+d`,
`f13`…), media keys (`media:next`, `media:playpause`, …), special
functions (`dpiup dpidown dpicycle dpidefault dpishift profilenext
profileprev profilecycle gshift batterystatus scrollleft scrollright`),
and `disable`. Raw forms: `key:0xNN`, `mouse:0xMASK`, `media:0xUSAGE`.

Note: profile *direct jump* isn't part of the format-7 binding catalog —
use `profilenext` / `profileprev` / `profilecycle` on a button, or the
`activate N` command.

## Install (Arch Linux, one paste)

```bash
git clone https://github.com/pablobc-mx/ratslap-g305x && cd ratslap-g305x && makepkg -si
```

`makepkg -si` builds the binary, runs the full test suite (`check()`)
— CRC vectors, HID++ framing, a format-7 golden sector round trip, and
a mock-device integration test; no mouse needs to be attached — and
installs the binary **and** the udev rule
(`/usr/lib/udev/rules.d/42-g305x.rules`).

Afterwards replug the mouse or its receiver once, or reload udev rules
without replugging:

```bash
sudo udevadm control --reload && sudo udevadm trigger
```

The repo layout is deliberately flat (krayon/ratslap style): makepkg
stages local sources into `./src` by file name, and directories are not
valid `source=()` entries — nested source directories would break
`makepkg -si` run from the clone, so don't reintroduce them.

### Install (generic Linux)

```bash
make && sudo make install     # or: cc -O2 -o ratslap-g305x *.c
sudo install -Dm644 42-g305x.rules /usr/lib/udev/rules.d/42-g305x.rules
sudo udevadm control --reload && sudo udevadm trigger
```

`make` needs only gcc + make; `make test` additionally uses bash, and
python3 only when `golden.h` is missing (`rm golden.h` to force a
regeneration).

## First run on the mouse (do this once, in order)

**1.** Plug the mouse in via its LIGHTSPEED receiver, then check what the
kernel sees:

```bash
lsusb | grep 046d
```

Note the receiver ID (this unit reports `046d:c547`, owner-confirmed) and
the mouse's wireless PID.

**2.** Plug the mouse in via its USB-C cable (wired mode) and repeat:

```bash
lsusb | grep 046d
```

Write down the wired PID too.

**3.** Install the udev rule and replug (see above), then check the tool
sees the mouse and note the exact IDs it reports:

```bash
ratslap-g305x list
ratslap-g305x info
```

**Please report the `046d:xxxx` values both commands print** (plus your
`lsusb` output) in a GitHub issue, so the table below can be extended.
Currently known for this family:

| USB ID | Meaning | Status |
|---|---|---|
| `046d:c547` | LIGHTSPEED USB receiver | owner-confirmed (this unit) |
| `046d:40bb` | G305 X LIGHTSPEED wireless wpid | owner-confirmed (this unit) |
| `046d:40bc` | G304 X wireless wpid (Asian-market sibling) | from libratbag PR #1911 |
| `046d:c0a4` | G305 X SUPERLIGHT wired USB-C | owner-confirmed (this unit) |
| `046d:c0a7` | G304 X wired USB-C | expected, unconfirmed |
| `046d:b046` | G305 X SUPERLIGHT (Bluetooth sibling) | from OpenGMice |
| `046d:4074` | G305 LIGHTSPEED (original, format 3) | long-known |

Verified end-to-end in hardware (2026-11): a G305 X SUPERLIGHT on
firmware `MPM 39.00.0012` — wireless through the `046d:c547` receiver
(wpid `046d:40bb`) and wired over USB-C (`046d:c0a4`) — where `list`,
`info` (format-7 decode, battery, live DPI / report rate), `backup`
and the modify-safety gate all behave exactly as the mock
integration test predicts.

Discovery is **runtime**: the tool scans `/dev/hidraw*`, pings HID++ 2.0,
walks the root/feature-set, and keeps devices exposing on-board profiles
(`0x8100`). The table above only feeds the udev rule comments — it is
never a functional gate, so an unknown PID works the moment the device
can be opened.

**4.** Back up the mouse before touching anything:

```bash
ratslap-g305x backup g305x-backup.txt
```

This registers the backup (in `~/.local/state/ratslap-g305x/`); modify
commands run freely afterwards. Without a backup they refuse — pass
`--yes` to force.

**5.** Remap something. Example: make the thumb button (G4) skip to the
next track, and G6 cycle DPI:

```bash
ratslap-g305x button 4 media:next
ratslap-g305x button 6 dpicycle
ratslap-g305x info
```

**6.** Verify from the kernel's point of view with `evtest`:

```bash
sudo pacman -S evtest
sudo evtest
# pick the "Logitech G305 X" event device, click the remapped button,
# and confirm the events arrive
```

(On-board bindings execute inside the mouse; `evtest` shows what the
mouse *emits*, which is exactly what games and apps will see.)

## Safety

- **Every write is verified**: the tool reads the sector, patches only
  the fields you changed, recomputes the sector's CRC-CCITT, writes it,
  reads it back and compares byte for byte. It never leaves a
  mismatched sector behind, and never writes a sector whose CRC is wrong.
- **The factory profile is safe**: it lives in read-only ROM (sector
  `0x0100+`); the tool refuses to write there. `reset` copies the ROM
  profile back into a writable slot.
- **Modify commands are gated**: `button`, `gbutton`, `dpi`, `rate`,
  `reset` and `restore` refuse to run until you have taken a `backup`
  (the marker must still point at an existing file) or you pass `--yes`.
- **`dpi --live`** is a preview only: the firmware refuses live sensor
  changes in on-board mode (HID++ error `0x05`), so the tool briefly
  switches to host mode and back, like OpenGMice does. Nothing is saved.
- Backups are plain text (`sector 0xNNNN <hex>`), diff- and
  grep-friendly, and `restore` refuses a backup whose device, profile
  format or sector size doesn't match.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | no compatible device found (hint text printed) |
| 2 | device I/O or protocol error (HID++ errors are named) |
| 3 | usage error |
| 4 | refused: destructive command without a backup or `--yes` |

## Testing & debugging seams

`make test` runs everything headless — no mouse attached:

- `test_crc.c` — CRC-CCITT vectors (canonical `0x29B1` check value
  plus golden sectors computed by an independent Python implementation,
  `gen_golden.py`; the generated `golden.h` is committed)
- `test_hidpp.c` — HID++ frame build/parse round trips, error
  classification
- `test_profile.c` — full format-7 (and format-3) sector
  decode/encode round trip against golden sectors, field-edit isolation
- `test_actions.c` — every binding token parse + describe
- `integration.sh` + `mock_hidraw.c` — the real binary,
  `LD_PRELOAD`ed against a fake G305 X (`046d:40bb`), exercising scan,
  probe, info, the safety gate, backup/restore, every write path, the
  ROM reset, and the host-mode DPI quirk

Environment overrides (also handy for debugging on real systems):
`RSLAP_SYSFS_PATH` (default `/sys/class/hidraw`) and `RSLAP_DEV_PREFIX`
(default `/dev`) redirect the scan; `RSLAP_STATE_DIR` redirects the
backup registry.

## Credits & license

- CLI/UX model: [`krayon/ratslap`](https://github.com/krayon/ratslap)
  (GPL) — the classic G300s tool this one is named after.
- Protocol source: [`sabam54/OpenGMice`](https://github.com/sabam54/OpenGMice)
  (GPL-3.0) — HID++ 2.0 framing, format-7 sector layout, CRC-CCITT usage
  and the setDpi mode quirk were extracted from its code, which is
  verified against real G305-family hardware. Where libratbag and
  OpenGMice disagree, this tool follows OpenGMice (libratbag has open
  format-7 bugs: PR #1911 unmerged, PR #1806 open).

License: **GPL-3.0-or-later** (see `LICENSE`). This project is
independent and not affiliated with or endorsed by Logitech. Logitech,
G HUB and LIGHTSPEED are trademarks of Logitech.
