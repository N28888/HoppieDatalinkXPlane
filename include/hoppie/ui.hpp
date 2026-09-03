#pragma once

#include "hoppie/application.hpp"

#include <XPLMDisplay.h>
#include <XPLMDataAccess.h>

#include <chrono>
#include <cstddef>
#include <string>

struct ImGuiContext;

namespace hoppie {

class DcduWindow {
public:
    DcduWindow(Application& application, std::string fontPath);
    ~DcduWindow();
    DcduWindow(const DcduWindow&) = delete;
    DcduWindow& operator=(const DcduWindow&) = delete;

    void show();
    void hide();
    void toggle();
    bool visible() const;

private:
    enum class Page { Status, Messages, Atc, Request, WxTelex, Settings };

    static void drawCallback(XPLMWindowID window, void* refcon);
    static int mouseCallback(XPLMWindowID window, int x, int y,
                             XPLMMouseStatus status, void* refcon);
    static int rightMouseCallback(XPLMWindowID window, int x, int y,
                                  XPLMMouseStatus status, void* refcon);
    static void keyCallback(XPLMWindowID window, char key, XPLMKeyFlags flags,
                            char virtualKey, void* refcon, int losingFocus);
    static XPLMCursorStatus cursorCallback(XPLMWindowID window, int x, int y, void* refcon);
    static int wheelCallback(XPLMWindowID window, int x, int y, int wheel,
                             int clicks, void* refcon);

    void draw();
    void renderHeader();
    void renderStatus();
    void renderMessages();
    void renderAtc();
    void renderRequest();
    void renderWxTelex();
    void renderSettings();
    void renderReplyButton(std::size_t index, ReplyAction action);
    void updateMouse(int x, int y);
    void fillFlightPlanDefaults();
    const char* tr(const char* key);

    Application& app_;
    XPLMWindowID window_{nullptr};
    ImGuiContext* context_{nullptr};
    XPLMDataRef modelviewRef_{nullptr};
    XPLMDataRef projectionRef_{nullptr};
    XPLMDataRef viewportRef_{nullptr};
    Page page_{Page::Status};
    std::chrono::steady_clock::time_point previousFrame_{};
    std::uint64_t selectedMessageId_{0};
    std::optional<SendFeedback> sendFeedback_;
    double sendFeedbackUntil_{0.0};
    bool rememberCode_{true};
    std::string callsign_;
    std::string logonCode_;
    std::string atsu_;
    std::string altitude_{"350"};
    std::string directTo_;
    std::string speed_{"MACH .80"};
    std::string expected_{"FL390"};
    std::string freeText_;
    bool freeTextReply_{true};
    PositionReport position_;
    std::string oceanAircraft_;
    std::string oceanEntry_;
    std::string oceanTime_;
    std::string oceanLevel_{"350"};
    std::string oceanMach_{".84"};
    std::string oceanRoute_;
    DclRequest dcl_;
    int infoProduct_{3};
    std::string infoIcao_;
    std::string telexTo_;
    std::string telexText_;
};

}  // namespace hoppie
