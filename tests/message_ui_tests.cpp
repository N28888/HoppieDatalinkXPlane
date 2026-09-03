#include "hoppie/message_ui.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "line " << __LINE__ << ": " #expression "\n"; ++failures; } } while (false)

void compareWrappedText(const char* marked, float width, float scale) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width + 40, 1600));
    ImGui::Begin("Message rendering test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(scale);
    ImGui::SetCursorScreenPos(ImVec2(20, 20));
    auto* list = ImGui::GetWindowDrawList();
    const auto display = hoppie::formatMessageText(marked);
    const int first = list->VtxBuffer.Size;
    hoppie::renderMessageText(marked);
    const float customBottom = ImGui::GetCursorScreenPos().y;
    const int second = list->VtxBuffer.Size;
    ImGui::SetCursorScreenPos(ImVec2(20, 20));
    ImGui::TextWrapped("%s", display.text.c_str());
    const float plainBottom = ImGui::GetCursorScreenPos().y;
    const int third = list->VtxBuffer.Size;
    CHECK(second - first == third - second);
    CHECK(std::abs(customBottom - plainBottom) < 1.1f);
    const auto normal = ImGui::GetColorU32(ImGuiCol_Text);
    const auto orange = ImGui::GetColorU32(ImVec4(1.0f, 0.67f, 0.1f, 1.0f));
    bool hasOrange = false;
    for (int i = 0; i < second - first && i < third - second; ++i) {
        const auto& colored = list->VtxBuffer[first + i];
        const auto& plain = list->VtxBuffer[second + i];
        CHECK(std::abs(colored.pos.x - plain.pos.x) < 1.1f);
        CHECK(std::abs(colored.pos.y - plain.pos.y) < 1.1f);
        CHECK(colored.uv.x == plain.uv.x && colored.uv.y == plain.uv.y); // no marker glyphs
        CHECK(colored.col == normal || colored.col == orange);
        hasOrange |= colored.col == orange;
    }
    if (display.text.find("ZLXY") != std::string::npos) CHECK(hasOrange);
    ImGui::End();
    ImGui::Render();
}
void compareSentReply(const hoppie::DatalinkMessage& message, const char* label, ImVec4 color, float scale) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(400, 300));
    ImGui::Begin("Reply rendering test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(scale);
    ImGui::SetCursorScreenPos(ImVec2(20, 20));
    auto* list = ImGui::GetWindowDrawList();
    const int first = list->VtxBuffer.Size;
    hoppie::renderSentReply(message);
    const float bottom = ImGui::GetCursorScreenPos().y;
    const int second = list->VtxBuffer.Size;
    if (!label) {
        CHECK(second == first);
        CHECK(bottom == 20.0f);
    } else {
        ImGui::SetCursorScreenPos(ImVec2(20, 20));
        ImGui::TextColored(color, "%s", label);
        const int third = list->VtxBuffer.Size;
        CHECK(second > first);
        CHECK(second - first == third - second);
        CHECK(bottom == ImGui::GetCursorScreenPos().y);
        for (int i = 0; i < second - first && i < third - second; ++i) {
            const auto& actual = list->VtxBuffer[first + i];
            const auto& expected = list->VtxBuffer[second + i];
            CHECK(actual.pos.x == expected.pos.x && actual.pos.y == expected.pos.y);
            CHECK(actual.uv.x == expected.uv.x && actual.uv.y == expected.uv.y);
            CHECK(actual.col == expected.col); // static text, exact label and expected color
        }
    }
    ImGui::End();
    ImGui::Render();
}

void testSentReplyPresentation() {
    using namespace hoppie;
    struct Case { ReplyAction action; const char* label; ImVec4 color; };
    const ImVec4 green(0.4f, 0.9f, 0.5f, 1.0f), red(1.0f, 0.4f, 0.3f, 1.0f), orange(1.0f, 0.67f, 0.1f, 1.0f);
    for (float scale : {1.0f, 1.5f, 2.0f}) {
        DatalinkMessage message;
        compareSentReply(message, nullptr, green, scale);
        message.replyInFlight = true;
        compareSentReply(message, nullptr, green, scale); // pending is not successful
        for (const auto& item : {Case{ReplyAction::Wilco, "WILCO", green},
                                 Case{ReplyAction::Unable, "UNABLE", red},
                                 Case{ReplyAction::Affirm, "AFFIRM", green},
                                 Case{ReplyAction::Negative, "NEGATIVE", red},
                                 Case{ReplyAction::Roger, "ROGER", green},
                                 Case{ReplyAction::Standby, "STANDBY", orange},
                                 Case{ReplyAction::Accept, "ACCEPTED", green},
                                 Case{ReplyAction::Reject, "REJECT", red}}) {
            message.sentReply = item.action;
            compareSentReply(message, item.label, item.color, scale);
        }
        message.direction = MessageDirection::Sent;
        compareSentReply(message, nullptr, green, scale);
    }
}
} // namespace

int main(int argc, char** argv) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(2000, 2000);
    io.DeltaTime = 1.0f / 60.0f;
    if (argc > 1) {
        static const ImWchar ranges[] = {0x20, 0x7e, 0x4e2d, 0x4e2d, 0x6587, 0x6587, 0};
        if (!io.Fonts->AddFontFromFileTTF(argv[1], 16.0f, nullptr, ranges)) return EXIT_FAILURE;
    } else io.Fonts->AddFontDefault();
    io.Fonts->Build();
    const char* body = "CLD 2035 260903 ZGHA PDC 002 CLRD TO @ZLXY@ OFF @36L@ VIA @OLT8X@ "
        "SQUAWK @1116@ NEXT FREQ @132.750@ INITIAL ALT @1200 M@ @QNH 1008@ "
        "NEXT FREQ @132.750@ DEP FREQ @132.750@ FINAL ALT @FL 11600 M@ @READBACK UNNECESSARY@";
    for (float width : {140.0f, 260.0f, 460.0f}) {
        for (float scale : {1.0f, 1.5f, 2.0f}) {
            compareWrappedText(body, width, scale);
            compareWrappedText("@ZLXY@\n\nQNH @1008\nHPA@\r\nEND\n", width, scale);
            compareWrappedText("PREFIX@ABCDEFGHIJKLMNOPQRSTUVWX@@YZ0123456789@SUFFIX", width, scale);
            compareWrappedText("NO MARKERS  MULTIPLE SPACES %s##TEST", width, scale);
            compareWrappedText("@中文@ @ZLXY@ TRAILING@", width, scale);
        }
    }
    compareWrappedText("@中文@", 1.0f, 1.0f);
    compareWrappedText("", 200.0f, 1.0f);
    compareWrappedText("@@", 200.0f, 1.0f);
    testSentReplyPresentation();
    ImGui::DestroyContext();
    if (failures) return EXIT_FAILURE;
    std::cout << "Colored message text and static crew replies match native ImGui glyphs, layout and colors\n";
    return EXIT_SUCCESS;
}
