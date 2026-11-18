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
  - Round 5 (sixth session, 17 Nov 2026; AUDIT.md groups 1 to 4), each built by a worker in its own worktree:
    - Item action sheet: `game::inventory_item_menu` shows as a `uilist` bottom sheet on mobile (no bounds, no curses pane): "Details" first (`mobile_picker::show_details`), then only the actions not rated `cant`, without "reassign". The switch that runs them is upstream's.
    - `mobile_around`: the nearby list (`surroundings_menu::mobile_action`, called from `execute()`): a `mobile_list` with upstream's three tabs that hands back upstream's action names. `mobile_describe`: look-around's Describe as a page with a chip per category (`extended_description_window::mobile_show`).
    - `mobile_body`: body status and medical (`body_status_ui::display`), morale (`player_morale::display`), sort armor (`outfit::sort_armor`), blood analysis (`Character::conduct_blood_analysis`), all on `mobile_list`.
    - `mobile_diary`: the diary (`diary::mobile_show`) and a full-screen multi-line text card that stands in for `string_editor_window::query_string`. Scores has a button at the end of the Actions drawer's "You" tab and on the diary. `mobile_death::show_page` shows the NPC and faction epilogues; "Open diary for the last time?" goes straight to the text card.
    - `mobile_camp`: camp and companion missions (`talk_function::display_and_choose_opts`), worker and job assignment (`basecamp::worker_assignment_ui`, `job_assignment_ui`), follower rules (`follower_rules_ui::draw_follower_rules_ui`), on `mobile_list`.
    - Choosing a point on the map: `mobile_overmap::wanted` now takes every map but the debug editor and the scent view. In pick mode the message is a banner, the tile sheet says "Choose this place" and returns upstream's `CONFIRM`; out of range greys the button and says why; the range edge is a red ring.
    - Crafting: `select_crafter_and_crafting_recipe` in `crafting_gui.cpp` hands over to `mobile_crafting::select`, so the workbench, long craft, view recipe and camp crafting all get the mobile screen.
    - `mobile_compare`: the comparison result (`compare_item_menu`), two columns in landscape, a name switch in portrait; also the gunmod attach and remove questions. `mobile_richtext`: wrapped text that keeps colour tags.
    - `mobile_terminal`: computer terminals (`computer_session`): the lines on a page, the options as buttons, "any key" as Continue. `mobile_vending`: vending machines on `mobile_list`.
    - Menus that set `desired_bounds` skip them on mobile, per caller: vehicle menus (`veh_menu::query`), the appliance screen (name and power as the sheet's heading), the teleporter list. The spellbook is a `mobile_list`.
    - New game: the world card shows the world that will be used and "Change" chooses among the worlds (`choice::world` to `mobile_new_character_world`); `worldfactory::pick_world` is a sheet as a fallback. `choose_ma_style` is a `mobile_list`. The creator's Summary step has "DETAILS": age, height, blood type, start dates, starting city (only with `SELECT_STARTING_CITY`), random description, Save template.
    - `mobile_prompt::extra_keys`: the debug-message screen's Continue / Ignore / Copy buttons, on desktop too.
- Tests: `tests/touch_input_test.cpp` (`[touch_input]`), `tests/mobile_prompt_test.cpp`.
- `tools/mobile_ui/`: the desktop test kit (`run.sh`, `drive.sh`, `build.sh`), an NDK syntax check, and `BRIEF.md`, the brief used for workers building one screen each.

### Status and open work (17 Nov 2026, sixth session)

#### Round 6 (seventh session, 18 Nov 2026): read this first

Everything is on the session branch `ccr-b20216f4-3gkwje` (pushed), **not yet merged into `mobile-ui`**. It builds on desktop (TILES) and the two unit-test tags pass. The three workers (`r6-map`, `r6-veh`, `r6-sys`, merged) were cut off by the usage limit before they reported, so what they saw on screen is **not known**; each branch ends in a commit "WIP when the worker stopped", which compiles but was seen by nobody. Nothing was run on a phone, a Steam Deck or with a controller. No APK or Gradle build was made; nothing new is a type or namespace named `detail` (a local variable of that name is harmless).

Built by the main session and seen on the desktop build (landscape; where it says so, portrait too):
- **Backdrop** (`mobile_ui::snapshot_backdrop`, `dim_screen`, `mobile_copy_display_buffer` in `sdltiles.cpp`): a list screen (`mobile_list::show_all`) or the inventory copies its last frame as it closes for an action, and the sheet, question or entry card that follows draws that picture under its dimming. The first frame that does not use it drops it. Seen: the item action sheet over the inventory; zones' Edit sheet and the name card over the Zones list (landscape and portrait).
- **HUD around sheets** (`mobile_ui::sheet_area`, `note_map_drawn`, `sheet_over_play_screen`; `mobile_hud::draw`): while a sheet, dialog, entry card or drawer is open and the game drew the map in that frame, the HUD pieces are drawn at half strength in the strips beside and above it, untouchable. This is what the "black" pause sheet was: the map was there, the HUD was not. Seen: pause sheet (both orientations), Log drawer.
- Drawers: window one pixel short and `keep_map_under()` each frame; their darkening used to add up to black.
- Android's back key / Escape on the play screen (`ACTION_MAIN_MENU`) opens the pause sheet (`handle_action.cpp`). Seen.
- A sheet from `mobile_ui::show_sheet` opens at its top (it kept the last sheet's scroll position).
- Mobile map: redrawn each frame while its tile sheet or a toast is up (`view::covers_map`); the dimming no longer adds up. Seen (two screenshots six seconds apart are identical).
- `mobile_entry`: a typed space or `q` stays in the field (seen); a description taller than about three lines goes under the buttons, cut at the screen edge (seen on the nearby list's Filter card).
- Calendar sheet: no key help in the heading on mobile (`calendar_ui.cpp`). Seen from the creator's "Start of game".
- "Back on follower rules ends the conversation" is upstream's data (`"topic": "TALK_DONE"` on the reply), not ours.
- Steam Deck groundwork (see `tools/mobile_ui/AUDIT.md`): `mobile_default` (`--mobile-ui`, `CATA_MOBILE_UI`, `SteamDeck=1`), touch-versus-mouse filter, and controller focus: `mobile_ui::pressable` is an ImGui navigation item with a focus ring, and the mobile screens' `BeginChild` areas are `ImGuiChildFlags_NavFlattened` (**use that flag in new screens**). `mobile_dialog`'s rows are `ImGuiItemFlags_NoNav`: upstream's loops move their own selection. Seen with arrow keys and Enter: main menu, New game page; upstream's selection on a sheet in portrait.

Built by the workers; seen by the main session after the merge only where it says so:
- Mobile map (`mobile_overmap.cpp`): Search, Notes and More in the app bar, level up/down with the level shown. **Seen:** the buttons, level up (shows +1), the More sheet (missions, toggles, Mark explored). Not seen by the main session: search results list, note list (Go there, Edit, Danger, Delete), the toggles' effect, any of it in pick mode or portrait.
- Loading screen in the canvas's look (`mobile_system.cpp`, `loading_ui.cpp`). **Seen** while a save loads.
- Vehicle shape: part rows without the damage symbols (that was the `|\?` junk: `vehicle_part::name`'s prefix), the cursor step with the look-around banner and taps (`veh_shape.cpp`). Smart engine controller page (`mobile_engine.{h,cpp}`). Construction list for a blueprint zone (`construction.cpp`). Not seen by the main session.
- `mobile_system`: auto notes and distraction managers on `mobile_list`, both on the Actions drawer's Game tab; swap inventory letters, sidebar panel manager, colour manager and keybindings taken off upstream's action menu and main menu while mobile is on (`drop_desktop_actions`). Pick-up with a preset or a limit goes through `mobile_picker::pick_many` (`pickup_selector::execute`). `Character::disp_info` opens the Info drawer, also for an NPC (`mobile_drawer::show_character`); during a blocking activity the note says to tap, and a tap asks whether to stop. Not seen by the main session.
- In the WIP commits (compile, unseen, may be half done): zones' "Show on map" and a study zone skill picker (`mobile_study.{h,cpp}`, `study_zone_ui.cpp`, `zone_manager_ui.cpp`); exosuit modules (`iuse.cpp`) and "Select door to toggle" (`vehicle_use.cpp`); `do_turn.cpp` puts the blocking-activity context on the input stack.

Still to do from this session's brief:
- **Step 2 was not done**: the "written but seen by nobody" lists below (round 5 and item 0) are unchanged, except the creator's "Start of game" sheet, zones' Edit menu and its name card, which are now seen. Portrait was only checked for the pause sheet and the zones sheet.
- Check the round 6 worker screens above on screen, both orientations; finish or remove the WIP parts.
- The curses build: see the note at the end of this section.
- Steam Deck: try `--mobile-ui`, a real touchscreen and a controller. Known gaps are listed in AUDIT.md.
- The UI canvas has no mockups for round 6; the map's new buttons follow its `.fab` and app bar patterns.
- Rough edges left from round 5: nearby list's first-frame redraw on other screens, "Default ALL" asking first, the missing columns listed below.


`mobile-ui` holds all of the above, round 5 included (merged 18 Nov 2026). The session branches `ccr-*` are all merged into it and can be deleted; the worker branches `r5-*` were never pushed.

The owner runs the APKs on a phone and reports back; this side only tests with scripted touches on the desktop build, so the Android-only gesture code in `sdltiles.cpp` is only known to compile here. Nothing below has been run on a real phone by this side.

Rounds 3 and 4: what was seen is in the git history of this file (fifth session). Still unseen from them: see item 0 below.

Round 5, seen by the main session in the merged desktop build (landscape only): the item action sheet (opened, no action run), the nearby list, Describe on an NPC, Medical, Sort armor, Morale, the New game world card and its "Change" sheet (one world), map pick mode from the debug teleport (not confirmed), follower rules (opened), the diary list and its Export question, long craft's screen, the comparison result in two columns.

Round 5, seen only by the workers in their own builds (screenshots gone with the session): item sheet actions (Wield, Drop, Open, Pocket settings, Details); nearby list's Details, Compare, Filter, Next place, Travel to, Look at, a monster; Describe's chips and furniture; Treat wounds, a broken and bleeding arm, body parts and stats tabs; sort armor's Move in, Take off, Wear something, Sort by layer, Encumbrance, More sheet; cosmetic variant and change-shape sheets; the appliance sheet; the spellbook list and Study; two worlds and starting without a world prompt; the martial-art list; creator Age, blood type, random description, Save template; debug message Continue; diary new page, typing, save, discard, delete, export; Scores from the drawer and the diary; the last-diary question and card; the faction epilogue page; pick mode's range ring and refusal (with a temporary range in the debug teleport); camp missions list and Start; workers; job priorities ("Set all", "Turn off"); follower rule change and engagement sheet; workbench crafting, long craft to the end, view recipe; gunmod "Attach modification?"; terminal login, menu, message, password question, failed hack; vending with and without money, Buy.

Round 5, written but seen by nobody: item sheet's insert, unload, reload, favourite, hide/show contents, auto pickup, and the sheet opened from Move items; nearby list's Fire, safe mode buttons, Sort, Clear filter; Describe's vehicle page, signs and graffiti; bitten, infected, splinted, bandaged or missing limbs, an NPC's status; morale with positive sources; sort armor's Move out, change side, hide; blood analysis with results; the teleporter list; "Select door to toggle"; the starting city row, Height, Start of game; `pick_world`'s own sheet; debug message Ignore and Copy; the NPC epilogue page; the diary's text card with Android's keyboard up (its half-height field is a guess); the zone outline and NPC path on the map, map picking from a real mortar, nuke terminal or camp mission; camp expansion tabs, Recall, single job priority, Clear all, rules Default / Default ALL / import / export; camp crafting, view recipe on a craft in progress; "Remove modification?"; a menu or yes/no over the terminal page, the terminal's landscape layout after its clipping fix; a vending machine emptied or on the bank. The curses build of the new files was not compiled (two workers ran a non-TILES syntax check of theirs). `tools/mobile_ui/ndkcheck.sh` needs `/tmp/ndk_cmd.txt` from a Gradle build and was not run by the workers.

Round 5 rough edges (the first four, the map darkening and the entry card were fixed in round 6, see above):
- The item action sheet shows over black, not over the inventory. Sheets and yes/no questions opened from a list screen show over the map (the known edge below).
- "Start of cataclysm" in the creator opens upstream's calendar as a sheet whose heading still says "Press h, LEFT…" (`calendar_ui.cpp`).
- Vehicle part rows in the shape sheets start with symbol junk such as `|\?`.
- The nearby list's Filter card: upstream's long help pushes the field off the card in landscape; typing still works.
- A full-screen mobile screen opened by a key from look-around drew its first frame under the banner; `mobile_list::show_all` and Describe now redraw twice. Others may need the same.
- Back on the follower rules screen ended the conversation (not checked against upstream). "Default ALL" asks first; upstream does not.
- The map darkens a little more each frame behind an open tile sheet on the mobile map.
- `mobile_entry`'s text card may close on a typed space or `q` (QUIT is bound to them in the "default" context); the diary card guards against it, the entry card was not checked.
- Sort armor: no warmth column, no "equip here"; body status: no ASCII body graph, no speed breakdown. Spellbook: no per-level table. Teleporter: no map preview. Diary: change descriptions only on the Summary.
- Worker worktrees: `setup_worktree.sh` copies the main checkout's objects; if main has moved on since the worktree's base they do not link. Create the worktrees from the commit the objects were built from, or touch the differing sources.

Test tips: `tools/mobile_ui/drive.sh tap` parks the real mouse on the spot first, and in look-around that already moves the cursor, so one scripted tap there confirms where a phone needs two. The debug menu has no key by default; bind one in the test folder's `config/keybindings.json` (`debug`, `DEFAULTMODE`, `"input_method": "keyboard_any"`, e.g. F12; `keyboard_code` does not work). "Spawn NPC follower" in the debug menu gives a real follower; a camp is founded through its dialogue on a field. A worker's test folder may be left in portrait (`TERMINAL_X` 97): the menu then fills half a landscape window. "Debug Hammerspace" (debug menu, Player, Mutate) lifts the requirements for installing and removing vehicle parts, but not for repairing.

**The UI is not finished.** `tools/mobile_ui/AUDIT.md` lists every desktop screen a touch player can still reach, with what round 5 did to each, and what a Steam Deck build (desktop Linux, touch plus controller) needs. The owner wants a full replacement and plans to try a Steam Deck. What is left: the P3 list there (overmap notes and search, exosuit, smart engine controller, vehicle shape cursor, study zone, blueprints, minigames, the System screens, debug tools) and the Steam Deck work, which the owner put off to a later chat: the mobile options are off by default off Android, and the mobile screens cannot be worked with a controller (no focus model).

The UI canvas was brought up to date on 17 Nov 2026 (the quick actions' Order list and extra buttons, Quit game on the title screen, Save and quit game on the pause sheet); the round 3, 4 and 5 screens have no mockups in it, only their patterns in the remaining screens plan.

Also done in the fifth session: "Quit game" on the home screen (its small buttons are four to a row) and "Save and quit game" at the end of the pause sheet (`mobile_menu::request_quit_after_save`, consumed at the top of `main_menu::opening_screen`); both seen working on the desktop build, the home screen in landscape only.

Open, most important first:

0. **Written but not seen on screen:**
   - Factions: the screen opens with its three tabs, all empty. Rows for a camp, a follower (Talk, radio, recall) and a faction were not seen; the follower the debug menu spawns is not on the game's follower list.
   - Bionics: no character with bionics was tried, so the bionics screen and its new Install / Uninstall weapon buttons are unseen. Spells likewise.
   - Vehicle screen: the Repair / Replace button (needs real tools and materials), an install or removal running to the end (it was too dark in the test), unload, mend, label, crew, shape.
   - Pocket settings: "Move an item", the whitelist and blacklist choices, presets.
   - Trade: the bank account row; a trade that the NPC refuses.
   - Zones: Edit (opens upstream's "What do you want to change" sheet, seen) beyond its first menu; personal zones.
1. **Known rough edges:**
   - A popup that follows a list screen (e.g. the vehicle screen's "too dark" message) shows over the map, not over the list: the list's window is closed by then.
   - The pause sheet (`mobile_menu`) still has a black background.
2. **Still upstream's:** see `tools/mobile_ui/AUDIT.md`. Keybindings, the colour manager and the ImGui style and demo screens are hidden on purpose.

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
