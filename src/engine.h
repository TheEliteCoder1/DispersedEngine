#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <json.hpp>
#include <fstream>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <array>
#include <tuple>
#include <stdio.h>

// ============================================================
// Platform Detection and Path Helpers
// ============================================================
#ifdef __EMSCRIPTEN__
#define PLATFORM_EMSCRIPTEN 1
#ifdef PROJECT_SCRIPT_BUILD
    // Project scripts on web: mounted at /FirstProject/
    const std::string PATH_PREFIX_PROJECTS = "/";
    const std::string PATH_PREFIX_ASSETS = "/";
    const std::string PATH_PREFIX_ENGINE = "/";
#else
    // Editor on web: projects mounted at /projects/, assets at /assets/
    const std::string PATH_PREFIX_PROJECTS = "/projects/";
    const std::string PATH_PREFIX_ASSETS = "/assets/";
    const std::string PATH_PREFIX_ENGINE = "/";
#endif

#elif defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS 1
#ifdef PROJECT_SCRIPT_BUILD
    // Project executable has the folder copied next to it, so use ./
    const std::string PATH_PREFIX_PROJECTS = "./";
    const std::string PATH_PREFIX_ASSETS = "./";
    const std::string PATH_PREFIX_ENGINE = "./";
#else
    // Editor needs to reach the source 'projects/' folder from build_windows/src/Release/
    const std::string PATH_PREFIX_PROJECTS = "../../../projects/";
    const std::string PATH_PREFIX_ASSETS = "../../../src/assets/";
    const std::string PATH_PREFIX_ENGINE = "../../../";
#endif

#elif defined(__linux__)
#define PLATFORM_LINUX 1
#ifdef PROJECT_SCRIPT_BUILD
    const std::string PATH_PREFIX_PROJECTS = "./";
    const std::string PATH_PREFIX_ASSETS = "./";
    const std::string PATH_PREFIX_ENGINE = "./";
#else
    const std::string PATH_PREFIX_PROJECTS = "./projects/"; // Adjust if your Linux build dir is different
    const std::string PATH_PREFIX_ASSETS = "./assets/";
    const std::string PATH_PREFIX_ENGINE = "./";
#endif

#elif defined(__APPLE__)
#define PLATFORM_MACOS 1
#ifdef PROJECT_SCRIPT_BUILD
    const std::string PATH_PREFIX_PROJECTS = "./";
    const std::string PATH_PREFIX_ASSETS = "./";
    const std::string PATH_PREFIX_ENGINE = "./";
#else
    const std::string PATH_PREFIX_PROJECTS = "./projects/"; // Adjust if your Mac build dir is different
    const std::string PATH_PREFIX_ASSETS = "./assets/";
    const std::string PATH_PREFIX_ENGINE = "./";
#endif

#else
#error "Unsupported platform"
#endif

// Update the helper functions to handle Emscripten's virtual FS better:
inline std::string getProjectsPath(const std::string& relativePath = "") {
#ifdef EMSCRIPTEN
    return PATH_PREFIX_PROJECTS + relativePath;
#else
    return PATH_PREFIX_PROJECTS + relativePath;
#endif
}

inline const std::filesystem::path getProjectsRootForScripts() {
#ifdef __EMSCRIPTEN__
    // On Emscripten, project scripts use root paths
    return std::filesystem::path("/");
#else
    return std::filesystem::path(PATH_PREFIX_PROJECTS);
#endif
}

inline std::string getAssetsPath(const std::string& relativePath = "") {
#ifdef EMSCRIPTEN
    // On Emscripten, assets are preloaded to /assets/
    if (!relativePath.empty() && relativePath[0] == '/') {
        return "/assets/" + relativePath.substr(1);
    }
    return "/assets/" + relativePath;
#else
    return PATH_PREFIX_ASSETS + relativePath;
#endif
}

inline std::string getEnginePath(const std::string& relativePath = "") {
#ifdef EMSCRIPTEN
    if (!relativePath.empty() && relativePath[0] == '/') {
        return "/" + relativePath.substr(1);
    }
    return "/" + relativePath;
#else
    return PATH_PREFIX_ENGINE + relativePath;
#endif
}

inline std::filesystem::path getProjectsPathFS(const std::string& relativePath = "") {
    return std::filesystem::path(getProjectsPath(relativePath));
}

using Entity = uint32_t;
const size_t MAX_ENTITIES = 1000000;
Entity lastSelectedEntity = (Entity)-1;

enum EditMode {
    Select = 0,
    MoveWithMouse,
    Delete,
    Dialog
};

EditMode currentEditMode = EditMode::Select;
// Remembers the mode that was active before a Dialog was opened, so it can be
// restored once the dialog closes (otherwise the editor gets stuck in Dialog
// mode forever, and edit_object_with_editor_mouse/gamepad both bail out early
// whenever currentEditMode == Dialog, silently disabling select/move/delete).
EditMode modeBeforeDialog = EditMode::Select;

enum SelectionMode {
    SingleSelect = 0,
    MultiSelect
};
SelectionMode currentSelectionmode = SelectionMode::SingleSelect;

// Editor scroll offsets and viewport (updated each frame in main.cpp)
float editorScrollX = 0.0f;
float editorScrollY = 0.0f;
bool inspectorVisible = true;

float canvasViewX = 105.0f;
float canvasViewY = 105.0f;
float canvasViewW = 1390.0f;
float canvasViewH = 690.0f;

inline bool isInsideCanvas(float x, float y) {
    return x >= canvasViewX && x <= canvasViewX + canvasViewW &&
           y >= canvasViewY && y <= canvasViewY + canvasViewH;
}

bool isDraggingLeftMouse = false;
int lastDragX = 0;
int lastDragY = 0;

bool isGamepadDragging = false;
bool gamepadDidDrag = false;
float lastGamepadCursorX = 0.0f;
float lastGamepadCursorY = 0.0f;

namespace Components {
    struct Metadata { std::string name; };
    struct Position { float x = 0.0f, y = 0.0f; };
    struct RectangleShape { float w = 50.0f, h = 50.0f; };
    struct ZIndex { int z = 0; };
    struct Selection {
        bool isSelected = false;
        SDL_Color selectionColor = {0, 255, 0, 255};
    };
    struct Velocity { float x = 0.0f, y = 0.0f; };
    struct Acceleration { float x = 0.0f, y = 0.0f; };
}

const float LOGICAL_CANVAS_WIDTH  = 1390.0f;
const float LOGICAL_CANVAS_HEIGHT = 690.0f;

inline void clamp_entity_position_to_canvas(Components::Position& pos, float entityWidth = 50.0f, float entityHeight = 50.0f) {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = LOGICAL_CANVAS_WIDTH - entityWidth;
    float maxY = LOGICAL_CANVAS_HEIGHT - entityHeight;
    // If the entity is larger than the canvas on an axis, maxX/maxY can end up
    // below minX/minY. std::clamp requires lo <= hi (UB otherwise), and in
    // practice that collapses the result to a fixed value every call -
    // permanently freezing that axis regardless of movement. Skip clamping on
    // an axis that can't fit rather than pinning it to a bogus position.
    if (maxX >= minX) pos.x = std::clamp(pos.x, minX, maxX);
    if (maxY >= minY) pos.y = std::clamp(pos.y, minY, maxY);
}

inline void clamp_guiElem_position_to_canvas(SDL_FPoint& pos, float guiElemWidth, float guiElemHeight) {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = LOGICAL_CANVAS_WIDTH - guiElemWidth;
    float maxY = LOGICAL_CANVAS_HEIGHT - guiElemHeight;
    // Same fix as clamp_entity_position_to_canvas: don't clamp an axis the
    // element can't possibly fit on (e.g. a Panel taller than the canvas),
    // or std::clamp's undefined behavior with lo > hi will freeze that axis
    // at a single fixed value forever.
    if (maxX >= minX) pos.x = std::clamp(pos.x, minX, maxX);
    if (maxY >= minY) pos.y = std::clamp(pos.y, minY, maxY);
}

struct ECSWorld {
    std::vector<uint8_t> has_metadata;
    std::vector<Components::Metadata> metadata_pool;
    std::vector<uint8_t> has_position;
    std::vector<Components::Position> position_pool;
    std::vector<uint8_t> has_rectangle_shape;
    std::vector<Components::RectangleShape> rectangle_shape_pool;
    std::vector<uint8_t> has_z_index;
    std::vector<Components::ZIndex> z_index_pool;
    std::vector<uint8_t> has_selection;
    std::vector<Components::Selection> selection_pool;
    std::vector<uint8_t> has_velocity;
    std::vector<Components::Velocity> velocity_pool;
    std::vector<uint8_t> has_acceleration;
    std::vector<Components::Acceleration> acceleration_pool;
    size_t entity_count = 0;

    ECSWorld() {
        has_metadata.reserve(MAX_ENTITIES);
        metadata_pool.reserve(MAX_ENTITIES);
        has_position.reserve(MAX_ENTITIES);
        position_pool.reserve(MAX_ENTITIES);
        has_rectangle_shape.reserve(MAX_ENTITIES);
        rectangle_shape_pool.reserve(MAX_ENTITIES);
        has_z_index.reserve(MAX_ENTITIES);
        z_index_pool.reserve(MAX_ENTITIES);
        has_selection.reserve(MAX_ENTITIES);
        selection_pool.reserve(MAX_ENTITIES);
        has_velocity.reserve(MAX_ENTITIES);
        velocity_pool.reserve(MAX_ENTITIES);
        has_acceleration.reserve(MAX_ENTITIES);
        acceleration_pool.reserve(MAX_ENTITIES);
    }

    Entity create_entity() {
        Entity id = entity_count++;
        has_metadata.push_back(0);
        metadata_pool.emplace_back();
        has_position.push_back(0);
        position_pool.emplace_back();
        has_rectangle_shape.push_back(0);
        rectangle_shape_pool.emplace_back();
        has_z_index.push_back(0);
        z_index_pool.emplace_back();
        has_selection.push_back(0);
        selection_pool.emplace_back();
        has_velocity.push_back(0);
        velocity_pool.emplace_back();
        has_acceleration.push_back(0);
        acceleration_pool.emplace_back();
        return id;
    }

    void delete_entity(Entity id) {
        if (id >= entity_count) return;
        Entity last = entity_count - 1;
        if (id != last) {
            has_metadata[id] = has_metadata[last];
            metadata_pool[id] = metadata_pool[last];
            has_position[id] = has_position[last];
            position_pool[id] = position_pool[last];
            has_rectangle_shape[id] = has_rectangle_shape[last];
            rectangle_shape_pool[id] = rectangle_shape_pool[last];
            has_z_index[id] = has_z_index[last];
            z_index_pool[id] = z_index_pool[last];
            has_selection[id] = has_selection[last];
            selection_pool[id] = selection_pool[last];
            has_velocity[id] = has_velocity[last];
            velocity_pool[id] = velocity_pool[last];
            has_acceleration[id] = has_acceleration[last];
            acceleration_pool[id] = acceleration_pool[last];
        }
        has_metadata.pop_back();
        metadata_pool.pop_back();
        has_position.pop_back();
        position_pool.pop_back();
        has_rectangle_shape.pop_back();
        rectangle_shape_pool.pop_back();
        has_z_index.pop_back();
        z_index_pool.pop_back();
        has_selection.pop_back();
        selection_pool.pop_back();
        has_velocity.pop_back();
        velocity_pool.pop_back();
        has_acceleration.pop_back();
        acceleration_pool.pop_back();
        entity_count--;
    }

    void add_metadata(Entity id) { if (id < entity_count) has_metadata[id] = 1; }
    void add_position(Entity id) { if (id < entity_count) has_position[id] = 1; }
    void add_rectangle_shape(Entity id) { if (id < entity_count) has_rectangle_shape[id] = 1; }
    void add_z_index(Entity id) { if (id < entity_count) has_z_index[id] = 1; }
    void add_selection(Entity id) { if (id < entity_count) has_selection[id] = 1; }
    void add_velocity(Entity id) { if (id < entity_count) has_velocity[id] = 1; }
    void add_acceleration(Entity id) { if (id < entity_count) has_acceleration[id] = 1; }
};

enum DialogState { Closed = 0, Opening, Opened, Closing };

namespace Gui {
    class IGuiElement {
    public:
        virtual ~IGuiElement() = default;
        
        // Serialization & Geometry
        virtual std::string getType() const = 0;
        virtual float getX() const = 0;
        virtual float getY() const = 0;
        virtual float getWidth() const = 0;
        virtual float getHeight() const = 0;
        virtual void setPos(SDL_Point p) = 0;
        virtual void setRect(SDL_FRect r) = 0;


        // Editor selection
        bool editorSelected = false;

        // Unified Lifecycle (Signatures must match exactly)
        virtual void render(float offsetX = 0.0f, float offsetY = 0.0f) = 0;
        virtual bool handleEvent(const SDL_Event& e, SDL_Window* window, float offsetX = 0.0f, float offsetY = 0.0f) = 0;
        virtual void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) = 0;

