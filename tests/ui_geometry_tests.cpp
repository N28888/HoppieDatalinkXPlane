#include "hoppie/ui_geometry.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
#define CHECK(expression) \
    do { if (!(expression)) { \
        std::cerr << "line " << __LINE__ << ": " #expression "\n"; ++failures; \
    } } while (false)

bool close(double a, double b) { return std::abs(a - b) < 0.001; }

hoppie::UiDrawTransform ortho(double left, double bottom, double width, double height,
                             std::array<int, 4> viewport) {
    hoppie::UiDrawTransform result;
    result.modelview = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    result.projection = {static_cast<float>(2 / width), 0, 0, 0,
                         0, static_cast<float>(2 / height), 0, 0,
                         0, 0, -1, 0,
                         static_cast<float>(-1 - 2 * left / width),
                         static_cast<float>(-1 - 2 * bottom / height), 0, 1};
    result.viewport = viewport;
    return result;
}

void checkPoint(const std::optional<hoppie::UiPoint>& actual, double x, double y) {
    CHECK(actual.has_value());
    if (actual) { CHECK(close(actual->x, x)); CHECK(close(actual->y, y)); }
}

void checkScissor(const hoppie::UiScissor& actual, int x, int y, int width, int height) {
    CHECK(actual.x == x); CHECK(actual.y == y);
    CHECK(actual.width == width); CHECK(actual.height == height);
}
}  // namespace

int main() {
    const hoppie::UiWindowBounds window{320, 640, 960, 128};
    for (const double scale : {1.0, 1.5, 2.0}) {
        const auto frame = ortho(0, 0, 1024, 1024,
                                  {0, 0, static_cast<int>(1024 * scale),
                                         static_cast<int>(1024 * scale)});
        // Button layout and all mouse callbacks use the same local boxel point.
        const auto click = window.toLocal(400, 600);
        CHECK(close(click.x, 80)); CHECK(close(click.y, 40));
        const auto global = window.toGlobal(click.x, click.y);
        checkPoint(frame.project(global), 400 * scale, 600 * scale);
        checkPoint(frame.project(window.toGlobal(0, 0)), 320 * scale, 640 * scale);
        checkScissor(frame.scissor(window, 0, 0, 640, 512),
                     static_cast<int>(320 * scale), static_cast<int>(128 * scale),
                     static_cast<int>(640 * scale), static_cast<int>(512 * scale));
        // A child/control clip is mapped to framebuffer pixels, not boxels.
        checkScissor(frame.scissor(window, 16, 24, 216, 56),
                     static_cast<int>(336 * scale), static_cast<int>(584 * scale),
                     static_cast<int>(200 * scale), static_cast<int>(32 * scale));
    }

    // Window and screen on a negative-origin monitor; viewport origin is not zero.
    const hoppie::UiWindowBounds secondary{-1600, 640, -960, 128};
    const auto multiMonitor = ortho(-2048, -256, 2048, 1024, {100, 50, 4096, 2048});
    const auto click = secondary.toLocal(-1520, 600);
    CHECK(close(click.x, 80)); CHECK(close(click.y, 40));
    checkPoint(multiMonitor.project(secondary.toGlobal(80, 40)), 1156, 1762);
    checkScissor(multiMonitor.scissor(secondary, 0, 0, 640, 512), 996, 818, 1280, 1024);

    // Pop-out has its own viewport and modelview translation, independent of the desktop.
    const hoppie::UiWindowBounds popped{2200, 1400, 2712, 888};
    auto popup = ortho(0, 0, 512, 512, {0, 0, 1024, 1024});
    popup.modelview[12] = -2200;
    popup.modelview[13] = -888;
    checkPoint(popup.project(popped.toGlobal(0, 0)), 0, 1024);
    checkPoint(popup.project(popped.toGlobal(80, 40)), 160, 944);
    checkScissor(popup.scissor(popped, 0, 0, 512, 512), 0, 0, 1024, 1024);
    checkScissor(popup.scissor(popped, 32, 48, 160, 80), 64, 864, 256, 64);

    // Moving/resizing uses fresh bounds; no cached desktop offsets are involved.
    const hoppie::UiWindowBounds moved{400, 720, 1168, 144};
    CHECK(close(moved.toLocal(480, 680).x, 80));
    CHECK(close(moved.toLocal(480, 680).y, 40));
    const auto desktop = ortho(0, 0, 2048, 1024, {0, 0, 3072, 1536});
    checkScissor(desktop.scissor(moved, 0, 0, 768, 576), 600, 216, 1152, 864);

    // Content clips never spill outside the native window or active viewport.
    const auto normal = ortho(0, 0, 1024, 1024, {0, 0, 1024, 1024});
    checkScissor(normal.scissor(window, -100, -100, 9999, 9999), 320, 128, 640, 512);
    const hoppie::UiWindowBounds partlyOutside{-128, 640, 512, 128};
    checkScissor(normal.scissor(partlyOutside, 0, 0, 640, 512), 0, 128, 512, 512);
    const hoppie::UiWindowBounds outside{1500, 640, 2140, 128};
    CHECK(normal.scissor(outside, 0, 0, 640, 512).width == 0);
    CHECK(normal.scissor(window, 20, 20, 10, 30).width == 0);
    auto minimized = normal;
    minimized.viewport[2] = 0;
    CHECK(minimized.scissor(window, 0, 0, 640, 512).width == 0);
    hoppie::UiDrawTransform invalid;
    CHECK(!invalid.project({0, 0}));
    invalid = normal;
    invalid.modelview[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!invalid.project({0, 0}));

    // Fractional clip edges round outward, retaining edge pixels at 150%.
    const auto fractional = ortho(0, 0, 1024, 1024, {0, 0, 1536, 1536});
    checkScissor(fractional.scissor(window, 0.25, 0.25, 10.25, 10.25), 480, 944, 16, 16);
    if (failures) return EXIT_FAILURE;
    std::cout << "UI geometry: DPI, mouse/render alignment, viewport offsets, pop-outs and clipping passed\n";
    return EXIT_SUCCESS;
}
