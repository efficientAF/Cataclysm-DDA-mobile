# CDDA Mobile fork: working notes

This is a fork of CleverRaven/Cataclysm-DDA. Its goal is a mobile-first Android UI: a full-screen map, a customizable HUD with icon status indicators, swipeable drawers, and real touch controls.

## Design sources (read these first)

- **Plan:** https://claude.ai/artifact/CyDCMzQSw8Qt858pM8Tb8T. Goals, analysis of the upstream code, touch model, roadmap and ground rules.
- **UI canvas:** https://claude.ai/artifact/CdnbCxX152E752rzbASjnB. An interactive mockup and the source of truth for UI changes. It covers the main menu, new game, character creation, the play screen with HUD presets (portrait and landscape), inventory, crafting, map, pause and wait sheets, settings and the death screen. The inspector panel shows design notes for each screen.
- How to read them: use the `Artifact` tool with `action: "read"` and the URL. Do not use WebFetch.
- **Saved HUD presets:** the canvas stores user-saved layouts in its database, collection `presets`. Read them with `ArtifactData` `list`, using the canvas URL. "View layout JSON" in the canvas shows the layout format the game should load:
  - Each piece has an anchor (`tl`, `tc`, `tr`, `bl`, `bc`, `br`) and `x`/`y` pixel offsets from that edge, plus options such as `rows`, `size`, `dir` and `mode`.
  - Drawers have `edge`, `off` (handle position, 0–1) and `size` (depth, 0–1).
- When the user changes the design, update the canvas artifact rather than inventing UI in code.

## Branches and upstream

- Work on `mobile-ui`. Keep `master` as a clean mirror of upstream.
- The `upstream` remote is https://github.com/CleverRaven/Cataclysm-DDA. Upstream lands many commits a day, so merge `upstream/master` into `mobile-ui` regularly.
- Clones in cloud sessions are shallow. Use bounded fetches such as `git fetch --depth=50 upstream master`.
- **Never push to, or open pull requests or issues against, CleverRaven/Cataclysm-DDA.** `upstream` is fetch-only. In a fresh clone, run `git remote set-url --push upstream DISABLED`. Every pull request targets `efficientAF/Cataclysm-DDA-mobile` (usually `mobile-ui`), and you must check the base repository before creating one. Offering work upstream is the owner's decision, made by hand.

## Ground rules

- Put new code in new files where possible: `src/mobile_hud.{h,cpp}`, `src/touch_input.{h,cpp}`, `data/json/ui/mobile_hud/`. Keep hooks into upstream files small. This keeps merges painless.
- Gate features behind options such as `MOBILE_HUD`, not only `#ifdef __ANDROID__`, so they build and run on desktop tiles builds for fast iteration.
- Reuse the existing widget system (`src/widget.cpp`, `doc/WIDGETS.md`) for values like hunger and pain. The HUD changes how they are drawn, not how they are computed.
- Follow upstream style: astyle (`make astyle`), JSON formatting (`tools/format`), and `doc/c++/` guidelines. Generic fixes, such as touch input for ImGui, should stay upstreamable.
- CDDA is CC-BY-SA 3.0. Icon art must be original or compatible. Do not use Project Zomboid assets.

## Key code locations (upstream baseline 66dfe4eb, Nov 2026)

- **Android touch, quick-shortcut bar and virtual joystick:** `src/sdltiles.cpp`, inside `#if defined(__ANDROID__)` blocks.
  - Every gesture is turned into a fake keypress through the global `last_input`; see `handle_finger_input`.
  - Touch-to-mouse is disabled at about line 676.
- **Overlay hook for the HUD:** `refresh_display()` in `sdltiles.cpp`. After the grid is blitted, it draws the quick shortcuts and joystick at native resolution. The HUD should draw at that same point.
- **Map width:** set from the sidebar width in `game.cpp` (about line 3179). Layouts live in `panels.cpp`; Android defaults to `sidebar-mobile`.
- **Mouse click on the map:** click-to-travel and right-click actions already exist via `ACTION_SELECT` / `ACTION_SEC_SELECT` (`handle_action.cpp` about line 3322). Touch should reuse these.
- **ImGui:** `src/cata_imgui.cpp`. Touch does not reach ImGui as pointer input. `cata_key_to_imgui` maps digits to letters (a bug) and is missing Enter.
- **Zoom:** `game::zoom_in` / `zoom_out` in `game.cpp`. Steps are powers of two and wrap at the ends. Pinch currently sends `z` or `Z` once, after release (`ANDROID_PINCH_*_KEY`).
- **Android options:** `src/options.cpp`, Android page, about lines 3048–3407.
- **Character creator:** `src/newcharacter.cpp`, already ImGui. Main menu: `src/main_menu.cpp`. Inventory: `src/inventory_ui.cpp` and `src/advanced_inv.cpp` (curses).

## Building

- **Desktop tiles:** needs SDL3 3.4 or newer, plus SDL3_image, SDL3_ttf and SDL3_mixer. Use CMake (`CMakePresets.json`) or `make TILES=1`. See `doc/c++/COMPILING.md`.
- **Android:** `android/` (Gradle). Needs the Android SDK and NDK; the SDL3 AARs download automatically (`android/app/build.gradle`). CI builds bundles with `.github/workflows/release-android-bundle.yaml`.
- **Tests:** `make tests` or the CMake test targets. The curses build (`make TILES=0`) needs no SDL and is the fastest way to compile-check logic.
- **Cloud environment `cataclysm-dda-mobile` (Nov 2026):** allows `dl.google.com`. It has no setup script, so each new session installs the tools first (about two minutes):
  - `apt-get install -y ccache gettext astyle glslang-tools`
  - Android SDK into `/opt/android-sdk`: command-line tools from `https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip`, then `sdkmanager "platform-tools" "platforms;android-35" "build-tools;35.0.0" "ndk;28.1.13356709" "cmake;3.22.1"`, and `sdk.dir=/opt/android-sdk` in `android/local.properties`.
