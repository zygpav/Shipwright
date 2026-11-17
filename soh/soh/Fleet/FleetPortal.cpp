#include "FleetPortal.h"
#include "FleetGame.h"
#include "FleetPassages.h"
#include "FleetRuntime.h"
#include "FleetRando.h"

#include <optional>
#include <cstring>
#include <string>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/SaveManager.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/ModApi/Forms/FormRegistry.h"
#include "soh/ModApi/CustomItemRegistry/CustomItemRegistry.h"
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64.h"
#include "global.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
void FileChoose_PrepareLoadedSave(GameState* state);
void Opening_Main(GameState* state);
void Title_Main(GameState* state);
void Sram_InitNewSave(void);
}

namespace {

constexpr const char* kMarketPortal = "market";

std::optional<std::string> sDeparture;
struct Arrival {
    int32_t entrance;
    int32_t slot;
    bool cross = false;
    // Fleet Combo: the Link who crossed: "child"/"adult" and the form he wore (nei.* key), kept on this side.
    std::string age;
    std::string form;
};
std::optional<Arrival> sArrival;
// Fleet Combo: the form to wear once the arrival scene has a player; "" = plain Link. Unset = leave it alone.
std::optional<std::string> sPendingForm;

bool DoorPortalsEnabled() {
    return CVarGetInteger("gFleetCombo.DoorPortals", 0) != 0;
}

bool IsTransformationForm(const std::string& key) {
    return key == SOH_FORM_KEY_DEKU || key == SOH_FORM_KEY_GORON || key == SOH_FORM_KEY_ZORA ||
           key == SOH_FORM_KEY_FIERCE_DEITY;
}

// A form is only kept across worlds when this world has its mask too: without it Link could not take it off.
bool CanWearForm(const std::string& key) {
    if (!IsTransformationForm(key)) {
        return false;
    }
    const SOHFormDefinition* form = FormRegistry_Find(key.c_str());
    return form != nullptr && (form->item == nullptr || form->item[0] == '\0' || CustomItemRegistry_IsOwned(form->item));
}

std::optional<int32_t> EntranceForPortal(const std::string& portal) {
    if (portal == kMarketPortal) {
        return ENTR_MARKET_DAY_OUTSIDE_HAPPY_MASK_SHOP;
    }
    // Fleet Combo: OoT warp songs played in Termina land on their Hyrule warp pads.
    if (portal == "warp.minuet") {
        return ENTR_SACRED_FOREST_MEADOW_WARP_PAD;
    }
    if (portal == "warp.bolero") {
        return ENTR_DEATH_MOUNTAIN_CRATER_WARP_PAD;
    }
    if (portal == "warp.serenade") {
        return ENTR_LAKE_HYLIA_WARP_PAD;
    }
    if (portal == "warp.requiem") {
        return ENTR_DESERT_COLOSSUS_WARP_PAD;
    }
    if (portal == "warp.nocturne") {
        return ENTR_GRAVEYARD_WARP_PAD;
    }
    if (portal == "warp.prelude") {
        return ENTR_TEMPLE_OF_TIME_WARP_PAD;
    }
    return FleetPassages_Entrance(portal);
}

bool IsPlaying() {
    return gPlayState != nullptr && gGameState == &gPlayState->state && gSaveContext.gameMode == GAMEMODE_NORMAL;
}

void Depart(const std::string& portal) {
    if (FleetRuntime_HasSession()) {
        FleetRuntime_Save();
    }
    const char* form = FormRegistry_GetCurrentKey();
    const std::string payload = nlohmann::json{ { "portal", portal },
                                                { "slot", gSaveContext.fileNum },
                                                { "age", LINK_IS_ADULT ? "adult" : "child" },
                                                { "form", form != nullptr ? form : "" } }
                                    .dump();
    const FleetDestination destination = { .game = nullptr, .entrance = 0, .payloadJson = payload.c_str() };
    FleetGame_RequestSwitch(&destination);
}

void BootIntoPlay(const Arrival& arrival) {
    FleetRuntime_Initialize();
    SaveManager::Instance->Init();
    gSaveContext.fileNum = arrival.slot;
    if (!SaveManager::Instance->SaveFile_Exist(arrival.slot)) {
        FleetAutomaticSave automatic;
        if (FleetRuntime_HasSession()) {
            FleetRando_CreateCombinedSave(FleetRuntime_Seed(), arrival.slot);
        } else {
            Sram_InitNewSave();
            gSaveContext.fileNum = arrival.slot;
            const uint8_t name[8] = { 0x15, 0x2C, 0x31, 0x2E, 0x3E, 0x3E, 0x3E, 0x3E };
            std::memcpy(gSaveContext.playerName, name, sizeof(name));
            std::memcpy(gSaveContext.newf, "ZELDAZ", sizeof(gSaveContext.newf));
            gSaveContext.entranceIndex = ENTR_LINKS_HOUSE_CHILD_SPAWN;
            gSaveContext.cutsceneIndex = 0;
            FleetRuntime_Save();
        }
    }
    Sram_OpenSave();
    if (FleetRuntime_HasSession() && (!IS_RANDO || Rando::Context::GetInstance()->GetSeed() !=
                                                       FleetRuntime_Seed().at("masterSeed").get<uint32_t>())) {
        throw std::runtime_error("OoT save does not belong to this Fleet seed");
    }
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    // Fleet Combo: arrive as the Link who left the other world. The equipment of each age swaps like time travel.
    if (!arrival.age.empty()) {
        const int32_t age = arrival.age == "adult" ? LINK_AGE_ADULT : LINK_AGE_CHILD;
        if (gSaveContext.linkAge != age) {
            Inventory_SwapAgeEquipment();
            gSaveContext.linkAge = age;
        }
    }
    if (!arrival.age.empty()) {
        sPendingForm = CanWearForm(arrival.form) ? arrival.form : std::string{};
    }
    SET_NEXT_GAMESTATE(gGameState, Play_Init, PlayState);
    FileChoose_PrepareLoadedSave(gGameState);
    if (arrival.entrance >= 0) {
        gSaveContext.respawnFlag = 0;
        gSaveContext.nextCutsceneIndex = 0xFFEF;
        if (arrival.cross && !FleetRuntime_QueueArrival(arrival.entrance)) {
            throw std::runtime_error("OoT rejected an arrival absent from this seed");
        }
        gSaveContext.entranceIndex =
            arrival.cross ? FleetRuntime_ConsumeArrival() : FleetRuntime_Materialize(arrival.entrance);
        gSaveContext.cutsceneIndex = 0;
        gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
    }
}

void EnterArrival() {
    if (gGameState != nullptr && gGameState->running) {
        // Going through TitleSetup destroys the parked scene before its destination loads.
        gGameState->running = false;
        SET_NEXT_GAMESTATE(gGameState, TitleSetup_Init, GameState);
    }
}

void RegisterPortalHooks() {
    if (!FleetGame_IsHosted()) {
        return;
    }
    FleetRuntime_Initialize();
    if (FleetRuntime_HasSession() && !sArrival) {
        sArrival = Arrival{ -1, FleetRuntime_Slot() };
    }
    // Fleet Combo: any Link uses every item (SoH's Timeless Equipment, which every age check here consults).
    if (CVarGetInteger("gFleetCombo.AnyAgeItems", 1) != 0 && CVarGetInteger(CVAR_CHEAT("TimelessEquipment"), 0) == 0) {
        CVarSetInteger(CVAR_CHEAT("TimelessEquipment"), 1);
        ShipInit::Init(CVAR_CHEAT("TimelessEquipment"));
    }
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](int16_t sceneNum) {
        FleetRuntime_ObserveScene(sceneNum);
        if (sceneNum == SCENE_HAPPY_MASK_SHOP && gSaveContext.gameMode == GAMEMODE_NORMAL && DoorPortalsEnabled()) {
            sDeparture = kMarketPortal;
        }
        // Fleet Combo: Song of Soaring travels between the worlds, so every file inside Fleet knows it.
        if (gSaveContext.gameMode == GAMEMODE_NORMAL && CustomItemRegistry_Find("mm.song.soaring") != nullptr &&
            !CustomItemRegistry_IsOwned("mm.song.soaring")) {
            CustomItemRegistry_SetOwned("mm.song.soaring", true);
        }
        if (sPendingForm && gSaveContext.gameMode == GAMEMODE_NORMAL) {
            FormRegistry_ApplyActive(sPendingForm->empty() ? nullptr : sPendingForm->c_str());
            sPendingForm.reset();
        }
    });
}

