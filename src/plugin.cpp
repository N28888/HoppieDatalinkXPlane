#include "hoppie/application.hpp"
#include "hoppie/ui.hpp"

#include <XPLMDataAccess.h>
#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMSound.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace {

std::unique_ptr<hoppie::Application> application;
std::unique_ptr<hoppie::DcduWindow> dcdu;
XPLMCommandRef toggleCommand = nullptr;
XPLMMenuID pluginMenu = nullptr;
int pluginMenuItem = -1;
XPLMDataRef latitudeRef = nullptr;
XPLMDataRef longitudeRef = nullptr;
XPLMDataRef elevationRef = nullptr;
FMOD_CHANNEL* alertChannel = nullptr;
bool enabled = false;

void copyPluginText(char* destination, const char* source) {
    const auto length = std::min<std::size_t>(std::strlen(source), 255);
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}

void debugStatus(const char* text) {
    XPLMDebugString("[HoppieDatalinkXP] ");
    XPLMDebugString(text);
    XPLMDebugString("\n");
}

std::filesystem::path simulatorRoot() {
    char path[2048]{};
    XPLMGetSystemPath(path);
    return std::filesystem::u8path(path);
}

std::filesystem::path pluginRoot() {
    char path[2048]{};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, path, nullptr, nullptr);
    return std::filesystem::u8path(path).parent_path().parent_path();
}

void alertComplete(void*, FMOD_RESULT) { alertChannel = nullptr; }

void stopAlert() {
    if (alertChannel) XPLMStopAudio(alertChannel);
    alertChannel = nullptr;
}

void playAlert() {
    if (alertChannel) return; // Coalesce arrivals during the three-beep sequence.
    static const auto samples = hoppie::buildMessageAlertPcm();
    alertChannel = XPLMPlayPCMOnBus(const_cast<std::int16_t*>(samples.data()),
                     static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t)),
                     FMOD_SOUND_FORMAT_PCM16, hoppie::messageAlertSampleRate, 1, 0,
                     xplm_AudioUI, alertComplete, nullptr);
}

float flightLoop(float, float, int, void*) {
    if (!application) return 0.1f;
    hoppie::AircraftSnapshot aircraft;
    if (latitudeRef) aircraft.latitude = XPLMGetDatad(latitudeRef);
    if (longitudeRef) aircraft.longitude = XPLMGetDatad(longitudeRef);
    if (elevationRef) aircraft.mslAltitudeFeet = XPLMGetDatad(elevationRef) * 3.280839895;
    application->tick(aircraft, std::chrono::steady_clock::now(),
                      std::chrono::system_clock::now());
    const bool soundPending = application->consumeSoundPending();
    if (!application->settings().soundEnabled) stopAlert();
    else if (soundPending) playAlert();
    return 0.1f;
}

int commandHandler(XPLMCommandRef, XPLMCommandPhase phase, void*) {
    if (phase == xplm_CommandBegin && dcdu) dcdu->toggle();
    return 1;
}

void menuHandler(void*, void*) {
    if (dcdu) dcdu->toggle();
}

}  // namespace

PLUGIN_API int XPluginStart(char* outName, char* outSignature, char* outDescription) {
    copyPluginText(outName, "HoppieDatalinkXP");
    copyPluginText(outSignature, "com.hoppiedatalinkxp.plugin");
    copyPluginText(outDescription, "Hoppie CPDLC, DCL, requests and periodic ADS-C for X-Plane 12");
    try {
        const auto settingsPath = simulatorRoot() / "Output" / "preferences" /
                                  "HoppieDatalinkXP.json";
        const auto fontPath = pluginRoot() / "Resources" / "NotoSansCJKsc-Regular.otf";
        application = std::make_unique<hoppie::Application>(settingsPath.u8string());
        dcdu = std::make_unique<hoppie::DcduWindow>(*application, fontPath.u8string());

        toggleCommand = XPLMCreateCommand("hoppiedatalinkxp/toggle_window",
                                           "Toggle the HoppieDatalinkXP DCDU window");
        XPLMRegisterCommandHandler(toggleCommand, commandHandler, 1, nullptr);
        const auto pluginsMenu = XPLMFindPluginsMenu();
        pluginMenuItem = XPLMAppendMenuItem(pluginsMenu, "HoppieDatalinkXP", nullptr, 0);
        pluginMenu = XPLMCreateMenu("HoppieDatalinkXP", pluginsMenu, pluginMenuItem,
                                    menuHandler, nullptr);
        XPLMAppendMenuItem(pluginMenu, "Toggle DCDU", nullptr, 1);

        latitudeRef = XPLMFindDataRef("sim/flightmodel/position/latitude");
        longitudeRef = XPLMFindDataRef("sim/flightmodel/position/longitude");
        elevationRef = XPLMFindDataRef("sim/flightmodel/position/elevation");
        debugStatus("started");
        return 1;
    } catch (...) {
        dcdu.reset();
        application.reset();
        debugStatus("start failed");
        return 0;
    }
}

PLUGIN_API void XPluginStop() {
    if (enabled) XPLMUnregisterFlightLoopCallback(flightLoop, nullptr);
    enabled = false;
    stopAlert();
    if (toggleCommand) {
        XPLMUnregisterCommandHandler(toggleCommand, commandHandler, 1, nullptr);
        toggleCommand = nullptr;
    }
    if (pluginMenu) {
        XPLMDestroyMenu(pluginMenu);
        pluginMenu = nullptr;
    }
    dcdu.reset();
    application.reset();
    debugStatus("stopped");
}

PLUGIN_API int XPluginEnable() {
    if (!application || !dcdu) return 0;
    XPLMRegisterFlightLoopCallback(flightLoop, 0.1f, nullptr);
    enabled = true;
    return 1;
}

PLUGIN_API void XPluginDisable() {
    if (enabled) XPLMUnregisterFlightLoopCallback(flightLoop, nullptr);
    enabled = false;
    stopAlert();
    if (application) application->disable();
    if (dcdu) dcdu->hide();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void*) {}
