# UI animation configuration

Copy `ui.cfg.example` to `ui.cfg` in the directory containing `luna.elf`
(for example `mc0:/APP_LUNA/ui.cfg`). Edit it as plain ASCII or UTF-8 without
a BOM, then restart LUNA. This is a launcher-wide file, independent of the
selected game device. LUNA reads it once after initializing storage, never
writes it, and does not require a checksum or settings-menu save.

The supplied example preserves the original timings and brightness. Missing
files/keys use those defaults. Blank lines, surrounding whitespace, CRLF/LF,
and `#` or `;` comments are supported. Keys are case sensitive. Unknown keys,
invalid integers and out-of-range values are ignored; the last valid duplicate
wins. Lines longer than 255 bytes are ignored. Files over 16 KiB or with read
errors use all defaults. Times accept 0..5000 ms except `menu_move_ms` (0..2000).
Brightness accepts 0..100 percent. Zero durations complete on the next frame.

| Key | Default | Effect |
| --- | ---: | --- |
| start_ring_fade_ms | 1000 | Start ring fade-in |
| start_label_delay_ms | 1500 | Wait before Start labels appear |
| start_label_fade_ms | 1000 | Start label fade-in after the delay |
| start_close_ms | 2000 | Start menu departure |
| start_orb_brightness_percent | 100 | All Start orbs, particles and ripple effects; does not dim labels |
| start_burst_brightness_percent | 100 | Extra entry burst intensity, multiplied by orb brightness |
| menu_open_ms | 500 | Settings/Appearance/Game Options category fade-in |
| menu_close_ms | 500 | Category fade-out |
| submenu_open_ms | 1440 | Crystal expansion and submenu entry |
| submenu_close_ms | 1440 | Crystal collapse and submenu return |
| menu_move_ms | 150 | Category selection glide |

For a quicker, quieter UI, try:

```ini
start_ring_fade_ms=200
start_label_delay_ms=0
start_label_fade_ms=200
start_close_ms=250
start_orb_brightness_percent=35
start_burst_brightness_percent=0
menu_open_ms=150
menu_close_ms=150
submenu_open_ms=250
submenu_close_ms=200
menu_move_ms=80
```

These settings change the shared category menus and Start menu. Library cover
animations, the Game Options library camera, blocking dialogs/storage work,
and controller repeat rates keep their existing behavior. The entry burst
setting cannot enable bursts where the current scene already suppresses them.