        // Draw a selection highlight around this element (called by editor)
        void renderSelectionOutline(SDL_Renderer* renderer, float offsetX = 0.0f, float offsetY = 0.0f) const {
            if (!editorSelected) return;
            SDL_FRect r = { getX() - offsetX - 2, getY() - offsetY - 2, getWidth() + 4, getHeight() + 4 };
            SDL_SetRenderDrawColor(renderer, 255, 200, 0, 255);
            SDL_RenderRect(renderer, &r);
            // corner handles
            const float hs = 6.0f;
            SDL_FRect corners[4] = {
                {r.x - hs*0.5f,           r.y - hs*0.5f,           hs, hs},
                {r.x + r.w - hs*0.5f,     r.y - hs*0.5f,           hs, hs},
                {r.x - hs*0.5f,           r.y + r.h - hs*0.5f,     hs, hs},
                {r.x + r.w - hs*0.5f,     r.y + r.h - hs*0.5f,     hs, hs},
            };
            for (auto& c : corners) SDL_RenderFillRect(renderer, &c);
        }
    };
    struct ITextInput {
        virtual void appendText(const std::string& str) = 0;
        virtual void removeLastChar() = 0;
        virtual bool isActive() const = 0;
        virtual void setActive(bool a) = 0;
        virtual const SDL_FRect& getRect() const = 0;
        virtual ~ITextInput() = default;
        virtual bool isNumericOnly() const { return false; }
    };

    // ----------------------------------------------------------------
    // Scrollbar — supports both vertical and horizontal orientation
    // ----------------------------------------------------------------
    enum class ScrollOrientation { Vertical, Horizontal };

    class Scrollbar {
    public:
        float trackX = 0, trackY = 0, trackW = 10, trackH = 100;
        float contentSize = 0;   // total scrollable content size (width or height)
        float viewSize    = 0;   // visible area size
        float offset      = 0;   // current scroll offset
        ScrollOrientation orientation = ScrollOrientation::Vertical;
        std::function<void(float)> onChange;

        void setOrientation(ScrollOrientation o) { orientation = o; }

        void setOffsetNoCallback(float v) {
            offset = std::clamp(v, 0.0f, maxOffset());
        }

        void setGeometry(float x, float y, float w, float h,
                        float contentSz, float viewSz) {
            trackX = x; trackY = y; trackW = w; trackH = h;
            contentSize = contentSz; viewSize = viewSz;
            offset = std::clamp(offset, 0.0f, maxOffset());
        }

        float maxOffset() const {
            return std::max(0.0f, contentSize - viewSize);
        }

        bool handleEvent(const SDL_Event& ev) {
            if (maxOffset() <= 0) return false;
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect thumb = thumbRect();
                if (inRect(mx, my, thumb)) {
                    dragging = true;
                    dragStart = (orientation == ScrollOrientation::Vertical) ? my : mx;
                    dragStartOffset = offset;
                    return true;
                }
            }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP)   { dragging = false; }
            if (ev.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                float d = (orientation == ScrollOrientation::Vertical)
                        ? ev.motion.y - dragStart
                        : ev.motion.x - dragStart;
                float trackSize = (orientation == ScrollOrientation::Vertical)
                                ? trackH - thumbSize()
                                : trackW - thumbSize();
                float ratio = trackSize > 0 ? d / trackSize : 0;
                setOffset(dragStartOffset + ratio * maxOffset());
                return true;
            }
            if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                float mx, my; SDL_GetMouseState(&mx, &my);
                SDL_FRect track = { trackX, trackY, trackW, trackH };
                if (!inRect(mx, my, track)) return false;
                float delta = (orientation == ScrollOrientation::Vertical)
                            ? (ev.wheel.y > 0 ? -40.f : 40.f)
                            : (ev.wheel.x > 0 ? -40.f : 40.f);
                setOffset(offset + delta);
                return true;
            }
            return false;
        }

        void render(SDL_Renderer* renderer, float offsetX = 0.0f, float offsetY = 0.0f) {
            SDL_FRect track = { trackX - offsetX, trackY - offsetY, trackW, trackH };
            // Track background – slightly lighter so it stands out
            SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
            SDL_RenderFillRect(renderer, &track);
            // Track border – a thin line for visibility
            SDL_SetRenderDrawColor(renderer, 120, 120, 140, 255);
            SDL_RenderRect(renderer, &track);

            if (maxOffset() <= 0) return;
            SDL_FRect thumb = thumbRect();
            thumb.x -= offsetX;
            thumb.y -= offsetY;
            // Thumb – bright and with a slight border
            SDL_SetRenderDrawColor(renderer, 180, 180, 200, 255);
            SDL_RenderFillRect(renderer, &thumb);
            SDL_SetRenderDrawColor(renderer, 220, 220, 240, 255);
            SDL_RenderRect(renderer, &thumb);
        }

            
        void setOffset(float v) {
            offset = std::clamp(v, 0.0f, maxOffset());
            if (onChange) onChange(offset);
        }

    private:
        bool  dragging = false;
        float dragStart = 0, dragStartOffset = 0;

        float thumbSize() const {
            if (contentSize <= 0) return (orientation == ScrollOrientation::Vertical) ? trackH : trackW;
            float trackSize = (orientation == ScrollOrientation::Vertical) ? trackH : trackW;
            return std::max(20.0f, trackSize * (viewSize / contentSize));
        }

        SDL_FRect thumbRect() const {
            float ratio = (maxOffset() > 0) ? offset / maxOffset() : 0;
            float trackSize = (orientation == ScrollOrientation::Vertical) ? trackH : trackW;
            float pos = ratio * (trackSize - thumbSize());
            if (orientation == ScrollOrientation::Vertical)
                return { trackX, trackY + pos, trackW, thumbSize() };
            else
                return { trackX + pos, trackY, thumbSize(), trackH };
        }

        static bool inRect(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
        }
    };

    class SpinBox : public ITextInput, public IGuiElement {
    public:
        SpinBox(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                SDL_FRect rect, float minVal = 1.0f, float maxVal = 9999.0f,
                float initial = 50.0f, float step = 0.5f)
            : renderer(renderer), textEngine(textEngine), font(font),
              rect(rect), minVal(minVal), maxVal(maxVal), value(initial), step(step) {
            value = snapToDecimal(value);
            rebuildDisplayStr();
        }

        std::string getType() const override { return "SpinBox"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override { rect = r; }
        void setPos(SDL_Point p) override {rect.x = p.x; rect.y = p.y;};
        float getMin() const { return minVal; }
        float getMax() const { return maxVal; }
        float getStep() const { return step; }
        void setMin(float v)  { minVal = v; value = std::clamp(value, minVal, maxVal); rebuildDisplayStr(); }
        void setMax(float v)  { maxVal = v; value = std::clamp(value, minVal, maxVal); rebuildDisplayStr(); }
        void setStep(float v) { step = v; }
        void setValue(float v){ value = snapToDecimal(std::clamp(v, minVal, maxVal)); rebuildDisplayStr(); }

        SpinBox(const SpinBox&) = delete;
        SpinBox& operator=(const SpinBox&) = delete;
        SpinBox(SpinBox&&) = default;
        SpinBox& operator=(SpinBox&&) = default;
        ~SpinBox() = default;

        void appendText(const std::string& str) override {
            for (char c : str) {
                if (std::isdigit(c)) editBuf += c;
                else if (c == '.' && editBuf.find('.') == std::string::npos) editBuf += c;
            }
        }
        void removeLastChar() override { if (!editBuf.empty()) editBuf.pop_back(); }
        bool isActive() const override { return active; }
        void setActive(bool a) override { active = a; }
        const SDL_FRect& getRect() const override { return rect; }
        bool isNumericOnly() const override { return true; }

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX;
            rect.y -= offsetY;
            auto restore = [&]() { rect = originalRect; };

            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                if (inRect(mx, my, decBtn())) { restore(); adjust(-step); return true; }
                if (inRect(mx, my, incBtn())) { restore(); adjust(+step); return true; }
                SDL_FRect textArea = fieldTextArea();
                if (inRect(mx, my, textArea)) { if (!active) { active = true; SDL_StartTextInput(window); } restore(); return true; }
                if (active) commitEdit(window);
                restore(); return false;
            }
            if (!active) { restore(); return false; }
            if (ev.type == SDL_EVENT_TEXT_INPUT) {
                for (char c : std::string(ev.text.text)) {
                    if (std::isdigit(c)) editBuf += c;
                    else if (c == '.' && editBuf.find('.') == std::string::npos) editBuf += c;
                }
                restore(); return true;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                switch (ev.key.key) {
                    case SDLK_BACKSPACE: if (!editBuf.empty()) editBuf.pop_back(); restore(); return true;
                    case SDLK_RETURN: commitEdit(window); restore(); return true;
                    case SDLK_ESCAPE: cancelEdit(window); restore(); return true;
                    default: break;
                }
                restore(); return false;
            }
            restore(); return false;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect dec = decBtn(); dec.x -= offsetX; dec.y -= offsetY;
            SDL_FRect inc = incBtn(); inc.x -= offsetX; inc.y -= offsetY;
            SDL_FRect ta = fieldTextArea(); ta.x -= offsetX; ta.y -= offsetY;
            bool inDec = inRect(cursorX, cursorY, dec);
            bool inInc = inRect(cursorX, cursorY, inc);
            bool inTa = inRect(cursorX, cursorY, ta);
            if (confirmDown && !confirmDownLastFrame) {
                if (inDec) adjust(-step);
                else if (inInc) adjust(+step);
                else if (inTa) {
                    if (!active) { active = true; }
                } else {
                    if (active) commitEdit(window);
                }
            }
        }

        void render(float offsetX, float offsetY) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX; // ADD THIS
            rect.y -= offsetY;
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            SDL_FRect dec = decBtn(), inc = incBtn();
            auto btnCol = [&](SDL_FRect b) -> SDL_Color {
                return inRect(mx, my, b) ? SDL_Color{80,80,80,255} : SDL_Color{60,60,60,255};
            };
            SDL_SetRenderDrawColor(renderer, btnCol(dec).r, btnCol(dec).g, btnCol(dec).b, 255);
            SDL_RenderFillRect(renderer, &dec);
            SDL_SetRenderDrawColor(renderer, btnCol(inc).r, btnCol(inc).g, btnCol(inc).b, 255);
            SDL_RenderFillRect(renderer, &inc);
            drawCentredText("-", dec, {220,220,220,255});
            drawCentredText("+", inc, {220,220,220,255});
            SDL_FRect ta = fieldTextArea();
            SDL_Color bg = active ? SDL_Color{255,255,255,255} : SDL_Color{245,245,245,255};
            SDL_Color brd = active ? SDL_Color{52,110,235,255} : SDL_Color{130,130,130,255};
            SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
            SDL_RenderFillRect(renderer, &ta);
            SDL_SetRenderDrawColor(renderer, brd.r, brd.g, brd.b, 255);
            SDL_RenderRect(renderer, &ta);
            std::string display = active ? editBuf : displayStr;
            if (active) {
                Uint64 ticks = SDL_GetTicks();
                if ((ticks / 500) % 2 == 0) display += "|";
            }
            SDL_Color tc = active ? SDL_Color{20,20,20,255} : SDL_Color{30,30,30,255};
            TTF_Text* t = TTF_CreateText(textEngine, font, display.c_str(), 0);
            if (t) {
                TTF_SetTextColor(t, tc.r, tc.g, tc.b, tc.a);
                TTF_DrawRendererText(t, ta.x + 6.0f, ta.y + (ta.h - 20.0f) * 0.5f);
                TTF_DestroyText(t);
            }
            rect = originalRect;
        }

        float getValue() const { return value; }
        void deactivate(SDL_Window* window) { if (active) commitEdit(window); }

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        float minVal, maxVal, value, step;
        bool active = false;
        std::string editBuf, displayStr;

        static float snapToDecimal(float v) { return std::round(v * 10.0f) / 10.0f; }
        void rebuildDisplayStr() {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f", value);
            displayStr = buf;
        }
        void adjust(float delta) {
            value = snapToDecimal(std::clamp(value + delta, minVal, maxVal));
            rebuildDisplayStr();
        }
        void commitEdit(SDL_Window* window) {
            if (!editBuf.empty()) {
                try {
                    float parsed = snapToDecimal(std::stof(editBuf));
                    value = std::clamp(parsed, minVal, maxVal);
                } catch (...) {}
            }
            rebuildDisplayStr();
            editBuf.clear();
            active = false;
            SDL_StopTextInput(window);
        }
        void cancelEdit(SDL_Window* window) {
            editBuf.clear();
            active = false;
            SDL_StopTextInput(window);
        }

        
        SDL_FRect decBtn() const { return { rect.x, rect.y, rect.h, rect.h }; }
        SDL_FRect incBtn() const { return { rect.x + rect.w - rect.h, rect.y, rect.h, rect.h }; }
        SDL_FRect fieldTextArea() const {
            float bw = rect.h;
            return { rect.x + bw, rect.y, rect.w - bw * 2.0f, rect.h };
        }
        static bool inRect(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
        }
        void drawCentredText(const char* str, SDL_FRect r, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, str, 0);
            if (!t) return;
            TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
            TTF_DrawRendererText(t, r.x + r.w * 0.35f, r.y + (r.h - 20.0f) * 0.5f);
            TTF_DestroyText(t);
        }
    };
    
    class OptionBox : public IGuiElement {
    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect optionRect;
        const std::vector<std::string>& options;
        int currentOption = 0;

    public:
        OptionBox(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font, 
                SDL_FRect rect, const std::vector<std::string>& options) 
                : renderer(renderer), textEngine(textEngine), font(font), optionRect(rect),
                    options(options) 
        {
        }

        std::string getType() const override { return "OptionBox"; }
        float getX() const override { return optionRect.x; }
        float getY() const override { return optionRect.y; }
        float getWidth() const override { return optionRect.w; }
        float getHeight() const override { return optionRect.h; }
        void setRect(SDL_FRect r) override { optionRect = r; }
        void setPos(SDL_Point p) override { optionRect.x = p.x; optionRect.y = p.y; }

        

        SDL_FRect decBtn() const { return { optionRect.x, optionRect.y, optionRect.h, optionRect.h }; }
        SDL_FRect incBtn() const { return { optionRect.x + optionRect.w - optionRect.h, optionRect.y, optionRect.h, optionRect.h }; }
        SDL_FRect fieldLabel() const {
            float bw = optionRect.h;
            return { optionRect.x + bw, optionRect.y, optionRect.w - bw * 2.0f, optionRect.h };
        }

        static bool inRect(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
        }

        void drawCentredText(const char* str, SDL_FRect r, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, str, 0);
            if (!t) return;
            TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
            TTF_DrawRendererText(t, r.x + r.w * 0.35f, r.y + (r.h - 20.0f) * 0.5f);
            TTF_DestroyText(t);
        }

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
            SDL_FRect originalRect = optionRect;
            originalRect.x -= offsetX;
            originalRect.y -= offsetY;
            auto restore = [&]() { optionRect = originalRect; };
            
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                
                if (inRect(mx, my, decBtn())) { 
                    restore(); 
                    if (currentOption > 0) {
                        currentOption -= 1; 
                    }
                    return true; 
                }
                if (inRect(mx, my, incBtn())) { 
                    restore();
                    // FIX 1: Prevent out-of-bounds. Valid indices are 0 to size()-1
                    if (currentOption < (int)options.size() - 1) {
                        currentOption += 1;
                    }
                    // FIX 2: Must return true to consume the event and prevent fall-through!
                    return true; 
                }
            }
            restore(); 
            return false;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect dec = decBtn(); dec.y -= offsetY;
            SDL_FRect inc = incBtn(); inc.y -= offsetY;
            bool inDec = inRect(cursorX, cursorY, dec);
            bool inInc = inRect(cursorX, cursorY, inc);
            
            if (confirmDown && !confirmDownLastFrame) {
                if (inDec) {
                    if (currentOption > 0) {
                        currentOption -= 1; 
                    }
                }
                else if (inInc) {
                    // FIX 1 applied here too
                    if (currentOption < (int)options.size() - 1) {
                        currentOption += 1;
                    }
                }
            }
        }

        void render(float offsetX, float offsetY) override {
            SDL_FRect originalRect = optionRect;
            optionRect.y -= offsetY;
            
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            
            SDL_FRect dec = decBtn(), inc = incBtn();
            auto btnCol = [&](SDL_FRect b) -> SDL_Color {
                return inRect(mx, my, b) ? SDL_Color{80,80,80,255} : SDL_Color{60,60,60,255};
            };
            
            SDL_SetRenderDrawColor(renderer, btnCol(dec).r, btnCol(dec).g, btnCol(dec).b, 255);
            SDL_RenderFillRect(renderer, &dec);
            SDL_SetRenderDrawColor(renderer, btnCol(inc).r, btnCol(inc).g, btnCol(inc).b, 255);
            SDL_RenderFillRect(renderer, &inc);
            
            drawCentredText("-", dec, {220,220,220,255});
            drawCentredText("+", inc, {220,220,220,255});
            
            SDL_FRect lbl = fieldLabel();
            SDL_Color bg = SDL_Color{255,255,255,255};
            SDL_Color brd = SDL_Color{130,130,130,255};
            
            // FIX 3: Actually draw the background and border for the label area!
            SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderFillRect(renderer, &lbl);
            SDL_SetRenderDrawColor(renderer, brd.r, brd.g, brd.b, brd.a);
            SDL_RenderRect(renderer, &lbl);
            
            SDL_Color tc = SDL_Color{20,20,20,255};
            
            // FIX 4: Handle empty options safely to prevent crashes
            std::string displayText = options.empty() ? "[Empty]" : options[currentOption];
            
            TTF_Text* t = TTF_CreateText(textEngine, font, displayText.c_str(), 0);
            if (t) {
                TTF_SetTextColor(t, tc.r, tc.g, tc.b, tc.a);
                TTF_DrawRendererText(t, lbl.x + 6.0f, lbl.y + (lbl.h - 20.0f) * 0.5f);
                TTF_DestroyText(t);
            }
            
            optionRect = originalRect;
        }

        // Returns the currently selected string safely
        std::string getCurrentOption() const { 
            return options.empty() ? "" : options[currentOption]; 
        }
        
        // Helper to get the raw index if needed
        int getCurrentIndex() const { return currentOption; }
    };

    class LineEdit : public ITextInput, public IGuiElement {
    public:
        LineEdit(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                 SDL_FRect rect, const std::string& placeholder = "Type here...")
            : renderer(renderer), textEngine(textEngine), font(font),
              rect(rect), placeholder(placeholder) {}

        void appendText(const std::string& str) override { text += str; }
        void removeLastChar() override { if (!text.empty()) text.pop_back(); }
        void setActive(bool a) override { active = a; }
        const SDL_FRect& getRect() const override { return rect; }

        std::string getType() const override { return "LineEdit"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override { rect = r; }
        void setPos(SDL_Point p) override {rect.x = p.x; rect.y = p.y;};
        const std::string& getPlaceholder() const { return placeholder; }
        void setPlaceholder(const std::string& p) { placeholder = p; }

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX, float offsetY) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX;
            rect.y -= offsetY;
            auto restore = [&]() { rect = originalRect; };

            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                bool hit = mx >= rect.x && mx <= rect.x + rect.w && my >= rect.y && my <= rect.y + rect.h;
                if (hit) { if (!active) { active = true; SDL_StartTextInput(window); } restore(); return true; } 
                else { if (active) { active = false; SDL_StopTextInput(window); } restore(); return false; }
            }
            if (!active) { restore(); return false; }
            if (ev.type == SDL_EVENT_TEXT_INPUT) { text += ev.text.text; restore(); return true; }
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_BACKSPACE && !text.empty()) { text.pop_back(); }
                bool ret = (ev.key.key != SDLK_RETURN && ev.key.key != SDLK_ESCAPE);
                restore(); return ret;
            }
            restore(); return false;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect r = rect; r.x -= offsetX; r.y -= offsetY; 
            bool hit = cursorX >= r.x && cursorX <= r.x + r.w && cursorY >= r.y && cursorY <= r.y + r.h;
            if (confirmDown && !confirmDownLastFrame) {
                if (hit) {
                    if (!active) { active = true; }
                } else {
                    if (active) active = false;
                }
            }
        }

        void render(float offsetX, float offsetY) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX;
            rect.y -= offsetY;
            
            SDL_Color bg = active ? SDL_Color{255,255,255,255} : SDL_Color{245,245,255,255};
            SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderFillRect(renderer, &rect);
            SDL_Color border = active ? SDL_Color{52,110,235,255} : SDL_Color{130,130,130,255};
            SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
            SDL_RenderRect(renderer, &rect);
            
            if (!font || !textEngine) {
                rect = originalRect;
                return;
            }
            
            bool showPlaceholder = text.empty() && !active;
            const std::string& display = showPlaceholder ? placeholder : text;
            SDL_Color col = showPlaceholder ? SDL_Color{150,150,150,255} : SDL_Color{20,20,20,255};
            
            std::string rendered = display;
            if (active) {
                Uint64 ticks = SDL_GetTicks();
                if ((ticks / 500) % 2 == 0) rendered += "|";
            }
            
            // Calculate text width
            int textWidth = 0, textHeight = 0;
            TTF_GetStringSize(font, rendered.c_str(), rendered.size(), &textWidth, &textHeight);
            
            float availableWidth = rect.w - 16.0f; // padding on both sides
            float textStartX = rect.x + 8.0f;
            float renderX = textStartX;
            
            // Horizontal scrolling logic
            if (textWidth > availableWidth && !rendered.empty()) {
                // Need to scroll - find the right starting position
                int visibleStart = 0;
                int accumulatedWidth = 0;
                
                // Measure characters to find where to start displaying
                for (size_t i = 0; i < rendered.size(); ++i) {
                    int charW = 0, charH = 0;
                    std::string ch = rendered.substr(i, 1);
                    TTF_GetStringSize(font, ch.c_str(), 1, &charW, &charH);
                    
                    if (i < rendered.size() - 1) { // Don't count cursor for scroll calculation
                        accumulatedWidth += charW;
                    }
                    
                    // If we've exceeded available width, start from here
                    if (accumulatedWidth > availableWidth) {
                        visibleStart = (int)i;
                        break;
                    }
                }
                
                // If text still doesn't fit, show the end of the text
                if (visibleStart == 0 && accumulatedWidth > availableWidth) {
                    // Work backwards to find what fits
                    visibleStart = 0;
                    accumulatedWidth = 0;
                    for (int i = (int)rendered.size() - 1; i >= 0; --i) {
                        int charW = 0, charH = 0;
                        std::string ch = rendered.substr(i, 1);
                        TTF_GetStringSize(font, ch.c_str(), 1, &charW, &charH);
                        accumulatedWidth += charW;
                        if (accumulatedWidth > availableWidth) {
                            visibleStart = i + 1;
                            break;
                        }
                    }
                }
                
                // Render visible portion
                if (visibleStart < (int)rendered.size()) {
                    std::string visibleText = rendered.substr(visibleStart);
                    TTF_Text* t = TTF_CreateText(textEngine, font, visibleText.c_str(), 0);
                    if (t) {
                        TTF_SetTextColor(t, col.r, col.g, col.b, col.a);
                        TTF_DrawRendererText(t, textStartX, rect.y + (rect.h - 20.0f) * 0.5f);
                        TTF_DestroyText(t);
                    }
                }
            } else {
                // Text fits - render normally
                TTF_Text* t = TTF_CreateText(textEngine, font, rendered.c_str(), 0);
                if (t) {
                    TTF_SetTextColor(t, col.r, col.g, col.b, col.a);
                    TTF_DrawRendererText(t, textStartX, rect.y + (rect.h - 20.0f) * 0.5f);
                    TTF_DestroyText(t);
                }
            }
            
            rect = originalRect;
        }

        void clear() { text.clear(); }
        void deactivate(SDL_Window* window) {
            active = false;
            SDL_StopTextInput(window);
        }
        const std::string& getText() const { return text; }
        bool isActive() const override { return active; }

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        std::string placeholder, text;
        bool active = false;
    };

    class VirtualKeyboard {
    private:
        struct VKey {
            std::string label, lower, upper;
            bool isShift = false, isBack = false, isSpace = false, isClose = false;
            SDL_FRect rect = {0,0,0,0};
            bool hovered = false;
            TTF_Text* textObj = nullptr;
            VKey() = default;
            ~VKey() { if (textObj) TTF_DestroyText(textObj); }
            VKey(const VKey&) = delete;
            VKey& operator=(const VKey&) = delete;
            VKey(VKey&& o) noexcept { *this = std::move(o); }
            VKey& operator=(VKey&& o) noexcept {
                if (this != &o) {
                    if (textObj) TTF_DestroyText(textObj);
                    label = std::move(o.label);
                    lower = std::move(o.lower);
                    upper = std::move(o.upper);
                    isShift = o.isShift; isBack = o.isBack; isSpace = o.isSpace; isClose = o.isClose;
                    rect = o.rect; hovered = o.hovered;
                    textObj = o.textObj;
                    o.textObj = nullptr;
                }
                return *this;
            }
        };

        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect = {0,0,0,0};
        std::vector<VKey> keys;
        bool isUpper = false;
        bool closeRequested = false;

    public:
        VirtualKeyboard(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font)
            : renderer(renderer), textEngine(textEngine), font(font) {}

        ~VirtualKeyboard() = default;

        void layoutKeys(SDL_FRect targetRect, float windowWidth, float windowHeight, bool numericOnly = false) {
            for (auto& k : keys) {
                if (k.textObj) {
                    TTF_DestroyText(k.textObj);
                    k.textObj = nullptr; // prevent double-free in destructor
                }
            }
            keys.clear();

            float kbY = targetRect.y + targetRect.h;
            rect = {0.0f, kbY, windowWidth, windowHeight - kbY};

            std::vector<std::vector<std::string>> labels;
            if (numericOnly) {
                labels = {
                    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
                    {".", "BACK", "CLOSE"}
                };
            } else {
                labels = {
                    {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
                    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
                    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
                    {"a", "s", "d", "f", "g", "h", "j", "k", "l"},
                    {"SHIFT", "z", "x", "c", "v", "b", "n", "m", "BACK"},
                    {"SPACE", "CLOSE"}
                };
            }

            float rowH = rect.h / labels.size();
            for (size_t r = 0; r < labels.size(); ++r) {
                float colW = rect.w / labels[r].size();
                for (size_t c = 0; c < labels[r].size(); ++c) {
                    VKey k;
                    k.label = labels[r][c];
                    k.lower = k.label;
                    k.upper = k.label;

                    if (k.label == "BACK") { k.isBack = true; }
                    else if (k.label == "CLOSE") { k.isClose = true; }
                    else if (k.label == "SPACE") { k.isSpace = true; k.lower = " "; k.upper = " "; }
                    else if (k.label == "SHIFT") { k.isShift = true; }
                    else if (k.label.length() == 1 && std::isalpha(k.label[0])) {
                        k.upper = std::string(1, std::toupper(k.label[0]));
                    }

                    k.rect = { c * colW, kbY + r * rowH, colW, rowH };
                    std::string disp = (isUpper && !k.isShift && !k.isBack && !k.isSpace && !k.isClose && !numericOnly)
                                       ? k.upper : k.label;
                    if (k.isShift && isUpper) disp = "SHIFT (ON)";
                    k.textObj = TTF_CreateText(textEngine, font, disp.c_str(), 0);
                    keys.push_back(std::move(k));
                }
            }
        }

        void updateLabels() {
            for (auto& k : keys) {
                if (k.textObj) {
                    std::string disp = (isUpper && !k.isShift && !k.isBack && !k.isSpace && !k.isClose)
                                       ? k.upper : k.label;
                    if (k.isShift && isUpper) disp = "SHIFT (ON)";
                    TTF_SetTextString(k.textObj, disp.c_str(), 0);
                }
            }
        }

        void processKey(VKey& k, ITextInput* target) {
            if (k.isShift) {
                isUpper = !isUpper;
                updateLabels();
            } else if (k.isBack) {
                if (target) target->removeLastChar();
            } else if (k.isSpace) {
                if (target) target->appendText(" ");
            } else if (k.isClose) {
                closeRequested = true;
            } else {
                if (target) target->appendText(isUpper ? k.upper : k.lower);
            }
        }

        void handleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame, ITextInput* target) {
            for (auto& k : keys) {
                bool hit = cursorX >= k.rect.x && cursorX <= k.rect.x + k.rect.w &&
                           cursorY >= k.rect.y && cursorY <= k.rect.y + k.rect.h;
                k.hovered = hit;
                if (hit && confirmDown && !confirmDownLastFrame) {
                    processKey(k, target);
                }
            }
        }

        // --- New gamepad helpers ---
        void toggleShift() {
            isUpper = !isUpper;
            updateLabels();
        }

        void requestClose() {
            closeRequested = true;
        }

        bool isCloseRequested() const { return closeRequested; }
        void resetCloseRequest() { closeRequested = false; }

        void handleMouse(const SDL_Event& ev, ITextInput* target) {
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                for (auto& k : keys) {
                    bool hit = mx >= k.rect.x && mx <= k.rect.x + k.rect.w &&
                               my >= k.rect.y && my <= k.rect.y + k.rect.h;
                    if (hit) { processKey(k, target); break; }
                }
            }
        }

        void render() {
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 240);
            SDL_RenderFillRect(renderer, &rect);
            for (const auto& k : keys) {
                SDL_Color bg = k.hovered ? SDL_Color{100,100,150,255} : SDL_Color{60,60,60,255};
                if (k.isShift && isUpper) bg = {150,150,50,255};
                SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
                SDL_RenderFillRect(renderer, &k.rect);
                SDL_SetRenderDrawColor(renderer, 200,200,200,255);
                SDL_RenderRect(renderer, &k.rect);
                if (k.textObj) {
                    TTF_SetTextColor(k.textObj, 255,255,255,255);
                    int tw = 0, th = 0;
                    TTF_GetTextSize(k.textObj, &tw, &th);
                    TTF_DrawRendererText(k.textObj,
                        k.rect.x + (k.rect.w - tw) * 0.5f,
                        k.rect.y + (k.rect.h - th) * 0.5f);
                }
            }
        }
    };

    class Button : public IGuiElement {
    public:
        Button(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, SDL_FPoint pos, float w, float h)
            : renderer(renderer), font(font), textStr(text), position(pos), width(w), height(h) {
            idleColor = {100,100,200,255};
            hoverColor = {150,150,250,255};
            pressColor = {50,50,150,255};
            textColor = {255,255,255,255};
            currentColor = idleColor;
            updateTextTexture();
        }

        ~Button() { if (textTexture) SDL_DestroyTexture(textTexture); }

        std::string getType() const override { return "Button"; }
        float getX() const override { return position.x; }
        float getY() const override { return position.y; }
        float getWidth() const override { return width; }
        float getHeight() const override { return height; }
        void setPos(SDL_Point p) override {position.x = p.x; position.y = p.y;};
        void setRect(SDL_FRect r) override { position = {r.x, r.y}; width = r.w; height = r.h; }
        const std::string& getText() const { return textStr; }
        void setText(const std::string& t) { textStr = t; updateTextTexture(); }

        // IGuiElement pure virtual overrides — delegate to Impl helpers
        void render(float offsetX = 0.0f, float offsetY = 0.0f) override { renderImpl(offsetX, offsetY); }
        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* /*window*/,
                        bool confirmDown, bool confirmDownLastFrame) override {
            handleGamepadImpl(cursorX, cursorY, offsetX, offsetY, confirmDown, confirmDownLastFrame);
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, bool confirmDown, bool confirmDownLastFrame) {
            handleGamepadImpl(cursorX, cursorY, offsetX, offsetY, confirmDown, confirmDownLastFrame);
        }
        void render(float offsetX, float offsetY, float /*cursorX*/, float /*cursorY*/) { renderImpl(offsetX, offsetY); }

        std::function<void()> onClicked;

        SDL_FPoint getPosition() { return position; }

    private:
        SDL_Renderer* renderer;
        TTF_Font* font;
        std::string textStr;
        SDL_FPoint position;
        float width, height;
        float textWidth = 0, textHeight = 0;
        SDL_Color idleColor, hoverColor, pressColor, textColor, currentColor;
        bool isHovered = false, isPressed = false;
        SDL_Texture* textTexture = nullptr;

    public:

        void handleEventImpl(const SDL_Event& e, float offsetX, float offsetY) {
            if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                float mouseX, mouseY;
                if (e.type == SDL_EVENT_MOUSE_MOTION) { mouseX = e.motion.x; mouseY = e.motion.y; }
                else { mouseX = e.button.x; mouseY = e.button.y; }
                float visualX = position.x - offsetX;
                float visualY = position.y - offsetY;
                bool inside = (mouseX >= visualX) && (mouseX <= visualX + width) &&
                            (mouseY >= visualY) && (mouseY <= visualY + height);
                if (!inside) {
                    isHovered = false; isPressed = false; currentColor = idleColor;
                } else {
                    isHovered = true;
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                        isPressed = true; currentColor = pressColor;
                    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                        isPressed = false;
                        if (onClicked) onClicked();
                    }
                    if (!isPressed) currentColor = hoverColor;
                }
            }
        }

        bool handleEvent(const SDL_Event & ev, SDL_Window* window, float offsetX, float offsetY) override {
            handleEventImpl(ev, offsetX, offsetY);
            if (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                float mouseX = (ev.type == SDL_EVENT_MOUSE_MOTION) ? ev.motion.x : ev.button.x;
                float mouseY = (ev.type == SDL_EVENT_MOUSE_MOTION) ? ev.motion.y : ev.button.y;
                float visualX = position.x - offsetX;
                float visualY = position.y - offsetY;
                bool inside = (mouseX >= visualX) && (mouseX <= visualX + width) &&
                            (mouseY >= visualY) && (mouseY <= visualY + height);
                if (inside) return true;
            }
            return false;
        }

        void handleGamepadImpl(float cursorX, float cursorY, float offsetX, float offsetY, bool confirmDown, bool confirmDownLastFrame) {
            float visualX = position.x - offsetX;
            float visualY = position.y - offsetY;
            bool inside = (cursorX >= visualX) && (cursorX <= visualX + width) &&
                        (cursorY >= visualY) && (cursorY <= visualY + height);
            if (!inside) {
                if (!isHovered) { isPressed = false; currentColor = idleColor; }
                return;
            }
            isHovered = true;
            if (confirmDown && !confirmDownLastFrame) {
                isPressed = true; currentColor = pressColor;
            } else if (!confirmDown && confirmDownLastFrame && isPressed) {
                isPressed = false;
                if (onClicked) onClicked();
            }
            if (!isPressed) currentColor = hoverColor;
        }

        void renderImpl(float offsetX, float offsetY) {
            SDL_FRect fillRect = { position.x - offsetX, position.y - offsetY, width, height };
            SDL_SetRenderDrawColor(renderer, currentColor.r, currentColor.g, currentColor.b, currentColor.a);
            SDL_RenderFillRect(renderer, &fillRect);
            if (textTexture) {
                SDL_FRect renderQuad = {
                    position.x + (width - textWidth) / 2.0f,
                    (position.y - offsetY) + (height - textHeight) / 2.0f,
                    textWidth, textHeight
                };
                SDL_RenderTexture(renderer, textTexture, nullptr, &renderQuad);
            }
        }

        void updateTextTexture() {
            if (textTexture) SDL_DestroyTexture(textTexture);
            SDL_Surface* surf = TTF_RenderText_Blended(font, textStr.c_str(), 0, textColor);
            if (surf) {
                textWidth = (float)surf->w; textHeight = (float)surf->h;
                textTexture = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_DestroySurface(surf);
            }
        }
    };

    // ----------------------------------------------------------------
    // Panel — coloured rectangle that owns child IGuiElements and an
    // internal Scrollbar.  The panel clips all child hit-testing and
    // rendering to its own bounds; it does NOT scroll with the editor
    // scrollbar (offsetY is always ignored for its own position and
    // passed as 0 to its children so they render relative to the panel).
    // ----------------------------------------------------------------
    class Panel : public IGuiElement {
    public:
        SDL_Color bgColor   = { 40,  40,  48, 230 };
        SDL_Color borderColor = { 80, 80, 100, 255 };
        bool      showBorder  = true;

        Panel(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
              SDL_FRect rect, SDL_Color bg = {40,40,48,230})
            : renderer(renderer), textEngine(textEngine), font(font),
              rect(rect), bgColor(bg)
        {
            const float sbW = 10.0f;
            scrollbar.setGeometry(rect.x + rect.w - sbW - 2,
                                  rect.y + 2, sbW, rect.h - 4,
                                  computeContentHeight(), rect.h - 4);
            scrollbar.onChange = [this](float v){ panelScrollOffset = v; };
        }

        // IGuiElement interface — Panel lives in scene-space (no editor scroll)
        std::string getType()   const override { return "Panel"; }
        float getX()            const override { return rect.x; }
        float getY()            const override { return rect.y; }
        float getWidth()        const override { return rect.w; }
        float getHeight()       const override { return rect.h; }
        void  setRect(SDL_FRect r) override {
            const float dx = r.x - rect.x;
            const float dy = r.y - rect.y;
            rect = r;
            if (dx != 0.0f || dy != 0.0f) shiftChildren(dx, dy);
            refreshScrollbarGeometry();
        }
        void setPos(SDL_Point p) override {
            const float dx = (float)p.x - rect.x;
            const float dy = (float)p.y - rect.y;
            rect.x = p.x; rect.y = p.y;
            if (dx != 0.0f || dy != 0.0f) shiftChildren(dx, dy);
            refreshScrollbarGeometry();
        };

        // Children are owned by the Panel
        void addChild(std::unique_ptr<IGuiElement> child) {
            children.push_back(std::move(child));
            refreshScrollbarGeometry();
        }
        const std::vector<std::unique_ptr<IGuiElement>>& getChildren() const { return children; }
        std::vector<std::unique_ptr<IGuiElement>>& getChildrenMutable() { return children; }

        // Returns the child (or this panel) that the screen-space point hits, for editor picking.
        // Children are tested first (they're on top visually).
        IGuiElement* hitTest(float screenX, float screenY) {
            if (!inRect(screenX, screenY, rect)) return nullptr;
            // Test children in reverse order (last added = topmost visually)
            for (int i = (int)children.size() - 1; i >= 0; --i) {
                auto* c = children[i].get();
                // Child positions are absolute screen coords; their Y is unscrolled here
                // because we want editor picking to always work regardless of panel scroll.
                SDL_FRect cr = { c->getX(), c->getY() - panelScrollOffset,
                                 c->getWidth(), c->getHeight() };
                if (inRect(screenX, screenY, cr)) return c;
            }
            return this; // hit the panel itself
        }

        // ------------------------------------------------------------------
        // render — Panel ignores the caller's offsetY completely; it is always
        // drawn at its scene-space position.  Children are scrolled by the
        // panel's own panelScrollOffset.
        // ------------------------------------------------------------------
        void render(float offsetX = 0.0f, float editorOffsetY = 0.0f) override {
            SDL_FRect originalRect = rect;
            rect.x -= offsetX;
            rect.y -= editorOffsetY;
            // ... Background and border drawing ...
            SDL_Rect clip = { (int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h };
            SDL_SetRenderClipRect(renderer, &clip);
            for (auto& child : children)
                child->render(offsetX, editorOffsetY + panelScrollOffset);
            SDL_SetRenderClipRect(renderer, nullptr);
            scrollbar.render(renderer, offsetX, editorOffsetY);
            for (auto& child : children)
                child->renderSelectionOutline(renderer, offsetX, editorOffsetY + panelScrollOffset);
            renderSelectionOutline(renderer, offsetX, editorOffsetY);
            rect = originalRect; // Restore
        }

        // ------------------------------------------------------------------
        // handleEvent — only processes events whose mouse position is within
        // the panel.  Wheel events inside the panel scroll the panel, not
        // the editor.
        // ------------------------------------------------------------------
        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetX = 0.0f, float editorOffsetY = 0.0f) override {
            float mx = 0, my = 0;
            bool hasPos = getEventPos(ev, mx, my);
            SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - editorOffsetY, rect.w, rect.h };

            if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                if (hasPos && inRect(mx, my, shiftedRect)) {
                    float origTX = scrollbar.trackX, origTY = scrollbar.trackY;
                    scrollbar.trackX -= offsetX; scrollbar.trackY -= editorOffsetY;
                    bool res = scrollbar.handleEvent(ev);
                    scrollbar.trackX = origTX; scrollbar.trackY = origTY;
                    return res;
                }
                return false;
            }
            if (hasPos && !inRect(mx, my, shiftedRect)) return false;
            
            float origTX = scrollbar.trackX, origTY = scrollbar.trackY;
            scrollbar.trackX -= offsetX; scrollbar.trackY -= editorOffsetY;
            bool sbRes = scrollbar.handleEvent(ev);
            scrollbar.trackX = origTX; scrollbar.trackY = origTY;
            if (sbRes) return true;

            for (auto& child : children)
                if (child->handleEvent(ev, window, offsetX, editorOffsetY + panelScrollOffset)) return true;
            return inRect(mx, my, shiftedRect);
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float editorOffsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - editorOffsetY, rect.w, rect.h };
            if (!inRect(cursorX, cursorY, shiftedRect)) return;
            for (auto& child : children)
                child->handleGamepad(cursorX, cursorY, offsetX, editorOffsetY + panelScrollOffset, window, confirmDown, confirmDownLastFrame);
        }

        float getPanelScrollOffset() const { return panelScrollOffset; }
        Scrollbar& getScrollbar() { return scrollbar; }

    private:
        SDL_Renderer*    renderer;
        TTF_TextEngine*  textEngine;
        TTF_Font*        font;
        SDL_FRect        rect;
        float            panelScrollOffset = 0.0f;
        Scrollbar        scrollbar;
        std::vector<std::unique_ptr<IGuiElement>> children;

        void refreshScrollbarGeometry() {
            const float sbW = 10.0f;
            scrollbar.setGeometry(rect.x + rect.w - sbW - 2,
                                  rect.y + 2, sbW, rect.h - 4,
                                  computeContentHeight(), rect.h - 4);
        }

        // Moves every child by (dx, dy) so they stay glued to the panel
        // whenever the panel itself is repositioned (e.g. re-centered on
        // window resize). Without this, children keep their old baked
        // absolute coordinates and visually detach from the panel.
        void shiftChildren(float dx, float dy) {
            for (auto& c : children) {
                c->setPos(SDL_Point{
                    (int)std::lround(c->getX() + dx),
                    (int)std::lround(c->getY() + dy)
                });
            }
        }

        float computeContentHeight() const {
            // NOTE: child Y is absolute screen-space; measure relative to
            // the panel's own rect.y, not the raw absolute value, or the
            // "does this content need to scroll" result (and therefore
            // scrollbar visibility) ends up depending on where the panel
            // happens to sit on screen instead of actual content size.
            float bottom = 0;
            for (auto& c : children)
                bottom = std::max(bottom, (c->getY() - rect.y) + c->getHeight());
            return bottom + 8.0f; // small padding
        }

        static bool inRect(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
        }
        // Extract pointer position from various SDL event types
        static bool getEventPos(const SDL_Event& ev, float& x, float& y) {
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                x = ev.button.x; y = ev.button.y; return true;
            }
            if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                x = ev.motion.x; y = ev.motion.y; return true;
            }
            if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                SDL_GetMouseState(&x, &y); return true;
            }
            return false;
        }
    };

    // ----------------------------------------------------------------
    // Base Container – manages children layout
    // ----------------------------------------------------------------
    class Container : public IGuiElement {
    public:
        virtual ~Container() = default;

        void addChild(std::unique_ptr<IGuiElement> child) {
            children.push_back(std::move(child));
            layoutChildren();
        }

        void setPadding(float p) { padding = p; layoutChildren(); }
        void setSpacing(float s) { spacing = s; layoutChildren(); }

        std::string getType() const override { return "Container"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override {
            rect = r;
            layoutChildren();
        }
        void setPos(SDL_Point p) override {
            rect.x = p.x; 
            rect.y = p.y;
            layoutChildren();
        }

        void render(float offsetX, float offsetY) override {
            for (auto& child : children) child->render(offsetX, offsetY);
        }

        bool handleEvent(const SDL_Event& e, SDL_Window* window, float offsetX, float offsetY) override {
            bool consumed = false;
            for (auto& child : children)
                if (child->handleEvent(e, window, offsetX, offsetY)) consumed = true;
            return consumed;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            for (auto& child : children)
                child->handleGamepad(cursorX, cursorY, offsetX, offsetY, window, confirmDown, confirmDownLastFrame);
        }

        const std::vector<std::unique_ptr<IGuiElement>>& getChildren() const { return children; }

    protected:
        std::vector<std::unique_ptr<IGuiElement>> children;
        SDL_FRect rect = {0, 0, 0, 0};
        float padding = 5.0f;
        float spacing = 5.0f;

        virtual void layoutChildren() = 0;
    };

    // ----------------------------------------------------------------
    // HBoxContainer – arranges children horizontally without resizing them
    // ----------------------------------------------------------------
    class HBoxContainer : public Container {
    public:
        std::string getType() const override { return "HBox"; }

    protected:
        void layoutChildren() override {
            if (children.empty()) return;
            float x = rect.x + padding;
            float y = rect.y + padding;
            float maxHeight = 0;
            for (auto& child : children)
                maxHeight = std::max(maxHeight, child->getHeight());
            for (auto& child : children) {
                float childW = child->getWidth();
                float childH = child->getHeight();
                float childY = y + (maxHeight - childH) * 0.5f;
                child->setRect({ x, childY, childW, childH });
                x += childW + spacing;
            }
        }
    };

    // ----------------------------------------------------------------
    // VBoxContainer – arranges children vertically without resizing them
    // ----------------------------------------------------------------
    class VBoxContainer : public Container {
    public:
        std::string getType() const override { return "VBox"; }

    protected:
        void layoutChildren() override {
            if (children.empty()) return;
            float x = rect.x + padding;
            float y = rect.y + padding;
            float maxWidth = 0;
            for (auto& child : children)
                maxWidth = std::max(maxWidth, child->getWidth());
            for (auto& child : children) {
                float childW = child->getWidth();
                float childH = child->getHeight();
                float childX = x + (maxWidth - childW) * 0.5f;
                child->setRect({ childX, y, childW, childH });
                y += childH + spacing;
            }
        }
    };
    
    class Dialog {
    public:
        enum class Action { None, Confirm, Cancel };

        virtual ITextInput* getCurrentTextInput() { return nullptr; }

        Dialog(const Dialog&) = delete;
        Dialog& operator=(const Dialog&) = delete;
        virtual ~Dialog() = default;

        float progress = 0.0f;
        DialogState state = DialogState::Closed;

        void open() {
            progress = 0.0f;
            state = DialogState::Opening;
            onOpen();
        }

        bool isOpen() const {
            return state == DialogState::Opening || state == DialogState::Opened;
        }

        void tick(float dt) {
            if (state == DialogState::Opening) {
                progress += dt * 4.0f;
                if (progress >= 1.0f) { progress = 1.0f; state = DialogState::Opened; }
            }
        }

        void setNewTitle(std::string newTitle) {
            title = std::move(newTitle);
        }


        Action handleEvent(const SDL_Event& ev) {
            if (state != DialogState::Opened) return Action::None;
            SDL_FRect win = animRect();
            if (onHandleEvent(ev)) return Action::None;
            auto hit = [](float x, float y, SDL_FRect r) {
                return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
            };
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                if (hit(mx, my, closeBtnRect(win)) || hit(mx, my, cancelBtnRect(win)))
                    return Action::Cancel;
                if (hit(mx, my, confirmBtnRect(win)))
                    return Action::Confirm;
                if (hit(mx, my, win)) return Action::None;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_RETURN) return Action::Confirm;
                if (ev.key.key == SDLK_ESCAPE) return Action::Cancel;
            }
            return Action::None;
        }

        virtual bool onHandleGamepad(float, float, bool, bool) { return false; }

        Action handleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) {
            if (state != DialogState::Opened) return Action::None;
            SDL_FRect win = animRect();
            if (onHandleGamepad(cursorX, cursorY, confirmDown, confirmDownLastFrame)) return Action::None;
            if (confirmDown && !confirmDownLastFrame) {
                if (hit(cursorX, cursorY, closeBtnRect(win)) || hit(cursorX, cursorY, cancelBtnRect(win)))
                    return Action::Cancel;
                if (hit(cursorX, cursorY, confirmBtnRect(win)))
                    return Action::Confirm;
            }
            return Action::None;
        }

        void render() {
            SDL_FRect win = animRect();
            bool fullOpen = (state == DialogState::Opened);
            SDL_FRect shadow = { win.x-10, win.y, win.w+20, win.h+10 };
            fillRect(shadow, {52,110,235,200});
            fillRect(win, {212,208,200,240});
            SDL_FRect titleBar = { win.x, win.y, win.w, 36.0f * progress };
            fillRect(titleBar, {52,110,235,255});
            if (!fullOpen || !font) return;
            float mx, my; SDL_GetMouseState(&mx, &my);
            drawText(title.c_str(), win.x + 8.0f, win.y + 9.0f, {255,255,255,255});
            SDL_FRect cb = closeBtnRect(win);
            bool hCb = hit(mx, my, cb);
            fillRect(cb, hCb ? SDL_Color{220,60,60,255} : SDL_Color{170,40,40,255});
            drawRect(cb, hCb ? SDL_Color{255,255,255,255} : SDL_Color{200,200,200,255});
            drawText("X", cb.x + 11.0f, cb.y + 8.0f, {255,255,255,255});
            onRender(win);
            SDL_FRect conf = confirmBtnRect(win);
            bool hConf = hit(mx, my, conf);
            fillRect(conf, hConf ? SDL_Color{80,200,100,255} : SDL_Color{52,160,75,255});
            drawText(confirmLabel.c_str(), conf.x + 10.0f, conf.y + 8.0f, {255,255,255,255});
            SDL_FRect canc = cancelBtnRect(win);
            bool hCanc = hit(mx, my, canc);
            fillRect(canc, hCanc ? SDL_Color{180,60,60,255} : SDL_Color{140,40,40,255});
            drawText(cancelLabel.c_str(), canc.x + 10.0f, canc.y + 8.0f, {255,255,255,255});
        }

        void reset() {
            progress = 0.0f;
            state = DialogState::Closed;
            onReset();
        }

    protected:
        Dialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine,
               TTF_Font* font, SDL_Window* window,
               SDL_FRect logicalRect,
               std::string title = "Dialog",
               std::string confirmLabel = "Confirm",
               std::string cancelLabel = "Cancel")
            : renderer(renderer), textEngine(textEngine),
              font(font), window(window),
              logicalRect(logicalRect),
              title(std::move(title)),
              confirmLabel(std::move(confirmLabel)),
              cancelLabel(std::move(cancelLabel)) {}

        virtual void onOpen() {}
        virtual bool onHandleEvent(const SDL_Event&) { return false; }
        virtual void onRender(SDL_FRect) {}
        virtual void onReset() {}

        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_Window* window;
        SDL_FRect logicalRect;

        SDL_FRect animRect() const {
            float cx = logicalRect.x + logicalRect.w * 0.5f;
            float cy = logicalRect.y + logicalRect.h * 0.5f;
            return { cx - logicalRect.w * 0.5f * progress,
                     cy - logicalRect.h * 0.5f * progress,
                     logicalRect.w * progress,
                     logicalRect.h * progress };
        }

        void fillRect(SDL_FRect r, SDL_Color c) {
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer, &r);
        }
        void drawRect(SDL_FRect r, SDL_Color c) {
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            SDL_RenderRect(renderer, &r);
        }
        void drawText(const char* s, float x, float y, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, s, 0);
            if (!t) return;
            TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
            TTF_DrawRendererText(t, x, y);
            TTF_DestroyText(t);
        }
        static bool hit(float x, float y, SDL_FRect r) {
            return x >= r.x && x <= r.x+r.w && y >= r.y && y <= r.y+r.h;
        }

    private:
        std::string title, confirmLabel, cancelLabel;
        static SDL_FRect closeBtnRect(SDL_FRect w) { return { w.x+w.w-20, w.y, 36, 36 }; }
        static SDL_FRect confirmBtnRect(SDL_FRect w) { return { w.x+w.w-120, w.y+w.h-50, 106, 36 }; }
        static SDL_FRect cancelBtnRect(SDL_FRect w) { return { w.x+14, w.y+w.h-50, 106, 36 }; }
    };

    // ----------------------------------------------------------------
    // SceneInspector — right-side panel that shows editable properties
    // for whichever IGuiElement (or entity) is currently selected.
    // Lives entirely in screen-space; does not scroll with the editor.
    // ----------------------------------------------------------------
    class AddChildDialog : public Dialog {
    public:
        AddChildDialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                    SDL_Window* window, Panel* parentPanel);

        bool onHandleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) override;

        Panel* parentPanel = nullptr;   // the panel we are adding a child to
        OptionBox typeOption;
        std::vector<std::string> options = {"Button", "LineEdit", "SpinBox"};

    protected:
        void onOpen() override;
        bool onHandleEvent(const SDL_Event& ev) override;
        void onRender(SDL_FRect win) override;
        void onReset() override;
    };

    class SceneInspector {
    private:
        std::vector<std::unique_ptr<IGuiElement>>* guiElementsPtr = nullptr;
        AddChildDialog* addChildDialog = nullptr;
        struct ChildButtonInfo {
            SDL_FRect rect;
            size_t childIndex;
        };
        std::vector<ChildButtonInfo> childRemoveButtons;
        SDL_FRect addChildButtonRect;
        float inspectorScrollOffset = 0.0f;
        Gui::Scrollbar inspectorScrollbar;

        void buildPanelChildrenFields(Gui::Panel* panel) {
            // This function is called from render() to generate the child list UI.
            // We'll implement it directly in render() to avoid storing extra state.
        }

        void handlePanelChildEvents(const SDL_Event& ev) {
            if (ev.type != SDL_EVENT_MOUSE_BUTTON_DOWN || ev.button.button != SDL_BUTTON_LEFT)
                return;
            float mx = ev.button.x, my = ev.button.y;

            // Check "Add Child" button (rendered in render())
            if (mx >= addChildButtonRect.x && mx <= addChildButtonRect.x + addChildButtonRect.w &&
                my >= addChildButtonRect.y && my <= addChildButtonRect.y + addChildButtonRect.h) {
                if (addChildDialog && target && target->getType() == "Panel") {
                    addChildDialog->parentPanel = static_cast<Panel*>(target);
                    modeBeforeDialog = currentEditMode;
                    currentEditMode = EditMode::Dialog;
                    addChildDialog->open();
                }
                return;
            }

            // Check remove buttons
            for (auto& info : childRemoveButtons) {
                if (mx >= info.rect.x && mx <= info.rect.x + info.rect.w &&
                    my >= info.rect.y && my <= info.rect.y + info.rect.h) {
                    if (target && target->getType() == "Panel") {
                        auto* panel = static_cast<Panel*>(target);
                        auto& children = panel->getChildrenMutable();
                        if (info.childIndex < children.size()) {
                            children.erase(children.begin() + info.childIndex);
                            rebuildFields();  // refresh the inspector
                        }
                    }
                    return;
                }
            }
        }

    public:
        static constexpr float PANEL_W = 260.0f;

        // Computed from real window width so resizing works correctly.
        float panelX() const {
            int w, h; SDL_GetWindowSize(window, &w, &h);
            return (float)w - PANEL_W;
        }

        SceneInspector(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_Window* w)
            : renderer(r), textEngine(te), font(f), window(w) {
            inspectorScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
            inspectorScrollbar.onChange = [this](float v){ inspectorScrollOffset = v; };
        }

        void setGuiElementsVector(std::vector<std::unique_ptr<IGuiElement>>* vec) { guiElementsPtr = vec; }

        void setAddChildDialog(AddChildDialog* dialog) { addChildDialog = dialog; }

        // Called once per frame — pass the scene GUI elements so the
        // inspector can both read and write their properties.
        void setTarget(IGuiElement* elem) {
            if (target == elem) return;
            commitAllFields(); // flush any pending edits when switching
            target = elem;
            rebuildFields();
            inspectorScrollOffset = 0.0f; // reset scroll on new target
        }

        void handleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) {
            if (!target) return;
            for (auto& f : fields) {
                f.edit.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
            }
        }

        IGuiElement* getTarget() const { return target; }

        // Returns true if the event was consumed by the inspector
        bool handleEvent(const SDL_Event& ev) {
            if (!target) return false;
            // Forward scrollbar events
            if (inspectorScrollbar.handleEvent(ev)) return true;

            bool consumed = false;
            for (auto& f : fields)
                if (f.edit.handleEvent(ev, window, 0.0f, 0.0f)) consumed = true;
            // Commit on Enter/Tab
            if (ev.type == SDL_EVENT_KEY_DOWN &&
                (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_TAB)) {
                commitAllFields();
            }
            if (target && target->getType() == "Panel") {
                handlePanelChildEvents(ev);
            }
            return consumed;
        }

        void render(float windowHeight) {
            if (!target) {
                // Draw empty panel with hint
                SDL_FRect bg = { panelX(), 0, PANEL_W, windowHeight };
                SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245);
                SDL_RenderFillRect(renderer, &bg);
                SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255);
                SDL_RenderRect(renderer, &bg);
                drawLabel("Inspector", panelX() + 10, 10, {180,180,200,255});
                drawLabel("Click a GUI element", panelX() + 10, 40, {100,100,120,255});
                return;
            }

            SDL_FRect bg = { panelX(), 0, PANEL_W, windowHeight };
            SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245);
            SDL_RenderFillRect(renderer, &bg);
            SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255);
            SDL_RenderRect(renderer, &bg);

            // Compute total content height
            float contentHeight = 0.0f;
            // Title and divider
            contentHeight += 10.0f + 22.0f + 22.0f + 8.0f; // inspector title, element type, divider
            // Each field: label (32) + edit (32) + 8 spacing
            contentHeight += fields.size() * (32.0f + 32.0f + 8.0f);
            if (target && target->getType() == "Panel") {
                contentHeight += 38.0f; // Add Child button + spacing
                auto* panel = static_cast<Panel*>(target);
                contentHeight += panel->getChildren().size() * (26.0f + 4.0f); // each child row + spacing
            }
            contentHeight += 8.0f; // bottom padding for hint

            // Set scrollbar geometry (right side)
            const float sbW = 12.0f;
            float viewHeight = windowHeight;
            inspectorScrollbar.setGeometry(
                panelX() + PANEL_W - sbW, 0.0f, sbW, viewHeight,
                contentHeight, viewHeight
            );

            // Update offset from scrollbar
            inspectorScrollOffset = inspectorScrollbar.offset;

            // Clip to panel area (exclude scrollbar area)
            SDL_Rect clip = {
                (int)panelX(),
                0,
                (int)(PANEL_W - sbW),
                (int)viewHeight
            };
            SDL_SetRenderClipRect(renderer, &clip);

            // Draw content with vertical offset
            float y = 10.0f - inspectorScrollOffset;
            drawLabel("Inspector", panelX() + 10, y, {200,200,220,255}); y += 22;
            drawLabel(("[" + target->getType() + "]").c_str(), panelX() + 10, y, {140,140,180,255}); y += 22;

            // Divider
            SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
            SDL_FRect div = { panelX() + 5, y, PANEL_W - 10, 1 };
            SDL_RenderFillRect(renderer, &div); y += 8;

            for (auto& f : fields) {
                drawLabel(f.label.c_str(), panelX() + 8, y, {160,160,190,255});
                y += 32;
                f.edit.setRect({ panelX() + 8, y, PANEL_W - sbW - 20, 26 });
                f.edit.render(0.0f, 0.0f);
                y += 32;
            }

            if (target && target->getType() == "Panel") {
                auto* panel = static_cast<Panel*>(target);
                const auto& children = panel->getChildren();

                // "Add Child" button
                SDL_FRect addBtn = { panelX() + 8, y, PANEL_W - sbW - 20, 30 };
                addChildButtonRect = addBtn;
                SDL_SetRenderDrawColor(renderer, 80, 120, 200, 255);
                SDL_RenderFillRect(renderer, &addBtn);
                drawLabel("+ Add Child", addBtn.x + 10, addBtn.y + 5, {255,255,255,255});
                y += addBtn.h + 8;

                // List children with remove buttons
                childRemoveButtons.clear();
                for (size_t i = 0; i < children.size(); ++i) {
                    auto* child = children[i].get();
                    SDL_FRect row = { panelX() + 8, y, PANEL_W - sbW - 20, 26 };
                    drawLabel(("Child " + std::to_string(i) + " (" + child->getType() + ")").c_str(),
                            row.x + 5, row.y + 2, {200,200,220,255});
                    SDL_FRect removeBtn = { row.x + row.w - 30, row.y, 26, 26 };
                    SDL_SetRenderDrawColor(renderer, 200, 40, 40, 255);
                    SDL_RenderFillRect(renderer, &removeBtn);
                    drawLabel("X", removeBtn.x + 8, removeBtn.y + 2, {255,255,255,255});
                    childRemoveButtons.push_back({removeBtn, i});
                    y += row.h + 4;
                }
            }

            // Hint
            drawLabel("Enter = commit changes", panelX() + 8, y + 4, {80, 80, 100, 255});

            SDL_SetRenderClipRect(renderer, nullptr);

            // Render scrollbar (on top)
            inspectorScrollbar.render(renderer, 0.0f, 0.0f);
        }

        // Flush all field values into the target element right now.
        // Called from the Save button handler in main.cpp too.
        void commitAllFields() {
            if (!target) return;
            applyFields();
        }

        // Keeps displayed field text in sync with the target's live values
        // (e.g. after it was dragged on the canvas), as long as the user
        // isn't actively typing in that field. Call this once per frame.
        // Without it, commitAllFields() (fired on deselect, Tab/Enter, or
        // Save) would re-apply the stale value captured when the field was
        // first built, silently undoing any external change like a drag.
        void syncFromTarget() {
            if (!target) return;
            for (auto& f : fields) {
                if (f.edit.isActive()) continue;
                std::string live = liveValueForKey(f.key);
                if (live != f.lastSyncedText) {
                    f.edit.clear();
                    for (char c : live) f.edit.appendText(std::string(1, c));
                    f.lastSyncedText = live;
                }
            }
        }

    private:
        struct Field {
            std::string label;
            std::string key;   // identifies which property this maps to
            LineEdit    edit;
            // Last value we pushed into (or committed from) this field.
            // Lets us tell "the user typed something here" apart from
            // "the target changed externally" (e.g. a mouse drag), so
            // committing never stomps a change the field doesn't know about.
            std::string lastSyncedText;
            Field(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f,
                const std::string& lbl, const std::string& k, const std::string& val)
                : label(lbl), key(k),
                edit(r, te, f, {0,0,1,1}, ""),
                lastSyncedText(val)
            {
                // Pre-fill with current value
                for (char c : val) edit.appendText(std::string(1,c));
            }
            // disable copy so vector moves correctly
            Field(const Field&) = delete;
            Field& operator=(const Field&) = delete;
            Field(Field&&) = default;
            Field& operator=(Field&&) = default;
        };

        SDL_Renderer*    renderer;
        TTF_TextEngine*  textEngine;
        TTF_Font*        font;
        SDL_Window*      window;
        IGuiElement*     target = nullptr;
        std::vector<Field> fields;

        void rebuildFields() {
            fields.clear();
            if (!target) return;

            // Common geometry fields for every element type
            auto addF = [&](const std::string& lbl, const std::string& key, float val) {
                char buf[32]; std::snprintf(buf, sizeof(buf), "%.1f", val);
                fields.emplace_back(renderer, textEngine, font, lbl, key, buf);
            };
            addF("X",      "x", target->getX());
            addF("Y",      "y", target->getY());
            addF("Width",  "w", target->getWidth());
            addF("Height", "h", target->getHeight());

            // Type-specific fields
            if (target->getType() == "Button") {
                auto* b = static_cast<Button*>(target);
                fields.emplace_back(renderer, textEngine, font, "Text", "btn_text", b->getText());
            } else if (target->getType() == "LineEdit") {
                auto* le = static_cast<LineEdit*>(target);
                fields.emplace_back(renderer, textEngine, font, "Placeholder", "le_placeholder", le->getPlaceholder());
            } else if (target->getType() == "SpinBox") {
                auto* sb = static_cast<SpinBox*>(target);
                addF("Min",     "sb_min",  sb->getMin());
                addF("Max",     "sb_max",  sb->getMax());
                addF("Step",    "sb_step", sb->getStep());
                addF("Value",   "sb_val",  sb->getValue());
            } else if (target->getType() == "Panel") {
                auto* p = static_cast<Panel*>(target);
                // bg_color r g b a
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.r);
                fields.emplace_back(renderer, textEngine, font, "BG Red",   "panel_r", buf);
                std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.g);
                fields.emplace_back(renderer, textEngine, font, "BG Green", "panel_g", buf);
                std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.b);
                fields.emplace_back(renderer, textEngine, font, "BG Blue",  "panel_b", buf);
                std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.a);
                fields.emplace_back(renderer, textEngine, font, "BG Alpha", "panel_a", buf);
            }
        }

        // Returns the current live value (formatted the same way rebuildFields
        // pre-fills it) for the given field key, read straight from target.
        std::string liveValueForKey(const std::string& key) const {
            char buf[32];
            if (key == "x") { std::snprintf(buf, sizeof(buf), "%.1f", target->getX());      return buf; }
            if (key == "y") { std::snprintf(buf, sizeof(buf), "%.1f", target->getY());      return buf; }
            if (key == "w") { std::snprintf(buf, sizeof(buf), "%.1f", target->getWidth());  return buf; }
            if (key == "h") { std::snprintf(buf, sizeof(buf), "%.1f", target->getHeight()); return buf; }

            if (target->getType() == "Button") {
                auto* b = static_cast<Button*>(target);
                if (key == "btn_text") return b->getText();
            } else if (target->getType() == "LineEdit") {
                auto* le = static_cast<LineEdit*>(target);
                if (key == "le_placeholder") return le->getPlaceholder();
            } else if (target->getType() == "SpinBox") {
                auto* sb = static_cast<SpinBox*>(target);
                if (key == "sb_min")  { std::snprintf(buf, sizeof(buf), "%.1f", sb->getMin());   return buf; }
                if (key == "sb_max")  { std::snprintf(buf, sizeof(buf), "%.1f", sb->getMax());   return buf; }
                if (key == "sb_step") { std::snprintf(buf, sizeof(buf), "%.1f", sb->getStep());  return buf; }
                if (key == "sb_val")  { std::snprintf(buf, sizeof(buf), "%.1f", sb->getValue()); return buf; }
            } else if (target->getType() == "Panel") {
                auto* p = static_cast<Panel*>(target);
                if (key == "panel_r") { std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.r); return buf; }
                if (key == "panel_g") { std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.g); return buf; }
                if (key == "panel_b") { std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.b); return buf; }
                if (key == "panel_a") { std::snprintf(buf, sizeof(buf), "%d", (int)p->bgColor.a); return buf; }
            }
            return "";
        }

        void applyFields() {
            if (!target) return;

            auto changed = [&](const std::string& key) -> bool {
                for (auto& f : fields)
                    if (f.key == key) return f.edit.getText() != f.lastSyncedText;
                return false;
            };
            auto fval = [&](const std::string& key) -> float {
                for (auto& f : fields) {
                    if (f.key == key) {
                        try { return std::stof(f.edit.getText()); } catch (...) {}
                    }
                }
                return 0.0f;
            };
            auto fstr = [&](const std::string& key) -> std::string {
                for (auto& f : fields)
                    if (f.key == key) return f.edit.getText();
                return "";
            };
            auto markSynced = [&](const std::string& key) {
                for (auto& f : fields)
                    if (f.key == key) f.lastSyncedText = f.edit.getText();
            };

            // Apply common geometry — only touch axes the user actually
            // edited; fall back to the target's current live value on the
            // rest, so we never stomp a position/size that changed
            // externally (e.g. by dragging the element on the canvas).
            bool xC = changed("x"), yC = changed("y"), wC = changed("w"), hC = changed("h");
            if (xC || yC || wC || hC) {
                SDL_FRect r = {
                    xC ? fval("x") : target->getX(),
                    yC ? fval("y") : target->getY(),
                    wC ? fval("w") : target->getWidth(),
                    hC ? fval("h") : target->getHeight()
                };
                if (r.w > 0 && r.h > 0) {
                    target->setRect(r);
                    markSynced("x"); markSynced("y"); markSynced("w"); markSynced("h");
                }
            }

            // Apply type-specific — same "only if actually edited" rule.
            if (target->getType() == "Button") {
                if (changed("btn_text")) {
                    std::string t = fstr("btn_text");
                    if (!t.empty()) { static_cast<Button*>(target)->setText(t); markSynced("btn_text"); }
                }
            } else if (target->getType() == "LineEdit") {
                if (changed("le_placeholder")) {
                    static_cast<LineEdit*>(target)->setPlaceholder(fstr("le_placeholder"));
                    markSynced("le_placeholder");
                }
            } else if (target->getType() == "SpinBox") {
                auto* sb = static_cast<SpinBox*>(target);
                if (changed("sb_min"))  { sb->setMin(fval("sb_min"));   markSynced("sb_min"); }
                if (changed("sb_max"))  { sb->setMax(fval("sb_max"));   markSynced("sb_max"); }
                if (changed("sb_step")) { sb->setStep(fval("sb_step")); markSynced("sb_step"); }
                if (changed("sb_val"))  { sb->setValue(fval("sb_val")); markSynced("sb_val"); }
            } else if (target->getType() == "Panel") {
                auto* p = static_cast<Panel*>(target);
                if (changed("panel_r")) { p->bgColor.r = (Uint8)std::clamp((int)fval("panel_r"), 0, 255); markSynced("panel_r"); }
                if (changed("panel_g")) { p->bgColor.g = (Uint8)std::clamp((int)fval("panel_g"), 0, 255); markSynced("panel_g"); }
                if (changed("panel_b")) { p->bgColor.b = (Uint8)std::clamp((int)fval("panel_b"), 0, 255); markSynced("panel_b"); }
                if (changed("panel_a")) { p->bgColor.a = (Uint8)std::clamp((int)fval("panel_a"), 0, 255); markSynced("panel_a"); }
            }
        }

        void drawLabel(const char* s, float x, float y, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, s, 0);
            if (!t) return;
            TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
            TTF_DrawRendererText(t, x, y);
            TTF_DestroyText(t);
        }
    };

    class EntityInspector {
    public:
        static constexpr float PANEL_W = 260.0f;

        float panelX() const {
            int w, h; SDL_GetWindowSize(window, &w, &h);
            return (float)w - PANEL_W;
        }

        EntityInspector(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_Window* w)
            : renderer(r), textEngine(te), font(f), window(w) {
            inspectorScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
            inspectorScrollbar.onChange = [this](float v){ inspectorScrollOffset = v; };
        }

        void setTarget(ECSWorld& w, Entity e) {
            if (world == &w && targetEntity == e) return;
            commitAllFields();
            world = &w;
            targetEntity = e;
            rebuildFields();
            inspectorScrollOffset = 0.0f;
        }

        void clearTarget() {
            commitAllFields();
            world = nullptr;
            targetEntity = (Entity)-1;
            fields.clear();
            inspectorScrollOffset = 0.0f;
        }

        bool handleEvent(const SDL_Event& ev) {
            // Handle scrollbar events first
            if (inspectorScrollbar.handleEvent(ev)) return true;

            if (!world || targetEntity == (Entity)-1) return false;
            bool consumed = false;
            for (auto& f : fields) {
                if (f.widget->handleEvent(ev, window, 0.0f)) consumed = true;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN &&
                (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_TAB)) {
                commitAllFields();
            }
            return consumed;
        }

        void handleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) {
            if (!world || targetEntity == (Entity)-1) return;
            for (auto& f : fields) {
                f.widget->handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
            }
        }

        void render(float windowHeight) {
            if (!world || targetEntity == (Entity)-1) {
                drawEmptyPanel(windowHeight);
                return;
            }

            SDL_FRect bg = { panelX(), 0, PANEL_W, windowHeight };
            SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245);
            SDL_RenderFillRect(renderer, &bg);
            SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255);
            SDL_RenderRect(renderer, &bg);

            // Compute total content height
            float contentHeight = 0.0f;
            contentHeight += 10.0f + 24.0f + 22.0f + 8.0f; // title, ID, divider
            contentHeight += fields.size() * (32.0f + 32.0f + 8.0f); // each field: label + widget + spacing
            contentHeight += 8.0f; // bottom hint

            // Set scrollbar geometry
            const float sbW = 12.0f;
            float viewHeight = windowHeight;
            inspectorScrollbar.setGeometry(
                panelX() + PANEL_W - sbW, 0.0f, sbW, viewHeight,
                contentHeight, viewHeight
            );
            inspectorScrollOffset = inspectorScrollbar.offset;

            // Clip to panel area (exclude scrollbar)
            SDL_Rect clip = {
                (int)panelX(),
                0,
                (int)(PANEL_W - sbW),
                (int)viewHeight
            };
            SDL_SetRenderClipRect(renderer, &clip);

            float y = 10.0f - inspectorScrollOffset;
            drawLabel("Entity Inspector", panelX() + 10, y, {200,200,220,255}); y += 24;
            drawLabel(("ID: " + std::to_string(targetEntity)).c_str(), panelX() + 10, y, {140,140,180,255}); y += 22;

            SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
            SDL_FRect div = { panelX() + 5, y, PANEL_W - sbW - 10, 1 };
            SDL_RenderFillRect(renderer, &div); y += 8;

            for (auto& f : fields) {
                drawLabel(f.label.c_str(), panelX() + 8, y, {160,160,190,255});
                y += 32;
                f.widget->setRect({ panelX() + 8, y, PANEL_W - sbW - 20, 26 });
                f.widget->render(0.0f);
                y += 32;
            }

            drawLabel("Enter = commit changes", panelX() + 8, y + 4, {80, 80, 100, 255});

            SDL_SetRenderClipRect(renderer, nullptr);

            // Render scrollbar
            inspectorScrollbar.render(renderer, 0.0f, 0.0f);
        }

        // Only writes to the world if the user actually modified the value
        void commitAllFields() {
            if (!world || targetEntity == (Entity)-1) return;
            Entity e = targetEntity;
            for (auto& f : fields) {
                if (f.widget->getType() == "SpinBox") {
                    auto* sb = static_cast<Gui::SpinBox*>(f.widget.get());
                    float val = sb->getValue();
                    if (val != f.lastSyncedValue) {
                        if (f.key == "pos_x") world->position_pool[e].x = val;
                        else if (f.key == "pos_y") world->position_pool[e].y = val;
                        else if (f.key == "rect_w") world->rectangle_shape_pool[e].w = val;
                        else if (f.key == "rect_h") world->rectangle_shape_pool[e].h = val;
                        else if (f.key == "z_index") world->z_index_pool[e].z = (int)val;
                        else if (f.key == "vel_x") world->velocity_pool[e].x = val;
                        else if (f.key == "vel_y") world->velocity_pool[e].y = val;
                        else if (f.key == "acc_x") world->acceleration_pool[e].x = val;
                        else if (f.key == "acc_y") world->acceleration_pool[e].y = val;
                        
                        f.lastSyncedValue = val;
                    }
                } else if (f.widget->getType() == "LineEdit") {
                    auto* le = static_cast<Gui::LineEdit*>(f.widget.get());
                    std::string text = le->getText();
                    if (text != f.lastSyncedText) {
                        if (f.key == "metadata_name") {
                            world->metadata_pool[e].name = text;
                            f.lastSyncedText = text;
                        }
                    }
                }
            }
        }

        // Updates the inspector fields to match the world (e.g. after a manual drag)
        void syncFromWorld() {
            if (!world || targetEntity == (Entity)-1) return;
            Entity e = targetEntity;
            for (auto& f : fields) {
                if (f.widget->getType() == "SpinBox") {
                    auto* sb = static_cast<Gui::SpinBox*>(f.widget.get());
                    // Don't overwrite the field if the user is currently typing in it
                    if (!sb->isActive()) {
                        float worldVal = 0.0f;
                        if (f.key == "pos_x") worldVal = world->position_pool[e].x;
                        else if (f.key == "pos_y") worldVal = world->position_pool[e].y;
                        else if (f.key == "rect_w") worldVal = world->rectangle_shape_pool[e].w;
                        else if (f.key == "rect_h") worldVal = world->rectangle_shape_pool[e].h;
                        else if (f.key == "z_index") worldVal = (float)world->z_index_pool[e].z;
                        else if (f.key == "vel_x") worldVal = world->velocity_pool[e].x;
                        else if (f.key == "vel_y") worldVal = world->velocity_pool[e].y;
                        else if (f.key == "acc_x") worldVal = world->acceleration_pool[e].x;
                        else if (f.key == "acc_y") worldVal = world->acceleration_pool[e].y;

                        if (worldVal != f.lastSyncedValue) {
                            sb->setValue(worldVal);
                            f.lastSyncedValue = worldVal;
                        }
                    }
                } else if (f.widget->getType() == "LineEdit") {
                    auto* le = static_cast<Gui::LineEdit*>(f.widget.get());
                    if (!le->isActive()) {
                        std::string worldText = "";
                        if (f.key == "metadata_name") worldText = world->metadata_pool[e].name;
                        
                        if (worldText != f.lastSyncedText) {
                            le->clear();
                            for (char c : worldText) {
                                le->appendText(std::string(1, c));
                            }
                            f.lastSyncedText = worldText;
                        }
                    }
                }
            }
        }

    private:
        struct Field {
            std::string label;
            std::string key;
            std::unique_ptr<Gui::IGuiElement> widget;
            float lastSyncedValue = 0.0f;      // Tracks the last value we read/wrote
            std::string lastSyncedText = "";
        };

        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_Window* window;
        ECSWorld* world = nullptr;
        Entity targetEntity = (Entity)-1;
        std::vector<Field> fields;
        Gui::Scrollbar inspectorScrollbar;
        float inspectorScrollOffset = 0.0f;

        void rebuildFields() {
            fields.clear();
            if (!world || targetEntity == (Entity)-1) return;
            Entity e = targetEntity;

            auto addSpinBox = [&](const std::string& label, const std::string& key, float val,
                                float min=0.0f, float max=9999.0f, float step=0.5f) {
                auto sb = std::make_unique<Gui::SpinBox>(
                    renderer, textEngine, font, SDL_FRect{0,0,1,1}, min, max, val, step);
                Field f;
                f.label = label;
                f.key = key;
                f.widget = std::move(sb);
                f.lastSyncedValue = val; // Initialize sync tracker
                fields.push_back(std::move(f));
            };

            auto addLineEdit = [&](const std::string & label, const std::string & key, const std::string & val) {
                auto le = std::make_unique<Gui::LineEdit>(
                    renderer, textEngine, font, SDL_FRect{0,0,1,1}, "Type here...");
                for (char c : val) {
                    le->appendText(std::string(1, c));
                }
                Field f;
                f.label = label;
                f.key = key;
                f.widget = std::move(le);
                f.lastSyncedText = val; // Initialize sync tracker
                fields.push_back(std::move(f));
            };
            
            if (world->has_metadata[e])
                addLineEdit("Name", "metadata_name", world->metadata_pool[e].name);

            if (world->has_position[e]) {
                addSpinBox("X", "pos_x", world->position_pool[e].x, -9999.0f, 9999.0f);
                addSpinBox("Y", "pos_y", world->position_pool[e].y, -9999.0f, 9999.0f);
            }

            if (world->has_rectangle_shape[e]) {
                addSpinBox("Width",  "rect_w", world->rectangle_shape_pool[e].w, 1.0f, 9999.0f);
                addSpinBox("Height", "rect_h", world->rectangle_shape_pool[e].h, 1.0f, 9999.0f);
            }

            if (world->has_z_index[e])
                addSpinBox("Z-Index", "z_index", (float)world->z_index_pool[e].z, 0, 1000, 1.0f);

            if (world->has_velocity[e]) {
                addSpinBox("Vel X", "vel_x", world->velocity_pool[e].x, -9999.0f, 9999.0f);
                addSpinBox("Vel Y", "vel_y", world->velocity_pool[e].y, -9999.0f, 9999.0f);
            }

            if (world->has_acceleration[e]) {
                addSpinBox("Acc X", "acc_x", world->acceleration_pool[e].x, -9999.0f, 9999.0f);
                addSpinBox("Acc Y", "acc_y", world->acceleration_pool[e].y, -9999.0f, 9999.0f);
            }
        }

        void drawEmptyPanel(float windowHeight) {
            SDL_FRect bg = { panelX(), 0, PANEL_W, windowHeight };
            SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245);
            SDL_RenderFillRect(renderer, &bg);
            SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255);
            SDL_RenderRect(renderer, &bg);
            drawLabel("Entity Inspector", panelX() + 10, 10, {180,180,200,255});
            drawLabel("Click an entity", panelX() + 10, 40, {100,100,120,255});
        }

        void drawLabel(const char* s, float x, float y, SDL_Color c) {
            TTF_Text* t = TTF_CreateText(textEngine, font, s, 0);
            if (!t) return;
            TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
            TTF_DrawRendererText(t, x, y);
            TTF_DestroyText(t);
        }
    };

    class TextEditor : public IGuiElement {
    public:
        TextEditor(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_FRect rct)
            : renderer(r), textEngine(te), font(f), rect(rct) {
            lines.push_back("");
            verticalScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
            horizontalScrollbar.setOrientation(Gui::ScrollOrientation::Horizontal);
            // Set callbacks to update editor scroll when user drags thumb
            verticalScrollbar.onChange = [this](float v){ scrollY = v; };
            horizontalScrollbar.onChange = [this](float v){ scrollX = v; };
            refreshScrollbarGeometry();
        }

        ~TextEditor() = default;

        // IGuiElement overrides
        std::string getType() const override { return "TextEditor"; }
        float getX() const override { return rect.x; }
        float getY() const override { return rect.y; }
        float getWidth() const override { return rect.w; }
        float getHeight() const override { return rect.h; }
        void setRect(SDL_FRect r) override { rect = r; refreshScrollbarGeometry(); }
        void setPos(SDL_Point p) override { rect.x = (float)p.x; rect.y = (float)p.y; refreshScrollbarGeometry(); }

    private:
        int tabWidth = 4;

    public:
        void setTabWidth(int w) { tabWidth = std::max(1, w); }
        int getTabWidth() const { return tabWidth; }

        bool handleEvent(const SDL_Event& e, SDL_Window* window, float offsetX, float offsetY) override {
            if (!visible) return false;
            // Handle scrollbars first
            float origVX = verticalScrollbar.trackX, origVY = verticalScrollbar.trackY;
            float origHX = horizontalScrollbar.trackX, origHY = horizontalScrollbar.trackY;
            verticalScrollbar.trackX -= offsetX; verticalScrollbar.trackY -= offsetY;
            horizontalScrollbar.trackX -= offsetX; horizontalScrollbar.trackY -= offsetY;
            bool sbConsumed = false;
            if (verticalScrollbar.handleEvent(e)) sbConsumed = true;
            if (horizontalScrollbar.handleEvent(e)) sbConsumed = true;
            verticalScrollbar.trackX = origVX; verticalScrollbar.trackY = origVY;
            horizontalScrollbar.trackX = origHX; horizontalScrollbar.trackY = origHY;
            if (sbConsumed) {
                updateScrollbars();
                return true;
            }            
            // Keyboard input
            if (e.type == SDL_EVENT_KEY_DOWN) {
                bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
                bool ctrl  = (e.key.mod & SDL_KMOD_CTRL) != 0;
                if (ctrl && e.key.key == SDLK_A) {
                    selAnchorRow = 0; selAnchorCol = 0;
                    cursorRow = (int)lines.size() - 1;
                    cursorCol = getLineLength(cursorRow);
                    hasSelection = true;
                    return true;
                }
                if (ctrl && e.key.key == SDLK_C) {
                    if (hasSelection) {
                        auto [sRow, sCol, eRow, eCol] = normalizedSelection();
                        std::string selectedText = "";
                        if (sRow == eRow) {
                            selectedText = lines[sRow].substr(sCol, eCol - sCol);
                        } else {
                            selectedText = lines[sRow].substr(sCol) + "\n";
                            for (int r = sRow + 1; r < eRow; ++r) {
                                selectedText += lines[r] + "\n";
                            }
                            selectedText += lines[eRow].substr(0, eCol);
                        }
                        SDL_SetClipboardText(selectedText.c_str());
                    }
                    return true;
                }

                // ── Clipboard: Cut (Ctrl+X) ───────────────────────────────
                if (ctrl && e.key.key == SDLK_X) {
                    if (hasSelection) {
                        auto [sRow, sCol, eRow, eCol] = normalizedSelection();
                        std::string selectedText = "";
                        if (sRow == eRow) {
                            selectedText = lines[sRow].substr(sCol, eCol - sCol);
                        } else {
                            selectedText = lines[sRow].substr(sCol) + "\n";
                            for (int r = sRow + 1; r < eRow; ++r) {
                                selectedText += lines[r] + "\n";
                            }
                            selectedText += lines[eRow].substr(0, eCol);
                        }
                        SDL_SetClipboardText(selectedText.c_str());
                        deleteSelection();
                        updateScrollbars();
                    }
                    return true;
                }

                // ── Clipboard: Paste (Ctrl+V) ─────────────────────────────
                if (ctrl && e.key.key == SDLK_V) {
                    char* clipText = SDL_GetClipboardText();
                    if (clipText) {
                        std::string text(clipText);
                        SDL_free(clipText); // SDL3 requires manual freeing of clipboard strings
                        
                        if (hasSelection) deleteSelection();
                        
                        for (char c : text) {
                            if (c == '\n') {
                                newline();
                            } else if (c == '\r') {
                                continue; // Skip Windows carriage returns
                            } else if (c == '\t') {
                                insertChar('\t');
                            } else if (c >= 32 && c < 127) {
                                insertChar(c);
                            }
                        }
                        updateScrollbars();
                    }
                    return true;
                }
                switch (e.key.key) {
                    case SDLK_LEFT:  moveCursor(cursorRow, cursorCol - 1, shift); updateScrollbars(); return true;
                    case SDLK_RIGHT: moveCursor(cursorRow, cursorCol + 1, shift); updateScrollbars(); return true;
                    case SDLK_UP:    moveCursor(cursorRow - 1, cursorCol, shift); updateScrollbars(); return true;
                    case SDLK_DOWN:  moveCursor(cursorRow + 1, cursorCol, shift); updateScrollbars(); return true;
                    case SDLK_HOME:  moveCursor(cursorRow, 0, shift); updateScrollbars(); return true;
                    case SDLK_END:   moveCursor(cursorRow, getLineLength(cursorRow), shift); updateScrollbars(); return true;
                    case SDLK_PAGEUP: {
                        int rows = (int)(rect.h / LINE_HEIGHT) - 1;
                        moveCursor(cursorRow - rows, cursorCol, shift);
                        updateScrollbars();
                        return true;
                    }
                    case SDLK_PAGEDOWN: {
                        int rows = (int)(rect.h / LINE_HEIGHT) - 1;
                        moveCursor(cursorRow + rows, cursorCol, shift);
                        updateScrollbars();
                        return true;
                    }
                    case SDLK_BACKSPACE: backspace(); updateScrollbars(); return true;
                    case SDLK_DELETE:    deleteChar(); updateScrollbars(); return true;
                    case SDLK_RETURN:    newline(); updateScrollbars(); return true;
                    case SDLK_TAB:       insertChar('\t'); updateScrollbars(); return true;
                    case SDLK_ESCAPE:
                        // Close via escape key instead of button
                        if (onClose) onClose();
                        return true;
                    default:
                        break;
                }
            }
            // Text input (characters)
            if (e.type == SDL_EVENT_TEXT_INPUT) {
                const char* text = e.text.text;
                for (int i = 0; text[i]; ++i) {
                    if (text[i] >= 32 && text[i] < 127) { // printable ASCII
                        insertChar(text[i]);
                    }
                }
                updateScrollbars();
                return true;
            }
            // Mouse wheel inside the editor area
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                float mx, my;
                SDL_GetMouseState(&mx, &my);
                SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
                if (mx >= shiftedRect.x && mx <= shiftedRect.x + shiftedRect.w &&
                    my >= shiftedRect.y && my <= shiftedRect.y + shiftedRect.h) {
                    float delta = (e.wheel.y > 0) ? -LINE_HEIGHT * 3 : LINE_HEIGHT * 3;
                    scrollY = std::clamp(scrollY + delta, 0.0f, verticalScrollbar.maxOffset());
                    if (e.wheel.x != 0) {
                        scrollX = std::clamp(scrollX + e.wheel.x * CHAR_WIDTH * 3, 0.0f, horizontalScrollbar.maxOffset());
                    }
                    updateScrollbars();
                    return true;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                float mx = e.button.x, my = e.button.y;
                SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - offsetY, rect.w - 12, rect.h - 12 };
                if (mx >= shiftedRect.x && mx <= shiftedRect.x + shiftedRect.w &&
                    my >= shiftedRect.y && my <= shiftedRect.y + shiftedRect.h) {
                    // Start text input so SDL generates SDL_EVENT_TEXT_INPUT events
                    SDL_StartTextInput(window);
                    int row, col;
                    hitTestRowCol(mx, my, shiftedRect, row, col);
                    bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                    moveCursor(row, col, shift);
                    mouseSelecting = true;
                    updateScrollbars();
                    return true;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION && mouseSelecting) {
                float mx = e.motion.x, my = e.motion.y;
                SDL_FRect shiftedRect = { rect.x - offsetX, rect.y - offsetY, rect.w - 12, rect.h - 12 };
                int row, col;
                hitTestRowCol(mx, my, shiftedRect, row, col);
                moveCursor(row, col, true);
                updateScrollbars();
                return true;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
                mouseSelecting = false;
            }
            return false;
        }

        // Maps a screen point to a (row, col), using real font metrics
        // (xToCol) rather than a fixed per-character width.
        void hitTestRowCol(float mx, float my, SDL_FRect shiftedRect, int& row, int& col) const {
            float localX = mx - shiftedRect.x + scrollX - PADDING_X;
            float localY = my - shiftedRect.y + scrollY - PADDING_Y;
            row = (int)(localY / LINE_HEIGHT);
            if (row < 0) row = 0;
            if (row >= (int)lines.size()) row = (int)lines.size() - 1;
            col = xToCol(lines[row], localX);
        }

        void handleGamepad(float cursorX, float cursorY, float offsetX, float offsetY,
                               SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            if (!visible) return;
            // We don't use cursor position; we rely on keyboard mapping.
            // However, if the user has a gamepad, they can use the stick to move the cursor.
            // This is a simplification – we'll just pass through to keyboard events.
            // For full integration, we'd need to map gamepad buttons to actions.
            // For now, we only support keyboard.
            (void)cursorX; (void)cursorY; (void)offsetX; (void)offsetY; (void)window;
            (void)confirmDown; (void)confirmDownLastFrame;
        }

        
        void render(float offsetX, float offsetY) override {
            if (!visible) return;

            // Background
            SDL_SetRenderDrawColor(renderer, 32, 32, 42, 255);
            SDL_FRect bgRect = { rect.x - offsetX, rect.y - offsetY, rect.w, rect.h };
            SDL_RenderFillRect(renderer, &bgRect);
            SDL_SetRenderDrawColor(renderer, 70, 70, 90, 255);
            SDL_RenderRect(renderer, &bgRect);

            // Clip
            SDL_Rect clip = { (int)bgRect.x, (int)bgRect.y, (int)bgRect.w, (int)bgRect.h };
            SDL_SetRenderClipRect(renderer, &clip);

            // Draw text and cursor
            renderText(offsetX, offsetY);
            renderCursor(offsetX, offsetY);

            // Stop clipping
            SDL_SetRenderClipRect(renderer, nullptr);

            // Draw scrollbars ONCE, passing the offsets
            verticalScrollbar.render(renderer, offsetX, offsetY);
            horizontalScrollbar.render(renderer, offsetX, offsetY);
        }

        // File operations
        void loadFile(const std::string& path) {
            std::ifstream file(path);
            if (!file.is_open()) {
                SDL_Log("TextEditor: failed to open %s", path.c_str());
                return;
            }
            lines.clear();
            std::string line;
            while (std::getline(file, line)) {
                std::string converted;
                converted.reserve(line.size() * tabWidth); // rough reserve
                for (char c : line) {
                    if (c == '\t') {
                        converted.append(tabWidth, ' ');
                    } else {
                        converted += c;
                    }
                }
                lines.push_back(converted);
            }
            filePath = path;
            cursorRow = 0;
            cursorCol = 0;
            scrollX = 0.0f;
            scrollY = 0.0f;
            hasSelection = false;
            mouseSelecting = false;
            refreshScrollbarGeometry();
            updateScrollbars();
        }

        void saveFile() {
            if (filePath.empty()) return;
            std::ofstream file(filePath);
            if (!file.is_open()) {
                SDL_Log("TextEditor: failed to save %s", filePath.c_str());
                return;
            }
            for (size_t i = 0; i < lines.size(); ++i) {
                file << lines[i];
                if (i + 1 < lines.size()) file << '\n';
            }
            SDL_Log("TextEditor: saved to %s", filePath.c_str());
        }

        bool isFileLoaded() const { return !filePath.empty(); }
        const std::string& getFilePath() const { return filePath; }

        // Visibility
        void setVisible(bool v) { visible = v; }
        bool isVisible() const { return visible; }

        // Close callback (called when user clicks close button)
        std::function<void()> onClose;
        

    private:
        SDL_Renderer* renderer;
        TTF_TextEngine* textEngine;
        TTF_Font* font;
        SDL_FRect rect;
        bool visible = true;

        std::string filePath;
        std::vector<std::string> lines;
        int cursorRow = 0, cursorCol = 0;
        float scrollX = 0.0f, scrollY = 0.0f;

        // Selection: anchor is where a drag or shift-select started; the
        // cursor is the live end. No selection while hasSelection is false.
        int selAnchorRow = 0, selAnchorCol = 0;
        bool hasSelection = false;
        bool mouseSelecting = false;

        Gui::Scrollbar verticalScrollbar;
        Gui::Scrollbar horizontalScrollbar;

        static constexpr float LINE_HEIGHT = 25.0f;
        static constexpr float CHAR_WIDTH = 25.0f;
        static constexpr float PADDING_X = 6.0f;
        static constexpr float PADDING_Y = 6.0f;
        // Helper methods
        float getLineHeight() const {
            if (!font) return 20.0f;
            return (float)TTF_GetFontHeight(font);
        }

        void refreshScrollbarGeometry() {
            // Compute max line width using actual font
            float maxLineWidth = 0.0f;
            for (const auto& line : lines) {
                int w = 0, h = 0;
                if (!line.empty() && font) {
                    TTF_GetStringSize(font, line.c_str(), line.size(), &w, &h);
                }
                maxLineWidth = std::max(maxLineWidth, (float)w);
            }
            float contentW = maxLineWidth + 2 * PADDING_X;
            float contentH = lines.size() * getLineHeight() + 2 * PADDING_Y; // use dynamic line height
            float viewW = rect.w - 12.0f;
            float viewH = rect.h - 12.0f;

            verticalScrollbar.setGeometry(rect.x + rect.w - 12, rect.y, 12, rect.h,
                                        contentH, viewH);
            horizontalScrollbar.setGeometry(rect.x, rect.y + rect.h - 12, rect.w - 12, 12,
                                            contentW, viewW);

            // Sync offsets without callback
            verticalScrollbar.setOffsetNoCallback(scrollY);
            horizontalScrollbar.setOffsetNoCallback(scrollX);
        }

        void renderText(float offsetX, float offsetY) {
            // Clip to visible area
            float viewX = rect.x - offsetX + PADDING_X;
            float viewY = rect.y - offsetY + PADDING_Y;
            float viewW = rect.w - 12.0f - PADDING_X * 2;
            float viewH = rect.h - 12.0f - PADDING_Y * 2;

            // Start rendering from the line that fits the scroll
            int startLine = (int)(scrollY / LINE_HEIGHT);
            float yOffset = viewY - scrollY + startLine * LINE_HEIGHT;

            int selStartRow = 0, selStartCol = 0, selEndRow = 0, selEndCol = 0;
            if (hasSelection) std::tie(selStartRow, selStartCol, selEndRow, selEndCol) = normalizedSelection();

            for (size_t i = startLine; i < lines.size(); ++i) {
                float y = yOffset + (i - startLine) * LINE_HEIGHT;
                if (y > viewY + viewH) break;
                const std::string& line = lines[i];
                float x = viewX - scrollX;

                // Selection highlight behind the text for this line
                if (hasSelection && (int)i >= selStartRow && (int)i <= selEndRow) {
                    int fromCol = ((int)i == selStartRow) ? selStartCol : 0;
                    int toCol   = ((int)i == selEndRow)   ? selEndCol   : (int)line.size();
                    float hx0 = x + textWidthUpTo(line, fromCol);
                    // Selecting past end-of-line (multi-line selection) shows
                    // a little highlight past the last character, like most editors.
                    float hx1 = x + textWidthUpTo(line, toCol);
                    if (toCol >= (int)line.size() && (int)i < selEndRow) hx1 += CHAR_WIDTH * 0.5f;
                    if (hx1 > hx0) {
                        SDL_FRect selRect = { hx0, y, hx1 - hx0, LINE_HEIGHT };
                        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(renderer, 80, 120, 220, 110);
                        SDL_RenderFillRect(renderer, &selRect);
                    }
                }

                // Syntax-highlighted text, one draw call per token so each
                // can have its own color (TTF_Text only supports one color
                // per object).
                for (const auto& tok : tokenizeLine(line)) {
                    std::string sub = line.substr(tok.start, tok.len);
                    if (sub.empty()) continue;
                    float tx = x + textWidthUpTo(line, (int)tok.start);
                    TTF_Text* t = TTF_CreateText(textEngine, font, sub.c_str(), 0);
                    if (t) {
                        TTF_SetTextColor(t, tok.color.r, tok.color.g, tok.color.b, tok.color.a);
                        TTF_DrawRendererText(t, tx, y);
                        TTF_DestroyText(t);
                    }
                }
            }
        }

        void renderCursor(float offsetX, float offsetY) {
            SDL_FPoint pos = getCursorPixelPos(offsetX, offsetY);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_FRect cursorRect = { pos.x, pos.y, 2.0f, LINE_HEIGHT };
            SDL_RenderFillRect(renderer, &cursorRect);
        }


        void moveCursor(int row, int col, bool extend = false) {
            if (row < 0) row = 0;
            if (row >= (int)lines.size()) row = (int)lines.size() - 1;
            int maxCol = getLineLength(row);
            if (col < 0) col = 0;
            if (col > maxCol) col = maxCol;
            if (extend) {
                if (!hasSelection) { selAnchorRow = cursorRow; selAnchorCol = cursorCol; hasSelection = true; }
            } else {
                hasSelection = false;
            }
            cursorRow = row;
            cursorCol = col;
            if (extend && selAnchorRow == cursorRow && selAnchorCol == cursorCol) hasSelection = false;
            // Ensure cursor is visible
            SDL_FPoint pos = getCursorPixelPos(0.0f, 0.0f);
            float viewX = rect.x + PADDING_X;
            float viewY = rect.y + PADDING_Y;
            float viewW = rect.w - 12.0f - PADDING_X * 2;
            float viewH = rect.h - 12.0f - PADDING_Y * 2;
            if (pos.x < viewX) scrollX = std::max(0.0f, scrollX - (viewX - pos.x));
            else if (pos.x + CHAR_WIDTH > viewX + viewW) scrollX += (pos.x + CHAR_WIDTH - viewX - viewW);
            if (pos.y < viewY) scrollY = std::max(0.0f, scrollY - (viewY - pos.y));
            else if (pos.y + LINE_HEIGHT > viewY + viewH) scrollY += (pos.y + LINE_HEIGHT - viewY - viewH);
            // Clamp scroll to max
            scrollX = std::clamp(scrollX, 0.0f, horizontalScrollbar.maxOffset());
            scrollY = std::clamp(scrollY, 0.0f, verticalScrollbar.maxOffset());
            horizontalScrollbar.offset = scrollX;
            verticalScrollbar.offset = scrollY;
        }
        

        void insertChar(char ch) {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            if (hasSelection) deleteSelection();
            std::string& line = lines[cursorRow];
            if (cursorCol < 0) cursorCol = 0;
            if (cursorCol > (int)line.size()) cursorCol = (int)line.size();

            if (ch == '\t') {
                line.insert(cursorCol, tabWidth, ' ');
                cursorCol += tabWidth;
            } else {
                line.insert(cursorCol, 1, ch);
                cursorCol++;
            }
        }

        void deleteChar() {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            if (hasSelection) { deleteSelection(); return; }
            std::string& line = lines[cursorRow];
            if (cursorCol < (int)line.size()) {
                line.erase(cursorCol, 1);
            } else if (cursorRow + 1 < (int)lines.size()) {
                // Join with next line
                std::string& next = lines[cursorRow + 1];
                line += next;
                lines.erase(lines.begin() + cursorRow + 1);
            }
        }

        void backspace() {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            if (hasSelection) { deleteSelection(); return; }
            if (cursorCol == 0 && cursorRow > 0) {
                // Merge with previous line
                std::string& prev = lines[cursorRow - 1];
                std::string& curr = lines[cursorRow];
                prev += curr;
                lines.erase(lines.begin() + cursorRow);
                cursorRow--;
                cursorCol = (int)prev.size();
            } else if (cursorCol > 0) {
                std::string& line = lines[cursorRow];
                line.erase(cursorCol - 1, 1);
                cursorCol--;
            }
        }

        void newline() {
            if (cursorRow < 0 || cursorRow >= (int)lines.size()) return;
            if (hasSelection) deleteSelection();
            std::string& line = lines[cursorRow];
            std::string rest = line.substr(cursorCol);
            line.erase(cursorCol);
            lines.insert(lines.begin() + cursorRow + 1, rest);
            cursorRow++;
            cursorCol = 0;
        }
        

        void updateScrollbars() {
            // Use setOffsetNoCallback to avoid triggering onChange
            horizontalScrollbar.setOffsetNoCallback(scrollX);
            verticalScrollbar.setOffsetNoCallback(scrollY);
        }

        SDL_FPoint getCursorPixelPos(float offsetX, float offsetY) const {
            float x = rect.x - offsetX + PADDING_X - scrollX + textWidthUpTo(lines[cursorRow], cursorCol);
            float y = rect.y - offsetY + PADDING_Y - scrollY + cursorRow * LINE_HEIGHT;
            return {x, y};
        }

        // Real pixel width of the first `col` characters of `line`, measured
        // with the actual font metrics. The UI font isn't monospace, so a
        // fixed per-character width (the old CHAR_WIDTH-based math) drifts
        // from the real glyph positions more with every character — this is
        // what caused the cursor/clicks to land in the wrong place.
        float textWidthUpTo(const std::string& line, int col) const {
            if (col <= 0 || line.empty() || !font) return 0.0f;
            std::string sub = line.substr(0, std::min((size_t)col, line.size()));
            int w = 0, h = 0;
            if (!sub.empty()) TTF_GetStringSize(font, sub.c_str(), sub.size(), &w, &h);
            return (float)w;
        }

        // Inverse of textWidthUpTo: given a local pixel X (relative to the
        // start of the line's text), returns the column whose character
        // boundary is closest to that X. Used for click-to-caret mapping.
        int xToCol(const std::string& line, float localX) const {
            if (localX <= 0.0f || line.empty()) return 0;
            float prevW = 0.0f;
            for (size_t i = 1; i <= line.size(); ++i) {
                float w = textWidthUpTo(line, (int)i);
                if (localX < (prevW + w) * 0.5f) return (int)i - 1;
                prevW = w;
            }
            return (int)line.size();
        }

        // Returns the selection as (startRow, startCol, endRow, endCol) with
        // start always before (or equal to) end, regardless of which
        // direction the user dragged/shift-selected in.
        std::tuple<int,int,int,int> normalizedSelection() const {
            if (selAnchorRow < cursorRow || (selAnchorRow == cursorRow && selAnchorCol <= cursorCol))
                return {selAnchorRow, selAnchorCol, cursorRow, cursorCol};
            return {cursorRow, cursorCol, selAnchorRow, selAnchorCol};
        }

        // Removes the selected text (if any), collapsing the cursor to
        // where the selection started. Called before typing/backspace/
        // delete/newline whenever hasSelection is true, so those actions
        // replace the selection instead of acting next to it.
        void deleteSelection() {
            if (!hasSelection) return;
            auto [sRow, sCol, eRow, eCol] = normalizedSelection();
            if (sRow == eRow) {
                lines[sRow].erase((size_t)sCol, (size_t)(eCol - sCol));
            } else {
                std::string tail = lines[eRow].substr((size_t)eCol);
                lines[sRow].erase((size_t)sCol);
                lines[sRow] += tail;
                lines.erase(lines.begin() + sRow + 1, lines.begin() + eRow + 1);
            }
            cursorRow = sRow; cursorCol = sCol;
            hasSelection = false;
        }

        // Minimal C++ token classifier for syntax highlighting: comments,
        // preprocessor directives, string literals, numbers, and a common
        // keyword list. Not a real parser — good enough to make code
        // readable, not to validate it.
        struct Token { size_t start, len; SDL_Color color; };
        std::vector<Token> tokenizeLine(const std::string& line) const {
            static const std::vector<std::string> keywords = {
                "if","else","for","while","do","return","class","struct","public","private","protected",
                "void","int","float","double","bool","char","const","static","virtual","override","auto",
                "namespace","using","new","delete","true","false","nullptr","template","typename","this",
                "break","continue","switch","case","default","enum","unsigned","long","short","inline",
                "std","string","vector","include","define","ifdef","ifndef","endif","pragma"
            };
            std::vector<Token> tokens;
            const SDL_Color normalColor  = {220, 220, 240, 255};
            const SDL_Color keywordColor = {110, 160, 230, 255};
            const SDL_Color commentColor = {90, 140, 90, 255};
            const SDL_Color stringColor  = {210, 150, 90, 255};
            const SDL_Color numberColor  = {180, 210, 140, 255};
            const SDL_Color ppColor      = {190, 140, 220, 255};

            size_t i = 0;
            while (i < line.size()) {
                if (i + 1 < line.size() && line[i] == '/' && line[i+1] == '/') {
                    tokens.push_back({i, line.size() - i, commentColor});
                    break;
                }
                if (line[i] == '#') {
                    size_t j = i + 1;
                    while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
                    tokens.push_back({i, j - i, ppColor});
                    i = j; continue;
                }
                if (line[i] == '"') {
                    size_t j = i + 1;
                    while (j < line.size() && line[j] != '"') {
                        if (line[j] == '\\' && j + 1 < line.size()) j++;
                        j++;
                    }
                    if (j < line.size()) j++; // include closing quote
                    tokens.push_back({i, j - i, stringColor});
                    i = j; continue;
                }
                if (isalpha((unsigned char)line[i]) || line[i] == '_') {
                    size_t j = i;
                    while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
                    std::string word = line.substr(i, j - i);
                    bool isKeyword = std::find(keywords.begin(), keywords.end(), word) != keywords.end();
                    tokens.push_back({i, j - i, isKeyword ? keywordColor : normalColor});
                    i = j; continue;
                }
                if (isdigit((unsigned char)line[i])) {
                    size_t j = i;
                    while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '.')) j++;
                    tokens.push_back({i, j - i, numberColor});
                    i = j; continue;
                }
                tokens.push_back({i, 1, normalColor});
                i++;
            }
            return tokens;
        }
        int getLineLength(int row) const {
            if (row < 0 || row >= (int)lines.size()) return 0;
            return (int)lines[row].size();
        }
    };

    class FileExplorer : public Dialog {
    public:
        FileExplorer(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f,
            SDL_Window* w, const std::string& rootPath, const std::string& filter)
        : Dialog(r, te, f, w, {0,0,600,500}, "Open File", "Open", "Cancel"),
        currentPath(rootPath.empty() ? std::filesystem::current_path().string() : rootPath),
        rootPath(rootPath.empty() ? std::filesystem::current_path().string() : rootPath),
        filter(filter),
        pathEdit(r, te, f, {0,0,1,1}, "Path..."),
        renameEdit(r, te, f, {0,0,1,1}, ""), // <--- ADD THIS
        renderer(r), font(f), textEngine(te)
    {
            refreshEntries();
            loadIcons();
        }

        ~FileExplorer() {
            if (folderIcon)  SDL_DestroyTexture(folderIcon);
            if (cppIcon)     SDL_DestroyTexture(cppIcon);
            if (hIcon)       SDL_DestroyTexture(hIcon);
            if (sceneIcon)   SDL_DestroyTexture(sceneIcon);
            if (textIcon)    SDL_DestroyTexture(textIcon);
        }

        void setCallback(std::function<void(const std::string&)> cb) { callback = cb; }
        void setFilter(const std::string& f) { filter = f; }

        void goToRoot() {
            currentPath = rootPath;
            refreshEntries();
        }

    private:
        SDL_FRect getItemContextMenuRect() const {
            float w = 160.0f, h = 28.0f * itemContextMenuItems.size() + 6.0f;
            float x = itemContextMenuX, y = itemContextMenuY;
            SDL_FRect win = animRect();
            if (x + w > win.x + win.w) x = win.x + win.w - w - 10;
            if (y + h > win.y + win.h) y = win.y + win.h - h - 10;
            if (x < win.x + 10) x = win.x + 10;
            if (y < win.y + 70) y = win.y + 70;
            return { x, y, w, h };
        }

        void commitRename() {
            std::string newName = renameEdit.getText();
            if (renameIndex >= 0 && renameIndex < (int)entries.size()) {
                std::string oldName = entries[renameIndex];
                
                if (!newName.empty() && newName != oldName) {
                    std::filesystem::path oldPath = std::filesystem::path(currentPath) / oldName;
                    std::filesystem::path newPath = std::filesystem::path(currentPath) / newName;
                    
                    if (!std::filesystem::exists(newPath)) {
                        std::error_code ec;
                        std::filesystem::rename(oldPath, newPath, ec);
                        if (ec) {
                            SDL_Log("Failed to rename '%s' to '%s': %s", oldName.c_str(), newName.c_str(), ec.message().c_str());
                        } else {
                            SDL_Log("Renamed '%s' to '%s'", oldName.c_str(), newName.c_str());
                        }
                    } else {
                        SDL_Log("Failed to rename: '%s' already exists.", newName.c_str());
                    }
                }
            }
            
            isRenaming = false;
            renameEdit.deactivate(window);
            refreshEntries();
            
            // Try to re-select the renamed item in the list
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == newName) {
                    selectedIndex = (int)i;
                    break;
                }
            }
        }

    protected:
        void onOpen() override {
            int winW, winH;
            SDL_GetWindowSize(window, &winW, &winH);
            logicalRect = {
                (winW - 600.0f) * 0.5f,
                (winH - 400.0f) * 0.5f,
                600.0f, 400.0f
            };
            pathEdit.setPlaceholder("Path...");
            selectedIndex = -1;
            selectedFilePath.clear();
            goToRoot();
            loadIcons();
        }

        bool onHandleEvent(const SDL_Event& ev) override {
            if (showDeleteConfirmation) {
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    float mx = ev.button.x, my = ev.button.y;
                    SDL_FRect win = animRect();
                    float boxW = 300.0f, boxH = 120.0f;
                    float boxX = win.x + (win.w - boxW) * 0.5f;
                    float boxY = win.y + (win.h - boxH) * 0.5f;
                    SDL_FRect yesBtn = { boxX + 20.0f, boxY + boxH - 50.0f, 120.0f, 36.0f };
                    SDL_FRect noBtn  = { boxX + boxW - 140.0f, boxY + boxH - 50.0f, 120.0f, 36.0f };
                    
                    if (mx >= yesBtn.x && mx <= yesBtn.x + yesBtn.w && my >= yesBtn.y && my <= yesBtn.y + yesBtn.h) {
                        if (deleteIndex >= 0 && deleteIndex < (int)entries.size()) {
                            std::string pathToDelete = (std::filesystem::path(currentPath) / entries[deleteIndex]).string();
                            std::error_code ec;
                            // remove_all works for both files and directories
                            std::filesystem::remove_all(pathToDelete, ec);
                            if (ec) SDL_Log("Failed to delete '%s': %s", pathToDelete.c_str(), ec.message().c_str());
                            else    SDL_Log("Deleted '%s'", pathToDelete.c_str());
                        }
                        showDeleteConfirmation = false;
                        refreshEntries();
                        return true;
                    }
                    if (mx >= noBtn.x && mx <= noBtn.x + noBtn.w && my >= noBtn.y && my <= noBtn.y + noBtn.h) {
                        showDeleteConfirmation = false;
                        return true;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_ESCAPE) { showDeleteConfirmation = false; return true; }
                }
                return true; // Consume all other events while confirming
            }
            // --- NEW: Handle Active Rename Mode ---
            if (isRenaming) {
                bool wasActive = renameEdit.isActive();
                renameEdit.handleEvent(ev, window, 0.0f, 0.0f);
                
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_RETURN) {
                        commitRename();
                        return true;
                    }
                    if (ev.key.key == SDLK_ESCAPE) {
                        isRenaming = false;
                        renameEdit.deactivate(window);
                        return true;
                    }
                }
                
                // If the user clicked outside the LineEdit, it deactivates itself. Commit the rename.
                if (wasActive && !renameEdit.isActive()) {
                    commitRename();
                    return true;
                }
                
                return true; // Consume all events while renaming
            }
            // Context menu handling
            if (showContextMenu) {
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    float mx = ev.button.x, my = ev.button.y;
                    SDL_FRect menuRect = getContextMenuRect();
                    if (mx >= menuRect.x && mx <= menuRect.x + menuRect.w &&
                        my >= menuRect.y && my <= menuRect.y + menuRect.h) {
                        float itemH = 28.0f;
                        int idx = (int)((my - menuRect.y) / itemH);
                        if (idx >= 0 && idx < (int)contextMenuItems.size()) {
                            if (idx == 0) createNewFolder();
                            if (idx == 1) createNewScene();
                            else if (idx == 2) createNewScript();
                            else if (idx == 3) createNewHeader();
                            showContextMenu = false;
                            return true;
                        }
                    } else {
                        showContextMenu = false;
                        return true;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    showContextMenu = false;
                    return true;
                }
                return true;
            }
            if (showItemContextMenu) {
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    float mx = ev.button.x, my = ev.button.y;
                    SDL_FRect menuRect = getItemContextMenuRect();
                    if (mx >= menuRect.x && mx <= menuRect.x + menuRect.w &&
                        my >= menuRect.y && my <= menuRect.y + menuRect.h) {
                        float itemH = 28.0f;
                        int idx = (int)((my - menuRect.y) / itemH);
                        if (idx == 0) { // Rename was clicked
                            isRenaming = true;
                            renameIndex = selectedIndex;
                            renameEdit.clear();
                            for (char c : entries[renameIndex]) renameEdit.appendText(std::string(1, c));
                            renameEdit.setActive(true);
                            SDL_StartTextInput(window);
                        } else if (idx == 1) {
                            showDeleteConfirmation = true;
                            deleteIndex = selectedIndex;
                            showItemContextMenu = false;
                        }
                        showItemContextMenu = false;
                        return true;
                    } else {
                        showItemContextMenu = false;
                        return true;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    showItemContextMenu = false;
                    return true;
                }
                return true;
            }
            // Back button
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect win = animRect();
                SDL_FRect backBtn = { win.x + 10, win.y + 40, 30, 28 };
                if (mx >= backBtn.x && mx <= backBtn.x + backBtn.w &&
                    my >= backBtn.y && my <= backBtn.y + backBtn.h) {
                    goToParent();
                    return true;
                }
            }

            // Path edit
            if (pathEdit.handleEvent(ev, window, 0.0f, 0.0f)) return true;

            // List interactions (left click)
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect win = animRect();
                SDL_FRect listRect = { win.x + 10, win.y + 70, win.w - 20, win.h - 110 };
                if (mx >= listRect.x && mx <= listRect.x + listRect.w &&
                    my >= listRect.y && my <= listRect.y + listRect.h) {
                    float rowHeight = 24.0f;
                    int idx = (int)((my - listRect.y) / rowHeight);
                    if (idx >= 0 && idx < (int)entries.size()) {
                        selectedIndex = idx;
                        if (isDir[idx]) {
                            goToDirectory(entries[idx]);
                        } else if (ev.button.clicks >= 2) {
                            selectedFilePath = (std::filesystem::path(currentPath) / entries[idx]).string();
                            selectFile(entries[idx]);
                        }
                        return true;
                    }
                }
            }

            // Right-click -> context menu
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_RIGHT) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect win = animRect();
                SDL_FRect listRect = { win.x + 10, win.y + 70, win.w - 20, win.h - 110 };
                if (mx >= listRect.x && mx <= listRect.x + listRect.w &&
                    my >= listRect.y && my <= listRect.y + listRect.h) {
                    float rowHeight = 24.0f;
                    int idx = (int)((my - listRect.y) / rowHeight);
                    
                    // --- MODIFIED: Distinguish between item and empty space ---
                    if (idx >= 0 && idx < (int)entries.size()) {
                        // Right-clicked on an item
                        selectedIndex = idx;
                        itemContextMenuX = mx;
                        itemContextMenuY = my;
                        showItemContextMenu = true;
                        showContextMenu = false; // Hide the "New..." menu
                    } else {
                        // Right-clicked on empty space
                        contextMenuX = mx;
                        contextMenuY = my;
                        showContextMenu = true;
                        showItemContextMenu = false; // Hide the item menu
                    }
                    return true;
                }
            }

            // Keyboard navigation
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_RETURN) {
                    if (selectedIndex >= 0 && selectedIndex < (int)entries.size()) {
                        if (isDir[selectedIndex]) {
                            goToDirectory(entries[selectedIndex]);
                        } else {
                            selectedFilePath = (std::filesystem::path(currentPath) / entries[selectedIndex]).string();
                            selectFile(entries[selectedIndex]);
                        }
                        return true;
                    }
                }
                if (ev.key.key == SDLK_BACKSPACE || ev.key.key == SDLK_UP) {
                    goToParent();
                    return true;
                }
                if (ev.key.key == SDLK_DOWN) {
                    selectedIndex = std::min(selectedIndex + 1, (int)entries.size() - 1);
                    return true;
                }
                if (ev.key.key == SDLK_UP) {
                    selectedIndex = std::max(selectedIndex - 1, 0);
                    return true;
                }
            }
            return false;
        }

        void onRender(SDL_FRect win) override {
            float mx, my;
            SDL_GetMouseState(&mx, &my);

            // Back button
            SDL_FRect backBtn = { win.x + 10, win.y + 40, 30, 28 };
            bool hovered = (mx >= backBtn.x && mx <= backBtn.x + backBtn.w &&
                            my >= backBtn.y && my <= backBtn.y + backBtn.h);
            SDL_Color backColor = hovered ? SDL_Color{80,80,80,255} : SDL_Color{60,60,60,255};
            fillRect(backBtn, backColor);
            drawText("<", backBtn.x + 8, backBtn.y + 4, {220,220,220,255});

            // Path bar
            pathEdit.setRect({ win.x + 45, win.y + 40, win.w - 98, 28 });
            pathEdit.render(0.0f, 0.0f);

            // List area
            SDL_FRect listRect = { win.x + 10, win.y + 75, win.w - 20, win.h - 140 };
            SDL_SetRenderDrawColor(renderer, 50, 50, 60, 255);
            SDL_RenderFillRect(renderer, &listRect);
            SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
            SDL_RenderRect(renderer, &listRect);

            SDL_Rect clip = { (int)listRect.x, (int)listRect.y, (int)listRect.w, (int)listRect.h };
            SDL_SetRenderClipRect(renderer, &clip);

            // Draw entries with icons
            float rowHeight = 24.0f;
            float y = listRect.y;
            const float iconSize = 32.0f;
            const float textOffset = iconSize + 6.0f;

            for (size_t i = 0; i < entries.size(); ++i) {
                SDL_Color bg = (i == selectedIndex) ? SDL_Color{70, 70, 120, 255} : SDL_Color{60, 60, 70, 255};
                SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
                SDL_FRect rowRect = { listRect.x, y, listRect.w, rowHeight };
                SDL_RenderFillRect(renderer, &rowRect);

                // Choose icon (texture)
                SDL_Texture* icon = nullptr;
                if (isDir[i]) {
                    icon = folderIcon;
                } else {
                    std::string ext = getFileExtension(entries[i]);
                    if (ext == ".cpp" || ext == ".hpp") icon = cppIcon;
                    else if (ext == ".h") icon = hIcon;
                    else if (ext == ".json") icon = sceneIcon;
                    else if (ext == ".txt" || ext == "")  icon = textIcon;
                }

                if (icon) {
                    SDL_FRect iconRect = { listRect.x + 4, y + (rowHeight - iconSize) * 0.5f, iconSize, iconSize };
                    SDL_RenderTexture(renderer, icon, nullptr, &iconRect);
                } else {
                    drawText(isDir[i] ? "[DIR]" : "[FILE]", listRect.x + 4, y + 2, {200,200,180,255});
                }

                SDL_Color textColor = isDir[i] ? SDL_Color{200,200,180,255} : SDL_Color{220,220,240,255};
                if (isRenaming && i == renameIndex) {
                    SDL_FRect editRect = { listRect.x + textOffset, y + 2, listRect.w - textOffset - 10, rowHeight - 4 };
                    renameEdit.setRect(editRect);
                    renameEdit.render(0.0f, 0.0f);
                } else {
                    SDL_Color textColor = isDir[i] ? SDL_Color{200,200,180,255} : SDL_Color{220,220,240,255};
                    drawText(entries[i].c_str(), listRect.x + textOffset, y + 2, textColor);
                }
                y += rowHeight;
            }

            SDL_SetRenderClipRect(renderer, nullptr);

            // Context menu
            if (showContextMenu) {
                SDL_FRect menuRect = getContextMenuRect();
                SDL_SetRenderDrawColor(renderer, 40, 40, 50, 220);
                SDL_RenderFillRect(renderer, &menuRect);
                SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
                SDL_RenderRect(renderer, &menuRect);

                float itemH = 28.0f;
                float yPos = menuRect.y;
                for (size_t i = 0; i < contextMenuItems.size(); ++i) {
                    SDL_FRect itemRect = { menuRect.x, yPos, menuRect.w, itemH };
                    float mX, mY;
                    SDL_GetMouseState(&mX, &mY);
                    bool hover = (mX >= itemRect.x && mX <= itemRect.x + itemRect.w &&
                                mY >= itemRect.y && mY <= itemRect.y + itemRect.h);
                    if (hover) {
                        SDL_SetRenderDrawColor(renderer, 80, 80, 120, 255);
                        SDL_RenderFillRect(renderer, &itemRect);
                    }
                    drawText(contextMenuItems[i].c_str(), menuRect.x + 8, yPos + 4, {255,255,255,255});
                    yPos += itemH;
                }
            }
            if (showItemContextMenu) {
                SDL_FRect menuRect = getItemContextMenuRect();
                SDL_SetRenderDrawColor(renderer, 40, 40, 50, 220);
                SDL_RenderFillRect(renderer, &menuRect);
                SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
                SDL_RenderRect(renderer, &menuRect);

                float itemH = 28.0f;
                float yPos = menuRect.y;
                for (size_t i = 0; i < itemContextMenuItems.size(); ++i) {
                    SDL_FRect itemRect = { menuRect.x, yPos, menuRect.w, itemH };
                    float mX, mY;
                    SDL_GetMouseState(&mX, &mY);
                    bool hover = (mX >= itemRect.x && mX <= itemRect.x + itemRect.w &&
                                mY >= itemRect.y && mY <= itemRect.y + itemRect.h);
                    if (hover) {
                        SDL_SetRenderDrawColor(renderer, 80, 80, 120, 255);
                        SDL_RenderFillRect(renderer, &itemRect);
                    }
                    drawText(itemContextMenuItems[i].c_str(), menuRect.x + 8, yPos + 4, {255,255,255,255});
                    yPos += itemH;
                }
            }
            if (showDeleteConfirmation) {
                SDL_FRect win = animRect();
                float boxW = 300.0f, boxH = 120.0f;
                float boxX = win.x + (win.w - boxW) * 0.5f;
                float boxY = win.y + (win.h - boxH) * 0.5f;
                
                // Darken background
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
                SDL_RenderFillRect(renderer, &win);
                
                // Dialog Box
                SDL_SetRenderDrawColor(renderer, 40, 40, 50, 240);
                SDL_FRect box = { boxX, boxY, boxW, boxH };
                SDL_RenderFillRect(renderer, &box);
                SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
                SDL_RenderRect(renderer, &box);
                
                // Message Text
                std::string msg = "Delete '" + (deleteIndex >= 0 && deleteIndex < (int)entries.size() ? entries[deleteIndex] : "") + "'?";
                drawText(msg.c_str(), boxX + 20.0f, boxY + 20.0f, {255, 255, 255, 255});
                
                // Buttons
                SDL_FRect yesBtn = { boxX + 20.0f, boxY + boxH - 50.0f, 120.0f, 36.0f };
                SDL_FRect noBtn  = { boxX + boxW - 140.0f, boxY + boxH - 50.0f, 120.0f, 36.0f };
                
                float mx, my; SDL_GetMouseState(&mx, &my);
                
                bool hoverYes = (mx >= yesBtn.x && mx <= yesBtn.x + yesBtn.w && my >= yesBtn.y && my <= yesBtn.y + yesBtn.h);
                SDL_SetRenderDrawColor(renderer, hoverYes ? 220 : 180, 60, 60, 255);
                SDL_RenderFillRect(renderer, &yesBtn);
                drawText("Yes", yesBtn.x + 45.0f, yesBtn.y + 8.0f, {255, 255, 255, 255});
                
                bool hoverNo = (mx >= noBtn.x && mx <= noBtn.x + noBtn.w && my >= noBtn.y && my <= noBtn.y + noBtn.h);
                SDL_SetRenderDrawColor(renderer, hoverNo ? 100 : 80, 100, 120, 255);
                SDL_RenderFillRect(renderer, &noBtn);
                drawText("No", noBtn.x + 48.0f, noBtn.y + 8.0f, {255, 255, 255, 255});
            }
        }

        void onReset() override {
            selectedIndex = -1;
            selectedFilePath.clear();
            pathEdit.clear();
            pathEdit.deactivate(window);
            showContextMenu = false;
        }

    public:
        bool hasSelectedFile() const { return !selectedFilePath.empty(); }
        std::string getSelectedFilePath() const { return selectedFilePath; }

        void triggerCallback() {
            if (!callback) return;
            std::string filePath = selectedFilePath;
            if (filePath.empty() && selectedIndex >= 0 && selectedIndex < (int)entries.size() && !isDir[selectedIndex]) {
                filePath = (std::filesystem::path(currentPath) / entries[selectedIndex]).string();
            }
            if (!filePath.empty()) {
                callback(filePath);
            }
        }

    private:
        std::string currentPath;
        std::string rootPath;
        std::string filter;
        std::vector<std::string> entries;
        std::vector<bool> isDir;
        int selectedIndex = -1;
        std::function<void(const std::string&)> callback;
        Gui::LineEdit pathEdit;
        Gui::LineEdit renameEdit;
        bool showHidden = false;
        std::string selectedFilePath;

        SDL_Renderer*   renderer;
        TTF_TextEngine* textEngine;
        TTF_Font*       font;

        // GPU textures
        SDL_Texture* folderIcon = nullptr;
        SDL_Texture* cppIcon    = nullptr;
        SDL_Texture* hIcon      = nullptr;
        SDL_Texture* sceneIcon  = nullptr;
        SDL_Texture* textIcon   = nullptr;

        bool showContextMenu = false;
        float contextMenuX = 0, contextMenuY = 0;
        std::vector<std::string> contextMenuItems = {
            "New Folder",
            "New Scene (.json)",
            "New Script (.cpp)",
            "New Header (.h)"
        };
        bool showItemContextMenu = false;
        float itemContextMenuX = 0, itemContextMenuY = 0;
        std::vector<std::string> itemContextMenuItems = {"Rename", "Delete"};
        bool isRenaming = false;
        int renameIndex = -1;
        bool showDeleteConfirmation = false;
        int deleteIndex = -1;


        // --------------------------------------------------------------
        // Helpers
        // --------------------------------------------------------------
        static std::string getFileExtension(const std::string& name) {
            size_t dot = name.rfind('.');
            if (dot == std::string::npos) return "";
            std::string ext = name.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            return ext;
        }

        void loadIcons() {
            // Helper: try to load texture; on failure, create fallback
            auto loadOrFallback = [this](const std::string& filename, SDL_Color fallbackColor, const std::string& fallbackLabel) -> SDL_Texture* {
                std::string path = getAssetsPath() + filename;
                SDL_Texture* tex = IMG_LoadTexture(renderer, path.c_str());
                if (tex) {
                    return tex;
                }
                SDL_Log("Failed to load icon: %s (%s)", path.c_str(), SDL_GetError());
                return nullptr;
            };

            if (!folderIcon)  folderIcon  = loadOrFallback("folder_icon.svg",  SDL_Color{200,180,50,255}, "Dir");
            if (!cppIcon)     cppIcon     = loadOrFallback("cpp_icon.svg",     SDL_Color{80,160,255,255}, "C++");
            if (!hIcon)       hIcon       = loadOrFallback("h_icon.svg",       SDL_Color{255,120,80,255}, "Head");
            if (!sceneIcon)   sceneIcon   = loadOrFallback("scene_icon.svg",   SDL_Color{80,200,120,255}, "Scne");
            if (!textIcon)    textIcon    = loadOrFallback("file_icon.svg",     SDL_Color{80,100,120,255}, "Txt");
        }

        SDL_FRect getContextMenuRect() const {
            float w = 160.0f, h = 28.0f * contextMenuItems.size() + 6.0f;
            float x = contextMenuX, y = contextMenuY;
            SDL_FRect win = animRect();
            if (x + w > win.x + win.w) x = win.x + win.w - w - 10;
            if (y + h > win.y + win.h) y = win.y + win.h - h - 10;
            if (x < win.x + 10) x = win.x + 10;
            if (y < win.y + 70) y = win.y + 70;
            return { x, y, w, h };
        }

        // --------------------------------------------------------------
        // Directory listing
        // --------------------------------------------------------------
        void refreshEntries() {
            entries.clear();
            isDir.clear();
            try {
                for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
                    std::string name = entry.path().filename().string();
                    if (!showHidden && name[0] == '.') continue;
                    bool dir = entry.is_directory();
                    if (!dir && !matchesFilter(name)) continue;
                    entries.push_back(name);
                    isDir.push_back(dir);
                }
                std::vector<size_t> order(entries.size());
                std::iota(order.begin(), order.end(), 0);
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                    if (isDir[a] != isDir[b]) return isDir[a] > isDir[b];
                    return entries[a] < entries[b];
                });
                std::vector<std::string> sortedEntries;
                std::vector<bool> sortedIsDir;
                for (size_t idx : order) {
                    sortedEntries.push_back(entries[idx]);
                    sortedIsDir.push_back(isDir[idx]);
                }
                entries.swap(sortedEntries);
                isDir.swap(sortedIsDir);
                selectedIndex = -1;
                pathEdit.clear();
                for (char c : currentPath) pathEdit.appendText(std::string(1, c));
            } catch (const std::exception& e) {
                SDL_Log("FileExplorer: error reading directory: %s", e.what());
            }
        }

        void goToParent() {
            if (currentPath == rootPath) return;
            std::filesystem::path p(currentPath);
            if (p.has_parent_path()) {
                currentPath = p.parent_path().string();
                refreshEntries();
            }
        }

        void goToDirectory(const std::string& dirName) {
            std::filesystem::path newPath = std::filesystem::path(currentPath) / dirName;
            if (std::filesystem::is_directory(newPath)) {
                currentPath = newPath.string();
                refreshEntries();
            }
        }

        void selectFile(const std::string& fileName) {
            std::string fullPath = (std::filesystem::path(currentPath) / fileName).string();
            if (callback) callback(fullPath);
        }

        bool matchesFilter(const std::string& fileName) const {
            if (filter == "*" || filter.empty()) return true;
            auto patterns = splitFilter();
            for (const auto& pat : patterns) {
                if (pat == "*") return true;
                
                // --- ADD THIS ---
                if (pat == "*.*") return true; // Treat *.* as a wildcard for all files
                // ----------------

                if (pat.find('*') != std::string::npos) {
                    std::string prefix = pat.substr(0, pat.find('*'));
                    std::string suffix = pat.substr(pat.find('*') + 1);
                    if (fileName.size() >= prefix.size() + suffix.size() &&
                        fileName.compare(0, prefix.size(), prefix) == 0 &&
                        fileName.compare(fileName.size() - suffix.size(), suffix.size(), suffix) == 0)
                        return true;
                } else {
                    if (fileName == pat) return true;
                }
            }
            return false;
        }

        std::vector<std::string> splitFilter() const {
            std::vector<std::string> parts;
            size_t start = 0, end;
            while ((end = filter.find(';', start)) != std::string::npos) {
                parts.push_back(filter.substr(start, end - start));
                start = end + 1;
            }
            parts.push_back(filter.substr(start));
            return parts;
        }

        void createNewFolder() {
            std::string base = "new_folder";
            int counter = 1;
            std::string fullName;
            do {
                fullName = base + (counter > 1 ? "_" + std::to_string(counter) : "");
                ++counter;
            } while (std::filesystem::exists(std::filesystem::path(currentPath) / fullName));

            std::string folderPath = (std::filesystem::path(currentPath) / fullName).string();
            
            // Create the directory on the disk
            if (!std::filesystem::create_directory(folderPath)) {
                SDL_Log("Failed to create folder: %s", folderPath.c_str());
                return;
            }

            refreshEntries();
            // Automatically select the newly created folder in the list
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == fullName) { selectedIndex = (int)i; break; }
            }
        }

        void createNewScene() {
            std::string base = "new_scene";
            std::string ext = ".json";
            int counter = 1;
            std::string fullName;
            do {
                fullName = base + (counter > 1 ? "_" + std::to_string(counter) : "") + ext;
                ++counter;
            } while (std::filesystem::exists(std::filesystem::path(currentPath) / fullName));

            std::string filePath = (std::filesystem::path(currentPath) / fullName).string();
            nlohmann::json j;
            j["scene_name"] = "New Scene";
            j["script_attached"] = "";
            j["entities"] = nlohmann::json::array();
            j["gui_elements"] = nlohmann::json::array();
            std::ofstream out(filePath);
            out << j.dump(4);
            out.close();

            refreshEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == fullName) { selectedIndex = (int)i; break; }
            }
        }

        void createNewScript() {
            std::string base = "new_script";
            std::string ext = ".cpp";
            int counter = 1;
            std::string fullName;
            do {
                fullName = base + (counter > 1 ? "_" + std::to_string(counter) : "") + ext;
                ++counter;
            } while (std::filesystem::exists(std::filesystem::path(currentPath) / fullName));

            std::string filePath = (std::filesystem::path(currentPath) / fullName).string();
            std::string templateContent =
                "#include \"engine.h\"\n"
                "\n"
                "class MyScript : public ScriptBase {\n"
                "public:\n"
                "    void onStart() override {}\n"
                "    void onUpdate(float dt) override {}\n"
                "    void onDraw() override {}\n"
                "    void onEnd() override {}\n"
                "    std::string getName() const override { return \"MyScript\"; }\n"
                "};\n"
                "\n"
                "extern \"C\" ScriptBase* create_script() {\n"
                "    return new MyScript();\n"
                "}\n";

            std::ofstream out(filePath);
            out << templateContent;
            out.close();

            refreshEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == fullName) { selectedIndex = (int)i; break; }
            }
        }

        void createNewHeader() {
            std::string base = "new_header";
            std::string ext = ".h";
            int counter = 1;
            std::string fullName;
            do {
                fullName = base + (counter > 1 ? "_" + std::to_string(counter) : "") + ext;
                ++counter;
            } while (std::filesystem::exists(std::filesystem::path(currentPath) / fullName));

            std::string filePath = (std::filesystem::path(currentPath) / fullName).string();
            std::string headerContent =
                "#pragma once\n"
                "\n"
                "#include \"engine.h\"\n"
                "\n"
                "class MyScript : public ScriptBase {\n"
                "public:\n"
                "    void onStart() override;\n"
                "    void onUpdate(float dt) override;\n"
                "    void onDraw() override;\n"
                "    void onEnd() override;\n"
                "    std::string getName() const override;\n"
                "};\n";

            std::ofstream out(filePath);
            out << headerContent;
            out.close();

            refreshEntries();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] == fullName) { selectedIndex = (int)i; break; }
            }
        }
    };

} // end namespace Gui


