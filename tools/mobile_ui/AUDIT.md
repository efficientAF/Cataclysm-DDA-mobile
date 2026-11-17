# Audit: desktop screens a touch player can still reach (17 Nov 2026)

Made by reading the code at `101f6a19`, not by trying each screen. Line numbers are
from that commit. Every screen below still gets the generic prompt buttons
(`mobile_prompt`), so most can be worked with taps; they look and lay out like the desktop.

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

Nothing here was tried on a Deck.

- **Options are off by default off Android** (`options.cpp:3411-3420`, `mobile_default`).
  Nothing but `options.json` turns `MOBILE_HUD`, `MOBILE_MENUS` and the `TOUCH_*` options on.
  A Deck build needs a way to default them on (a build flag, a command-line switch or
  detection).
- **Real touch does reach the game on desktop** (`sdltiles.cpp:6936-6948` to
  `touch_input::desktop_finger_event`), through the same code as Android. Risk: the hints
  that stop SDL also sending a touch as a mouse event are set only on Android
  (`sdltiles.cpp:676-680`), so on a real touchscreen each tap may arrive twice (finger and
  mouse). The scripted tests never produce those mouse events. Steam Input may also turn
  the touchscreen into a mouse.
- **The controller works where upstream's bindings do** (`sdl_gamepad.cpp`; 314 gamepad
  bindings): the play screen, the radial menus, upstream's own menus, and probably `uilist`
  sheets (their input loop is upstream's; not tested).
- **The mobile screens cannot be worked with a controller.** They register only QUIT (so B
  backs out) and act on taps through `mobile_ui::pressable`; none handles up, down or
  confirm, and there is no focus highlight. Exceptions: the direction ring takes
  directions. This is the main piece of work for a Deck: a focus model for `pressable`
  (ImGui navigation, or one of our own) and a visible focus ring.
- **Android-only pieces the Deck lacks:** the manual-key prompt buttons (uilist hotkeys,
  the debug-message keys), re-layout after a text size change
  (`request_terminal_relayout`), the back key, the keyboard insets. Whether SDL opens
  Steam's on-screen keyboard for text fields is unknown.

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
