#include "ModMenu.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <spdlog/spdlog.h>

#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include <imgui.h>

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

namespace {

struct RegisteredWidget {
    SOHModMenuWidget definition;
    std::string section;
    std::string sidebar;
    std::string label;
    std::string cvar;
    std::string tooltip;
    std::string anchor;
    std::vector<std::string> values;
    bool after = false;
    std::function<void()> draw; // Fleet Combo: gathered pages draw themselves
};

std::deque<RegisteredWidget> sWidgets;

struct RegisteredSidebar {
    std::string section;
    std::string sidebar;
    std::string cvar;
    uint8_t columns;
};

std::deque<RegisteredSidebar> sSidebars;

// The map points into the registered strings, which a deque element never moves.
std::map<int32_t, const char*> BuildComboMap(const std::vector<std::string>& values) {
    std::map<int32_t, const char*> comboMap;
    for (size_t index = 0; index < values.size(); index++) {
        comboMap[static_cast<int32_t>(index)] = values[index].c_str();
    }
    return comboMap;
}

void AddWidget(const RegisteredWidget& registered) {
    const auto& definition = registered.definition;
    WidgetPath path = { registered.section, registered.sidebar, (SectionColumns)definition.column };
    if (!SohGui::mSohMenu->HasModWidgetPath(path)) {
        SPDLOG_ERROR("[ModMenu] Invalid path for widget '{}': {}/{} column {}", registered.label, registered.section,
                     registered.sidebar, definition.column);
        return;
    }
    WidgetType type;
    switch (definition.type) {
        case SOH_MOD_MENU_CHECKBOX:
            type = WIDGET_CVAR_CHECKBOX;
            break;
        case SOH_MOD_MENU_INT_SLIDER:
            type = WIDGET_CVAR_SLIDER_INT;
            break;
        case SOH_MOD_MENU_FLOAT_SLIDER:
            type = WIDGET_CVAR_SLIDER_FLOAT;
            break;
        case SOH_MOD_MENU_SEPARATOR:
            type = WIDGET_SEPARATOR;
            break;
        case SOH_MOD_MENU_TEXT:
            type = WIDGET_TEXT;
            break;
        case SOH_MOD_MENU_CUSTOM:
            type = WIDGET_CUSTOM;
            break;
        case SOH_MOD_MENU_COMBOBOX:
            type = WIDGET_CVAR_COMBOBOX;
            break;
        default:
            SPDLOG_ERROR("[ModMenu] Unknown widget type {} for '{}'", (int32_t)definition.type, registered.label);
            return;
    }

    auto& widget = SohGui::mSohMenu->AddWidget(path, registered.label, type);
    if (!registered.cvar.empty()) {
        widget.CVar(registered.cvar.c_str());
    }
    switch (definition.type) {
        case SOH_MOD_MENU_CHECKBOX:
            widget.Options(UIWidgets::CheckboxOptions()
                               .DefaultValue(definition.defaultInt != 0)
                               .Tooltip(registered.tooltip.c_str()));
            break;
        case SOH_MOD_MENU_INT_SLIDER:
            widget.Options(UIWidgets::IntSliderOptions()
                               .DefaultValue(definition.defaultInt)
                               .Min(definition.minInt)
                               .Max(definition.maxInt)
                               .Tooltip(registered.tooltip.c_str()));
            break;
        case SOH_MOD_MENU_FLOAT_SLIDER:
            widget.Options(UIWidgets::FloatSliderOptions()
                               .DefaultValue(definition.defaultFloat)
                               .Min(definition.minFloat)
                               .Max(definition.maxFloat)
                               .Tooltip(registered.tooltip.c_str()));
            break;
        case SOH_MOD_MENU_TEXT:
            widget.Options(UIWidgets::TextOptions().Tooltip(registered.tooltip.c_str()));
            break;
        case SOH_MOD_MENU_COMBOBOX:
            widget.Options(UIWidgets::ComboboxOptions()
                               .ComboMap(BuildComboMap(registered.values))
                               .DefaultIndex(static_cast<uint32_t>(definition.defaultInt))
                               .Tooltip(registered.tooltip.c_str()));
            break;
        default:
            break;
    }
    if (registered.draw) {
        widget.customFunction = [draw = registered.draw](WidgetInfo&) { draw(); };
    } else if (definition.onChange != nullptr) {
        if (definition.type == SOH_MOD_MENU_CUSTOM) {
            widget.customFunction = [callback = definition.onChange](WidgetInfo&) { callback(); };
        } else {
            widget.Callback([callback = definition.onChange](WidgetInfo&) { callback(); });
        }
    }
    if (!registered.anchor.empty()) {
        if (!SohGui::mSohMenu->MoveModWidget(path, registered.label, registered.anchor, registered.after)) {
            SPDLOG_WARN("[ModMenu] Anchor '{}' not found for widget '{}'", registered.anchor, registered.label);
        }
    }
}

void AddSidebar(const RegisteredSidebar& sidebar) {
    SohGui::mSohMenu->EnsureModSidebar(sidebar.section, sidebar.sidebar, sidebar.cvar.c_str(), sidebar.columns);
}

bool IsMenuBuilt() {
    return SohGui::mSohMenu != nullptr && SohGui::mSohMenu->AreMenuElementsInitialized();
}

void RegisterWidgets() {
    for (const auto& sidebar : sSidebars) {
        AddSidebar(sidebar);
    }
    for (const auto& widget : sWidgets) {
        AddWidget(widget);
    }
}

RegisterMenuInitFunc sMenuInit(RegisterWidgets);

bool IsFilled(const char* text) {
    return text != nullptr && text[0] != '\0';
}

bool HasComboValues(const SOHModMenuWidget* widget) {
    return widget->structSize >= SOH_MOD_MENU_WIDGET_COMBOBOX_SIZE && widget->values != nullptr &&
           widget->valueCount > 0;
}

bool IsValidWidget(const SOHModMenuWidget* widget) {
    if (widget == nullptr || widget->structSize < SOH_MOD_MENU_WIDGET_MIN_SIZE) {
        return false;
    }
    if (!IsFilled(widget->section) || !IsFilled(widget->sidebar) || !IsFilled(widget->label) || widget->column > 2) {
        return false;
    }
    switch (widget->type) {
        case SOH_MOD_MENU_CHECKBOX:
        case SOH_MOD_MENU_INT_SLIDER:
        case SOH_MOD_MENU_FLOAT_SLIDER:
            return IsFilled(widget->cvar);
        case SOH_MOD_MENU_COMBOBOX:
            return IsFilled(widget->cvar) && HasComboValues(widget);
        case SOH_MOD_MENU_SEPARATOR:
        case SOH_MOD_MENU_TEXT:
        case SOH_MOD_MENU_CUSTOM:
            return true;
        default:
            return false;
    }
}

std::vector<std::string> CopyComboValues(const SOHModMenuWidget* widget) {
    std::vector<std::string> values;
    if (widget->type != SOH_MOD_MENU_COMBOBOX) {
        return values;
    }
    for (uint32_t index = 0; index < widget->valueCount; index++) {
        values.emplace_back(widget->values[index] == nullptr ? "" : widget->values[index]);
    }
    return values;
}


// Fleet Combo: mods each add their own menu pages. Gather them into one "NEI" section, one page per category. Each
// page is drawn by a single custom widget: every mod gets a collapsible header holding its settings, and mod test
// tools go to Dev Tools. Drawing the gathered settings here does not depend on the menu's per-sidebar column rules.
struct MenuHome {
    std::string section;
    std::string sidebar;
    std::string header; // empty: no header
};

MenuHome RemapModMenu(const std::string& section, const std::string& sidebar) {
    if (section != "Mods") {
        return { section, sidebar, "" };
    }
    static const std::map<std::string, std::string> kCategory = {
        { "Majora's Mask", "Items & Masks" },  { "Ocarina of Time", "Items & Masks" }, { "Items", "Items & Masks" },
        { "Equipment", "Equipment" },          { "Ikana Shield", "Equipment" },        { "Partners", "Companions" },
        { "Companions", "Companions" },        { "Gameplay", "Gameplay" },
    };
    if (sidebar == "Item Drop Tester" || sidebar == "Animation Viewer" || sidebar == "Debug") {
        return { "Dev Tools", "Mod Tools", sidebar };
    }
    auto category = kCategory.find(sidebar);
    if (category != kCategory.end()) {
        return { "NEI", category->second, sidebar };
    }
    return { "NEI", "Other", sidebar };
}

struct GatheredGroup {
    std::string name;
    std::vector<RegisteredWidget*> widgets; // deque elements never move
};

struct GatheredPage {
    std::string section;
    std::string sidebar;
    std::vector<GatheredGroup> groups;
};

std::deque<GatheredPage> sGatheredPages;
std::deque<RegisteredWidget> sGatheredWidgets;

void DrawGatheredWidget(RegisteredWidget& registered) {
    const auto& definition = registered.definition;
    const char* label = registered.label.c_str();
    const char* cvar = registered.cvar.c_str();
    bool changed = false;
    ImGui::PushID(&registered);
    switch (definition.type) {
        case SOH_MOD_MENU_CHECKBOX:
            changed = UIWidgets::CVarCheckbox(label, cvar,
                                              UIWidgets::CheckboxOptions()
                                                  .DefaultValue(definition.defaultInt != 0)
                                                  .Tooltip(registered.tooltip)
                                                  .Color(THEME_COLOR));
            break;
        case SOH_MOD_MENU_INT_SLIDER:
            changed = UIWidgets::CVarSliderInt(label, cvar,
                                               UIWidgets::IntSliderOptions()
                                                   .DefaultValue(definition.defaultInt)
                                                   .Min(definition.minInt)
                                                   .Max(definition.maxInt)
                                                   .Tooltip(registered.tooltip)
                                                   .Color(THEME_COLOR));
            break;
        case SOH_MOD_MENU_FLOAT_SLIDER:
            changed = UIWidgets::CVarSliderFloat(label, cvar,
                                                 UIWidgets::FloatSliderOptions()
                                                     .DefaultValue(definition.defaultFloat)
                                                     .Min(definition.minFloat)
                                                     .Max(definition.maxFloat)
                                                     .Tooltip(registered.tooltip)
                                                     .Color(THEME_COLOR));
            break;
        case SOH_MOD_MENU_COMBOBOX: {
            static std::map<const RegisteredWidget*, std::map<int32_t, const char*>> sComboMaps;
            auto& comboMap = sComboMaps[&registered];
            if (comboMap.empty()) {
                comboMap = BuildComboMap(registered.values);
            }
            changed = UIWidgets::CVarCombobox(label, cvar, comboMap,
                                              UIWidgets::ComboboxOptions()
                                                  .DefaultIndex(static_cast<uint32_t>(definition.defaultInt))
                                                  .Tooltip(registered.tooltip)
                                                  .Color(THEME_COLOR));
            break;
        }
        case SOH_MOD_MENU_SEPARATOR:
            ImGui::Separator();
            break;
        case SOH_MOD_MENU_TEXT:
            ImGui::TextWrapped("%s", label);
            break;
        case SOH_MOD_MENU_CUSTOM:
            if (definition.onChange != nullptr) {
                definition.onChange(); // a custom widget's callback is its draw function
            }
            break;
        default:
            break;
    }
    if (changed && definition.type != SOH_MOD_MENU_CUSTOM && definition.onChange != nullptr) {
        definition.onChange();
    }
    ImGui::PopID();
}

void DrawGatheredPage(const GatheredPage& page) {
    if (page.groups.empty()) {
        ImGui::TextDisabled("No mod settings here.");
        return;
    }
    for (const auto& group : page.groups) {
        ImGui::PushID(group.name.c_str());
        if (ImGui::CollapsingHeader(group.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            for (auto* widget : group.widgets) {
                DrawGatheredWidget(*widget);
            }
            ImGui::Unindent();
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
}

void DrawFleetComboPage() {
    static const std::map<int32_t, const char*> kEra = {
        { -1, "Follow Link's age" }, { 0, "Child era (past)" }, { 1, "Adult era (future)" }
    };
    ImGui::TextWrapped("Settings for the combined OoT + MM game.");
    ImGui::Spacing();
    UIWidgets::CVarCombobox("World Era", "gFleetCombo.Era", kEra,
                            UIWidgets::ComboboxOptions()
                                .DefaultIndex(static_cast<uint32_t>(-1))
                                .Tooltip("Which Hyrule you walk in. 'Follow Link's age' is the normal game. Lock it "
                                         "to bring adult Link into the past or child Link into the future. Takes "
                                         "effect at the next area.")
                                .Color(THEME_COLOR));
    UIWidgets::CVarCheckbox("Any Link uses every item", "gFleetCombo.AnyAgeItems",
                            UIWidgets::CheckboxOptions()
                                .DefaultValue(true)
                                .Tooltip("Child, adult and MM Link can all use every item (Timeless Equipment).")
                                .Color(THEME_COLOR));
    UIWidgets::CVarCheckbox("Door portals between worlds", "gFleetCombo.DoorPortals",
                            UIWidgets::CheckboxOptions()
                                .DefaultValue(false)
                                .Tooltip("Also travel through the Happy Mask Shop / Clock Tower doors. The Song of "
                                         "Soaring always works.")
                                .Color(THEME_COLOR));
}

void AddCustomPageWidget(const std::string& section, const std::string& sidebar, std::function<void()> draw) {
    RegisteredWidget page = {};
    page.definition.structSize = sizeof(page.definition);
    page.definition.type = SOH_MOD_MENU_CUSTOM;
    page.section = section;
    page.sidebar = sidebar;
    page.label = "##gathered." + section + "." + sidebar;
    page.draw = std::move(draw);
    sWidgets.push_back(std::move(page));
    if (IsMenuBuilt()) {
        AddWidget(sWidgets.back());
    }
}

void EnsureSidebar(const std::string& section, const std::string& sidebar, const std::string& cvar) {
    const bool known = std::any_of(sSidebars.begin(), sSidebars.end(), [&](const auto& registered) {
        return registered.section == section && registered.sidebar == sidebar;
    });
    if (known) {
        return;
    }
    sSidebars.push_back({ section, sidebar, cvar, 1 });
    if (IsMenuBuilt()) {
        AddSidebar(sSidebars.back());
    }
}

GatheredPage& GetGatheredPage(const std::string& section, const std::string& sidebar) {
    for (auto& page : sGatheredPages) {
        if (page.section == section && page.sidebar == sidebar) {
            return page;
        }
    }
    const std::string cvar = "gFleetCombo.Menu." + section + "Page";
    if (section == "NEI" && sGatheredPages.empty()) {
        EnsureSidebar("NEI", "Fleet Combo", cvar);
        AddCustomPageWidget("NEI", "Fleet Combo", DrawFleetComboPage);
    }
    sGatheredPages.push_back({ section, sidebar, {} });
    GatheredPage* page = &sGatheredPages.back();
    EnsureSidebar(section, sidebar, cvar);
    AddCustomPageWidget(section, sidebar, [page]() { DrawGatheredPage(*page); });
    return *page;
}

void GatherWidget(const MenuHome& home, RegisteredWidget registered) {
    GatheredPage& page = GetGatheredPage(home.section, home.sidebar);
    auto group = std::find_if(page.groups.begin(), page.groups.end(),
                              [&](const auto& existing) { return existing.name == home.header; });
    if (group == page.groups.end()) {
        page.groups.push_back({ home.header, {} });
        group = std::prev(page.groups.end());
        SPDLOG_WARN("[ModMenu] {} > {} > {}", home.section, home.sidebar, home.header);
    }
    sGatheredWidgets.push_back(std::move(registered));
    RegisteredWidget* stored = &sGatheredWidgets.back();
    auto position = group->widgets.end();
    if (!stored->anchor.empty()) {
        auto anchor = std::find_if(group->widgets.begin(), group->widgets.end(),
                                   [&](const auto* widget) { return widget->label == stored->anchor; });
        if (anchor != group->widgets.end()) {
            position = stored->after ? std::next(anchor) : anchor;
        }
    }
    group->widgets.insert(position, stored);
}

bool StoreWidget(const SOHModMenuWidget* widget, const char* anchor, bool after) {
    if (!IsValidWidget(widget)) {
        SPDLOG_WARN("[ModMenu] Rejected widget '{}' ({}/{})",
                    widget != nullptr && widget->label != nullptr ? widget->label : "?",
                    widget != nullptr && widget->section != nullptr ? widget->section : "?",
                    widget != nullptr && widget->sidebar != nullptr ? widget->sidebar : "?");
        return false;
    }
    RegisteredWidget registered = {};
    // A mod built against an older header passes a shorter struct; the missing tail stays zero.
    std::memcpy(&registered.definition, widget, std::min<size_t>(widget->structSize, sizeof(SOHModMenuWidget)));
    registered.label = widget->label;
    registered.cvar = widget->cvar == nullptr ? "" : widget->cvar;
    registered.tooltip = widget->tooltip == nullptr ? "" : widget->tooltip;
    registered.anchor = anchor == nullptr ? "" : anchor;
    registered.values = CopyComboValues(widget);
    registered.after = after;
    const MenuHome home = RemapModMenu(widget->section, widget->sidebar);
    if (home.section != widget->section || home.sidebar != widget->sidebar) {
        GatherWidget(home, std::move(registered));
        return true;
    }
    registered.section = home.section;
    registered.sidebar = home.sidebar;
    sWidgets.push_back(std::move(registered));
    if (IsMenuBuilt()) {
        AddWidget(sWidgets.back());
    }
    return true;
}

} // namespace

bool ModMenu_RegisterWidget(const SOHModMenuWidget* widget) {
    return StoreWidget(widget, nullptr, false);
}

bool ModMenu_RegisterSidebar(const char* section, const char* sidebar, const char* selectionCvar, uint8_t columns) {
    if (section == nullptr || section[0] == '\0' || sidebar == nullptr || sidebar[0] == '\0' ||
        selectionCvar == nullptr || selectionCvar[0] == '\0' || columns < 1 || columns > 3) {
        return false;
    }
    const MenuHome home = RemapModMenu(section, sidebar);
    if (home.section != section || home.sidebar != sidebar) {
        GetGatheredPage(home.section, home.sidebar); // gathered pages are created on demand
        return true;
    }
    sSidebars.push_back({ section, sidebar, selectionCvar, columns });
    if (IsMenuBuilt()) {
        AddSidebar(sSidebars.back());
    }
    return true;
}

bool ModMenu_RegisterWidgetAt(const SOHModMenuWidget* widget, const char* anchor, bool after) {
    if (anchor == nullptr || anchor[0] == '\0') {
        return false;
    }
    return StoreWidget(widget, anchor, after);
}

bool ModUI_BeginTab(const char* label) {
    return label != nullptr && ImGui::BeginTabItem(label);
}

void ModUI_EndTab(void) {
    ImGui::EndTabItem();
}

bool ModUI_BeginSection(const char* label) {
    return label != nullptr && ImGui::TreeNode(label);
}

void ModUI_EndSection(void) {
    ImGui::TreePop();
}

void ModUI_Text(const char* text) {
    if (text != nullptr) {
        ImGui::TextUnformatted(text);
    }
}

bool ModUI_Checkbox(const char* label, bool* value) {
    if (label == nullptr || value == nullptr) {
        return false;
    }
    UIWidgets::PushStyleCheckbox(THEME_COLOR);
    const bool changed = ImGui::Checkbox(label, value);
    UIWidgets::PopStyleCheckbox();
    return changed;
}

bool ModUI_InputInt(const char* label, int32_t* value) {
    if (label == nullptr || value == nullptr) {
        return false;
    }
    UIWidgets::PushStyleInput(THEME_COLOR);
    const bool changed = ImGui::InputInt(label, value);
    UIWidgets::PopStyleInput();
    return changed;
}