// Scene and SceneParser are defined after the Gui namespace (at the bottom of this file).

void movement_system(ECSWorld& world, float dt) {
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i] && world.has_velocity[i] && world.has_acceleration[i]) {
            world.velocity_pool[i].x += world.acceleration_pool[i].x * dt;
            world.velocity_pool[i].y += world.acceleration_pool[i].y * dt;
            world.position_pool[i].x += world.velocity_pool[i].x * dt;
            world.position_pool[i].y += world.velocity_pool[i].y * dt;
        }
    }
}

void deselect_all(ECSWorld& world) {
    for (Entity i = 0; i < world.entity_count; i++) {
        if (world.has_selection[i]) world.selection_pool[i].isSelected = false;
    }
}

void render_system_and_scene_gui_in_editor(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
const ECSWorld & world, float viewX, float viewY,
float scrollX, float scrollY, std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements) {

    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i]) {
            float logicalX = world.position_pool[i].x;
            float logicalY = world.position_pool[i].y;
            // Fixed typo: "s crollX" -> "scrollX"
            float screenX = viewX + logicalX - scrollX;
            float screenY = viewY + logicalY - scrollY;
            SDL_FRect outlineRect = { screenX, screenY, 50, 50 };
            if (world.has_rectangle_shape[i]) {
                // Fixed typo: "rectangle_shape_ pool" -> "rectangle_shape_pool"
                outlineRect.w = world.rectangle_shape_pool[i].w;
                outlineRect.h = world.rectangle_shape_pool[i].h;
            }
            // Fixed typo: "& &" -> "&&" and spacing
            if (world.has_selection[i] && world.selection_pool[i].isSelected) {
                SDL_SetRenderDrawColor(renderer, world.selection_pool[i].selectionColor.r,
                world.selection_pool[i].selectionColor.g,
                world.selection_pool[i].selectionColor.b,
                world.selection_pool[i].selectionColor.a);
            } else {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            }
            if (world.has_metadata[i]) {
                // Fixed typo: "TTF_ CreateText" -> "TTF_CreateText"
                TTF_Text* textObj = TTF_CreateText(textEngine, font, world.metadata_pool[i].name.c_str(), 0);
                if (textObj) {
                    TTF_SetTextColor(textObj, 255, 255, 255, 255);
                    TTF_DrawRendererText(textObj, screenX, screenY);
                    // Fixed typo: "TTF_DestroyTex t" -> "TTF_DestroyText"
                    TTF_DestroyText(textObj);
                }
            }
            SDL_RenderRect(renderer, &outlineRect);
        }
    }

    // Must match the convention used for entities (screen = pos + view - scroll)
    // and for the editor's Select/Move hit-testing, or GUI elements render and
    // hit-test at different screen positions than entities do.
    float guiOffsetX = scrollX - viewX;
    float guiOffsetY = scrollY - viewY;
    for (auto & elem : guiElements) {
        // 1. Pass the calculated canvas offsets so they render in the correct logical space
        elem->render(guiOffsetX, guiOffsetY);
        
        // 2. Draw the yellow selection outline around the element if it's selected
        elem->renderSelectionOutline(renderer, guiOffsetX, guiOffsetY);
    }
}

