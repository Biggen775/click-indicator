# v2.2.2

- Release Offset (default +12 ms): shifts every assisted release later, for macros whose releases sit a touch early in wave/ship.
- Hold Slack (default 40 ms): if you're still holding at the macro's end, the assist waits for your own release before releasing for you.

# v2.2.1

- Fixed Spam Assist eating clicks: a click outside the strength window but near a macro press (within 150 ms) now goes through normally. Only taps with nothing in the macro anywhere near them are swallowed.

# v2.2.0

- Aim Assist rewritten around holds and spam (wave / ship): extra taps during a macro hold are absorbed, holding through a spam section follows the macro's pattern, a late press into a long hold joins it, and stray taps that match nothing are swallowed. Spam Assist toggle + Max Hold / Max Gap settings.

# v2.1.2

- Pre-Click Cue now has 7 built-in sounds (Beep, High Beep, Low Tone, Click, Buzz, Double Beep, Ping) plus a Custom file option, its own volume slider, and a preview button in the menu.

# v2.1.1

- Pre-Click Cue: a heads-up sound a set number of ms before each click (Practice page, lead time in Settings).

# v2.1.0

- Auto-Play (bot mode) with a speed slider for slow-motion study.
- Section practice: auto checkpoint at the start percent, auto restart at the end percent.
- Progressive assist: clean runs lower the assist strength by one.
- Count-in beats before the first click. Next-click countdown. Level timeline across the top.
- Offset trail: dots on the lane notes show how early/late you were (this attempt and the last).
- Attempt history chart in the stats popup. Custom PERFECT/GREAT/GOOD windows. Themes.
- Macro editing: shift +/-5 ms, trim before the current point, save edited, export to .gdr2. Merge recordings.
- First-time tutorial popup. Experimental Android build workflow.
- Menu now has four pages: Display, Aim Assist, Practice, Tools.

# v2.0.0

- Record your own macro: turn on Recording (Tools page), play to 100% in practice mode, and it saves `<level name>.gdr.json` into the macro folder. Checkpoints keep the clicks from before them.
- Attempt stats: popup on level complete (accuracy, judgment counts, average offset, furthest-off clicks) and a Show Stats button. Optional one-line summary on death.
- Trouble spots: clicks you keep getting wrong turn red in the bars and the lane. Clear Trouble button.
- Click sounds: metronome-style tick at each macro press, optional softer tick on releases, volume slider.
- Lane look-ahead (seconds) replaces lane speed. Horizontal (Guitar Hero style) lane mode. Player 2 lane color and gap.
- Aim assist: separate release strength, auto-off after a percent, yellow flash + ASSIST label when a click is corrected.
- Hide the overlay after a percent. Hotkeys to toggle the overlay (F7) and the assist (F9).
- Macro picker in the menu, with a per-level choice. Remember settings per level.
- Menu now has three pages: Display, Aim Assist, Tools.

# v1.3.0

- Aim Assist: at strength 5 and up, letting go early no longer ends the hold. The mod keeps holding until the macro's release. Below 5, early releases are lined up only within the strength window.

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
