# CDDA Mobile fork: working notes

This is a fork of CleverRaven/Cataclysm-DDA. Its goal is a mobile-first Android UI: a full-screen map, a customizable HUD with icon status indicators, swipeable drawers, and real touch controls.

## Design sources (read these first)

- **Plan:** https://claude.ai/artifact/CyDCMzQSw8Qt858pM8Tb8T. Goals, analysis of the upstream code, touch model, roadmap and ground rules.
- **UI canvas:** https://claude.ai/artifact/CdnbCxX152E752rzbASjnB. An interactive mockup and the source of truth for UI changes. It covers the main menu, new game, character creation, the play screen with HUD presets (portrait and landscape), inventory, crafting, map, pause and wait sheets, settings and the death screen. The inspector panel shows design notes for each screen.
- **Remaining screens plan:** https://claude.ai/artifact/2CGCTUKcXfwfVB9NDZF7Y7. Inventory of every screen that still falls back to upstream's desktop UI, the eight patterns (A item picker, B map pick mode, C tabbed list with detail, D conversation and trade, E vehicle and construction, F entry and choice sheets, G settings lists, H move items) that should replace them, and the build order. Awaiting the owner's answers to the decisions at its end; nothing in it is built yet.
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
- **Cloud environment `cataclysm-dda-mobile` (Nov 2026):** allows `dl.google.com`. It has no setup script (the owner declined saving one), so each new session first runs `bash tools/mobile_ui/setup_env.sh` from the directory that holds the checkout (about two minutes). It installs the test tools (xvfb, xdotool, imagemagick, astyle, glslang, ccache), builds SDL3 3.4.10, SDL3_image 3.4.4 and SDL3_ttf 3.2.2 into `/usr/local`, puts the Android SDK and NDK into `/opt/android-sdk`, writes `android/local.properties` and downloads the picture tilesets.
- **Tilesets:** git only holds `gfx/ASCIITileset` (letters drawn as tiles). `bash tools/mobile_ui/fetch_tilesets.sh` downloads the picture tilesets (UltimateCataclysm, MShockXotto+, Altica and others, about 55 MB) from the latest release of I-am-Erk/CDDA-Tilesets into `gfx/`, which git ignores. The Gradle build bundles whatever is in `gfx/`, so run the script before building an APK. The owner installs APKs built in the session, not from CI.
- **APK:** `cd android && ./gradlew --no-daemon -Pj=4 -Plocalize=false -Pabi_arm_32=false assembleExperimentalDebug`. About 30 minutes from scratch on 4 cores. Output is in `android/app/build/outputs/apk/experimental/debug/`. After adding a source file, `touch android/app/jni/CMakeLists.txt` so it is picked up.
  - App ID is `com.efficientaf.cddamobile` (plus `.experimental`); debug builds are signed with `android/app/debug.keystore`.
  - Android-only code does not compile on desktop. Check it with the NDK before pushing; a full Gradle build is the real test.
- **Desktop build:** `make -j4 TILES=1 SOUND=0 LOCALIZE=0 CCACHE=1 RUNTESTS=0 ASTYLE=0 LINTJSON=0 DEBUGSYMS= cataclysm-tiles`, in the repo itself. About 25 minutes from scratch; ccache makes later builds and other worktrees fast.
- **Testing touch without a phone:** use `tools/mobile_ui/` (needs `xvfb xdotool imagemagick x11-utils`), or by hand: run the desktop build under `Xvfb` with `SOFTWARE_RENDERING` on and the `TOUCH_*` options on, and feed it gestures through `CATA_TOUCH_SCRIPT` (format in `src/touch_input.h`). Take screenshots with ImageMagick's `import`. On X11, ImGui follows the real mouse pointer, so park it (`xdotool mousemove`) where a scripted tap on an ImGui window will land.

## Mobile code (this fork)

Everything is behind options registered at the end of `add_options_android()` in `options.cpp` (`MOBILE_HUD`, `MOBILE_MENUS`, `TOUCH_*`), on by default on Android. Hooks in upstream files are marked `// Mobile UI:`.

