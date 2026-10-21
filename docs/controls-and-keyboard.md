# Controls from sa2-nx and the Switch software keyboard (2026-10-21, package 36z5)

## Controls (sa2-nx's Switch input driver)

`sdk/src/input/switch/switch_input_driver.cpp` and its header are sa2-nx's driver as of 2026-10-20 (credit: sa2-nx),
with Banjo's file names in the comments and one log line added. What it brings over Banjo's previous driver:

- **Xbox layout by position** (`input_xbox_layout = true`, now also the code default): the bottom face button (B on
  Switch controllers) is the Xbox A, the right one B, the left one X, the top one Y. The ImGui menus follow it
  (`window_switch.cpp`). `false` = by letter.
- **Separate players**: slot 0 is the handheld, slots 1-4 are No1-No4 (`kSlotCount = 5`); before, the handheld and No1
  shared slot 0, so a second controller in handheld mode also drove player 1. The input system hands out guest users
  in connection order (`SlotAssignment`: ordinal N = guest user N).
- **Per-player button maps**: `[input_p1]` / `[input_p2]` sections of `renut.toml` (the cvar loader flattens
  `[input_p1] a = ...` to `input_p1_a`). Values: comma-separated Switch buttons (A B X Y L R ZL ZR PLUS MINUS LSTICK
  RSTICK UP DOWN LEFT RIGHT SL SR NONE), `""` = default. Player 1's map also serves players 3 and 4. Unknown names are
  logged and that button keeps its default.
- **Single Joy-Cons sideways** (`input_joycon_sideways = true`, init only): the system is told the horizontal grip; the
  four buttons under the right thumb become the face buttons, SL/SR the triggers.
- **L+R+ZR**: the system's controller applet (`hidLaShowControllerSupport`), on the UI thread.
- `InputDriver::OnDevicesAssigned` (new virtual, `input_driver.h`) is called by `InputSystem::RefreshDevices` after a
  change, so the driver learns each slot's player; Banjo's input-latency calls in `input_system.cpp` are kept.
- Banjo addition: `controller: Pro Controller (No1) is player 1` in the log whenever the assignment changes.

The `[input_p*]` sections must stay at the end of `renut.toml` (every line after a TOML table header belongs to it).
`tools/make_enhancement_configs.py` therefore inserts keys before the first section, and `tools/enhancement_keys.py`
reads section keys as `<section>_<key>`.

## The Switch software keyboard for XamShowKeyboardUI

The game's XShowKeyboardUI (one call site, `sub_822A1938`: a system-UI state machine shared with the device selector)
used to open the SDK's ImGui text box, which cannot be typed into on the console. With `switch_software_keyboard =
true` (default) it opens the system's swkbd library applet instead:

- `sdk/src/kernel/xam/xam_ui.cpp`, `XamShowKeyboardUISwitch`: reads the prompts and the default text on the game's
  thread (bounded: a missing terminator cannot run on), then completes the overlapped through
  `xeXamDispatchHeadlessEx` on the kernel's dispatch thread (a host thread; the global lock is released while an item
  runs). XN_SYS_UI is broadcast as for the other dialogs, and `XamIsUIActive` is true while the applet is up.
- `sdk/src/kernel/xam/xam_ui_switch.cpp`, `ShowSwitchKeyboard` (kept apart so `<switch.h>` stays out of the XAM
  headers): `swkbdConfigMakePresetDefault` (QWERTY in the system's language variant, blurred background), no newline
  key, `stringLenMax` = the game's buffer minus its terminator (at most 500, swkbd's limit). Up to 32 characters the
  one-line field shows the game's title as the header and its description as the sub text (swkbd shows those only in
  `SwkbdTextDrawType_Line`, which it allows only up to 32); longer, a box, with the title as the guide text.
  VKBD numeric/phone/IP flags give the number pad (IP with "."), VKBD password the password field.
- Results: accepted: the text into the game's buffer (`copy_and_swap_truncating`: at most buffer - 1 units and a
  terminator), extended error 0; cancelled (libnx `LibAppletBadExit`): the buffer untouched, extended error
  ERROR_CANCELLED (the game decides with `XGetOverlappedExtendedError() == 0`, overlapped + 0x18); the applet could not
  be shown: the default text, accepted (as with no UI). After the applet closes the driver waits (at most 1 s) until no
  button is held, so the closing press does not reach the game as a new one.

### Test hook and emulator check

`switch_software_keyboard_test = <seconds>` makes the game's input thread (`XamInputGetState`) call
`XamShowKeyboardUI` once, that long after its first controller read, with guest-memory prompts, a 16-character buffer
and an overlapped, and log the outcome. Our Ryujinx build answers the keyboard by itself when `RYUJINX_SWKBD_AUTO` is
set (`__cancel__` cancels) and logs what it was asked for (`swkbd request: ...`). Runs of 2026-10-21 (scratchpad
`emu_kbd.sh`): accepted "Kazooie Ünïcode 1234567890" came back as the 15 characters "Kazooie Ünïcode" (result 0,
extended 0); cancel gave extended 0x4C7 and an untouched buffer; the game ran on at 29 fps; Ryujinx saw header, sub,
guide, initial text, max 15, single line. The driver listed Ryujinx's three Pro Controllers as players 1-3.

### Open (console only)

- The game's real flags and buffer size (the log line `XamShowKeyboardUI: Switch keyboard for user ...`). The VKBD
  values used (phone 0x08, IP 0x10, numeric 0x20, password 0x80) are the XDK's as remembered, not checked against a
  header here; with flags 0 nothing depends on them.
- Whether the game keeps drawing behind the applet without slowing the keyboard, and how input looks to the game
  while the applet has focus (the release wait covers the closing press).
- Player 2 on the 360 may need a signed-in profile for some modes; untested.