static RegisterShipInitFunc sPortalInit(RegisterPortalHooks, {});

} // namespace

extern "C" void FleetPortal_RequestDeparture(const char* portal) {
    sDeparture = portal;
}

extern "C" bool FleetPortal_LoadPending(void) {
    if (!sArrival) {
        return false;
    }
    BootIntoPlay(*sArrival);
    sArrival.reset();
    return true;
}

extern "C" void FleetPortal_Arrive(const FleetDestination* arrival) {
    if (arrival == nullptr || arrival->payloadJson == nullptr) {
        return;
    }
    const nlohmann::json payload = nlohmann::json::parse(arrival->payloadJson, nullptr, false);
    if (payload.is_object()) {
        const int slot = payload.value("slot", 0);
        if (slot < 0 || slot > 2) {
            return;
        }
        if (payload.value("session", false)) {
            sArrival = Arrival{ -1, slot };
        } else if (payload.value("cross", false)) {
            sArrival = Arrival{ payload.at("entrance").get<int32_t>(), slot, true };
        } else if (const auto entrance = EntranceForPortal(payload.value("portal", ""))) {
            sArrival = Arrival{ *entrance, slot };
            sArrival->age = payload.value("age", "");
            sArrival->form = payload.value("form", "");
        }
        if (sArrival) {
            EnterArrival();
        }
    }
}

extern "C" void FleetPortal_FrameEnd(void) {
    const auto hasFiles = reinterpret_cast<bool (*)()>(FleetGame_GetHostFunction("HasFileSelect"));
    if (!FleetRuntime_HasSession() && hasFiles != nullptr && hasFiles() && gGameState != nullptr &&
        (gGameState->main == Opening_Main || gGameState->main == Title_Main)) {
        gGameState->running = false;
        SET_NEXT_GAMESTATE(gGameState, FileChoose_Init, FileChooseContext);
    }
    FleetDestination destination = {};
    std::string payload;
    if (FleetRuntime_TakeDeparture(destination, payload)) {
        FleetGame_RequestSwitch(&destination);
        return;
    }
    if (sDeparture) {
        const std::string portal = *sDeparture;
        sDeparture.reset();
        Depart(portal);
    }
    if (sArrival) {
        EnterArrival();
    }
}
