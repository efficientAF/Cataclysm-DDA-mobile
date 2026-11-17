# CDDA Mobile fork: working notes

This is a fork of CleverRaven/Cataclysm-DDA. Its goal is a mobile-first Android UI: a full-screen map, a customizable HUD with icon status indicators, swipeable drawers, and real touch controls.

## Design sources (read these first)

- **Plan:** https://claude.ai/artifact/CyDCMzQSw8Qt858pM8Tb8T. Goals, analysis of the upstream code, touch model, roadmap and ground rules.
- **UI canvas:** https://claude.ai/artifact/CdnbCxX152E752rzbASjnB. An interactive mockup and the source of truth for UI changes. It covers the main menu, new game, character creation, the play screen with HUD presets (portrait and landscape), inventory, crafting, map, pause and wait sheets, settings and the death screen. The inspector panel shows design notes for each screen.
- **Remaining screens plan:** https://claude.ai/artifact/2CGCTUKcXfwfVB9NDZF7Y7. Inventory of every screen that still falls back to upstream's desktop UI, the eight patterns (A item picker, B map pick mode, C tabbed list with detail, D conversation and trade, E vehicle and construction, F entry and choice sheets, G settings lists, H move items) that should replace them, and the build order. The owner approved it (17 Nov 2026: "go with what makes the most sense"; own keypad for numbers; ring plus tappable tiles; hide keybindings). All four rounds are built (round 4 on 17 Nov 2026, fifth session); what was seen on the desktop build and what was not is listed under Status below. The owner also asked for, and got, an Actions drawer and a Nearby drawer.
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
  - **Run `python3 tools/mobile_ui/stale.py` before `make` whenever a header changed.** With the precompiled header on, the Makefile's dependency files are nearly empty, so `make` does not rebuild the sources that include a changed header. Objects then disagree about a struct's layout and the game crashes in unrelated-looking places (seen 17 Nov 2026: `mobile_list::row`). The script touches every source whose object is older than a header it includes. The Android build (CMake) tracks headers properly.
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
  - Quick actions: the buttons come from a table in `mobile_hud.cpp` (`quick_entries`), named as in `mobile_hud::quick_item_ids()`. A layout may carry `"items"`, the names in the player's order; `quick_order()` puts those first and the rest after. The first `count` − 1 show, Menu is always last. Each orientation has its own order.