// 2. Pure Game Render System (NO editor canvas offset)
// This replaces render_system_in_editor by removing the "+ 105" offset
void render_system_game(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font, const ECSWorld& world) {
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i]) {
            // Pure position, NO + 105 offset!
            SDL_FRect outlineRect = { 
                world.position_pool[i].x, 
                world.position_pool[i].y, 
                50.0f, 50.0f 
            };
            if (world.has_rectangle_shape[i]) {
                outlineRect.w = world.rectangle_shape_pool[i].w;
                outlineRect.h = world.rectangle_shape_pool[i].h;
            }
            
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            
            if (world.has_metadata[i]) {
                TTF_Text* textObj = TTF_CreateText(textEngine, font, world.metadata_pool[i].name.c_str(), 0);
                if (textObj) {
                    TTF_SetTextColor(textObj, 255, 255, 255, 255);
                    TTF_DrawRendererText(textObj, world.position_pool[i].x, world.position_pool[i].y);
                    TTF_DestroyText(textObj);
                }
            }
            SDL_RenderRect(renderer, &outlineRect);
        }
    }
}

void edit_object_with_editor_mouse(SDL_Renderer* renderer, ECSWorld& world,
std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements,
Gui::IGuiElement*& selectedGuiElem,
const SDL_Event& e)
{
    if (currentEditMode == EditMode::Dialog) return;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        isDraggingLeftMouse = false;
    }
    if (currentEditMode == EditMode::Select) {
         if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
             float logicalX = e.button.x - canvasViewX + editorScrollX;
             float logicalY = e.button.y - canvasViewY + editorScrollY;
             
             // 1. Check GUI elements (using primary rectangle)
             Gui::IGuiElement* topmostGui = nullptr;
             for (auto& elem : guiElements) {
                 float ex = elem->getX();
                 float ey = elem->getY();
                 float ew = elem->getWidth();
                 float eh = elem->getHeight();
                 if (logicalX >= ex && logicalX <= ex + ew && logicalY >= ey && logicalY <= ey + eh) {
                     topmostGui = elem.get();
                 }
             }
             
             // 2. Check Entities
             Entity topmostEntity = (Entity)-1;
             int maxZ = 0;
             bool foundEntity = false;
             for (Entity i = 0; i < world.entity_count; i++) {
                 if (world.has_position[i] && world.has_selection[i]) {
                     float entityW = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
                     // Fixed typo: "rectangle_s hape_pool" -> "rectangle_shape_pool"
                     float entityH = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
                     if (logicalX >= world.position_pool[i].x && logicalX <= world.position_pool[i].x + entityW &&
                         logicalY >= world.position_pool[i].y && logicalY <= world.position_pool[i].y + entityH) {
                         int currentZ = world.has_z_index[i] ? world.z_index_pool[i].z : 0;
                         if (!foundEntity || currentZ > maxZ) {
                             maxZ = currentZ;
                             topmostEntity = i;
                             foundEntity = true;
                         }
                     }
                 }
             }
             
             if (topmostGui) {
                 if (selectedGuiElem) selectedGuiElem->editorSelected = false;
                 selectedGuiElem = topmostGui;
                 selectedGuiElem->editorSelected = true;
                 deselect_all(world);
                 lastSelectedEntity = (Entity)-1;
             } else if (foundEntity) {
                 if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
                 if (currentSelectionmode == SelectionMode::SingleSelect) {
                     bool wasSelected = world.selection_pool[topmostEntity].isSelected;
                     for (Entity i = 0; i < world.entity_count; i++) {
                         if (world.has_selection[i]) world.selection_pool[i].isSelected = false;
                     }
                     if (!wasSelected) {
                         world.selection_pool[topmostEntity].isSelected = true;
                         // Fixed typo: "la stSelectedEntity" -> "lastSelectedEntity"
                         lastSelectedEntity = topmostEntity;
                     } else {
                         lastSelectedEntity = (Entity)-1;
                     }
                 } else if (currentSelectionmode == SelectionMode::MultiSelect) {
                     // Fixed typo: "i sSelected" -> "isSelected"
                     world.selection_pool[topmostEntity].isSelected = !world.selection_pool[topmostEntity].isSelected;
                     if (world.selection_pool[topmostEntity].isSelected)
                         lastSelectedEntity = topmostEntity;
                     else
                         // Fixed typo: "(Enti ty)" -> "(Entity)"
                         lastSelectedEntity = (Entity)-1;
                 }
             } else {
                 // Fixed typo: "& &" -> "&&"
                 bool insideCanvas = (e.button.y >= canvasViewY) &&
                                     (e.button.x >= canvasViewX) &&
                                     (e.button.x <= canvasViewX + canvasViewW);
                 if (insideCanvas) {
                     if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
                     if (currentSelectionmode == SelectionMode::SingleSelect) {
                         deselect_all(world);
                         lastSelectedEntity = (Entity)-1;
                     }
                 }
             }
         }
     } else if (currentEditMode == EditMode::MoveWithMouse) {
         if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
             if (isInsideCanvas(e.button.x, e.button.y)) {
                 isDraggingLeftMouse = true;
                 lastDragX = e.button.x;
                 lastDragY = e.button.y;
             }
         } else if (e.type == SDL_EVENT_MOUSE_MOTION && isDraggingLeftMouse) {
             if (isInsideCanvas(e.motion.x, e.motion.y)) {
                 int dx = e.motion.x - lastDragX;
                 int dy = e.motion.y - lastDragY;
                 if (dx != 0 || dy != 0) {
                     // Move Entities
                     for (Entity i = 0; i < world.entity_count; i++) {
                         if (world.has_position[i] && world.has_selection[i] && world.selection_pool[i].isSelected) {
                             world.position_pool[i].x += dx;
                             world.position_pool[i].y += dy;
                             if (world.has_rectangle_shape[i]) {
                                 clamp_entity_position_to_canvas(world.position_pool[i],
                                     world.rectangle_shape_pool[i].w,
                                     world.rectangle_shape_pool[i].h);
                             } else {
                                 clamp_entity_position_to_canvas(world.position_pool[i]);
                             }
                         }
                     }
                     // Move GUI Elements
                     if (selectedGuiElem) {
                         SDL_FPoint newPos = { selectedGuiElem->getX() + dx, selectedGuiElem->getY() + dy };
                         clamp_guiElem_position_to_canvas(newPos, selectedGuiElem->getWidth(), selectedGuiElem->getHeight());
                         selectedGuiElem->setPos({ static_cast<int>(newPos.x), static_cast<int>(newPos.y) });
                     }
                     lastDragX = e.motion.x;
                     lastDragY = e.motion.y;
                 }
             } else {
                 isDraggingLeftMouse = false;
             }
         }
     } else if (currentEditMode == EditMode::Delete) {
         if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
             float logicalX = e.button.x - canvasViewX + editorScrollX;
             float logicalY = e.button.y - canvasViewY + editorScrollY;
             // Delete GUI element
             Gui::IGuiElement* guiToDelete = nullptr;
             for (auto& elem : guiElements) {
                 float ex = elem->getX();
                 float ey = elem->getY();
                 float ew = elem->getWidth();
                 float eh = elem->getHeight();
                 if (logicalX >= ex && logicalX <= ex + ew && logicalY >= ey && logicalY <= ey + eh) {
                     guiToDelete = elem.get();
                 }
             }
             if (guiToDelete) {
                 if (selectedGuiElem == guiToDelete) selectedGuiElem = nullptr;
                 guiElements.erase(std::remove_if(guiElements.begin(), guiElements.end(),
                     [guiToDelete](const std::unique_ptr<Gui::IGuiElement>& p) { return p.get() == guiToDelete; }),
                     guiElements.end());
                 return;
             }
             // Delete Entity
             Entity topmostEntity = (Entity)-1;
             int maxZ = 0;
             bool found = false;
             for (Entity i = 0; i < world.entity_count; i++) {
                 if (world.has_position[i] && world.has_selection[i]) {
                     float entityW = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
                     float entityH = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
                     if (logicalX >= world.position_pool[i].x && logicalX <= world.position_pool[i].x + entityW &&
                         logicalY >= world.position_pool[i].y && logicalY <= world.position_pool[i].y + entityH) {
                         int currentZ = world.has_z_index[i] ? world.z_index_pool[i].z : 0;
                         if (!found || currentZ > maxZ) {
                             maxZ = currentZ;
                             topmostEntity = i;
                             found = true;
                         }
                     }
                 }
             }
             if (topmostEntity != (Entity)-1) world.delete_entity(topmostEntity);
         }
     }
}


