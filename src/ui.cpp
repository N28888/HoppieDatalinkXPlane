#include "hoppie/ui.hpp"

#include "hoppie/core.hpp"
#include "hoppie/message_ui.hpp"
#include "hoppie/ui_geometry.hpp"

#include <XPLMDefs.h>
#include <XPLMGraphics.h>

#if IBM
#include <Windows.h>
#endif
#if APL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <imgui.h>
#include <imgui_impl_opengl2.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace hoppie {
namespace {

// ImGui's stock GL2 renderer replaces the viewport/projection with a desktop-sized
// pixel surface. XPLM has already supplied a boxel transform for this window. Keep
// it, applying only the local top-left origin / Y flip; project scissors separately.
// Font texture lifecycle still uses the unmodified Dear ImGui GL2 backend.
void renderWindowDrawData(ImDrawData* data, const UiWindowBounds& bounds,
                          const UiDrawTransform& transform) {
    if (!data || transform.viewport[2] <= 0 || transform.viewport[3] <= 0) return;
    XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
    // XPLM may further restrict this surface. There is no SDK dataref for scissor state.
    const bool hostClips = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    GLint hostClip[4]{};
    if (hostClips) glGetIntegerv(GL_SCISSOR_BOX, hostClip);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT | GL_SCISSOR_BIT |
                 GL_TEXTURE_BIT | GL_LIGHTING_BIT | GL_TRANSFORM_BIT | GL_CURRENT_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    const auto setup = [&] {
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(transform.projection.data());
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(transform.modelview.data());
        glTranslatef(static_cast<float>(bounds.left), static_cast<float>(bounds.top), 0.0f);
        glScalef(1.0f, -1.0f, 1.0f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_COLOR_MATERIAL);
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glShadeModel(GL_SMOOTH);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    };
    setup();
    for (const auto* list : data->CmdLists) {
        for (const auto& command : list->CmdBuffer) {
            if (command.UserCallback) {
                if (command.UserCallback == ImDrawCallback_ResetRenderState) setup();
                else command.UserCallback(list, &command);
                continue;
            }
            auto clip = transform.scissor(bounds, command.ClipRect.x, command.ClipRect.y,
                                           command.ClipRect.z, command.ClipRect.w);
            if (hostClips) {
                const int left = std::max(clip.x, hostClip[0]);
                const int bottom = std::max(clip.y, hostClip[1]);
                const int right = std::min(clip.x + clip.width, hostClip[0] + hostClip[2]);
                const int top = std::min(clip.y + clip.height, hostClip[1] + hostClip[3]);
                clip = {left, bottom, right - left, top - bottom};
            }
            if (clip.width <= 0 || clip.height <= 0) continue;
            glScissor(clip.x, clip.y, clip.width, clip.height);
            const auto* vertices = list->VtxBuffer.Data + command.VtxOffset;
            glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert), &vertices->pos);
            glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert), &vertices->uv);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), &vertices->col);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(command.GetTexID()));
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(command.ElemCount),
                           sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                           list->IdxBuffer.Data + command.IdxOffset);
        }
    }
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
}

void uppercaseEdited(std::string& value) {
    value = uppercaseAscii(value);
}

bool inputUpper(const char* label, std::string& value) {
    const bool changed = ImGui::InputText(label, &value, ImGuiInputTextFlags_CharsUppercase);
    if (changed) uppercaseEdited(value);
    return changed;
}

template <typename Builder>
bool canBuild(Builder&& builder) {
    try {
        (void)builder();
        return true;
    } catch (...) {
        return false;
    }
}

bool validAviationEntry(const std::string& value) {
    return !trim(value).empty() && value.size() <= 1024 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return c >= 0x20 && c <= 0x7e;
           });
}

const char* stateName(MessageState state) {
    switch (state) {
        case MessageState::Sending: return "SENDING";
        case MessageState::PendingReply: return "PENDING";
        case MessageState::Standby: return "STANDBY";
        case MessageState::Complete: return "COMPLETE";
        case MessageState::Failed: return "FAILED";
    }
    return "UNKNOWN";
}

