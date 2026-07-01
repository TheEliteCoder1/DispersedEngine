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
#include <functional>
#include <vector>
#include <json.hpp>
#include <fstream>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <array>
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
    pos.x = std::clamp(pos.x, minX, maxX);
    pos.y = std::clamp(pos.y, minY, maxY);
}

inline void clamp_guiElem_position_to_canvas(SDL_FPoint& pos, float guiElemWidth, float guiElemHeight) {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = LOGICAL_CANVAS_WIDTH - guiElemWidth;
    float maxY = LOGICAL_CANVAS_HEIGHT - guiElemHeight;
    pos.x = std::clamp(pos.x, minX, maxX);
    pos.y = std::clamp(pos.y, minY, maxY);
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
        virtual void render(float offsetY = 0.0f) = 0;
        virtual bool handleEvent(const SDL_Event& e, SDL_Window* window, float offsetY = 0.0f) = 0;
        virtual void handleGamepad(float cursorX, float cursorY, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) = 0;

        // Draw a selection highlight around this element (called by editor)
        void renderSelectionOutline(SDL_Renderer* renderer) const {
            if (!editorSelected) return;
            SDL_FRect r = { getX() - 2, getY() - 2, getWidth() + 4, getHeight() + 4 };
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

        void render(SDL_Renderer* renderer) {
            SDL_FRect track = { trackX, trackY, trackW, trackH };
            SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
            SDL_RenderFillRect(renderer, &track);
            if (maxOffset() <= 0) return;
            SDL_FRect thumb = thumbRect();
            bool hov = false;
            float mx, my; SDL_GetMouseState(&mx, &my);
            hov = inRect(mx, my, thumb) || dragging;
            SDL_SetRenderDrawColor(renderer, hov ? 160 : 110, hov ? 160 : 110, hov ? 160 : 110, 255);
            SDL_RenderFillRect(renderer, &thumb);
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

        void setOffset(float v) {
            offset = std::clamp(v, 0.0f, maxOffset());
            if (onChange) onChange(offset);
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

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetY) override {
            SDL_FRect originalRect = rect;
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

        void handleGamepad(float cursorX, float cursorY, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect dec = decBtn(); dec.y -= offsetY;
            SDL_FRect inc = incBtn(); inc.y -= offsetY;
            SDL_FRect ta = fieldTextArea(); ta.y -= offsetY;
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

        void render(float offsetY) override {
            SDL_FRect originalRect = rect;
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

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetY) override {
            SDL_FRect originalRect = rect;
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

        void handleGamepad(float cursorX, float cursorY, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            SDL_FRect r = rect; r.y -= offsetY;
            bool hit = cursorX >= r.x && cursorX <= r.x + r.w && cursorY >= r.y && cursorY <= r.y + r.h;
            if (confirmDown && !confirmDownLastFrame) {
                if (hit) {
                    if (!active) { active = true; }
                } else {
                    if (active) active = false;
                }
            }
        }

        void render(float offsetY) override {
            SDL_FRect originalRect = rect;
            rect.y -= offsetY; // Temporarily offset
            SDL_Color bg = active ? SDL_Color{255,255,255,255} : SDL_Color{245,245,245,255};
            SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderFillRect(renderer, &rect);
            SDL_Color border = active ? SDL_Color{52,110,235,255} : SDL_Color{130,130,130,255};
            SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
            SDL_RenderRect(renderer, &rect);
            if (!font || !textEngine) return;
            bool showPlaceholder = text.empty() && !active;
            const std::string& display = showPlaceholder ? placeholder : text;
            SDL_Color col = showPlaceholder ? SDL_Color{150,150,150,255} : SDL_Color{20,20,20,255};
            std::string rendered = display;
            if (active) {
                Uint64 ticks = SDL_GetTicks();
                if ((ticks / 500) % 2 == 0) rendered += "|";
            }
            TTF_Text* t = TTF_CreateText(textEngine, font, rendered.c_str(), 0);
            if (t) {
                TTF_SetTextColor(t, col.r, col.g, col.b, col.a);
                TTF_DrawRendererText(t, rect.x + 8.0f, rect.y + (rect.h - 20.0f) * 0.5f);
                TTF_DestroyText(t);
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
        void render(float offsetY = 0.0f) override { renderImpl(offsetY); }
        void handleGamepad(float cursorX, float cursorY, float offsetY, SDL_Window* /*window*/,
                           bool confirmDown, bool confirmDownLastFrame) override {
            handleGamepadImpl(cursorX, cursorY, offsetY, confirmDown, confirmDownLastFrame);
        }

        void handleGamepad(float cursorX, float cursorY, float offsetY, bool confirmDown, bool confirmDownLastFrame) {
            handleGamepadImpl(cursorX, cursorY, offsetY, confirmDown, confirmDownLastFrame);
        }
        void render(float offsetY, float /*cursorX*/, float /*cursorY*/) { renderImpl(offsetY); }

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

        void handleEventImpl(const SDL_Event& e, float offsetY) {
            if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                float mouseX, mouseY;
                if (e.type == SDL_EVENT_MOUSE_MOTION) { mouseX = e.motion.x; mouseY = e.motion.y; }
                else { mouseX = e.button.x; mouseY = e.button.y; }
                float visualY = position.y - offsetY;
                bool inside = (mouseX >= position.x) && (mouseX <= position.x + width) &&
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

        bool handleEvent(const SDL_Event & ev, SDL_Window* window, float offsetY) override {
            handleEventImpl(ev, offsetY);
            
            // Consume the event if the mouse is inside the button to prevent 
            // it from falling through to the editor and causing accidental deselections.
            if (ev.type == SDL_EVENT_MOUSE_MOTION || 
                ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || 
                ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                
                float mouseX = (ev.type == SDL_EVENT_MOUSE_MOTION) ? ev.motion.x : ev.button.x;
                float mouseY = (ev.type == SDL_EVENT_MOUSE_MOTION) ? ev.motion.y : ev.button.y;
                
                float visualY = position.y - offsetY;
                bool inside = (mouseX >= position.x) && (mouseX <= position.x + width) &&
                            (mouseY >= visualY) && (mouseY <= visualY + height);
                            
                if (inside) return true;
            }
            return false;
        }

        void handleGamepadImpl(float cursorX, float cursorY, float offsetY, bool confirmDown, bool confirmDownLastFrame) {
            float visualY = position.y - offsetY;
            bool inside = (cursorX >= position.x) && (cursorX <= position.x + width) &&
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

        void renderImpl(float offsetY) {
            SDL_FRect fillRect = { position.x, position.y - offsetY, width, height };
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
            rect = r;
            refreshScrollbarGeometry();
        }
        void setPos(SDL_Point p) override {rect.x = p.x; rect.y = p.y; refreshScrollbarGeometry();};

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
        void render(float /*editorOffsetY*/ = 0.0f) override {
            // Background
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
            SDL_RenderFillRect(renderer, &rect);
            if (showBorder) {
                SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
                SDL_RenderRect(renderer, &rect);
            }

            // Clip children to panel bounds via SDL viewport / render clip
            SDL_Rect clip = {
                (int)rect.x, (int)rect.y,
                (int)rect.w, (int)rect.h
            };
            SDL_SetRenderClipRect(renderer, &clip);

            // Children receive panelScrollOffset as their offsetY
            for (auto& child : children)
                child->render(panelScrollOffset);

            SDL_SetRenderClipRect(renderer, nullptr);

            // Scrollbar drawn on top (outside clip so thumb isn't cut)
            scrollbar.render(renderer);

            // Editor selection outlines drawn outside the clip rect so they're always visible
            for (auto& child : children)
                child->renderSelectionOutline(renderer);
            renderSelectionOutline(renderer);
        }

        // ------------------------------------------------------------------
        // handleEvent — only processes events whose mouse position is within
        // the panel.  Wheel events inside the panel scroll the panel, not
        // the editor.
        // ------------------------------------------------------------------
        bool handleEvent(const SDL_Event& ev, SDL_Window* window,
                         float /*editorOffsetY*/ = 0.0f) override {
            float mx = 0, my = 0;
            bool hasPos = getEventPos(ev, mx, my);

            // Wheel events: only consume if mouse is inside panel
            if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                if (hasPos && inRect(mx, my, rect)) {
                    scrollbar.handleEvent(ev);
                    return true;
                }
                return false;
            }

            // For all other pointer events, only forward if inside panel
            if (hasPos && !inRect(mx, my, rect)) return false;

            // Scrollbar gets first crack at drag events
            if (scrollbar.handleEvent(ev)) return true;

            // Forward to children (they use panelScrollOffset as their offsetY)
            for (auto& child : children)
                if (child->handleEvent(ev, window, panelScrollOffset)) return true;

            return inRect(mx, my, rect); // consume clicks inside panel
        }

        void handleGamepad(float cursorX, float cursorY, float /*editorOffsetY*/,
                           SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) override {
            if (!inRect(cursorX, cursorY, rect)) return;
            for (auto& child : children)
                child->handleGamepad(cursorX, cursorY, panelScrollOffset,
                                     window, confirmDown, confirmDownLastFrame);
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

        float computeContentHeight() const {
            float bottom = 0;
            for (auto& c : children)
                bottom = std::max(bottom, c->getY() + c->getHeight());
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

        void render(float offsetY) override {
            for (auto& child : children)
                child->render(offsetY);
        }

        bool handleEvent(const SDL_Event& e, SDL_Window* window, float offsetY) override {
            bool consumed = false;
            for (auto& child : children)
                if (child->handleEvent(e, window, offsetY))
                    consumed = true;
            return consumed;
        }

        void handleGamepad(float cursorX, float cursorY, float offsetY, SDL_Window* window,
                        bool confirmDown, bool confirmDownLastFrame) override {
            for (auto& child : children)
                child->handleGamepad(cursorX, cursorY, offsetY, window, confirmDown, confirmDownLastFrame);
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
    class SceneInspector {
    public:
        static constexpr float PANEL_W = 260.0f;

        // Computed from real window width so resizing works correctly.
        float panelX() const {
            int w, h; SDL_GetWindowSize(window, &w, &h);
            return (float)w - PANEL_W;
        }

        SceneInspector(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_Window* w)
            : renderer(r), textEngine(te), font(f), window(w)
        {}

        // Called once per frame — pass the scene GUI elements so the
        // inspector can both read and write their properties.
        void setTarget(IGuiElement* elem) {
            if (target == elem) return;
            commitAllFields(); // flush any pending edits when switching
            target = elem;
            rebuildFields();
        }

        void handleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) {
            if (!target) return;
            for (auto& f : fields) {
                // LineEdit::handleGamepad signature: (float, float, float, SDL_Window*, bool, bool)
                f.edit.handleGamepad(cursorX, cursorY, 0.0f, window, confirmDown, confirmDownLastFrame);
            }
        }

        IGuiElement* getTarget() const { return target; }

        // Returns true if the event was consumed by the inspector
        bool handleEvent(const SDL_Event& ev) {
            if (!target) return false;
            bool consumed = false;
            for (auto& f : fields)
                if (f.edit.handleEvent(ev, window, 0.0f)) consumed = true;
            // Commit on Enter/Tab
            if (ev.type == SDL_EVENT_KEY_DOWN &&
                (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_TAB)) {
                commitAllFields();
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

            float y = 10.0f;
            drawLabel("Inspector", panelX() + 10, y, {200,200,220,255}); y += 22;
            drawLabel(("[" + target->getType() + "]").c_str(), panelX() + 10, y, {140,140,180,255}); y += 22;

            // Divider
            SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
            SDL_FRect div = { panelX() + 5, y, PANEL_W - 10, 1 };
            SDL_RenderFillRect(renderer, &div); y += 8;

            for (auto& f : fields) {
                drawLabel(f.label.c_str(), panelX() + 8, y, {160,160,190,255});
                y += 32;
                f.edit.setRect({ panelX() + 8, y, PANEL_W - 20, 26 });
                f.edit.render(0.0f);
                y += 32;
            }

            // Hint
            drawLabel("Enter = commit changes", panelX() + 8, y + 4, {80, 80, 100, 255});
        }

        // Flush all field values into the target element right now.
        // Called from the Save button handler in main.cpp too.
        void commitAllFields() {
            if (!target) return;
            applyFields();
        }

    private:
        struct Field {
            std::string label;
            std::string key;   // identifies which property this maps to
            LineEdit    edit;
            Field(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f,
                  const std::string& lbl, const std::string& k, const std::string& val)
                : label(lbl), key(k),
                  edit(r, te, f, {0,0,1,1}, "")
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

        void applyFields() {
            if (!target) return;

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

            // Apply common geometry
            SDL_FRect r = {
                fval("x"), fval("y"), fval("w"), fval("h")
            };
            if (r.w > 0 && r.h > 0) target->setRect(r);

            // Apply type-specific
            if (target->getType() == "Button") {
                std::string t = fstr("btn_text");
                if (!t.empty()) static_cast<Button*>(target)->setText(t);
            } else if (target->getType() == "LineEdit") {
                static_cast<LineEdit*>(target)->setPlaceholder(fstr("le_placeholder"));
            } else if (target->getType() == "SpinBox") {
                auto* sb = static_cast<SpinBox*>(target);
                sb->setMin(fval("sb_min"));
                sb->setMax(fval("sb_max"));
                sb->setStep(fval("sb_step"));
                sb->setValue(fval("sb_val"));
            } else if (target->getType() == "Panel") {
                auto* p = static_cast<Panel*>(target);
                p->bgColor.r = (Uint8)std::clamp((int)fval("panel_r"), 0, 255);
                p->bgColor.g = (Uint8)std::clamp((int)fval("panel_g"), 0, 255);
                p->bgColor.b = (Uint8)std::clamp((int)fval("panel_b"), 0, 255);
                p->bgColor.a = (Uint8)std::clamp((int)fval("panel_a"), 0, 255);
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
            : renderer(r), textEngine(te), font(f), window(w) {}

        void setTarget(ECSWorld& w, Entity e) {
            if (world == &w && targetEntity == e) return;
            commitAllFields();
            world = &w;
            targetEntity = e;
            rebuildFields();
        }

        void clearTarget() {
            commitAllFields();
            world = nullptr;
            targetEntity = (Entity)-1;
            fields.clear();
        }

        bool handleEvent(const SDL_Event& ev) {
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
                f.widget->handleGamepad(cursorX, cursorY, 0.0f, window, confirmDown, confirmDownLastFrame);
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

            float y = 10.0f;
            drawLabel("Entity Inspector", panelX() + 10, y, {200,200,220,255}); y += 24;
            drawLabel(("ID: " + std::to_string(targetEntity)).c_str(), panelX() + 10, y, {140,140,180,255}); y += 22;

            SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
            SDL_FRect div = { panelX() + 5, y, PANEL_W - 10, 1 };
            SDL_RenderFillRect(renderer, &div); y += 8;

            for (auto& f : fields) {
                drawLabel(f.label.c_str(), panelX() + 8, y, {160,160,190,255});
                y += 32;
                f.widget->setRect({ panelX() + 8, y, PANEL_W - 20, 26 });
                f.widget->render(0.0f);
                y += 32;
            }

            drawLabel("Enter = commit changes", panelX() + 8, y + 4, {80, 80, 100, 255});
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

void render_system_in_editor(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                             const ECSWorld& world, float viewX, float viewY,
                             float scrollX, float scrollY) {
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i]) {
            float logicalX = world.position_pool[i].x;
            float logicalY = world.position_pool[i].y;
            float screenX = viewX + logicalX - scrollX;
            float screenY = viewY + logicalY - scrollY;
            SDL_FRect outlineRect = { screenX, screenY, 50, 50 };
            if (world.has_rectangle_shape[i]) {
                outlineRect.w = world.rectangle_shape_pool[i].w;
                outlineRect.h = world.rectangle_shape_pool[i].h;
            }
            if (world.has_selection[i] && world.selection_pool[i].isSelected) {
                SDL_SetRenderDrawColor(renderer, world.selection_pool[i].selectionColor.r,
                                       world.selection_pool[i].selectionColor.g,
                                       world.selection_pool[i].selectionColor.b,
                                       world.selection_pool[i].selectionColor.a);
            } else {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            }
            if (world.has_metadata[i]) {
                TTF_Text* textObj = TTF_CreateText(textEngine, font, world.metadata_pool[i].name.c_str(), 0);
                TTF_SetTextColor(textObj, 255, 255, 255, 255);
                TTF_DrawRendererText(textObj, screenX, screenY);
                TTF_DestroyText(textObj);
            }
            SDL_RenderRect(renderer, &outlineRect);
        }
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

void edit_object_with_editor_mouse(SDL_Renderer* renderer, ECSWorld & world, const std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements, const SDL_Event & e) {
    if (currentEditMode == EditMode::Dialog) return;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        isDraggingLeftMouse = false;
    }
    if (currentEditMode == EditMode::Select) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            // Convert screen click to logical canvas coordinates
            float logicalX = e.button.x - canvasViewX + editorScrollX;
            float logicalY = e.button.y - canvasViewY + editorScrollY;
            Entity topmostEntity = (Entity)-1;
            int maxZ = 0;
            bool found = false;
            for (Entity i = 0; i < world.entity_count; i++) {
                if (world.has_position[i] && world.has_selection[i]) {
                    float entityW = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].w : 50.0f;
                    float entityH = world.has_rectangle_shape[i] ? world.rectangle_shape_pool[i].h : 50.0f;
                    // Check if logical click is inside entity's logical rectangle
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
            if (topmostEntity != (Entity)-1) {
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
                // Click on empty canvas -> deselect all
                // Only deselect if the click was actually inside the canvas bounds.
                // This prevents clicking toolbar buttons (like "Move") from clearing the selection.
                bool insideCanvas = (e.button.y >= canvasViewY) && 
                                    (e.button.x >= canvasViewX) && 
                                    (e.button.x <= canvasViewX + canvasViewW);
                                    
                if (currentSelectionmode == SelectionMode::SingleSelect && insideCanvas) {
                    deselect_all(world);
                    lastSelectedEntity = (Entity)-1;
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
                    for (const auto & elem : guiElements)
                    {
                        auto guiElem = elem.get();
                        if (guiElem->editorSelected) {
                            SDL_FPoint newPos = {guiElem->getX() + dx, guiElem->getY() + dy};
                            clamp_guiElem_position_to_canvas(newPos, guiElem->getWidth(), guiElem->getHeight());
                            guiElem->setPos({static_cast<int>(newPos.x), static_cast<int>(newPos.y)});
                        }
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


void edit_object_with_editor_gamepad(ECSWorld& world, float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) {
    if (currentEditMode == EditMode::Dialog) return;
    if (!confirmDown) isGamepadDragging = false;

    // Convert screen cursor to logical canvas coordinates
    float logicalX = cursorX - canvasViewX + editorScrollX;
    float logicalY = cursorY - canvasViewY + editorScrollY;

    Entity topmostEntity = (Entity)-1;
    int maxZ = -1;
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

    if (currentEditMode == EditMode::Select) {
        if (!confirmDown && confirmDownLastFrame && !gamepadDidDrag) {
            if (topmostEntity != (Entity)-1) {
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
                // Same bounds check for gamepad cursor
                bool insideCanvas = (cursorY >= canvasViewY) && 
                                    (cursorX >= canvasViewX) && 
                                    (cursorX <= canvasViewX + canvasViewW);
                                    
                if (currentSelectionmode == SelectionMode::SingleSelect && insideCanvas) {
                    deselect_all(world);
                    lastSelectedEntity = (Entity)-1;
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
            if (topmostEntity != (Entity)-1) world.delete_entity(topmostEntity);
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

    std::string engine_root = getEnginePath();
    std::string projects_root = getProjectsPath();

    // ==========================================
    // LOAD SCENE
    // ==========================================
    Scene loadFromFile(const std::string& filepath) {
        
        Scene scene;
        
        #ifdef EMSCRIPTEN
            // On Emscripten, use the path as-is (it's already in virtual FS format)
            std::string fullPath = filepath;
            // Ensure it starts with / for virtual FS
            if (!fullPath.empty() && fullPath[0] != '/') {
                fullPath = "/" + fullPath;
            }
        #else
            // On native platforms, use engine_root
            std::string fullPath = engine_root + filepath;
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