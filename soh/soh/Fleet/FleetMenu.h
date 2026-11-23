#pragma once

// Fleet Combo: one menu for both games. A "panel" is a named piece of a game's menu (its NEI settings for a
// category, its inventory editor, its layout editor). Either game's menu draws a page as both games' panels side by
// side or one above the other, calling the other game through the host so that game's own code, settings and save
// are the ones used. Outside Fleet Combo a page shows only this game's panel.

#include <functional>
#include <string>

enum class FleetSplit {
    Columns, // OoT | MM side by side: settings pages
    Stacked, // OoT above MM, each under its own header: wide editors
};

// Registers this game's draw function for a panel.
void FleetMenu_RegisterPanel(const std::string& panel, std::function<void()> draw);
// Draws a page: both games' panels when hosted, this game's alone otherwise.
void FleetMenu_DrawSplit(const char* panel, FleetSplit split);
// False while this game is drawn from the other game's menu: its game loop is paused then, so anything that runs
// game code (mod callbacks, giving items) waits for FleetMenu_Defer to run it once this game is played again.
bool FleetMenu_IsActive();
void FleetMenu_Defer(std::function<void()> action);
// Runs the deferred actions; the game calls it each frame it is active.
void FleetMenu_RunDeferred();
size_t FleetMenu_DeferredCount();
// Shown in place of something that only works in the running game.
void FleetMenu_InactiveNote(const char* what);

extern "C" bool FleetMenu_DrawPanel(const char* panel, bool active);
