#pragma once

#include "hoppie/core.hpp"
#include <imgui.h>

#include <algorithm>
#include <cfloat>

namespace hoppie {

inline void renderSentReply(const DatalinkMessage& message) {
    if (!message.sentReply || message.direction != MessageDirection::Received) return;
    const auto action = *message.sentReply;
    ImVec4 color(0.4f, 0.9f, 0.5f, 1.0f);
    if (action == ReplyAction::Unable || action == ReplyAction::Negative || action == ReplyAction::Reject)
        color = ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
    else if (action == ReplyAction::Standby)
        color = ImVec4(1.0f, 0.67f, 0.1f, 1.0f);
    const auto label = action == ReplyAction::Accept ? "ACCEPTED" : replyText(action);
    ImGui::TextColored(color, "%s", label.c_str());
}

// Use ImGui's font wrapping on the complete, marker-free text so changing color
// does not insert spaces or turn each highlighted field into a separate line.
inline void renderMessageText(std::string_view body) {
    const auto message = formatMessageText(body);
    const auto& text = message.text;
    auto* font = ImGui::GetFont();
    auto* draw = ImGui::GetWindowDrawList();
    const float size = ImGui::GetFontSize();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImU32 normal = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 orange = ImGui::GetColorU32(ImVec4(1.0f, 0.67f, 0.1f, 1.0f));
    float y = origin.y;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto newline = text.find('\n', start);
        const auto paragraphEnd = newline == std::string::npos ? text.size() : newline;
        auto lineEnd = static_cast<std::size_t>(font->CalcWordWrapPositionA(
            size / font->FontSize, text.data() + start, text.data() + paragraphEnd, width) - text.data());
        // ImGui may force a one-byte wrap in a tiny window; finish that UTF-8 glyph.
        while (lineEnd < paragraphEnd && (static_cast<unsigned char>(text[lineEnd]) & 0xc0) == 0x80)
            ++lineEnd;
        float x = origin.x;
        for (auto run = start; run < lineEnd;) {
            auto end = run + 1;
            while (end < lineEnd && message.highlighted[end] == message.highlighted[run]) ++end;
            const char* first = text.data() + run;
            const char* last = text.data() + end;
            draw->AddText(font, size, ImVec2(x, y), message.highlighted[run] ? orange : normal, first, last);
            x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, first, last).x;
            run = end;
        }
        y += ImGui::GetTextLineHeight();
        start = lineEnd;
        if (start == paragraphEnd && start < text.size()) ++start; // explicit newline
        else while (start < paragraphEnd && (text[start] == ' ' || text[start] == '\t')) ++start;
    }
    ImGui::Dummy(ImVec2(width, std::max(size, y - origin.y)));
}

} // namespace hoppie