void edit_object_with_editor_gamepad(ECSWorld& world,
std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements,
Gui::IGuiElement*& selectedGuiElem,
float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame)
{
    if (currentEditMode == EditMode::Dialog) return;
    if (!confirmDown) isGamepadDragging = false;

    float logicalX = cursorX - canvasViewX + editorScrollX;
    float logicalY = cursorY - canvasViewY + editorScrollY;

    Gui::IGuiElement* topmostGui = nullptr;
    for (auto& elem : guiElements) {
        float ex = elem->getX();
        float ey = elem->getY();
        float ew = elem->getWidth();
        float eh = elem->getHeight();
        if (logicalX >= ex && logicalX <= ex + ew && logicalY >= ey && logicalY <= ey + eh) {
            topmostGui = elem.get();
        }
    }

    Entity topmostEntity = (Entity)-1;
    int maxZ = -1;
    bool foundEntity = false;
    for (Entity i = 0; i < world.entity_count; i++) {
        if (world.has_position[i] && world.has_selection[i]) {
            float entityW = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
            float entityH = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
            if (logicalX >= world.position_pool[i].x && logicalX <= world.position_pool[i].x + entityW &&
                logicalY >= world.position_pool[i].y && logicalY <= world.position_pool[i].y + entityH) {
                int currentZ = world.has_z_index[i] ? world.z_index_pool[i].z : 0;
                if (!foundEntity || currentZ > maxZ) {
                    maxZ = currentZ;
                    topmostEntity = i;
                    foundEntity = true;
                }
            }
        }
    }

    if (currentEditMode == EditMode::Select) {
        if (!confirmDown && confirmDownLastFrame && !gamepadDidDrag) {
            if (topmostGui) {
                if (selectedGuiElem) selectedGuiElem->editorSelected = false;
                selectedGuiElem = topmostGui;
                selectedGuiElem->editorSelected = true;
                deselect_all(world);
                lastSelectedEntity = (Entity)-1;
            } else if (foundEntity) {
                if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
                if (currentSelectionmode == SelectionMode::SingleSelect) {
                    bool wasSelected = world.selection_pool[topmostEntity].isSelected;
                    for (Entity i = 0; i < world.entity_count; i++) {
                        if (world.has_selection[i]) world.selection_pool[i].isSelected = false;
                    }
                    if (!wasSelected) {
                        world.selection_pool[topmostEntity].isSelected = true;
                        lastSelectedEntity = topmostEntity;
                    } else {
                        lastSelectedEntity = (Entity)-1;
                    }
                } else if (currentSelectionmode == SelectionMode::MultiSelect) {
                    world.selection_pool[topmostEntity].isSelected = !world.selection_pool[topmostEntity].isSelected;
                    if (world.selection_pool[topmostEntity].isSelected)
                        lastSelectedEntity = topmostEntity;
                    else
                        lastSelectedEntity = (Entity)-1;
                }
            } else {
                bool insideCanvas = (cursorY >= canvasViewY) && 
                                    (cursorX >= canvasViewX) && 
                                    (cursorX <= canvasViewX + canvasViewW);
                if (insideCanvas) {
                    if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
                    if (currentSelectionmode == SelectionMode::SingleSelect) {
                        deselect_all(world);
                        lastSelectedEntity = (Entity)-1;
                    }
                }
            }
        }
        if (!confirmDown) gamepadDidDrag = false;
    } else if (currentEditMode == EditMode::MoveWithMouse) {
        if (confirmDown && !confirmDownLastFrame) {
            if (isInsideCanvas(cursorX, cursorY)) {
                isGamepadDragging = true;
                gamepadDidDrag = false;
                lastGamepadCursorX = cursorX;
                lastGamepadCursorY = cursorY;
            }
        } else if (confirmDown && isGamepadDragging) {
            if (isInsideCanvas(cursorX, cursorY)) {
                float dx = cursorX - lastGamepadCursorX;
                float dy = cursorY - lastGamepadCursorY;
                if (dx != 0.0f || dy != 0.0f) {
                    for (Entity i = 0; i < world.entity_count; i++) {
                        if (world.has_position[i] && world.has_selection[i] && world.selection_pool[i].isSelected) {
                            world.position_pool[i].x += dx;
                            world.position_pool[i].y += dy;
                            clamp_entity_position_to_canvas(world.position_pool[i]);
                        }
                    }
                    if (selectedGuiElem) {
                        SDL_FPoint newPos = { selectedGuiElem->getX() + dx, selectedGuiElem->getY() + dy };
                        clamp_guiElem_position_to_canvas(newPos, selectedGuiElem->getWidth(), selectedGuiElem->getHeight());
                        selectedGuiElem->setPos({ static_cast<int>(newPos.x), static_cast<int>(newPos.y) });
                    }
                    gamepadDidDrag = true;
                    lastGamepadCursorX = cursorX;
                    lastGamepadCursorY = cursorY;
                }
            } else {
                isGamepadDragging = false;
            }
        }
    } else if (currentEditMode == EditMode::Delete) {
        if (!confirmDown && confirmDownLastFrame) {
            if (topmostGui) {
                if (selectedGuiElem == topmostGui) selectedGuiElem = nullptr;
                guiElements.erase(std::remove_if(guiElements.begin(), guiElements.end(),
                    [topmostGui](const std::unique_ptr<Gui::IGuiElement>& p) { return p.get() == topmostGui; }),
                    guiElements.end());
            } else if (topmostEntity != (Entity)-1) {
                world.delete_entity(topmostEntity);
            }
        }
    }
}

