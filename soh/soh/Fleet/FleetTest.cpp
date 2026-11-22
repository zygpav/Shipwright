// Fleet Combo test mode, game side. The host reads <install>/fleet-test/commands.txt; button and stick steps reach
// the game through FleetTest_InjectPad, everything else arrives here as a game command once per frame.
// Inactive (one host lookup at start) when the host offers no test services.
#include "FleetTest.h"
#include "FleetGame.h"
#include "FleetPortal.h"

#include "soh/ModApi/CustomItemRegistry/CustomItemRegistry.h"
#include "soh/ModApi/CustomEquipRegistry/CustomEquipRegistry.h"
#include "soh/ModApi/Forms/FormRegistry.h"
#include "soh/ModApi/Layout/ModLayout.h"
#include "soh/ModApi/ModMenu/ModMenu.h"
#include "soh/SohGui/SohMenu.h"

#include <libultraship/libultraship.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/ConsoleWindow.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "z64.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
}

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

namespace {

using TestPadFn = bool (*)(uint32_t*, int8_t*, int8_t*);
using TestCommandFn = const char* (*)();
using TestReportFn = void (*)(const char*);

bool sLooked = false;
TestPadFn sPad = nullptr;
TestCommandFn sCommand = nullptr;
TestReportFn sReport = nullptr;

void Look() {
    if (sLooked) {
        return;
    }
    sLooked = true;
    if (!FleetGame_IsHosted()) {
        return;
    }
    sPad = reinterpret_cast<TestPadFn>(FleetGame_GetHostFunction("TestPad"));
    sCommand = reinterpret_cast<TestCommandFn>(FleetGame_GetHostFunction("TestCommand"));
    sReport = reinterpret_cast<TestReportFn>(FleetGame_GetHostFunction("TestReport"));
}

void Report(const std::string& line) {
    if (sReport != nullptr) {
        sReport(("oot: " + line).c_str());
    }
}

bool IsEquipment(const char* key) {
    return CustomEquipRegistry_Find(key) != nullptr;
}

bool IsOwned(const char* key) {
    return IsEquipment(key) ? CustomEquipRegistry_IsOwned(key) : CustomItemRegistry_IsOwned(key);
}

bool SetOwned(const char* key, bool owned) {
    if (IsEquipment(key)) {
        return CustomEquipRegistry_SetOwned(key, owned);
    }
    if (owned && !CustomItemRegistry_IsOwned(key)) {
        CustomItemRegistry_ReceiveCopies(key);
        return CustomItemRegistry_IsOwned(key);
    }
    return CustomItemRegistry_SetOwned(key, owned);
}

long ParseNumber(const std::string& text) {
    return std::strtol(text.c_str(), nullptr, 0);
}

void ReportState() {
    PlayState* play = gPlayState;
    char line[512];
    std::snprintf(line, sizeof(line),
                  "state: play=%d scene=0x%X entrance=0x%X age=%s health=%d/%d rupees=%d magic=%d gameMode=%d "
                  "cutscene=0x%X form=%s",
                  play != nullptr, play != nullptr ? play->sceneNum : -1, gSaveContext.entranceIndex,
                  LINK_IS_ADULT ? "adult" : "child", gSaveContext.health, gSaveContext.healthCapacity,
                  gSaveContext.rupees, gSaveContext.magic, gSaveContext.gameMode, gSaveContext.cutsceneIndex,
                  FormRegistry_GetActiveKey() != nullptr ? FormRegistry_GetActiveKey() : "none");
    Report(line);
    if (play == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(play);
    std::snprintf(line, sizeof(line),
                  "state: pos=(%.0f,%.0f,%.0f) pause=%d pausePage=%d msgState=%d choice=%d textId=0x%X "
                  "transition=%d stateFlags1=0x%X stateFlags2=0x%X",
                  player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z,
                  play->pauseCtx.state, play->pauseCtx.pageIndex, Message_GetState(&play->msgCtx),
                  play->msgCtx.choiceIndex, play->msgCtx.textId, play->transitionTrigger, player->stateFlags1,
                  player->stateFlags2);
    Report(line);
    std::snprintf(line, sizeof(line), "state: B=0x%X C=0x%X,0x%X,0x%X dpad=0x%X,0x%X,0x%X,0x%X",
                  gSaveContext.equips.buttonItems[0], gSaveContext.equips.buttonItems[1],
                  gSaveContext.equips.buttonItems[2], gSaveContext.equips.buttonItems[3],
                  gSaveContext.equips.buttonItems[4], gSaveContext.equips.buttonItems[5],
                  gSaveContext.equips.buttonItems[6], gSaveContext.equips.buttonItems[7]);
    Report(line);
}

void ListItems(const std::string& filter, bool ownedOnly) {
    int shown = 0;
    for (uint32_t i = 0; i < CustomItemRegistry_GetCount(); ++i) {
        const SOHCustomItemDefinition* definition = CustomItemRegistry_GetAt(i);
        if (definition == nullptr || definition->key == nullptr) {
            continue;
        }
        const std::string key = definition->key;
        const bool owned = IsOwned(definition->key);
        if ((ownedOnly && !owned) || (!filter.empty() && key.find(filter) == std::string::npos)) {
            continue;
        }
        char where[160] = {};
        ModLayout_DescribeItem(definition->key, where, sizeof(where));
        Report(std::string("item ") + key + (owned ? " [owned]" : "") + (IsEquipment(definition->key) ? " (equip)" : "") +
               " " + (definition->displayName != nullptr ? definition->displayName : "") + " @ " + where);
        ++shown;
    }
    Report("items listed: " + std::to_string(shown));
}

void DumpMenu() {
    std::vector<std::string> lines;
    if (SohGui::mSohMenu != nullptr) {
        SohGui::mSohMenu->Describe(lines);
    }
    ModMenu_Describe(lines);
    for (const auto& line : lines) {
        Report("menu " + line);
    }
}

void Warp(int entrance) {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        Report("warp: not playing");
        return;
    }
    play->nextEntranceIndex = entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextCutsceneIndex = 0;
    Report("warp: 0x" + [&] {
        char hex[16];
        std::snprintf(hex, sizeof(hex), "%X", entrance);
        return std::string(hex);
    }());
}

void Run(const std::string& line) {
    std::stringstream words(line);
    std::string verb;
    words >> verb;
    std::string rest;
    std::getline(words, rest);
    if (!rest.empty() && rest[0] == ' ') {
        rest.erase(0, 1);
    }
    std::stringstream args(rest);
    if (verb == "menu") {
        std::string how;
        args >> how;
        if (SohGui::mSohMenu == nullptr) {
            Report("menu: none");
        } else if (how == "show") {
            SohGui::mSohMenu->Show();
        } else if (how == "hide") {
            SohGui::mSohMenu->Hide();
        } else {
            SohGui::mSohMenu->ToggleVisibility();
        }
    } else if (verb == "menudump") {
        DumpMenu();
    } else if (verb == "cvar") {
        std::string name;
        std::string value;
        args >> name;
        std::getline(args, value);
        if (!value.empty() && value[0] == ' ') {
            value.erase(0, 1);
        }
        char* end = nullptr;
        const long number = std::strtol(value.c_str(), &end, 0);
        if (!value.empty() && end != nullptr && *end == '\0') {
            CVarSetInteger(name.c_str(), static_cast<int32_t>(number));
        } else {
            CVarSetString(name.c_str(), value.c_str());
        }
        Report("cvar " + name + " = " + value);
    } else if (verb == "console") {
        auto console = std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"));
        if (console != nullptr) {
            console->Dispatch(rest);
        }
    } else if (verb == "give" || verb == "take") {
        std::string key;
        args >> key;
        if (CustomItemRegistry_Find(key.c_str()) == nullptr && CustomEquipRegistry_Find(key.c_str()) == nullptr) {
            Report(verb + ": unknown " + key);
        } else {
            const bool ok = SetOwned(key.c_str(), verb == "give");
            Report(verb + " " + key + (ok ? " ok" : " failed") + (IsOwned(key.c_str()) ? " (owned)" : " (not owned)"));
        }
    } else if (verb == "giveall") {
        int count = 0;
        for (uint32_t i = 0; i < CustomItemRegistry_GetCount(); ++i) {
            const SOHCustomItemDefinition* definition = CustomItemRegistry_GetAt(i);
            if (definition != nullptr && CustomItemRegistry_IsEditorVisible(definition->key) &&
                SetOwned(definition->key, true)) {
                ++count;
            }
        }
        Report("giveall: " + std::to_string(count));
    } else if (verb == "vanilla") {
        std::string item;
        args >> item;
        if (gPlayState != nullptr) {
            Item_Give(gPlayState, static_cast<u8>(ParseNumber(item)));
            Report("vanilla " + item);
        }
    } else if (verb == "equip") {
        std::string key;
        int button = 1;
        args >> key >> button;
        const bool ok = IsEquipment(key.c_str()) ? CustomEquipRegistry_Wear(key.c_str())
                                                 : CustomItemRegistry_Equip(static_cast<uint8_t>(button), key.c_str());
        Report("equip " + key + (ok ? " ok" : " failed"));
    } else if (verb == "items" || verb == "owned") {
        std::string filter;
        args >> filter;
        ListItems(filter, verb == "owned");
    } else if (verb == "state") {
        ReportState();
    } else if (verb == "warp") {
        std::string entrance;
        args >> entrance;
        Warp(static_cast<int>(ParseNumber(entrance)));
    } else if (verb == "travel") {
        std::string portal;
        args >> portal;
        FleetPortal_RequestDeparture(portal.c_str());
        Report("travel " + portal);
    } else if (verb == "age") {
        std::string age;
        args >> age;
        gSaveContext.linkAge = age == "adult" ? LINK_AGE_ADULT : LINK_AGE_CHILD;
        Warp(gSaveContext.entranceIndex);
    } else if (verb == "health") {
        std::string value;
        args >> value;
        gSaveContext.health = static_cast<s16>(ParseNumber(value));
    } else if (verb == "textbox") {
        if (gPlayState != nullptr) {
            CustomItemRegistry_ShowTextbox(gPlayState, rest.c_str(), false);
        }
    } else if (verb == "magic") {
        std::string value;
        args >> value;
        gSaveContext.isMagicAcquired = true;
        gSaveContext.magicLevel = std::max<s8>(gSaveContext.magicLevel, 1);
        gSaveContext.magicCapacity = gSaveContext.magicLevel * 48;
        gSaveContext.magic = static_cast<s8>(std::min<long>(ParseNumber(value), gSaveContext.magicCapacity));
        Report("magic " + std::to_string(gSaveContext.magic));
    } else if (verb == "file") {
        std::string slot;
        args >> slot;
        Report(std::string("file ") + slot + (FleetGame_SelectFile(static_cast<int>(ParseNumber(slot))) ? " ok" : " failed"));
    } else {
        Report("unknown command: " + line);
    }
}

} // namespace

extern "C" void FleetTest_InjectPad(OSContPad* pad) {
    Look();
    if (sPad == nullptr || pad == nullptr) {
        return;
    }
    uint32_t buttons = pad->button;
    int8_t x = pad->stick_x;
    int8_t y = pad->stick_y;
    if (sPad(&buttons, &x, &y)) {
        pad->button = static_cast<decltype(pad->button)>(buttons);
        pad->stick_x = x;
        pad->stick_y = y;
    }
}

extern "C" void FleetTest_Tick(void) {
    Look();
    if (sCommand == nullptr) {
        return;
    }
    if (const char* command = sCommand()) {
        Run(command);
    }
}