- `src/touch_input.{h,cpp}`: tap, long-press, the zoom and pan dials (`dial_motion`, `dial_poll`), pinch and two-finger pan (option `TOUCH_TWO_FINGER_GESTURES`, off by default; the map screen always pinches), ImGui pointer input (tap = click, drag = scroll, raw-drag areas), zoom range, and the desktop stand-in for the Android gesture code, including a stand-in for the floating joystick.
- `src/mobile_ui.{h,cpp}`: the canvas look: palette, fonts (`data/font/Barlow*.ttf`, `JetBrainsMono-Medium.ttf`), icons (`data/mobile_ui/icons/*.svg`), `pressable`, `box`, `app_bar`, bottom sheets.
- `src/mobile_prompt.{h,cpp}`: the prompt buttons that replace upstream's shortcut bar. Two general sources: key hints in an on-screen note (`input_context::get_desc` records the key lists it hands out; a note that contains one gets a button named after the hint's text, e.g. "Accept your fate"), and the registered actions of the input context on top of the stack (named buttons, overflow behind "More"). Mobile screens opt out with `mobile_prompt::own_controls`. Drawn on ImGui's foreground list at the end of each UI frame (`ui_manager.cpp`); fingers come from `touch_input.cpp`.
- `src/mobile_hud.{h,cpp}`: play-screen overlay drawn with SDL in `refresh_display()`. Pieces: location, status icons, body, wielded, log, quick actions, zoom dial, pan dial, recenter, movement pad, drawer handles.
  - Zoom has two styles (`style` in the layout JSON): `dial`, round, and `slider`, 44 units thick with `dir` `col` or `row` and `width` 120–320 for its length; its thumb sits where the zoom is. Both are dragged relative to where the thumb went down.
  - Zoom and pan (`zoom`, `pan` in the layout JSON, `size` 44–120) are round and worked by touch and drag. Zoom: one level per 48 units of travel, live in quarter levels, whole level at release. Pan: scrolls toward the drag at 3–40 tiles a second, top speed 70 units out; a tap recenters.
  - Movement joystick: option `TOUCH_JOYSTICK_MODE`. `fixed` is the HUD's pad (the layout's `mode` says `toggle` or `always`); `floating` is upstream's drag-anywhere joystick, and the pad piece is not drawn.
  - The zoom dial is not drawn while `USE_TILES` is off: the letter map cannot zoom.
