#pragma once

#include "ui/canvas.hpp"
#include "ui/keymap.hpp"
#include "ui/ui_context.hpp"
#include "ui/views.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;

namespace groovebox {

struct TabSlot {
    std::string key_label;              // "[L1]", "[L2]", "[R2]", "[R1]", "[G]"
    std::vector<ViewId> views;          // вікна, які циклічно відкриває цей слот (наприклад SEQ/SYNTH для L1)
    int width_px = 64;
};

class UiShell {
public:
    static constexpr int kTabBarH = 16;
    static constexpr int kHudH = 32;
    static constexpr int kWorkspaceH = kHighResHeight - kTabBarH - kHudH; // 240 - 16 - 32 = 192 px

    UiShell();
    UiShell(SDL_Renderer* custom_renderer, int w, int h);
    ~UiShell();

    bool init();
    void shutdown();
    void applyLogicalResolution(bool high_res);

    void registerView(std::unique_ptr<IView> view);
    IView& currentView(const UiContext& ctx);
    IView* findView(ViewId id);

    void setTabSlots(std::vector<TabSlot> slots);
    const std::vector<TabSlot>& tabSlots() const { return tab_slots_; }

    InputRouter& router() { return router_; }
    const InputRouter& router() const { return router_; }

    void handleInput(UiContext& ctx);

    void render(
        AppModel& app,
        AudioEngine& audio,
        HardwareMonitor* hw_mon,
        const InputState& in,
        const HardwareSnapshot& hw,
        const AudioEngineTelemetry& tel,
        const MidiTelemetry& midi,
        uint32_t now_ms
    );

private:
    void renderTabBar(Canvas& cv, const UiContext& ctx);
    void renderHud(Canvas& cv, const UiContext& ctx);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    int logical_w_ = kHighResWidth;
    int logical_h_ = kHighResHeight;

    InputRouter router_;
    std::array<std::unique_ptr<IView>, kViewCount> views_{};
    std::vector<TabSlot> tab_slots_;
};

// Фабрична функція, яка конфігурує всі View та TabSlots у layout.cpp
void setupAppLayout(UiShell& shell);

} // namespace groovebox
