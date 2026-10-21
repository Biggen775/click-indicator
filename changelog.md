# v1.2.3

- Fixed the big input delay introduced in v1.2.2: the timing source went back to the v1.2.1 one (player position), which lines up with the bars. The wave overhold fix stays.

# v1.2.2

- Aim Assist timing: uses the game's tick counter (the same frame number macro bots record) and sends held-back clicks right before the game reads inputs, removing up to ~8 ms of delay on assisted presses and releases.

# v1.2.1

- Fixed Aim Assist holding too long (mostly in wave): pressing again while an early release was being held back merged two holds into one. Inputs are now queued in order, and a held-back release is never delayed more than 100 ms.

# v1.2.0

- Aim Assist: early clicks and releases are held back and lined up with the macro. Strength 1 (1 ms) to 10 (1 second).
- Safe Mode: no progress is saved while the assist is on (can be turned off in Settings).

# v1.1.0

- In-game menu: button in the pause menu and a hotkey (F8 by default) to toggle every feature mid-level.
- Reload Macro and Open Macro Folder buttons.

# v1.0.0

- First release: click bars, hit line, rhythm lane and timing judgments.