ImGuiKey translateKey(unsigned char key) {
    switch (key) {
        case XPLM_VK_BACK: return ImGuiKey_Backspace;
        case XPLM_VK_TAB: return ImGuiKey_Tab;
        case XPLM_VK_RETURN: return ImGuiKey_Enter;
        case XPLM_VK_ESCAPE: return ImGuiKey_Escape;
        case XPLM_VK_SPACE: return ImGuiKey_Space;
        case XPLM_VK_PRIOR: return ImGuiKey_PageUp;
        case XPLM_VK_NEXT: return ImGuiKey_PageDown;
        case XPLM_VK_END: return ImGuiKey_End;
        case XPLM_VK_HOME: return ImGuiKey_Home;
        case XPLM_VK_LEFT: return ImGuiKey_LeftArrow;
        case XPLM_VK_UP: return ImGuiKey_UpArrow;
        case XPLM_VK_RIGHT: return ImGuiKey_RightArrow;
        case XPLM_VK_DOWN: return ImGuiKey_DownArrow;
        case XPLM_VK_DELETE: return ImGuiKey_Delete;
        default: return ImGuiKey_None;
    }
}

}  // namespace

DcduWindow::DcduWindow(Application& application, std::string fontPath) : app_(application) {
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    std::ifstream font(std::filesystem::u8path(fontPath), std::ios::binary | std::ios::ate);
    bool loadedFont = false;
    if (font) {
        const auto size = font.tellg();
        if (size > 0 && size <= 32 * 1024 * 1024) {
            font.seekg(0);
            auto* data = IM_ALLOC(static_cast<std::size_t>(size));
            if (data && font.read(static_cast<char*>(data), size)) {
                loadedFont = io.Fonts->AddFontFromMemoryTTF(
                    data, static_cast<int>(size), 19.0f, nullptr,
                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) != nullptr;
            } else if (data) {
                IM_FREE(data);
            }
        }
    }
    if (!loadedFont)
        io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.035f, 0.03f, 0.98f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.08f, 0.18f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.33f, 0.20f, 1.0f);
    ImGui_ImplOpenGL2_Init();
    modelviewRef_ = XPLMFindDataRef("sim/graphics/view/modelview_matrix");
    projectionRef_ = XPLMFindDataRef("sim/graphics/view/projection_matrix");
    viewportRef_ = XPLMFindDataRef("sim/graphics/view/viewport");

    int screenLeft = 0, screenTop = 0, screenRight = 1920, screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
    const auto& settings = app_.settings();
    const int width = std::clamp(settings.windowRight - settings.windowLeft, 520, 1100);
    const int height = std::clamp(settings.windowTop - settings.windowBottom, 420, 900);
    const int left = std::clamp(settings.windowLeft, screenLeft, std::max(screenLeft, screenRight - width));
    const int top = std::clamp(settings.windowTop, std::min(screenBottom + height, screenTop), screenTop);

    XPLMCreateWindow_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.left = left;
    parameters.top = top;
    parameters.right = left + width;
    parameters.bottom = top - height;
    parameters.visible = 0;
    parameters.drawWindowFunc = drawCallback;
    parameters.handleMouseClickFunc = mouseCallback;
    parameters.handleKeyFunc = keyCallback;
    parameters.handleCursorFunc = cursorCallback;
    parameters.handleMouseWheelFunc = wheelCallback;
    parameters.refcon = this;
    parameters.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    parameters.layer = xplm_WindowLayerFloatingWindows;
    parameters.handleRightClickFunc = rightMouseCallback;
    window_ = XPLMCreateWindowEx(&parameters);
    XPLMSetWindowTitle(window_, "HoppieDatalinkXP DCDU");
    XPLMSetWindowResizingLimits(window_, 520, 420, 1100, 900);

    callsign_ = app_.flightPlan().callsign;
    logonCode_ = app_.logonCode();
    infoProduct_ = app_.settings().defaultAtisSource == "ivaoatis" ? 4
                   : app_.settings().defaultAtisSource == "peatis" ? 5 : 3;
}

DcduWindow::~DcduWindow() {
    if (window_) {
        int left = 0, top = 0, right = 0, bottom = 0;
        XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
        auto& settings = app_.settings();
        settings.windowLeft = left;
        settings.windowTop = top;
        settings.windowRight = right;
        settings.windowBottom = bottom;
        XPLMDestroyWindow(window_);
    }
    ImGui::SetCurrentContext(context_);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui::DestroyContext(context_);
    app_.saveSettings();
}

