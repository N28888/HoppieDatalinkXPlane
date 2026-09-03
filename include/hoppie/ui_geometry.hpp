#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace hoppie {

struct UiPoint {
    double x, y;
};

// Both window geometry and mouse callbacks use global desktop boxels, not pixels.
struct UiWindowBounds {
    int left, top, right, bottom;

    UiPoint toLocal(double x, double y) const { return {x - left, top - y}; }
    UiPoint toGlobal(double x, double y) const { return {left + x, top - y}; }
};

struct UiScissor {
    int x{}, y{}, width{}, height{};
};

struct UiDrawTransform {
    std::array<float, 16> modelview{}, projection{};
    std::array<int, 4> viewport{};

    // Column-major OpenGL transforms supplied for this particular XPLM draw callback.
    // Do not infer these from desktop size: pop-outs and high-DPI windows differ.
    std::optional<UiPoint> project(UiPoint global) const {
        const std::array<double, 4> point{global.x, global.y, 0.0, 1.0};
        std::array<double, 4> eye{}, clip{};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                eye[row] += modelview[col * 4 + row] * point[col];
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                clip[row] += projection[col * 4 + row] * eye[col];
        if (!std::isfinite(clip[3]) || std::abs(clip[3]) < 1e-12) return std::nullopt;
        UiPoint pixel{viewport[0] + (clip[0] / clip[3] + 1.0) * viewport[2] * 0.5,
                      viewport[1] + (clip[1] / clip[3] + 1.0) * viewport[3] * 0.5};
        if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y)) return std::nullopt;
        return pixel;
    }

    UiScissor scissor(const UiWindowBounds& window, double left, double top,
                      double right, double bottom) const {
        // Keep every ImGui draw command (including popups) inside the XPLM content area.
        left = std::max(0.0, left);
        top = std::max(0.0, top);
        right = std::min(static_cast<double>(window.right - window.left), right);
        bottom = std::min(static_cast<double>(window.top - window.bottom), bottom);
        if (right <= left || bottom <= top || viewport[2] <= 0 || viewport[3] <= 0) return {};
        const std::array<UiPoint, 4> corners{{{left, top}, {right, top},
                                             {left, bottom}, {right, bottom}}};
        double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
        for (const auto corner : corners) {
            const auto pixel = project(window.toGlobal(corner.x, corner.y));
            if (!pixel) return {};
            minX = std::min(minX, pixel->x);
            minY = std::min(minY, pixel->y);
            maxX = std::max(maxX, pixel->x);
            maxY = std::max(maxY, pixel->y);
        }
        // Round outward, then intersect the current framebuffer viewport.
        minX = std::max(std::floor(minX), static_cast<double>(viewport[0]));
        minY = std::max(std::floor(minY), static_cast<double>(viewport[1]));
        maxX = std::min(std::ceil(maxX), static_cast<double>(viewport[0]) + viewport[2]);
        maxY = std::min(std::ceil(maxY), static_cast<double>(viewport[1]) + viewport[3]);
        if (maxX <= minX || maxY <= minY) return {};
        return {static_cast<int>(minX), static_cast<int>(minY),
                static_cast<int>(maxX - minX), static_cast<int>(maxY - minY)};
    }
};

}  // namespace hoppie
