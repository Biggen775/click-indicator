# Click Indicator

Shows the clicks from a macro (replay) file while you play, osu!mania style:

- **Click bars** in the level: a thin line is a tap, a wide green bar is a hold.
- **Hit line** that follows your player.
- **Rhythm lane** on the side with falling notes and a receptor that lights up when you click.
- **Aim Assist** (optional): early clicks are lined up with the macro, strength 1-10. Safe Mode keeps it from saving progress.
- **Timing judgments** (PERFECT / GREAT / GOOD / MISS) comparing your clicks to the macro.
- **Record your own macro** in practice mode, no bot needed.
- **Stats, trouble spots, click sounds**, a horizontal lane, hotkeys and per-level settings.
- **Auto-play**, section practice, progressive assist, offset trail, timeline, macro editing and .gdr2 export.

## How to use

1. Open the mod's settings and press the folder button on **Macro Folder** (or open `geode/config/brittj.click-indicator/macros`).
2. Put a macro there named after the level ID or level name, e.g. `12345.gdr` or `Bloodbath.gdr.json`.
3. Play the level. A notification tells you the macro was loaded.

You can also pick one **Fallback Macro File** that is used for any level without a match.

Supported: `.gdr` (msgpack), `.gdr.json`, `.gdr2` and similar JSON macros. Classic (non-platformer) levels.