void DcduWindow::show() { XPLMSetWindowIsVisible(window_, 1); }
void DcduWindow::hide() { XPLMSetWindowIsVisible(window_, 0); }
void DcduWindow::toggle() { XPLMSetWindowIsVisible(window_, visible() ? 0 : 1); }
bool DcduWindow::visible() const { return XPLMGetWindowIsVisible(window_) != 0; }

void DcduWindow::drawCallback(XPLMWindowID, void* refcon) {
    static_cast<DcduWindow*>(refcon)->draw();
}

void DcduWindow::draw() {
    ImGui::SetCurrentContext(context_);
    int left = 0, top = 0, right = 0, bottom = 0;
    XPLMGetWindowGeometry(window_, &left, &top, &right, &bottom);
    if (right <= left || top <= bottom) return;
    UiDrawTransform transform;
    if (!modelviewRef_ || !projectionRef_ || !viewportRef_ ||
        XPLMGetDatavf(modelviewRef_, transform.modelview.data(), 0, 16) != 16 ||
        XPLMGetDatavf(projectionRef_, transform.projection.data(), 0, 16) != 16 ||
        XPLMGetDatavi(viewportRef_, transform.viewport.data(), 0, 4) != 4) return;

    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(right - left), static_cast<float>(top - bottom));
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = previousFrame_.time_since_epoch().count() == 0
                       ? 1.0f / 60.0f
                       : std::max(0.001f, std::chrono::duration<float>(now - previousFrame_).count());
    previousFrame_ = now;
    XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(right - left),
                                    static_cast<float>(top - bottom)));
    ImGui::Begin("##HoppieDatalinkXP", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    renderHeader();
    ImGui::Separator();
    if (ImGui::GetTime() >= sendFeedbackUntil_) {
        sendFeedback_ = app_.takeSendFeedback();
        if (sendFeedback_) sendFeedbackUntil_ = ImGui::GetTime() + 7.0;
    }
    if (sendFeedback_) {
        ImGui::PushStyleColor(ImGuiCol_Text, sendFeedback_->success ? ImVec4(0.3f, 1.0f, 0.5f, 1.0f)
                                                                   : ImVec4(1.0f, 0.4f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s  #%llu -> %s", tr(sendFeedback_->success ? "send_success" : "send_failed"),
                           static_cast<unsigned long long>(sendFeedback_->messageLocalId),
                           sendFeedback_->recipient.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    switch (page_) {
        case Page::Status: renderStatus(); break;
        case Page::Messages: renderMessages(); break;
        case Page::Atc: renderAtc(); break;
        case Page::Request: renderRequest(); break;
        case Page::WxTelex: renderWxTelex(); break;
        case Page::Settings: renderSettings(); break;
    }
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 28.0f));
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.16f, 1.0f), "%s", tr("warning"));
    ImGui::End();

    ImGui::Render();
    renderWindowDrawData(ImGui::GetDrawData(), {left, top, right, bottom}, transform);

    auto& settings = app_.settings();
    settings.windowLeft = left;
    settings.windowTop = top;
    settings.windowRight = right;
    settings.windowBottom = bottom;
}

void DcduWindow::renderHeader() {
    ImGui::TextUnformatted(tr("title"));
    ImGui::SameLine();
    ImGui::TextDisabled("v%s", HOPPIE_VERSION);

    auto pageButton = [&](Page page, const char* label, bool alert = false) {
        if (alert && std::fmod(ImGui::GetTime(), 1.0) < 0.5)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.40f, 0.03f, 1.0f));
        if (ImGui::Button(label)) page_ = page;
        if (alert && std::fmod(ImGui::GetTime(), 1.0) < 0.5) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    pageButton(Page::Status, tr("status"));
    const auto unread = app_.unreadCount();
    const auto messageLabel = std::string(tr("messages")) + (unread ? " (" + std::to_string(unread) + ")" : "");
    pageButton(Page::Messages, messageLabel.c_str(), unread != 0);
    pageButton(Page::Atc, tr("atc"));
    pageButton(Page::Request, tr("request"));
    pageButton(Page::WxTelex, tr("wx_telex"));
    if (ImGui::Button(tr("settings"))) page_ = Page::Settings;
}