- **APK:** `cd android && ./gradlew --no-daemon -Pj=4 -Plocalize=false -Pabi_arm_32=false assembleExperimentalDebug`. About 30 minutes from scratch on 4 cores. Output is in `android/app/build/outputs/apk/experimental/debug/`. After adding a source file, `touch android/app/jni/CMakeLists.txt` so it is picked up.
  - App ID is `com.efficientaf.cddamobile` (plus `.experimental`); debug builds are signed with `android/app/debug.keystore`.
  - Android-only code does not compile on desktop. Check it with the NDK before pushing; a full Gradle build is the real test.
- **Desktop tiles:** Ubuntu 24.04 has no SDL3 packages. Build SDL3 3.4.10, SDL3_image 3.4.4 and SDL3_ttf 3.2.2 from the release tarballs on GitHub (`github.com/libsdl-org/<repo>/releases/download/...`) with CMake into `/usr/local`, then `make -j4 TILES=1 SOUND=0 LOCALIZE=0 RUNTESTS=0 ASTYLE=0 LINTJSON=0 cataclysm-tiles`.
- **Testing touch without a phone:** use `tools/mobile_ui/` (needs `xvfb xdotool imagemagick x11-utils`), or by hand: run the desktop build under `Xvfb` with `SOFTWARE_RENDERING` on and the `TOUCH_*` options on, and feed it gestures through `CATA_TOUCH_SCRIPT` (format in `src/touch_input.h`). Take screenshots with ImageMagick's `import`. On X11, ImGui follows the real mouse pointer, so park it (`xdotool mousemove`) where a scripted tap on an ImGui window will land.

## Mobile code (this fork)

Everything is behind options registered at the end of `add_options_android()` in `options.cpp` (`MOBILE_HUD`, `MOBILE_MENUS`, `TOUCH_*`), on by default on Android. Hooks in upstream files are marked `// Mobile UI:`.

- `src/touch_input.{h,cpp}`: tap, long-press, pinch, pan, ImGui pointer input (tap = click, drag = scroll, raw-drag areas), zoom range, and the desktop stand-in for the Android gesture code.
- `src/mobile_ui.{h,cpp}`: the canvas look: palette, fonts (`data/font/Barlow*.ttf`, `JetBrainsMono-Medium.ttf`), icons (`data/mobile_ui/icons/*.svg`), `pressable`, `box`, `app_bar`, bottom sheets.
- `src/mobile_hud.{h,cpp}`: play-screen overlay drawn with SDL in `refresh_display()`, fixed at the Balanced preset.
- Screens (ImGui, one file pair each): `mobile_menu` (main menu, new game, pause and wait sheets), `mobile_chargen`, `mobile_inventory` (also replaces the pick-up menu), `mobile_crafting`, `mobile_drawer` (Info and Log drawers, minimap), `mobile_overmap`, `mobile_settings`, `mobile_death`, `mobile_dialog` (popups, yes/no, `uilist` as a sheet).
- Tests: `tests/touch_input_test.cpp` (`[touch_input]`).
- `tools/mobile_ui/`: the desktop test kit (`run.sh`, `drive.sh`, `build.sh`), an NDK syntax check, and `BRIEF.md`, the brief used for workers building one screen each.

### Status and open work (16 Nov 2026)

Every canvas screen exists and was checked on a desktop build with scripted touches; the owner tests APKs on a phone. Open, most important first:

1. **Prompts that expect a key** have no touch answer: for example "Press ESC, q, Q or SPACE to accept your fate…" on death and "RETURN to move anyway" from safe mode. The owner wants on-screen buttons that pop up only in these states, richer than upstream's single-character shortcut row (named buttons for the keys the current prompt or input context accepts).
2. Upstream's shortcut bar is hidden on the map and over mobile screens (`touch_input::hide_shortcut_bar`) but still shows over upstream text screens; item 1 should replace it there.
3. Screens still upstream's: keybindings, full options list, `uilist` menus with tabs, callbacks or fixed positions, and anything the canvas does not cover (NPC dialogue, bionics, mutations, vehicles, construction, look-around).
4. Play screen: movement-pad toggle and zoom buttons as HUD pieces, Edit HUD mode, presets from `data/json/ui/mobile_hud/`, left handle opening straight onto the map; portrait HUD was not verified.
5. Map: a drag lands about one tile short; two fingers only zoom. Sheets cannot be dragged down. Yes/no questions still say "(Case Sensitive)".
6. Smooth pinch scaling between zoom levels.

## Suggested order of work (roadmap phases 0–3)

1. **Build environment:**
   - Desktop tiles build running; a curses build for quick checks.
   - Then an Android debug APK, locally or through a CI workflow on this fork.
2. **Touch plumbing:**
   - Tap next to you acts in that direction; tap further away previews a route, and a second tap travels (reuse `ACTION_SELECT`).
   - Long-press opens the action ring.
   - Pinch zooms live and snaps to steps; two-finger drag pans, with a Recenter button.
   - Feed finger events to ImGui as pointer input, and fix the `cata_key_to_imgui` bugs.
   - Remove the 300 ms double-tap wait where double-tap is not bound.
3. **`MOBILE_HUD` option:**
   - Zero-width sidebar, so the map fills the screen.
   - HUD pieces drawn in the `refresh_display()` hook.
   - Load presets from `data/json/ui/mobile_hud/` in the canvas's layout format.
4. **Then:** drawers, the in-game Edit HUD mode, icon art, and the other screens from the canvas.
