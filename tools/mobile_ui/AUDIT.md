# Audit: desktop screens a touch player can still reach (17 Nov 2026)

Made by reading the code at `101f6a19`, not by trying each screen. Line numbers are
from that commit. Every screen below still gets the generic prompt buttons
(`mobile_prompt`), so most can be worked with taps; they look and lay out like the desktop.

## Round 5 (17 Nov 2026, sixth session): what is done

Groups 1 to 4 of the suggested order are built. What was and was not seen on screen is in
CLAUDE.md's Status section. By item number below:

- **Done:** 1 (without "reassign"), 2, 3, 4 (all four entry points; choosing another crafter
  is not built), 5 (with the gunmod questions), 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 (with a
  direct Scores button on the Actions drawer), 17, 18, 19, 20, 21, 29, and 37 (random
  description included).
- **Still open:** 22 to 28, 30 to 36, and the Steam Deck section. Also "show zone on map":
  the map now takes it, but the mobile zone list has no button for it.
- The sheet test in `uilist.cpp` is unchanged; callers skip `desired_bounds` on mobile. Still
  setting bounds, and still upstream's window: `editmap.cpp`, the `wish.cpp` debug menus, the
  pocket selector at `item_contents.cpp:3111`.
- The map test now only refuses the debug editor and the scent view.

## Two rules that decide most of it