- `src/mobile_hud_layout.{h,cpp}`: layouts in the canvas's JSON format. Built-in presets are game data, `data/json/ui/mobile_hud/presets.json` (`"type": "mobile_hud_preset"`); the player's layout is `mobile_hud.json` in the config folder. Positions count from the edge of the canvas's phone; `place()` swaps the canvas's notch insets for the real ones.
- `src/mobile_hud_edit.{h,cpp}`: Edit HUD mode (pause sheet and Settings): drag pieces and drawer handles, tap a piece for options, Presets, Pieces. Edits the orientation the phone is held in.
- Screens (ImGui, one file pair each): `mobile_menu` (main menu, new game, pause and wait sheets), `mobile_chargen`, `mobile_inventory` (also replaces the pick-up menu; `pick_up_what_fits` hands upstream's pick-up only what can be stashed, because it stops at the first item with no room), `mobile_crafting`, `mobile_drawer` (Info and Log drawers, minimap), `mobile_overmap`, `mobile_settings` (also the Tiles/ASCII switch and the tileset chooser), `mobile_death`, `mobile_dialog` (popups, yes/no, `uilist` as a sheet).
- Tests: `tests/touch_input_test.cpp` (`[touch_input]`), `tests/mobile_prompt_test.cpp`.
- `tools/mobile_ui/`: the desktop test kit (`run.sh`, `drive.sh`, `build.sh`), an NDK syntax check, and `BRIEF.md`, the brief used for workers building one screen each.

### Status and open work (16 Nov 2026, third session)

`mobile-ui` holds all of the above. The session branches `ccr-df1babd3-3p6es0`, `ccr-eb648aad-5peoju` and `ccr-b8adc6d9-u8s16x` are merged into it and can be deleted.

The owner runs the APKs on a phone and reports back; this side only tests with scripted touches on the desktop build, so the Android-only gesture code in `sdltiles.cpp` is only known to compile here.

Open, most important first:

1. **Try on a phone:** the zoom and pan dials, the fixed pad and the floating joystick (upstream's code path, which the desktop stand-in does not exercise), pinch with the setting off (must do nothing on the play screen) and on, the Tiles/ASCII switch in Settings.
2. **ASCII map (`USE_TILES` off)** works with the HUD, taps, pan and the pad. Known gaps: no zoom at all (the dial hides); the letters are the size of the map font, small on a phone, and only the text size setting changes that; the start zoom of `touch_input::default_zoom` does not apply. Without `fetch_tilesets.sh` the only tileset is letters drawn as tiles.
3. **Old screens** still upstream's: keybindings, full options list, `uilist` menus with tabs, callbacks or fixed positions, NPC dialogue, bionics, mutations, vehicles, construction, look-around. They get the prompt buttons, but no mockups exist yet. Add the mockups to the canvas first, then build to them (`tools/mobile_ui/BRIEF.md`).
4. **Not verified:** the safe-mode "Go back / Move anyway" prompt in a live game, dragging a drawer handle in the editor, drawers on the bottom edge, the pan dial with an isometric tileset (it scrolls in map directions, not screen directions).
5. Text entry prompts (e.g. "Damage for how much?") are upstream's field with no buttons; the soft keyboard has to do.
6. Sheets cannot be dragged down. A plan: give `touch_input::set_raw_drag_rect` a frame stamp so it expires, and pull the sheet by its head. The pause sheet's scroll position also differs from one opening to the next.
7. Pinch still jumps a whole zoom level at a time; the zoom dial moves in quarter levels (`dial_zoom`), and pinch could use the same.
8. The death camera shows no HUD; the prompt button floats over the bare map.
9. Menus padded into columns with `|` are split by `mobile_dialog::split_column`; `Character::dispose_item` ("Stop wielding") has a hook that says why Store and Wear are greyed out. Other upstream menus that grey options out give no reason.
10. The tileset chooser lists upstream's whole `TILES` list, overmap tilesets included. Isometric tilesets are untested with touch.
11. `tools/format/json_formatter` rejects `data/json/ui/mobile_hud/presets.json` (it takes `"rows"` for an array), so the file is formatted by hand.

## Suggested order of work (roadmap phases 0–3)

1. **Build environment:**
   - Desktop tiles build running; a curses build for quick checks.
   - Then an Android debug APK, locally or through a CI workflow on this fork.
2. **Touch plumbing:**
   - Tap next to you acts in that direction; tap further away previews a route, and a second tap travels (reuse `ACTION_SELECT`).
   - Long-press opens the action ring.
   - Zoom and pan are round HUD pieces (`zoom`, `pan`) that you touch and drag, as in the canvas:
     - Zoom: drag up or down; the zoom changes live and snaps to a step on release.
     - Pan: drag in any direction to scroll the view, faster the further you drag; a tap recenters. The Recenter button stays.
     - Both are movable and resizable (`size`) in Edit HUD.
     - Pinch and two-finger drag become a setting, off by default.
   - Feed finger events to ImGui as pointer input, and fix the `cata_key_to_imgui` bugs.
   - Remove the 300 ms double-tap wait where double-tap is not bound.
3. **`MOBILE_HUD` option:**
   - Zero-width sidebar, so the map fills the screen.
   - HUD pieces drawn in the `refresh_display()` hook.
   - Load presets from `data/json/ui/mobile_hud/` in the canvas's layout format.
4. **Then:** drawers, the in-game Edit HUD mode, icon art, and the other screens from the canvas.