void render_editor_canvas(SDL_Renderer* renderer) {
    SDL_FRect rect = { canvasViewX, canvasViewY, canvasViewW, canvasViewH };
    SDL_SetRenderDrawColor(renderer, 125, 125, 125, 255);
    SDL_RenderRect(renderer, &rect);
}

// ============================================================
// ScriptBase — every scene must reference a script that derives
// from this class.  onStart/onUpdate/onDraw/onEnd are called by
// the runtime; the editor validates that the attachment exists.
// ============================================================
class ScriptBase {
public:
    virtual ~ScriptBase() = default;
    virtual void onStart(){}
    virtual void onUpdate(float /*dt*/){}
    virtual void onDraw(){}
    virtual void onEnd(){}
    virtual std::string getName() const { return "ScriptBase"; }
};

// ============================================================
// Scene and SceneParser — defined here so all Gui types are
// fully available (Button, LineEdit, SpinBox, Panel, IGuiElement).
// ============================================================
struct Scene {
    std::string name = "Untitled Scene";

    // Script attachment: the filename of the C++ script for this scene.
    // An empty string or missing key means the scene is invalid.
    std::string scriptAttached;
    bool        scriptValid = false;   // set by SceneParser after load

    ECSWorld world;
    std::vector<std::unique_ptr<Gui::IGuiElement>> guiElements;
};



