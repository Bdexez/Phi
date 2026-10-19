#include <bdex/ui/Ui.hpp>

#include <algorithm>
#include <format>
#include <functional>

namespace bdex::ui {

namespace {

const Vec4 kBackground(0.05f, 0.06f, 0.08f, 0.85f);
const Vec4 kTitle(0.16f, 0.3f, 0.55f, 0.95f);
const Vec4 kWidget(0.18f, 0.2f, 0.25f, 1.0f);
const Vec4 kHover(0.26f, 0.29f, 0.36f, 1.0f);
const Vec4 kAccent(0.35f, 0.58f, 0.95f, 1.0f);
const Vec4 kText(0.9f, 0.95f, 1.0f, 1.0f);

}  // namespace

Input fromInputState(const platform::InputState& input) {
    return {input.mousePosition(), input.mouseButtonDown(platform::MouseButton::Left),
            input.mouseButtonPressed(platform::MouseButton::Left),
            input.mouseButtonReleased(platform::MouseButton::Left)};
}

void Context::begin(const Input& input, render::DebugDraw& draw) {
    input_ = input;
    draw_ = &draw;
    lastPanels_ = std::move(panels_);
    panels_.clear();
}

void Context::end() {
    if (!input_.down) {
        active_ = 0;  // nothing is held any more
    }
    draw_ = nullptr;
}

u64 Context::idOf(std::string_view text) const {
    const u64 a = std::hash<std::string_view>{}(panel_);
    const u64 b = std::hash<std::string_view>{}(text);
    return (a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2))) | 1ull;  // never 0
}

void Context::text(const Vec2& at, std::string_view content, const Vec4& color) {
    draw_->screenText(at, content, kTextHeight, color);
}

void Context::panel(std::string_view title, const Vec2& at, f32 width) {
    panel_ = std::string(title);
    panelAt_ = at;
    panelWidth_ = width;
    // The background goes first, so widgets land on it: as tall as the
    // panel was last frame.
    if (const auto known = heights_.find(panel_); known != heights_.end() && draw_ != nullptr) {
        draw_->screenRect(at, at + Vec2(width, known->second), kBackground);
    }
    if (draw_ != nullptr) {
        draw_->screenRect(at, at + Vec2(width, kRowHeight), kTitle);
        text(at + Vec2(kPadding, (kRowHeight - kTextHeight) * 0.5f), title, kText);
    }
    cursor_ = at.y + kRowHeight + kPadding;
}

void Context::endPanel() {
    const f32 height = cursor_ - panelAt_.y;
    heights_[panel_] = height;
    panels_.push_back({panelAt_, panelAt_ + Vec2(panelWidth_, height)});
    panel_.clear();
}

Context::Rect Context::row() {
    const Rect rect{Vec2(panelAt_.x + kPadding, cursor_), Vec2(panelAt_.x + panelWidth_ - kPadding, cursor_ + kRowHeight)};
    cursor_ += kRowHeight + kPadding;
    return rect;
}

void Context::label(std::string_view content) {
    const Rect rect = row();
    if (draw_ != nullptr) {
        text(rect.min + Vec2(0.0f, (kRowHeight - kTextHeight) * 0.5f), content, kText);
    }
}

bool Context::button(std::string_view content) {
    const Rect rect = row();
    const u64 id = idOf(content);
    const bool over = rect.contains(input_.pointer);
    if (over && input_.pressed) {
        active_ = id;
    }
    const bool clicked = active_ == id && over && input_.released;
    if (draw_ != nullptr) {
        draw_->screenRect(rect.min, rect.max, active_ == id && over ? kAccent : over ? kHover : kWidget);
        const f32 width = render::DebugDraw::textWidth(content, kTextHeight);
        text(Vec2((rect.min.x + rect.max.x - width) * 0.5f, rect.min.y + (kRowHeight - kTextHeight) * 0.5f), content,
             kText);
    }
    return clicked;
}

bool Context::checkbox(std::string_view content, bool& value) {
    const Rect rect = row();
    const u64 id = idOf(content);
    const bool over = rect.contains(input_.pointer);
    if (over && input_.pressed) {
        active_ = id;
    }
    const bool clicked = active_ == id && over && input_.released;
    if (clicked) {
        value = !value;
    }
    if (draw_ != nullptr) {
        const Vec2 box = rect.min + Vec2(0.0f, 3.0f);
        const f32 side = kRowHeight - 6.0f;
        draw_->screenRect(box, box + Vec2(side), over ? kHover : kWidget);
        if (value) {
            draw_->screenRect(box + Vec2(3.0f), box + Vec2(side - 3.0f), kAccent);
        }
        text(Vec2(box.x + side + kPadding, rect.min.y + (kRowHeight - kTextHeight) * 0.5f), content, kText);
    }
    return clicked;
}

bool Context::slider(std::string_view content, f32& value, f32 min, f32 max) {
    const Rect rect = row();
    const u64 id = idOf(content);
    const bool over = rect.contains(input_.pointer);
    if (over && input_.pressed) {
        active_ = id;
    }
    bool changed = false;
    if (active_ == id && input_.down && max > min) {
        // Dragging: the pointer's place along the track, wherever it is.
        const f32 along = std::clamp((input_.pointer.x - rect.min.x) / (rect.max.x - rect.min.x), 0.0f, 1.0f);
        const f32 next = min + along * (max - min);
        changed = next != value;
        value = next;
    }
    if (draw_ != nullptr) {
        const f32 fraction = max > min ? std::clamp((value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
        draw_->screenRect(rect.min, rect.max, over || active_ == id ? kHover : kWidget);
        draw_->screenRect(rect.min, Vec2(rect.min.x + (rect.max.x - rect.min.x) * fraction, rect.max.y),
                          Vec4(kAccent.r, kAccent.g, kAccent.b, 0.6f));
        text(rect.min + Vec2(kPadding, (kRowHeight - kTextHeight) * 0.5f), std::format("{} {:.2f}", content, value),
             kText);
    }
    return changed;
}

bool Context::wantsPointer() const {
    if (active_ != 0) {
        return true;
    }
    const auto over = [&](const Rect& rect) { return rect.contains(input_.pointer); };
    return std::any_of(panels_.begin(), panels_.end(), over) || std::any_of(lastPanels_.begin(), lastPanels_.end(), over);
}

}  // namespace bdex::ui
