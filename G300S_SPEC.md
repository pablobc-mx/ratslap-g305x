# G300s support — specification (feature/g300s → v0.2.0)

Second device backend: the Logitech **G300/G300s** (wired, `046d:c246`).
This closes the lineage circle: the original `krayon/ratslap` — the
project this fork is named after and lays itself out like — was a
G300/G300s tool. Scope is **original parity**: everything krayon's tool
could configure.

Owner decisions (2026-11-25): hardware is on the owner's desk for
validation rounds; scope = original parity (LED colours and key combos
included); work happens on `feature/g300s`, merged as **v0.2.0** after
hardware validation.

## Device facts (from krayon/ratslap, confirmed reference)

- Wired USB only, VID:PID `046d:c246`, no HID++ protocol at all.
- Configuration goes over **HID feature reports** (SET_REPORT control
  transfers) on a separate vendor HID interface, report IDs
  `0xF0`–`0xF5`; the mode data write (`0xF5`) carries a 0x23-byte
  (35-decimal) mode block. Reference: `G300s_USB_sniffing.txt` in the
  krayon repo + its `main.c` (1809 lines, complete implementation).
- 3 profiles, named F3/F4/F5 by Logitech. Per profile: LED colour
  (8 values: black red green yellow blue magenta cyan white), report
  rate (4 supported values), DPI 1–4 + default + DPI Shift, and 9
  button bindings — mouse buttons, keyboard keys, modifier combos,
  ModeSwitch, DPICycle.
- Original limitations we lift for free: krayon requires root +
  libusb driver detach. We talk through **hidraw feature ioctls**
  (`HIDIOCSFEATURE`/`HIDIOCGFEATURE`) — unprivileged under the udev
  rule already shipped, no driver detach. Round 1 must confirm the
  vendor interface exposes its own hidraw node and that feature
  ioctls reach it.

## Architecture

- `g300s.c` / `g300s.h`: transport (feature ioctls), mode-block
  decode/encode, the G300s binding catalog (ported from krayon's key
  tables — its own encoding, not forced into the HID++ format-7
  binding layout), LED colour mapping.
- Discovery: `list` gains a G300s probe (PID match + feature-report
  handshake). Runtime branching per device family, same CLI.
- CLI mapping onto the existing surface: profiles 1–3 == F3/F4/F5.
  `info` prints the three modes with colours; `activate N` = mode
  select; `button N <binding>`, `dpi`, `rate` work per active mode;
  `backup`/`restore` dump/restore the three mode blocks; `reset N`
  restores the factory default block (krayon's
  G300s_Default_Configuration dump is the golden source). One new
  subcommand: `colour N <colour>` (G305 X answers "not supported by
  this device").
- Safety: the existing backup gate applies to all G300s writes;
  every write verifies by read-back, exactly like the G305 X path.

## Mock and tests

- `mock_hidraw.c` gains a G300s model: interpose `ioctl()` for the
  feature-report path (the interposer currently hooks read/write —
  ioctl interception is the new piece), `MOCK_G300S_NODE` env knob,
  golden mode blocks ported from the factory dump. Same
  factory-fresh-vs-configured discipline the G305 X mock now has.
- Unit: mode-block decode/encode round trips against golden vectors.
- Integration: full chain on the mock G300s — list, info, backup,
  colour/dpi/rate/button writes with verification, activate, reset,
  restore.
- Hardware rounds with the owner (the five-round G305 X saga is the
  template; the reference implementation should make this much
  shorter).

## Phases

1. **Done in this commit**: spec, branch, kanban card.
2. Read-only slice, hardware round 1: transport via hidraw ioctls,
   `list`/`info`/`backup` on the real G300s. Probe list for round 1
   is in the card.
3. Write slice + mock: mode-block writes with verify, all subcommands,
   full integration suite green.
4. Hardware rounds 2+: the write chain on real hardware, one firmware
   rule at a time if the G300s has surprises (mock mirrors each
   confirmed rule — same red→green loop as v0.1.3–v0.1.7).
5. Merge to main as **v0.2.0**, release notes, README gains a G300s
   section, AUR pin moves.

## Acceptance criteria

- `list` finds both mice; every subcommand works on both, with
  device-appropriate behaviour (`colour` only on G300s).
- Full write chain validated on the owner's real G300s, verified by
  read-back, reversible by `restore`.
- Mock models both devices; integration suite covers both; every
  confirmed firmware rule is a permanent regression.
- v0.2.0 tagged and released after merge.

## Open question (does not block)

Repo is named `ratslap-g305x`; after v0.2.0 the name understates the
tool. Renaming a published repo is the owner's call — flagged for the
v0.2.0 release notes, decided then.