void DcduWindow::renderStatus() {
    fillFlightPlanDefaults();
    ImGui::Text("%s: %s", tr("status"), app_.statusText().c_str());
    ImGui::Text("HOPPIE: %s", app_.connected() ? "CONNECTED" : "DISCONNECTED");
    if (app_.connected()) {
        if (!app_.pollingActivated()) ImGui::TextDisabled("POLL: WAITING FOR FIRST REQUEST");
        else if (app_.pollInFlight()) ImGui::TextDisabled("POLL: RECEIVING");
        else if (const auto next = app_.nextPoll()) {
            const auto seconds = std::max<long long>(0, std::chrono::duration_cast<std::chrono::seconds>(
                *next - std::chrono::steady_clock::now()).count());
            ImGui::TextDisabled("NEXT POLL: %lld S", seconds);
        }
        if (const auto last = app_.lastSuccessfulPoll()) {
            const auto seconds = std::max<long long>(0, std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - *last).count());
            ImGui::TextDisabled("LAST POLL OK: %lld S AGO", seconds);
        }
    }
    ImGui::Text("CPDLC: %s", app_.session().currentAtsu().empty()
                                     ? "NO ACTIVE STATION"
                                     : app_.session().currentAtsu().c_str());
    if (!app_.session().pendingAtsu().empty())
        ImGui::Text("PENDING STATION: %s", app_.session().pendingAtsu().c_str());
    if (!app_.session().lastError().empty())
        ImGui::TextWrapped("CPDLC: %s", app_.session().lastError().c_str());
    ImGui::SetNextItemWidth(220.0f);
    inputUpper(tr("callsign"), callsign_);
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("HOPPIE LOGON CODE", &logonCode_, ImGuiInputTextFlags_Password);
    ImGui::Checkbox("REMEMBER IN OS SECURE STORE", &rememberCode_);
    if (!app_.connected()) {
        ImGui::BeginDisabled(app_.busy() || !validCallsign(callsign_) || logonCode_.empty());
        if (ImGui::Button(tr("connect"))) {
            app_.setManualField("callsign", callsign_);
            app_.setCallsign(callsign_);
            app_.setLogonCode(logonCode_, rememberCode_);
            app_.connect();
        }
        ImGui::EndDisabled();
    } else if (ImGui::Button(tr("disconnect"))) {
        app_.disconnect();
    }
    ImGui::SeparatorText("CPDLC");
    ImGui::SetNextItemWidth(180.0f);
    inputUpper(tr("atsu"), atsu_);
    ImGui::SameLine();
    if (app_.session().currentAtsu().empty()) {
        ImGui::BeginDisabled(!app_.connected() || !validIcao(atsu_) ||
                             !app_.session().pendingAtsu().empty());
        if (ImGui::Button(tr("logon"))) app_.logon(atsu_);
        ImGui::EndDisabled();
    } else {
        ImGui::BeginDisabled(!validIcao(atsu_) || atsu_ == app_.session().currentAtsu() ||
                             !app_.session().pendingAtsu().empty());
        if (ImGui::Button(tr("handover"))) app_.logon(atsu_);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("logoff"))) app_.logoff();
    }
    ImGui::SeparatorText("FLIGHT PLAN SOURCES");
    ImGui::Text("CALLSIGN %s   TYPE %s", app_.flightPlan().callsign.c_str(),
                app_.flightPlan().aircraftType.c_str());
    ImGui::Text("%s -> %s", app_.flightPlan().departure.c_str(),
                app_.flightPlan().destination.c_str());
    ImGui::TextWrapped("ROUTE: %s", app_.flightPlan().route.c_str());
}