- `src/mobile_hud_layout.{h,cpp}`: layouts in the canvas's JSON format. Built-in presets are game data, `data/json/ui/mobile_hud/presets.json` (`"type": "mobile_hud_preset"`); the player's layout is `mobile_hud.json` in the config folder. Positions count from the edge of the canvas's phone; `place()` swaps the canvas's notch insets for the real ones.
- `src/mobile_hud_edit.{h,cpp}`: Edit HUD mode (pause sheet and Settings): drag pieces and drawer handles, tap a piece for options, Presets, Pieces. Edits the orientation the phone is held in. The quick actions' options have an "Order" button: a list of the buttons in the order they appear (`order_window`), where a row is dragged by its grip (a raw-drag area, `touch_input::set_raw_drag_rect`; a drag anywhere else scrolls the list).
- Screens (ImGui, one file pair each): `mobile_menu` (main menu, new game, pause and wait sheets), `mobile_chargen`, `mobile_inventory` (also replaces the pick-up menu; `pick_up_what_fits` hands upstream's pick-up only what can be stashed, because it stops at the first item with no room), `mobile_crafting`, `mobile_drawer` (Info, Log, Actions and Nearby drawers, minimap; Info has the tabs Body, Status, Skills, Traits, Gear, Area and stands in for the character sheet; the Nearby drawer lists `mobile_inventory::nearby_items()` and a tap calls `pick_up_now`, then the drawer returns through `uistate.open_menu` while items remain; the Actions drawer lists the game's actions on six tabs and returns the one tapped through the `ACTION_ACTIONMENU` hook in `handle_action.cpp`; the handles are drawers `actions` and `nearby` in the layout JSON; both open through `mobile_drawer::request` and the `ACTION_ACTIONMENU` hook), `mobile_overmap`, `mobile_settings` (also the Tiles/ASCII switch and the tileset chooser), `mobile_death`, `mobile_dialog` (popups, yes/no, `uilist` as a sheet).
- Shared pieces from the remaining screens plan:
  - `mobile_picker` (pattern A): the item picker. `inventory_pick_selector::execute`, `inventory_multiselector::execute` and `inventory_drop_selector::execute` hand over to it; it lists the selector's own entries, so every menu built on those classes (eat, wear, wield, take off, read, use, drop...) gets it. `ammo_inventory_selector` and `unload_selector` hand over too. Compare, haul and the examiner are still upstream's. `show_details` is the item card: the picker's Details button and upstream's `iteminfo_window::execute` both use it.
  - `mobile_pick` (pattern B): the direction ring. `choose_direction` and `choose_adjacent` in `action.cpp` hand over when no callback is passed; `choose_adjacent_highlight` tells it which tiles qualify and takes a single one without asking. `look_banner` is look-around and aiming (`target_ui::draw_ui_window` in `ranged.cpp` fills it through `set`, with the odds from `mobile_aim_lines`): `game::look_around` keeps its loop, the banner replaces its text window, and a tap (which arrives as `SELECT`) moves the cursor, or confirms on the cursor's own tile.
  - `mobile_list` (pattern C): tabs, a list, and a panel for the selected row with its description and one action button; `show` returns the row whose button was tapped. Users: `mobile_screens` (bionics, mutations, missions, hooked at the top of `avatar::power_bionics`, `avatar::power_mutations`, `game::list_missions`), `character_martial_arts::pick_style`, `known_magic::select_spell`, and `construction_menu` in `construction.cpp` (categories as tabs, requirements as the description, Build runs upstream's `place_construction`).
  - More users of `mobile_list`, hooked inside upstream's own files:
    - Zones: `zone_manager_ui.cpp`. The mobile list stands in for upstream's "redraw, read a key" step and hands back upstream's action names (`ADD_ZONE`, `CONFIRM`, `ENABLE_ZONE`...), so all the zone logic is upstream's.
    - Factions, camps and followers: top of `faction_ui::execute` in `faction_ui.cpp`.
    - Vehicles: `src/mobile_vehicle.cpp` defines extra members of `veh_interact` (`mobile_main_loop`, `mobile_install`, `mobile_overview`...), called from `do_main_loop` and `overview`. Tabs "Parts here" (Repair/Replace, Remove) and "Whole vehicle" (refill, siphon, unload, mend, rename, label, crew, shape); buttons "Install a part" (categories as tabs) and "Other tile" (look-around). It sets `sel_cmd` and friends; upstream's `assign_activity` does the work.
  - Trade (pattern D): a sheet in `trade_ui::perform_trade` (what you give, what you get, settle from bank, offer) over `mobile_picker::pick_many`.
  - `mobile_move` (pattern H): move items. Replaces the advanced inventory (`ACTION_ADVANCEDINV` in `handle_action.cpp`): pick a place, tick items, pick where they go.
  - Compare: `inventory_compare_selector::execute` asks for two items with `mobile_picker::pick_one`, one after the other. The comparison itself is upstream's two-column window.
  - Pocket settings: `pocket_favorite_callback::mobile_show` in `item_contents.cpp` (called from `pocket_management_menu`): one row per pocket with upstream's side-pane text, "Settings" (a sheet of upstream's actions, run through the callback's own `key()`), and "Move an item" (upstream's two steps). Reached from Inventory, an item's "More…", pocket settings.
  - Bionics that hold a weapon: "Install weapon" / "Uninstall weapon" on the bionics screen (`mobile_screens.cpp`), upstream's code from `bionics_ui.cpp`.
  - Sheets, dialogs with buttons and entry cards (`uilist` as a sheet, `query_popup`, `mobile_ui::show_sheet`, `mobile_entry`) have windows one pixel short of the screen and call `mobile_ui::keep_map_under()` before each redraw, so the map shows dimmed behind them. Without both, the screen behind them is black.
  - Notes (`static_popup` and other popups without buttons) get a window only as large as their card (`mobile_dialog::note_rect`, used by `query_popup_impl::get_bounds`). A full-screen window there stops everything under it, the map included, from being redrawn. Notes at the top move below the look-around banner (`mobile_pick::banner_bottom`).
  - Look-around: `TRAVEL_TO`, `EXTENDED_DESCRIPTION` and `LIST_ITEMS` come first in the prompt bar as "Travel here", "Describe", "Items nearby" (`mobile_prompt.cpp`); the banner also names the vehicle part under the cursor. The aiming banner has a "Steadiness" line.
  - Round 4 (pattern G and C), each built by a worker in its own worktree:
    - `mobile_options`: the full options list ("All options" in Settings, and the world options of world creation). `mobile_options::show` stands in for the draw-and-read-a-key loop of `options_manager::show`; everything after that loop (compare, "Save changes?", save, side effects) is upstream's. Search across pages, pages as chips, a switch / stepper / choice sheet / text card per option. `options.h` has a `friend struct mobile_options::access`.
    - `mobile_world`: new world (`show_worldgen_basic` and `show_worldgen_advanced` hand over) and mods (`show_worldgen_tab_modselection`, `show_active_world_mods`), using upstream's `mod_ui` for adding, removing and ordering. "All world options" calls `show_worldgen_tab_options`, which ends in the options screen above.
    - `mobile_rules`: the auto-pickup manager (`auto_pickup::user_interface::show`), the safe mode manager (`safemode::mobile_show`), the rule test results (`show_matches`) and scores (`scores_ui_impl::mobile_show`), all on `mobile_list`.
    - `mobile_help`: help as a list of categories and a page for each (`help_window::show`).
    - Hidden on the phone: the main menu's Settings sheet lists only Options, Autopickup and Safemode (`mobile_menu.cpp`), and the prompt bar has no "Keys" button. Upstream's keybindings window crashed under scripted touches.
  - Drawers are never shallower than 320 units (or the room there is): a share of the height alone is too little on a phone held sideways (`mobile_drawer.cpp`).
  - `mobile_talk` (pattern D): conversation; on a wide screen what was said is on the left and the replies on the right. `dialogue_imgui_impl::draw_controls` hands its history and replies to `mobile_talk::draw` and takes the tapped reply. The other dialogue actions stay in the prompt bar.
  - `mobile_entry` (pattern F): number keypad sheet and text card. Hooked into `string_input_popup::query_string` (standalone popups only) and the ImGui `input_popup` classes, which hide while it shows.
  - Keyboard: `toggle_soft_keyboard()` in `sdltiles.cpp`, a button at the start of the prompt bar and a row on the pause sheet. Android's back key closes the keyboard if it is up and otherwise sends Escape.
