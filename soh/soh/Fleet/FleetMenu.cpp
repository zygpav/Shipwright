#include "FleetMenu.h"

#include <imgui.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <map>
#include <utility>
#include <vector>

extern "C" bool FleetGame_IsHosted(void);
extern "C" void* FleetGame_GetHostFunction(const char* name);

namespace {

constexpr const char* SelfName = "oot";

std::map<std::string, std::function<void()>>& Panels() {
    static std::map<std::string, std::function<void()>> panels;
    return panels;
}

bool sActive = true;
std::vector<std::function<void()>> sDeferred;

const char* Title(const std::string& game) {
    return game == "oot" ? "Ocarina of Time" : game == "mm" ? "Majora's Mask" : game.c_str();
}

const char* World(const std::string& game) {
    return game == "oot" ? "Hyrule" : "Termina";
}

void DrawLocal(const char* panel) {
    auto found = Panels().find(panel);
    if (found == Panels().end() || !found->second) {
        ImGui::TextDisabled("Nothing for %s here.", Title(SelfName));
        return;
    }
    found->second();
}

void DrawGame(const std::string& game, const char* panel) {
    ImGui::PushID(game.c_str());
    if (game == SelfName) {
        DrawLocal(panel);
    } else {
        using DrawGamePanelFn = bool (*)(const char* game, const char* panel);
        auto draw = reinterpret_cast<DrawGamePanelFn>(FleetGame_GetHostFunction("DrawGamePanel"));
        if (draw == nullptr || !draw(game.c_str(), panel)) {
            ImGui::TextWrapped("%s has not started yet. Travel to %s once (Song of Soaring) and its options show "
                               "up here.",
                               Title(game), World(game));
        }
    }
    ImGui::PopID();
}

} // namespace

void FleetMenu_RegisterPanel(const std::string& panel, std::function<void()> draw) {
    Panels()[panel] = std::move(draw);
}

void FleetMenu_DrawSplit(const char* panel, FleetSplit split) {
    if (!FleetGame_IsHosted()) {
        DrawLocal(panel);
        return;
    }
    const std::string games[2] = { "oot", "mm" };
    ImGui::PushID(panel);
    if (split == FleetSplit::Columns &&
        ImGui::BeginTable("fleet.split", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
        for (const auto& game : games) {
            ImGui::TableSetupColumn(Title(game), ImGuiTableColumnFlags_WidthStretch);
        }
        ImGui::TableNextRow();
        for (int column = 0; column < 2; ++column) {
            ImGui::TableSetColumnIndex(column);
            ImGui::SeparatorText(Title(games[column]));
            DrawGame(games[column], panel);
        }
        ImGui::EndTable();
    } else {
        for (const auto& game : games) {
            ImGui::PushID(game.c_str());
            const bool open = ImGui::CollapsingHeader(Title(game), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopID();
            if (open) {
                ImGui::Indent(6);
                DrawGame(game, panel);
                ImGui::Unindent(6);
                ImGui::Spacing();
            }
        }
    }
    ImGui::PopID();
}

bool FleetMenu_IsActive() {
    return sActive;
}

void FleetMenu_Defer(std::function<void()> action) {
    if (!action) {
        return;
    }
    if (sActive) {
        action();
    } else {
        sDeferred.push_back(std::move(action));
    }
}

void FleetMenu_RunDeferred() {
    if (sDeferred.empty()) {
        return;
    }
    auto actions = std::move(sDeferred);
    sDeferred.clear();
    for (auto& action : actions) {
        action();
    }
}

size_t FleetMenu_DeferredCount() {
    return sDeferred.size();
}

void FleetMenu_InactiveNote(const char* what) {
    ImGui::TextDisabled("%s: open it while playing %s.", what, Title(SelfName));
}

extern "C" bool FleetMenu_DrawPanel(const char* panel, bool active) {
    if (panel == nullptr) {
        return false;
    }
    // ImGui's current context is a per-module global: make sure this game's copy points at the shared one.
    ImGui::SetCurrentContext(Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetImGuiContext());
    const bool previous = sActive;
    sActive = active;
    DrawLocal(panel);
    if (!active && !sDeferred.empty()) {
        ImGui::TextDisabled("%zu change(s) apply when you return to %s.", sDeferred.size(), World(SelfName));
    }
    sActive = previous;
    return true;
}