void DcduWindow::renderMessages() {
    ImGui::BeginDisabled(app_.messages().empty());
    if (ImGui::Button(tr("clear_messages"))) {
        app_.clearMessages();
        selectedMessageId_ = 0;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("inbox_only"));
    auto& messages = app_.messages();
    if (messages.empty()) {
        ImGui::TextUnformatted(tr("no_messages"));
        return;
    }
    std::uint64_t deleteId = 0;
    ImGui::BeginChild("message-list", ImVec2(225.0f, -40.0f), ImGuiChildFlags_Borders);
    for (std::size_t index = 0; index < messages.size(); ++index) {
        auto& message = messages[index];
        if (message.direction != MessageDirection::Received) continue;
        const auto label = std::string("RX ") + message.peer + "  " + stateName(message.state) + "##" +
                           std::to_string(message.localId);
        const bool wasUnread = message.unread;
        if (wasUnread) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.67f, 0.1f, 1.0f));
        if (ImGui::Selectable(label.c_str(), selectedMessageId_ == message.localId)) {
            selectedMessageId_ = message.localId;
            message.unread = false;
        }
        if (wasUnread) ImGui::PopStyleColor();
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem(tr("delete_message"))) deleteId = message.localId;
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    if (deleteId != 0) {
        app_.deleteMessage(deleteId);
        if (selectedMessageId_ == deleteId) selectedMessageId_ = 0;
    }
    ImGui::SameLine();
    ImGui::BeginChild("message-detail", ImVec2(0.0f, -40.0f), ImGuiChildFlags_Borders);
    const auto selected = std::find_if(messages.begin(), messages.end(), [&](const auto& message) {
        return message.localId == selectedMessageId_ && message.direction == MessageDirection::Received;
    });
    if (selected != messages.end()) {
        auto& message = *selected;
        ImGui::Text("%s / %s / %s", message.peer.c_str(), message.type.c_str(), stateName(message.state));
        const bool rawCpdlc = uppercaseAscii(message.type) == "CPDLC" && !message.cpdlc;
        if (message.cpdlc) {
            const auto& packet = *message.cpdlc;
            ImGui::TextDisabled("ID %s / REPLY TO %s / RESPONSE %s", packet.messageId.c_str(),
                packet.replyTo.empty() ? "-" : packet.replyTo.c_str(),
                packet.responseKind.empty() ? "(EMPTY)" : packet.responseKind.c_str());
            if (!isKnownCpdlcResponseKind(packet.responseKind))
                ImGui::TextDisabled("UNSUPPORTED RESPONSE TYPE — READ ONLY");
        } else if (rawCpdlc) {
            ImGui::TextDisabled("UNRECOGNIZED CPDLC — RAW MESSAGE / READ ONLY");
        }
        ImGui::Separator();
        if (rawCpdlc) ImGui::TextWrapped("%s", message.body.c_str());
        else renderMessageText(message.body);
        renderSentReply(message);
        const auto replies = availableReplies(message);
        if (!replies.empty()) {
            ImGui::SeparatorText("REPLY");
            const auto selectedIndex = static_cast<std::size_t>(std::distance(messages.begin(), selected));
            for (const auto action : replies) {
                renderReplyButton(selectedIndex, action);
                ImGui::SameLine();
            }
        } else if (message.replyInFlight) {
            ImGui::TextDisabled("SENDING REPLY...");
        }
    }
    ImGui::EndChild();
}

void DcduWindow::renderReplyButton(std::size_t index, ReplyAction action) {
    const auto label = replyText(action);
    if (ImGui::Button(label.c_str())) app_.reply(index, action);
}