- **Sheet test** (`src/uilist.cpp:45-49`): a `uilist` becomes a bottom sheet only if it has
  no callback (or the callback's `mobile_sheet_ok()` is true), no categories and **no
  `desired_bounds`**. Only `pointmenu_cb`, `veh_menu_cb` and `actmenu_cb` say they are fine.
  A menu that sets `desired_bounds` falls back to upstream's window even when its callback
  says the sheet is fine.
- **Map test** (`mobile_overmap::wanted`, `src/mobile_overmap.cpp:48-53`): false whenever a
  message, a distance, a path or a zone is set. So every `ui::omap::choose_point` call opens
  upstream's map with its sidebar and keyboard cursor.

## P1: ordinary daily play

1. **Item action menu** (`game::inventory_item_menu`, `game.cpp:2123-2460`): the list sets
   `desired_bounds` (2248) and draws a curses item pane (2254). Reached from the mobile
   inventory's "More…" and "Open" (`mobile_inventory.cpp:695-706`, `game_inventory.cpp:336`).
   Not covered at all. From it: "view recipe" opens upstream's crafting window, and
   "reassign" needs a typed key.
2. **List items and monsters** (`surroundings_menu::execute`, `surroundings_menu.cpp:1195`):
   ImGui window. Reached from the Actions drawer ("Around") and look-around's "Items nearby".
3. **Extended description** (`extended_description_window`, `game.cpp:6298`): look-around's
   "Describe".

## P2: every few hours, or one style of play

4. **Upstream crafting window** (`crafting_gui.cpp:2977`): only `ACTION_CRAFT` is hooked.
   Still upstream: workbench "Craft items" (`iexamine.cpp:7370`), `ACTION_LONGCRAFT` (on the
   Actions drawer, `handle_action.cpp:2885`), "view recipe" (`game.cpp:1925/1935/1963`), camp
   crafting (`faction_camp.cpp:3328`).
5. **Item comparison** (`game_menus::inv::compare_item_menu`): the two-column window. Also the
   gunmod attach/remove confirmations (`iuse.cpp:5042`, `iuse_actor.cpp:4746`).
6. **Body status / Medical** (`body_status_ui.cpp:200`, callback plus bounds at 331-334).
7. **Sort armor** (`armor_layers.cpp:594`): curses.
8. **Morale** (`morale.cpp:530`): curses.
9. **Appliance screen** (`veh_appliance.cpp:726`, `desired_bounds` at 234).
10. **Vehicle menus whose rows carry a map location** (`veh_utils.cpp:519-521` sets
    `desired_bounds`): "Select door to toggle" (`vehicle_use.cpp:147`) and "Choose cosmetic
    variant" (`veh_shape.cpp:145`). Looks like a bug: the callback says the sheet is fine.
11. **Computer terminals** (`computer_session.cpp:153`): the terminal text is curses; its
    option menu is a sheet.
12. **Vending machine** (`iexamine.cpp:1046`): curses.
13. **World picker for a new character** (`worldfactory::pick_world`, `worldfactory.cpp:465`):
    curses, shown when there are two or more worlds (`main_menu.cpp:1115, 1163`). The New game
    screen's world card shows only the first world and its "Change" opens Manage world.
14. **Martial-art choice after character creation** (`choose_ma_style`,
    `newcharacter.cpp:383`): uilist with a callback.
15. **Death: NPC and faction epilogues, last diary** (`game.cpp:2950-3074`, `diary.cpp:514`):
    curses text.
16. **Diary and the long text editor** (`diary_ui.cpp:175`, `string_editor_window.cpp:364`).
    Scores is only reachable through the diary.
17. **Camps and followers**: mission list (`mission_companion.cpp:775`), worker and job
    assignment (`faction_camp.cpp:2081, 2165`), follower rules (`npctalk_rules.cpp:62`).
18. **Choosing a point on the map** (`ui::omap::choose_point`, `overmap_ui.cpp:2836`): map
    terminals, camp placement and missions, mortars, NPC travel; also "show zone on map".
19. **Debug-message screen** (`debug.cpp:313`): waits for space, I or C. The buttons for those
    keys exist only on Android (`mobile_prompt.cpp:434-443`), so a desktop or Deck build
    needs a real key.

## P3: rare, debug, or not for phones

20. Spellbook study (`iuse_actor.cpp:2741`).
21. Teleporter list (`magic_teleporter_list.cpp:206`).
22. Overmap note list, search, danger marks, level up/down: the mobile map has no way to
    them (`mobile_overmap.cpp` sends only zoom, centre, quit, choose destination).
23. Exosuit modules (`iuse.cpp:1669`).
24. Smart engine controller (`smart_controller_ui.cpp:136`).
25. Vehicle shape cursor (`veh_shape.cpp:33`).
26. Study zone skill picker (`study_zone_ui.cpp:40`).
27. Blueprint construction (`construction.cpp:543`, the hook needs `!blueprint`).
28. Full character sheet and proficiencies where `ACTION_PL_INFO` is not the way in: NPC
    stats, keys pressed during a blocking activity (`do_turn.cpp:259`), debug.
29. Blood analysis (`character_health.cpp:412`).
30. Minigames (`iuse_software_*.cpp`).
31. Through the pause sheet's "All actions", System: sidebar panel manager, auto notes,
    distraction manager, colour manager (all curses).
32. Debug tools: wish menus, map editor, debug console, overmap editor.
33. Hidden on purpose, still in the code: keybindings, ImGui style and demo screens.
34. Pick-up with a preset selection or a volume/mass limit (`game_inventory.cpp:2546`): NPC
    dialogue, telekinesis-type spells.
35. Swap inventory letters: the picker is mobile, then a letter must be typed.
36. Loading screen: not interactive, desktop look.
37. **Missing from the mobile character creator** (no route at all): age, height, blood type,
    the two start dates, starting city, Save template, random description
    (`newcharacter.cpp:3520-3640` handlers are never sent).

## Steam Deck (Linux desktop build, 1280x800 touch plus gamepad)

Nothing here was tried on a Deck, with a controller or on a real touchscreen. What is
marked "seen" was seen on the desktop build under Xvfb with keys standing in for the d-pad.

- **Done (18 Nov 2026): the options default to on** (`src/mobile_default.{h,cpp}`,
  `options.cpp`): with `--mobile-ui`, with `CATA_MOBILE_UI=1` in the environment, when Steam
  sets `SteamDeck=1`, or in a build with `-DCATA_MOBILE_UI_DEFAULT`. `CATA_MOBILE_UI=0` keeps
  them off. Defaults only: an `options.json` written by an earlier run keeps its values, so a
  Deck that already ran the desktop UI must switch `MOBILE_HUD`, `MOBILE_MENUS` and the
  `TOUCH_*` options on by hand (Options, Interface). Not seen: no run was started with the
  switch.
- **Done, not testable here: touch versus mouse.** With `TOUCH_IMGUI_POINTER` or
  `TOUCH_TAP_TO_ACT` on, the hints that stop SDL reporting a finger as a mouse are set on
  desktop too (`sdltiles.cpp`, where the window is made), and mouse events that carry
  `SDL_TOUCH_MOUSEID` are dropped at the top of the event loop (`mobile_touch_mouse_copy`).
  Steam Input can still turn the touchscreen into a real mouse (then there are no finger
  events at all, and taps arrive as clicks: ImGui screens work, the HUD and tap-to-act do
  not). To check on a Deck.
- **Done: controller focus on the mobile screens.** `mobile_ui::pressable` is an ImGui
  navigation item (`ImGuiButtonFlags_EnableNav`): the arrow keys, and through ImGui's SDL
  backend the d-pad and left stick, move a focus ring (`focus_ring` in `mobile_ui.cpp`,
  drawn over the control); Enter, Space or A presses it; Escape or B is still QUIT. The
  ring shows after the first direction key and hides on a touch. Scrolling areas are
  `ImGuiChildFlags_NavFlattened`, so the focus moves into them and they scroll to it.
  Seen with keys: main menu, New game page (focus moves into the scrolling cards and
  scrolls), Enter opening a page. Not seen: any screen with a real controller.
  - Upstream's `uilist` sheets and `query_popup` questions keep upstream's own key and
    controller handling (their rows are `ImGuiItemFlags_NoNav`), with the selected row
    shown as before.
  - The mobile main menu no longer passes keys on to upstream's menu underneath
    (`main_menu.cpp`): before, Enter also confirmed upstream's hidden selection.
  - Known gaps: the HUD pieces on the play screen are SDL-drawn and have no focus (the
    controller plays through upstream's bindings there); sliders, steppers and switches that
    are not `pressable` (ImGui widgets in Settings) follow ImGui's own rules; text fields
    need the system keyboard (whether SDL opens Steam's on-screen keyboard is unknown); a
    screen opens with nothing focused until a direction is pressed; ImGui reads the pad once
    per drawn frame, and frames are only drawn on input, so a very short press could be
    missed.
- **The controller works where upstream's bindings do** (`sdl_gamepad.cpp`; 314 gamepad
  bindings): the play screen, the radial menus, upstream's own menus, `uilist` sheets.
- **Android-only pieces the Deck lacks:** the manual-key prompt buttons (uilist hotkeys),
  re-layout after a text size change (`request_terminal_relayout`), the back key, the
  keyboard insets.

## Suggested order

1. The item action menu (1), then the surroundings list (2) and Describe (3): they are met
   every few minutes.
2. The cheap ones: stop `desired_bounds` from blocking sheets where the callback is fine
   (10, 9, 20, 21), the world picker (13), the martial-art choice (14), the creator's missing
   fields (37), the debug-message buttons off Android (19).
3. Pattern C screens on `mobile_list`: body status, morale, sort armor, diary, epilogues,
   camp lists, follower rules (6, 7, 8, 15, 16, 17).
4. Crafting's other entry points (4), comparison as a mobile screen (5), map point picking
   (18), terminals and vending (11, 12).
5. Deck: default the options on, sort out touch versus mouse, then controller focus.
