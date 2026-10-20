# Click Indicator (Geode mod)

Shows a macro's clicks as osu!mania-style bars in the level, a hit line on the player,
a rhythm lane on the side, and timing judgments for your own clicks.

Made for **Geometry Dash 2.2081** and **Geode 5.11.2**, built for Windows.

## Building on GitHub (no installs needed)

1. Make a free account at github.com, then click **+ → New repository**.
   Name it `click-indicator`, set it to Public, and click **Create repository**.
2. On the new repo page, click **uploading an existing file**.
3. Unzip this project, open the `click-indicator` folder, select **everything inside it**
   (including the `.github` folder), and drag it onto the upload page. Click **Commit changes**.
4. Open the **Actions** tab. The "Build Geode Mod" run starts by itself (about 5 to 10 minutes).
   If GitHub asks you to enable workflows, click the green button first.
5. When the run has a green check, click it, scroll to **Artifacts**, and download **Build Output**.
   Unzip it to get `brittj.click-indicator.geode`.

If the `.github` folder didn't upload: in the Actions tab click **set up a workflow yourself**,
paste the contents of `.github/workflows/build.yml`, and commit.

If a build fails (red X), click the failed run, open the failed step, and copy the error text.

## Installing

Put `brittj.click-indicator.geode` into `Geometry Dash/geode/mods` and restart the game.

## Using it

1. In Geode, open the mod's settings and click the folder button next to **Macro Folder**.
2. Put a macro there named after the level's ID or name: `12345.gdr`, `Bloodbath.gdr.json`, etc.
3. Play the level. A notification shows when a macro is loaded.

Supported formats: `.gdr`, `.gdr.json`, `.gdr2`, and similar JSON macros (xdBot, Eclipse and other GDR bots).
Classic levels only (not platformer).