void DcduWindow::renderAtc() {
    fillFlightPlanDefaults();
    if (ImGui::CollapsingHeader("ALTITUDE / DIRECT / SPEED", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(110.0f); inputUpper("FL##alt", altitude_); ImGui::SameLine();
        ImGui::BeginDisabled(!app_.connected() || !validFlightLevel(altitude_));
        if (ImGui::Button("REQUEST ALTITUDE")) {
            try { app_.sendCpdlc(buildAltitudeRequest(altitude_)); } catch (...) {}
        }
        ImGui::SameLine();
        if (ImGui::Button("REQUEST DESCENT")) {
            try { app_.sendCpdlc(buildDescentRequest(altitude_)); } catch (...) {}
        }
        ImGui::EndDisabled();
        ImGui::SetNextItemWidth(150.0f); inputUpper("WAYPOINT##direct", directTo_); ImGui::SameLine();
        const bool directValid = canBuild([&] { return buildDirectRequest(directTo_); });
        ImGui::BeginDisabled(!app_.connected() || !directValid);
        if (ImGui::Button("REQUEST DIRECT")) {
            try { app_.sendCpdlc(buildDirectRequest(directTo_)); } catch (...) {}
        }
        ImGui::EndDisabled();
        ImGui::SetNextItemWidth(150.0f); inputUpper("SPEED / MACH##speed", speed_); ImGui::SameLine();
        const bool speedValid = canBuild([&] { return buildSpeedRequest(speed_); });
        ImGui::BeginDisabled(!app_.connected() || !speedValid);
        if (ImGui::Button("REQUEST SPEED")) {
            try { app_.sendCpdlc(buildSpeedRequest(speed_)); } catch (...) {}
        }
        ImGui::EndDisabled();
        ImGui::SetNextItemWidth(150.0f); inputUpper("TARGET##expect", expected_); ImGui::SameLine();
        const bool expectedValid = canBuild([&] { return buildWhenExpectedRequest(expected_); });
        ImGui::BeginDisabled(!app_.connected() || !expectedValid);
        if (ImGui::Button("WHEN CAN WE EXPECT")) {
            try { app_.sendCpdlc(buildWhenExpectedRequest(expected_)); } catch (...) {}
        }
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("POSITION REPORT")) {
        inputUpper("CURRENT FIX", position_.currentFix);
        inputUpper("TIME (HHMM)", position_.currentTime);
        inputUpper("FLIGHT LEVEL", position_.flightLevel);
        inputUpper("NEXT FIX", position_.nextFix);
        inputUpper("NEXT ETA", position_.nextEta);
        inputUpper("FOLLOWING FIX", position_.followingFix);
        position_.callsign = callsign_;
        const bool positionValid = canBuild([&] { return buildPositionReport(position_); });
        ImGui::BeginDisabled(!app_.connected() || !positionValid);
        if (ImGui::Button("SEND POSITION REPORT")) {
            try { app_.sendCpdlc(buildPositionReport(position_), "N"); } catch (...) {}
        }
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("FREE TEXT")) {
        ImGui::InputTextMultiline("##free", &freeText_, ImVec2(-1.0f, 80.0f),
                                  ImGuiInputTextFlags_CharsUppercase);
        ImGui::Checkbox("REPLY EXPECTED", &freeTextReply_);
        ImGui::BeginDisabled(!app_.connected() || !validAviationEntry(freeText_));
        if (ImGui::Button("SEND FREE TEXT")) app_.sendCpdlc(freeText_, freeTextReply_ ? "Y" : "N");
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("OCEANIC CLEARANCE")) {
        inputUpper("AIRCRAFT TYPE##ocean", oceanAircraft_);
        inputUpper("ENTRY POINT", oceanEntry_);
        inputUpper("ENTRY TIME", oceanTime_);
        inputUpper("FL##ocean", oceanLevel_);
        inputUpper("MACH##ocean", oceanMach_);
        ImGui::InputTextMultiline("ROUTE##ocean", &oceanRoute_, ImVec2(-1.0f, 55.0f),
                                  ImGuiInputTextFlags_CharsUppercase);
        const bool oceanValid = canBuild([&] {
            return buildOceanicClearance(callsign_, oceanAircraft_, oceanEntry_, oceanTime_,
                                         oceanLevel_, oceanMach_, oceanRoute_);
        });
        ImGui::BeginDisabled(!app_.connected() || !oceanValid);
        if (ImGui::Button("REQUEST OCEANIC CLEARANCE")) {
            try {
                app_.sendCpdlc(buildOceanicClearance(callsign_, oceanAircraft_, oceanEntry_,
                                                      oceanTime_, oceanLevel_, oceanMach_, oceanRoute_));
            } catch (...) {}
        }
        ImGui::EndDisabled();
    }
}

void DcduWindow::renderRequest() {
    fillFlightPlanDefaults();
    ImGui::SeparatorText("DCL — PRE-DEPARTURE CLEARANCE");
    inputUpper("Station##dcl", dcl_.atsu);
    ImGui::InputText("CALLSIGN##dcl", &dcl_.callsign, ImGuiInputTextFlags_ReadOnly);
    inputUpper("AIRCRAFT TYPE##dcl", dcl_.aircraftType);
    inputUpper("DEPARTURE##dcl", dcl_.departure);
    inputUpper("DESTINATION##dcl", dcl_.destination);
    inputUpper("STAND##dcl", dcl_.stand);
    inputUpper("ATIS##dcl", dcl_.atis);
    ImGui::InputTextMultiline("REMARKS##dcl", &dcl_.remarks, ImVec2(-1.0f, 65.0f),
                              ImGuiInputTextFlags_CharsUppercase);
    const bool dclValid = validIcao(dcl_.atsu) && canBuild([&] { return buildDclRequest(dcl_); });
    ImGui::BeginDisabled(!app_.connected() || !dclValid);
    if (ImGui::Button("SEND DCL REQUEST")) app_.sendDcl(dcl_);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Clearance is displayed for review and is never written to the FMS.");
}

void DcduWindow::renderWxTelex() {
    static const char* products[] = {"METAR", "TAF", "SHORT TAF", "VATSIM ATIS",
                                     "IVAO ATIS", "PILOTEDGE ATIS"};
    static const char* wireProducts[] = {"metar", "taf", "shorttaf", "vatatis",
                                         "ivaoatis", "peatis"};
    ImGui::SeparatorText("WEATHER / ATIS");
    ImGui::Combo("PRODUCT", &infoProduct_, products, static_cast<int>(std::size(products)));
    inputUpper("ICAO", infoIcao_);
    ImGui::BeginDisabled(!app_.connected() || !validIcao(infoIcao_));
    if (ImGui::Button("REQUEST INFORMATION")) app_.sendInfo(wireProducts[infoProduct_], infoIcao_);
    ImGui::EndDisabled();
    ImGui::SeparatorText("TELEX");
    inputUpper("TO", telexTo_);
    ImGui::InputTextMultiline("MESSAGE", &telexText_, ImVec2(-1.0f, 100.0f),
                              ImGuiInputTextFlags_CharsUppercase);
    ImGui::BeginDisabled(!app_.connected() || !validCallsign(telexTo_) ||
                         !validAviationEntry(telexText_));
    if (ImGui::Button("SEND TELEX")) app_.sendTelex(telexTo_, telexText_);
    ImGui::EndDisabled();
}

void DcduWindow::renderSettings() {
    auto& settings = app_.settings();
    int language = settings.language == "zh" ? 0 : 1;
    const char* languages[] = {"中文", "English"};
    if (ImGui::Combo(tr("language"), &language, languages, 2))
        settings.language = language == 0 ? "zh" : "en";
    ImGui::InputText("VATSIM CID", &settings.vatsimCid,
                     ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if (ImGui::Button(tr("fetch_vatsim"))) {
        app_.saveSettings();
        app_.requestVatsim();
    }
    ImGui::InputText("SIMBRIEF PILOT ID", &settings.simbriefId,
                     ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if (ImGui::Button(tr("fetch_simbrief"))) {
        app_.saveSettings();
        app_.requestSimBrief();
    }
    static const char* atisLabels[] = {"VATSIM", "IVAO", "PilotEdge"};
    static const char* atisSources[] = {"vatatis", "ivaoatis", "peatis"};
    int atis = settings.defaultAtisSource == "ivaoatis" ? 1
               : settings.defaultAtisSource == "peatis" ? 2 : 0;
    if (ImGui::Combo("DEFAULT ATIS", &atis, atisLabels, 3))
        settings.defaultAtisSource = atisSources[atis];
    ImGui::Checkbox(tr("sound"), &settings.soundEnabled);
    bool adscEnabled = settings.adscEnabled;
    if (ImGui::Checkbox(tr("adsc"), &adscEnabled)) {
        auto updated = settings;
        updated.adscEnabled = adscEnabled;
        app_.updateSettings(std::move(updated));
    }
    if (ImGui::Button(tr("save"))) app_.saveSettings();
    if (!app_.secureStorageAvailable())
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.1f, 1.0f),
                           "Hoppie code is held only for this simulator session.");
    ImGui::TextWrapped("Messages remain in memory only. Logs contain status, error category, and message ID only.");
}

void DcduWindow::fillFlightPlanDefaults() {
    const auto& plan = app_.flightPlan();
    if (callsign_.empty()) callsign_ = plan.callsign;
    if (position_.callsign.empty()) position_.callsign = callsign_;
    if (oceanAircraft_.empty()) oceanAircraft_ = plan.aircraftType;
    if (oceanRoute_.empty()) oceanRoute_ = plan.route;
    dcl_.callsign = app_.session().callsign();
    if (dcl_.aircraftType.empty()) dcl_.aircraftType = plan.aircraftType;
    if (dcl_.departure.empty()) dcl_.departure = plan.departure;
    if (dcl_.destination.empty()) dcl_.destination = plan.destination;
    if (dcl_.atsu.empty()) dcl_.atsu = plan.departure;
    if (!plan.waypoints.empty()) {
        if (directTo_.empty()) directTo_ = plan.waypoints.front();
        if (position_.currentFix.empty()) position_.currentFix = plan.waypoints.front();
        if (position_.nextFix.empty() && plan.waypoints.size() > 1)
            position_.nextFix = plan.waypoints[1];
        if (position_.followingFix.empty() && plan.waypoints.size() > 2)
            position_.followingFix = plan.waypoints[2];
    }
}

const char* DcduWindow::tr(const char* key) {
    thread_local std::string value;
    value = translate(app_.settings().language, key);
    return value.c_str();
}

void DcduWindow::updateMouse(int x, int y) {
    UiWindowBounds bounds{};
    XPLMGetWindowGeometry(window_, &bounds.left, &bounds.top, &bounds.right, &bounds.bottom);
    const auto point = bounds.toLocal(x, y);
    ImGui::SetCurrentContext(context_);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(point.x), static_cast<float>(point.y));
}