TTF_Font* ProjectScript_TTF_OpenFont(const char* file, float ptsize)
{
    return TTF_OpenFont((getProjectsRootForScripts() / std::filesystem::path(file)).string().c_str(), (int)ptsize);
}

class SceneParser {
private:
    SDL_Renderer* renderer;
    TTF_TextEngine* textEngine;
    TTF_Font* font;
    SDL_Window* window;

public:
    SceneParser(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_Window* w)
        : renderer(r), textEngine(te), font(f), window(w) {}

    std::string engine_root = std::filesystem::absolute(getEnginePath()).string();
    std::string projects_root = getProjectsPath();

    // ==========================================
    // LOAD SCENE
    // ==========================================
    Scene loadFromFile(const std::string& filepath) {
        
        Scene scene;
        
        std::string fullPath = filepath;
        #ifdef EMSCRIPTEN
            // On Emscripten, use the path as-is (it's already in virtual FS format)
            if (!fullPath.empty() && fullPath[0] != '/') {
                fullPath = "/" + fullPath;
            }
        #else
            // On native platforms, if the path is not absolute, prepend engine_root.
            // Absolute paths (e.g., C:\... or /...) are used directly.
            if (!fullPath.empty()) {
                bool isAbsolute = false;
        #ifdef _WIN32
                // Windows absolute path: drive letter or starts with \ or /
                if (fullPath.size() > 1 && fullPath[1] == ':') isAbsolute = true;
                if (fullPath[0] == '\\' || fullPath[0] == '/') isAbsolute = true;
        #else
                // Unix absolute path: starts with /
                if (fullPath[0] == '/') isAbsolute = true;
        #endif
                if (!isAbsolute) {
                    fullPath = engine_root + fullPath;
                }
            }
        #endif
                    
            std::ifstream file(fullPath);
            if (!file.is_open()) {
                SDL_Log("Current Working Directory: %s\n", std::filesystem::current_path().string().c_str());
                SDL_Log("Failed to open scene file: %s (tried: %s)\n", filepath.c_str(), fullPath.c_str());
                scene.scriptValid = false;
                return scene;
            }

            nlohmann::json j;
            file >> j;

            scene.name = j.value("scene_name", "Untitled");

            // --- 0. Script attachment (required) ---
            scene.scriptAttached = j.value("script_attached", "");
            if (scene.scriptAttached.empty()) {
                std::cerr << "[Editor] WARNING: Scene '" << scene.name
                        << "' has no script_attached — scene marked INVALID.\n";
                scene.scriptValid = false;
            } else {
        #ifdef EMSCRIPTEN
                // On Emscripten, check in virtual FS
                std::string trueScriptAttached = scene.scriptAttached;
                if (!trueScriptAttached.empty() && trueScriptAttached[0] != '/') {
                    trueScriptAttached = "/projects/" + trueScriptAttached;
                }
        #else
                // On native, use projects_root
                std::string trueScriptAttached = projects_root + scene.scriptAttached;
        #endif
                scene.scriptValid = std::filesystem::exists(trueScriptAttached);
                if (!scene.scriptValid)
                    std::cerr << "[Editor] WARNING: script_attached '"
                            << scene.scriptAttached
                            << "' not found on disk (tried: " << trueScriptAttached << ") — scene marked INVALID.\n";
                else
                    std::cout << "[Editor] Scene '" << scene.name
                            << "' — script '" << scene.scriptAttached << "' OK.\n";
            }

            // --- 1. Parse ECS Entities ---
            if (j.contains("entities")) {
                for (const auto& entityJson : j["entities"]) {
                    Entity id = scene.world.create_entity();
                    scene.world.add_selection(id);
                    scene.world.selection_pool[id].isSelected = false;
                    scene.world.selection_pool[id].selectionColor = {0, 255, 0, 255};

                    const auto& comps = entityJson["components"];

                    if (comps.contains("Metadata")) {
                        scene.world.add_metadata(id);
                        scene.world.metadata_pool[id].name = comps["Metadata"].value("name", "");
                    }
                    if (comps.contains("Position")) {
                        scene.world.add_position(id);
                        scene.world.position_pool[id].x = comps["Position"].value("x", 0.0f);
                        scene.world.position_pool[id].y = comps["Position"].value("y", 0.0f);
                    }
                    if (comps.contains("RectangleShape")) {
                        scene.world.add_rectangle_shape(id);
                        scene.world.rectangle_shape_pool[id].w = comps["RectangleShape"].value("w", 50.0f);
                        scene.world.rectangle_shape_pool[id].h = comps["RectangleShape"].value("h", 50.0f);
                    }
                    if (comps.contains("ZIndex")) {
                        scene.world.add_z_index(id);
                        scene.world.z_index_pool[id].z = comps["ZIndex"].value("z", 0);
                    }
                    if (comps.contains("Velocity")) {
                        scene.world.add_velocity(id);
                        scene.world.velocity_pool[id].x = comps["Velocity"].value("x", 0.0f);
                        scene.world.velocity_pool[id].y = comps["Velocity"].value("y", 0.0f);
                    }
                    if (comps.contains("Acceleration")) {
                        scene.world.add_acceleration(id);
                        scene.world.acceleration_pool[id].x = comps["Acceleration"].value("x", 0.0f);
                        scene.world.acceleration_pool[id].y = comps["Acceleration"].value("y", 0.0f);
                    }
                }
            }

            // --- 2. Parse GUI Elements (top-level and Panels) ---
            if (j.contains("gui_elements")) {
                for (const auto& elemJson : j["gui_elements"]) {
                    auto elem = parseGuiElement(elemJson);
                    if (elem) scene.guiElements.push_back(std::move(elem));
                }
            }
            return scene;
    }