- Tests: `tests/touch_input_test.cpp` (`[touch_input]`), `tests/mobile_prompt_test.cpp`.
- `tools/mobile_ui/`: the desktop test kit (`run.sh`, `drive.sh`, `build.sh`), an NDK syntax check, and `BRIEF.md`, the brief used for workers building one screen each.

### Status and open work (17 Nov 2026, fifth session)

`mobile-ui` holds all of the above. The local worker branches `r4-options`, `r4-rules` and `r4-world` are merged and were never pushed. The session branches `ccr-df1babd3-3p6es0`, `ccr-eb648aad-5peoju`, `ccr-b8adc6d9-u8s16x`, `ccr-97367a66-rv15hd` and `ccr-8824ee27-jlf80r` are merged into it and can be deleted.

The owner runs the APKs on a phone and reports back; this side only tests with scripted touches on the desktop build, so the Android-only gesture code in `sdltiles.cpp` is only known to compile here. Nothing below has been run on a real phone by this side.

Seen working on the desktop build in the fourth session: zones (list, add, name, pick both corners, turn off, remove), move items, compare, conversation and trade with an NPC, the vehicle screen (parts and requirements, Remove, Install a part, Other tile, siphon, refill's message, rename), pocket settings (list, Settings sheet, priority), haul through the picker, the look-around buttons, the steadiness line while aiming.

Seen working after that (same day, same way): the quick actions' Order list (drag a row by its grip, scroll elsewhere, the bar and the saved layout follow), the map dimmed behind sheets and cards, the conversation's two columns in landscape, vehicle part names on the direction ring, the "Zone name:" card without upstream's field under it.

Round 4, fifth session. Seen by the main session in the merged build: Help's category list, the Worlds sheet, New world, Mods (active list), World options, the Settings sheet, Settings, All options (General page), the empty auto-pickup manager, and a top-edge Log drawer in landscape with its Map tab. Seen only by the workers, in their own builds (screenshots under `/home/user/tk-opt`, `tk-rules`, `tk-world`, gone with the session): everything else they reported, such as changing and saving options, creating a world and checking its files, adding, removing and ordering mods, adding, testing and removing rules, a help page, the scores tabs.

Round 4, not seen by anyone: the options screen's Previous / Next bar (copying a world), `int_map` options, the language choice with LOCALIZE on, tileset reload or terminal resize after an option change, a mod blocked by a dependency, the Kills tab with kills, Copy and Move down in the rule managers. Scores is only reachable through upstream's diary screen. Help text has no colours. The curses build of the new files was only syntax-checked.

Test tips: `tools/mobile_ui/drive.sh tap` parks the real mouse on the spot first, and in look-around that already moves the cursor, so one scripted tap there confirms where a phone needs two. The debug menu has no key by default; bind one in the test folder's `config/keybindings.json` (`debug`, `DEFAULTMODE`). "Debug Hammerspace" (debug menu, Player, Mutate) lifts the requirements for installing and removing vehicle parts, but not for repairing.

Open, most important first:

0. **Written but not seen on screen:**
   - Factions: the screen opens with its three tabs, all empty. Rows for a camp, a follower (Talk, radio, recall) and a faction were not seen; the follower the debug menu spawns is not on the game's follower list.
   - Bionics: no character with bionics was tried, so the bionics screen and its new Install / Uninstall weapon buttons are unseen. Spells likewise.
   - Vehicle screen: the Repair / Replace button (needs real tools and materials), an install or removal running to the end (it was too dark in the test), unload, mend, label, crew, shape.
   - Pocket settings: "Move an item", the whitelist and blacklist choices, presets.
   - Trade: the bank account row; a trade that the NPC refuses.
   - Zones: Edit (opens upstream's "What do you want to change" sheet, seen) beyond its first menu; personal zones.
1. **The canvas is behind the game in one place:** the quick actions' Order list and the eight extra buttons exist in the game but not in the UI canvas (its `QA` list, the `quick` case of the piece renderer and the `quick` rows of `renderProps`). Add them there: `items` in the layout JSON, a sortable list under "Buttons".
1. **Known rough edges:**
   - A popup that follows a list screen (e.g. the vehicle screen's "too dark" message) shows over the map, not over the list: the list's window is closed by then.
   - Compare ends in upstream's two-column text window; Describe (look-around) opens upstream's text screen. Both are readable in landscape only.
   - The pause sheet (`mobile_menu`) still has a black background.
2. **Still upstream's:** `uilist` menus whose callback draws a pane (body status and others), appliances, the smart engine controller, armor layering, morale, the full character sheet, the diary (and with it the way to scores), camps' mission screens, follower rules, auto-notes and distraction settings, the long text editor, computer terminals. Keybindings, the colour manager and the ImGui style and demo screens are hidden on purpose.

3. **Try on a phone:** the zoom and pan dials, the fixed pad and the floating joystick (upstream's code path, which the desktop stand-in does not exercise), pinch with the setting off (must do nothing on the play screen) and on, the Tiles/ASCII switch in Settings.
4. **ASCII map (`USE_TILES` off)** works with the HUD, taps, pan and the pad. Known gaps: no zoom at all (the dial hides); the letters are the size of the map font, small on a phone, and only the text size setting changes that; the start zoom of `touch_input::default_zoom` does not apply. Without `fetch_tilesets.sh` the only tileset is letters drawn as tiles.
5. **Old screens** still upstream's (see 2 above) get the prompt buttons. The remaining screens plan has mockups for the patterns, not for each screen; add a screen to the canvas before building it where its look is in doubt (`tools/mobile_ui/BRIEF.md`).
6. **Not verified:** the safe-mode "Go back / Move anyway" prompt in a live game, dragging a drawer handle in the editor, drawers on the bottom edge, the pan dial with an isometric tileset (it scrolls in map directions, not screen directions).
7. Sheets cannot be dragged down. A plan: give `touch_input::set_raw_drag_rect` a frame stamp so it expires, and pull the sheet by its head. The pause sheet's scroll position also differs from one opening to the next.
8. Pinch still jumps a whole zoom level at a time; the zoom dial moves in quarter levels (`dial_zoom`), and pinch could use the same.
9. The death camera shows no HUD; the prompt button floats over the bare map.
10. Menus padded into columns with `|` are split by `mobile_dialog::split_column`; `Character::dispose_item` ("Stop wielding") has a hook that says why Store and Wear are greyed out. Other upstream menus that grey options out give no reason.
11. The tileset chooser lists upstream's whole `TILES` list, overmap tilesets included. Isometric tilesets are untested with touch.
12. `tools/format/json_formatter` rejects `data/json/ui/mobile_hud/presets.json` (it takes `"rows"` for an array), so the file is formatted by hand.

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