int DcduWindow::mouseCallback(XPLMWindowID window, int x, int y,
                              XPLMMouseStatus status, void* refcon) {
    auto* self = static_cast<DcduWindow*>(refcon);
    self->updateMouse(x, y);
    ImGui::SetCurrentContext(self->context_);
    if (status == xplm_MouseDown) {
        XPLMTakeKeyboardFocus(window);
        ImGui::GetIO().AddMouseButtonEvent(0, true);
    } else if (status == xplm_MouseUp) {
        ImGui::GetIO().AddMouseButtonEvent(0, false);
    }
    return 1;
}

int DcduWindow::rightMouseCallback(XPLMWindowID, int x, int y,
                                   XPLMMouseStatus status, void* refcon) {
    auto* self = static_cast<DcduWindow*>(refcon);
    self->updateMouse(x, y);
    ImGui::SetCurrentContext(self->context_);
    if (status == xplm_MouseDown) ImGui::GetIO().AddMouseButtonEvent(1, true);
    if (status == xplm_MouseUp) ImGui::GetIO().AddMouseButtonEvent(1, false);
    return 1;
}

void DcduWindow::keyCallback(XPLMWindowID, char key, XPLMKeyFlags flags,
                             char virtualKey, void* refcon, int losingFocus) {
    auto* self = static_cast<DcduWindow*>(refcon);
    ImGui::SetCurrentContext(self->context_);
    auto& io = ImGui::GetIO();
    if (losingFocus) {
        io.AddFocusEvent(false);
        return;
    }
    io.AddFocusEvent(true);
    const bool down = (flags & xplm_DownFlag) != 0;
    io.AddKeyEvent(ImGuiMod_Ctrl, (flags & xplm_ControlFlag) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (flags & xplm_ShiftFlag) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (flags & xplm_OptionAltFlag) != 0);
    const auto translated = translateKey(static_cast<unsigned char>(virtualKey));
    if (translated != ImGuiKey_None) io.AddKeyEvent(translated, down);
    if (down && (flags & (xplm_ControlFlag | xplm_OptionAltFlag)) == 0 &&
        static_cast<unsigned char>(key) >= 0x20)
        io.AddInputCharacter(static_cast<unsigned char>(key));
}

XPLMCursorStatus DcduWindow::cursorCallback(XPLMWindowID, int x, int y, void* refcon) {
    static_cast<DcduWindow*>(refcon)->updateMouse(x, y);
    return xplm_CursorDefault;
}

int DcduWindow::wheelCallback(XPLMWindowID, int x, int y, int wheel,
                              int clicks, void* refcon) {
    auto* self = static_cast<DcduWindow*>(refcon);
    self->updateMouse(x, y);
    ImGui::SetCurrentContext(self->context_);
    ImGui::GetIO().AddMouseWheelEvent(wheel == 0 ? 0.0f : static_cast<float>(clicks),
                                      wheel == 0 ? static_cast<float>(clicks) : 0.0f);
    return 1;
}

}  // namespace hoppie