    Scene ProjectScript_loadFromFile(const std::string& filepath) 
    {
        Scene scene;
        
    #ifdef __EMSCRIPTEN__
        // On Emscripten, prepend / to make it absolute path
        std::string fullPath = filepath;
        if (!fullPath.empty() && fullPath[0] != '/') {
            fullPath = "/" + fullPath;
        }
    #else
        // On native, use the projects root
        std::string fullPath = (getProjectsRootForScripts() / std::filesystem::path(filepath)).string();
    #endif
        
        std::ifstream file(fullPath);
        if (!file.is_open()) {
            SDL_Log("Current Working Directory: %s\n", std::filesystem::current_path().string().c_str());
            SDL_Log("Failed to open scene file: %s (tried: %s)\n", filepath.c_str(), fullPath.c_str());
            scene.scriptValid = false;
            return scene;
        }

        nlohmann::json j;
        file >> j;

        scene.name = j.value("scene_name", "Untitled");

        // --- 0. Script attachment (required) ---
        scene.scriptAttached = j.value("script_attached", "");
        if (scene.scriptAttached.empty()) {
            std::cerr << "[Editor] WARNING: Scene '" << scene.name
                    << "' has no script_attached — scene marked INVALID.\n";
            scene.scriptValid = false;
        } else {
    #ifdef __EMSCRIPTEN__
            // On Emscripten, we can't check for .cpp files at runtime since they're compiled in.
            // Just mark as valid if a script is attached.
            scene.scriptValid = true;
            std::cout << "[Editor] Scene '" << scene.name
                    << "' — script '" << scene.scriptAttached << "' OK (compiled in).\n";
    #else
            // On native, check that the referenced script file actually exists
            std::string trueScriptAttached = (getProjectsRootForScripts() / std::filesystem::path(scene.scriptAttached)).string();
            scene.scriptValid = std::filesystem::exists(trueScriptAttached);
            if (!scene.scriptValid)
                std::cerr << "[Editor] WARNING: script_attached '"
                        << scene.scriptAttached
                        << "' not found (tried: " << trueScriptAttached << ") — scene marked INVALID.\n";
            else
                std::cout << "[Editor] Scene '" << scene.name
                        << "' — script '" << scene.scriptAttached << "' OK.\n";
    #endif
        }

        // --- 1. Parse ECS Entities ---
        if (j.contains("entities")) {
            for (const auto& entityJson : j["entities"]) {

                Entity id = scene.world.create_entity();
                scene.world.add_selection(id);
                scene.world.selection_pool[id].isSelected = false;
                scene.world.selection_pool[id].selectionColor = {0, 255, 0, 255};

                const auto& comps = entityJson["components"];

                if (comps.contains("Metadata")) {
                    scene.world.add_metadata(id);
                    scene.world.metadata_pool[id].name = comps["Metadata"].value("name", "");
                }
                if (comps.contains("Position")) {
                    scene.world.add_position(id);
                    scene.world.position_pool[id].x = comps["Position"].value("x", 0.0f);
                    scene.world.position_pool[id].y = comps["Position"].value("y", 0.0f);
                }
                if (comps.contains("RectangleShape")) {
                    scene.world.add_rectangle_shape(id);
                    scene.world.rectangle_shape_pool[id].w = comps["RectangleShape"].value("w", 50.0f);
                    scene.world.rectangle_shape_pool[id].h = comps["RectangleShape"].value("h", 50.0f);
                }
                if (comps.contains("ZIndex")) {
                    scene.world.add_z_index(id);
                    scene.world.z_index_pool[id].z = comps["ZIndex"].value("z", 0);
                }
                if (comps.contains("Velocity")) {
                    scene.world.add_velocity(id);
                    scene.world.velocity_pool[id].x = comps["Velocity"].value("x", 0.0f);
                    scene.world.velocity_pool[id].y = comps["Velocity"].value("y", 0.0f);
                }
                if (comps.contains("Acceleration")) {
                    scene.world.add_acceleration(id);
                    scene.world.acceleration_pool[id].x = comps["Acceleration"].value("x", 0.0f);
                    scene.world.acceleration_pool[id].y = comps["Acceleration"].value("y", 0.0f);
                }
            }
        }

        // --- 2. Parse GUI Elements (top-level and Panels) ---
        if (j.contains("gui_elements")) {
            for (const auto& elemJson : j["gui_elements"]) {
                auto elem = parseGuiElement(elemJson);
                if (elem) scene.guiElements.push_back(std::move(elem));
            }
        }
        return scene;
    }

    // ==========================================
    // SAVE SCENE
    // ==========================================
    void saveToFile(const Scene& scene, const std::string& filepath) {
        nlohmann::json j;
        j["scene_name"]      = scene.name;
        j["script_attached"] = scene.scriptAttached;

        // --- 1. Save ECS Entities ---
        nlohmann::json entitiesJson = nlohmann::json::array();
        for (Entity i = 0; i < scene.world.entity_count; ++i) {
            nlohmann::json comps;
            if (scene.world.has_metadata[i])
                comps["Metadata"]["name"] = scene.world.metadata_pool[i].name;
            if (scene.world.has_position[i]) {
                comps["Position"]["x"] = scene.world.position_pool[i].x;
                comps["Position"]["y"] = scene.world.position_pool[i].y;
            }
            if (scene.world.has_rectangle_shape[i]) {
                comps["RectangleShape"]["w"] = scene.world.rectangle_shape_pool[i].w;
                comps["RectangleShape"]["h"] = scene.world.rectangle_shape_pool[i].h;
            }
            if (scene.world.has_z_index[i])
                comps["ZIndex"]["z"] = scene.world.z_index_pool[i].z;
            if (scene.world.has_velocity[i]) {
                comps["Velocity"]["x"] = scene.world.velocity_pool[i].x;
                comps["Velocity"]["y"] = scene.world.velocity_pool[i].y;
            }
            if (scene.world.has_acceleration[i]) {
                comps["Acceleration"]["x"] = scene.world.acceleration_pool[i].x;
                comps["Acceleration"]["y"] = scene.world.acceleration_pool[i].y;
            }
            entitiesJson.push_back({{"components", comps}});
        }
        j["entities"] = entitiesJson;

        // --- 2. Save GUI Elements ---
        nlohmann::json guiArray = nlohmann::json::array();
        for (const auto& elem : scene.guiElements)
            guiArray.push_back(serializeGuiElement(*elem));
        j["gui_elements"] = guiArray;

        std::ofstream outFile(filepath);
        outFile << j.dump(4);
    }

private:
    // ------------------------------------------------------------------
    // Helpers: parse / serialize a single IGuiElement (handles Panel recursion)
    // ------------------------------------------------------------------
    std::unique_ptr<Gui::IGuiElement> parseGuiElement(const nlohmann::json & elemJson) {
        std::string type = elemJson.value("type", "");
        float x = elemJson.value("x", 0.0f);
        float y = elemJson.value("y", 0.0f);
        float w = elemJson.value("w", 100.0f);
        float h = elemJson.value("h",  50.0f);
        SDL_FRect rect = {x, y, w, h};

        if (type == "Button") {
            return std::make_unique<Gui::Button>(
                renderer, font, elemJson.value("text", "Button"), SDL_FPoint{x, y}, w, h);
        }
        if (type == "LineEdit") {
            return std::make_unique<Gui::LineEdit>(
                renderer, textEngine, font, rect,
                elemJson.value("placeholder", "Type here..."));
        }
        if (type == "SpinBox") {
            return std::make_unique<Gui::SpinBox>(
                renderer, textEngine, font, rect,
                elemJson.value("min", 1.0f), elemJson.value("max", 9999.0f),
                elemJson.value("initial", 50.0f), elemJson.value("step", 0.5f));
        }
        if (type == "Panel") {
            auto bgArr = elemJson.value("bg_color", nlohmann::json::array({40,40,48,230}));
            SDL_Color bg = {
                (Uint8)bgArr[0].get<int>(), (Uint8)bgArr[1].get<int>(),
                (Uint8)bgArr[2].get<int>(), (Uint8)bgArr[3].get<int>()
            };
            auto panel = std::make_unique<Gui::Panel>(renderer, textEngine, font, rect, bg);

            // Children positions in JSON are relative to panel origin
            if (elemJson.contains("children")) {
                for (const auto& childJson : elemJson["children"]) {
                    // Adjust child x/y: they are stored relative to panel top-left;
                    // at runtime children receive panelScrollOffset as their offsetY
                    // and their x/y are already in panel-local screen coords (panel.x + local_x)
                    nlohmann::json adjusted = childJson;
                    adjusted["x"] = x + childJson.value("x", 0.0f);
                    adjusted["y"] = y + childJson.value("y", 0.0f);
                    auto child = parseGuiElement(adjusted);
                    if (child) panel->addChild(std::move(child));
                }
            }
            return panel;
        }
        std::cerr << "[SceneParser] Unknown gui element type: " << type << "\n";
        return nullptr;
    }

    nlohmann::json serializeGuiElement(const Gui::IGuiElement& elem) {
        nlohmann::json j;
        j["type"] = elem.getType();
        j["x"]    = elem.getX();
        j["y"]    = elem.getY();
        j["w"]    = elem.getWidth();
        j["h"]    = elem.getHeight();

        if (elem.getType() == "Button") {
            j["text"] = static_cast<const Gui::Button&>(elem).getText();
        } else if (elem.getType() == "LineEdit") {
            j["placeholder"] = static_cast<const Gui::LineEdit&>(elem).getPlaceholder();
        } else if (elem.getType() == "SpinBox") {
            const auto& sb = static_cast<const Gui::SpinBox&>(elem);
            j["min"] = sb.getMin(); j["max"] = sb.getMax();
            j["step"] = sb.getStep(); j["initial"] = sb.getValue();
        } else if (elem.getType() == "Panel") {
            const auto& panel = static_cast<const Gui::Panel&>(elem);
            j["bg_color"] = { panel.bgColor.r, panel.bgColor.g,
                               panel.bgColor.b, panel.bgColor.a };
            nlohmann::json childArr = nlohmann::json::array();
            for (const auto& child : panel.getChildren()) {
                auto childJson = serializeGuiElement(*child);
                // Store children relative to panel origin
                childJson["x"] = child->getX() - elem.getX();
                childJson["y"] = child->getY() - elem.getY();
                childArr.push_back(childJson);
            }
            j["children"] = childArr;
        }
        return j;
    }
};