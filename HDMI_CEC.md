# HDMI CEC Behavior

This document describes current HDMI CEC behavior implemented in `hdmi_cec.cpp`.

## Config

- `hdmi_cec=1`: enable HDMI CEC.
- `hdmi_cec_input_mode=0|1|2`:
  - `0`: ignore all incoming CEC remote key input.
  - `1`: accept CEC keys only when OSD/menu is active, except the configured OSD trigger key.
  - `2`: always accept mapped CEC keys.
- `hdmi_cec_osd_key=none|back|red|green|yellow|blue`.
  - Legacy numeric values also accepted: `0=none`, `1=back`, `2=red`, `3=green`, `4=yellow`, `5=blue`.
- `hdmi_cec_name=<text>`: OSD name sent via CEC (`SET_OSD_NAME`), max 14 bytes on wire.
- `hdmi_cec_sleep=0|1`: after no input activity for `video_off` minutes (with 1-4 as presets: 15/30/45/60), send CEC `STANDBY` (requires `video_off>0`).
- `hdmi_cec_wake=0|1`: on first input after that idle period, send CEC wake (`IMAGE_VIEW_ON`/`TEXT_VIEW_ON` + `ACTIVE_SOURCE`).
- Defaults: `hdmi_cec_input_mode=1`, `hdmi_cec_osd_key=back`, `hdmi_cec_name=MiSTer`, `hdmi_cec_sleep=0`, `hdmi_cec_wake=0`.

## Device Identity

- Logical device type: Playback.
- OSD name: configured by `hdmi_cec_name` (default `MiSTer`).
- Physical address: read from EDID CEA extension (with loose fallback parser).

## MiSTer -> TV/Broadcast (TX)

Sent during CEC init:

| Opcode | Name | Destination | Purpose |
| --- | --- | --- | --- |
| `0x84` | REPORT_PHYSICAL_ADDRESS | Broadcast (`0xF`) | Advertise physical address and playback type. |
| `0x47` | SET_OSD_NAME | TV (`0x0`) | Publish configured OSD name. |
| `0x04` | IMAGE_VIEW_ON | TV (`0x0`) | Wake/select display path. |
| `0x0D` | TEXT_VIEW_ON | TV (`0x0`) | Wake/select display path. |
| `0x82` | ACTIVE_SOURCE | Broadcast (`0xF`) | Announce current active source path. |

Boot follow-up:

- One delayed retry sends `IMAGE_VIEW_ON`, `TEXT_VIEW_ON`, and `ACTIVE_SOURCE`.

## TV/Bus -> MiSTer (RX handling)

| Incoming opcode | MiSTer behavior |
| --- | --- |
| `0x83` GIVE_PHYSICAL_ADDRESS | Replies with `REPORT_PHYSICAL_ADDRESS`. |
| `0x46` GIVE_OSD_NAME | Replies with `SET_OSD_NAME`. |
| `0x8C` GIVE_DEVICE_VENDOR_ID | Ignored. |
| `0x9F` GET_CEC_VERSION | Replies with `CEC_VERSION` (`1.4`). |
| `0x8F` GIVE_DEVICE_POWER_STATUS | Replies `REPORT_POWER_STATUS` = ON. |
| `0x85` REQUEST_ACTIVE_SOURCE | Replies with `ACTIVE_SOURCE`. |
| `0x86` SET_STREAM_PATH | If path matches MiSTer physical address, replies with `ACTIVE_SOURCE`. |
| `0x8D` MENU_REQUEST | Replies `MENU_STATUS` active. |
| `0x44` USER_CONTROL_PRESSED | Translates supported remote buttons into MiSTer keys. |
| `0x45` USER_CONTROL_RELEASED | Releases pressed MiSTer key. |

## Remote Button Mapping

Mapped:

- Directional keys -> `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`
- Select/OK -> `KEY_ENTER`
- Exit/Back (`0x0D`) -> `KEY_ESC` (normal back), or `KEY_F12` when `hdmi_cec_osd_key=back`
- Play/Pause/Stop/Rewind/FastForward -> `KEY_SPACE`, `KEY_S`, `KEY_R`, `KEY_F`
- Numeric `0-9` -> `KEY_0..KEY_9`
- **Configured menu trigger key (`hdmi_cec_osd_key`) -> `KEY_F12` (`back`/color options)**

Not mapped:

- Unselected CEC color keys.
- CEC volume/mute keys (TV/AVR keeps volume control).

## TX Result Logging Semantics

- TX is treated as success unless an explicit nack/arbitration failure is reported (some setups don't reliably surface a TX-done indication).
- Debug logging prints only explicit nack/arbitration failures as `CEC: TX NACK ...`; other TX is silent.
- Only repeated explicit `NACK` events trigger temporary TX suppression.

## Standby / Wake (Idle)

When enabled:

- `hdmi_cec_sleep=1`: after no input activity for `video_off` minutes (with 1-4 as presets: 15/30/45/60), MiSTer sends broadcast CEC `STANDBY` (`0x36`), but only if MiSTer is currently the active source.
- `hdmi_cec_wake=1`: on first input activity after that idle period, MiSTer sends `IMAGE_VIEW_ON` (`0x04`), `TEXT_VIEW_ON` (`0x0D`), and broadcast `ACTIVE_SOURCE` (`0x82`).

Notes:

- This uses global input activity (keyboard/gamepad/mouse), so it applies while running any core.
- These messages target the TV/CEC bus; MiSTer itself does not power down.
- If `log_file_entry=1`, MiSTer creates `/tmp/IDLE` after the idle timeout elapses and deletes it on the next input activity (for external integration).
