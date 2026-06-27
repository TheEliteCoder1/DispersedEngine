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


namespace Tools {
    void executeCommand() {
        std::cout << std::filesystem::current_path();
    }
}

using Entity = uint32_t;
const size_t MAX_ENTITIES = 1000000;

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

SDL_FRect editorCanvasRect = { 105, 105, 1390, 690 };

inline bool isInsideCanvas(float x, float y) {
    return x >= editorCanvasRect.x && x <= editorCanvasRect.x + editorCanvasRect.w &&
           y >= editorCanvasRect.y && y <= editorCanvasRect.y + editorCanvasRect.h;
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

inline void clamp_entity_position_to_canvas(Components::Position& pos, float entityWidth = 50.0f, float entityHeight = 50.0f) {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = editorCanvasRect.w - entityWidth;
    float maxY = editorCanvasRect.h - entityHeight;
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

void render_system_in_editor(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font, const ECSWorld& world) {
    for (Entity i = 0; i < world.entity_count; ++i) {
        if (world.has_position[i]) {
            SDL_FRect outlineRect = { world.position_pool[i].x + 105, world.position_pool[i].y + 105, 50, 50 };
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
                TTF_DrawRendererText(textObj, world.position_pool[i].x + 105, world.position_pool[i].y + 105);
                TTF_DestroyText(textObj);
            }
            SDL_RenderRect(renderer, &outlineRect);
        }
    }
}

void edit_object_with_editor_mouse(SDL_Renderer* renderer, ECSWorld& world, const SDL_Event& e) {
    if (currentEditMode == EditMode::Dialog) return;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        isDraggingLeftMouse = false;
    }
    if (currentEditMode == EditMode::Select) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point mpos = { static_cast<int>(e.button.x), static_cast<int>(e.button.y) };
            Entity topmostEntity = (Entity)-1;
            int maxZ = 0;
            bool found = false;
            for (Entity i = 0; i < world.entity_count; i++) {
                if (world.has_position[i] && world.has_selection[i]) {
                    SDL_Rect entityrect = {
                        static_cast<int>(world.position_pool[i].x + 105),
                        static_cast<int>(world.position_pool[i].y + 105),
                        50, 50
                    };
                    if (SDL_PointInRect(&mpos, &entityrect)) {
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
                    if (!wasSelected) world.selection_pool[topmostEntity].isSelected = true;
                } else if (currentSelectionmode == SelectionMode::MultiSelect) {
                    world.selection_pool[topmostEntity].isSelected = !world.selection_pool[topmostEntity].isSelected;
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
                    lastDragX = e.motion.x;
                    lastDragY = e.motion.y;
                }
            } else {
                isDraggingLeftMouse = false;
            }
        }
    } else if (currentEditMode == EditMode::Delete) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point mpos = { static_cast<int>(e.button.x), static_cast<int>(e.button.y) };
            Entity topmostEntity = (Entity)-1;
            int maxZ = 0;
            bool found = false;
            for (Entity i = 0; i < world.entity_count; i++) {
                if (world.has_position[i] && world.has_selection[i]) {
                    SDL_Rect entityrect = {
                        static_cast<int>(world.position_pool[i].x + 105),
                        static_cast<int>(world.position_pool[i].y + 105),
                        50, 50
                    };
                    if (SDL_PointInRect(&mpos, &entityrect)) {
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

    Entity topmostEntity = (Entity)-1;
    int maxZ = -1;
    bool found = false;
    for (Entity i = 0; i < world.entity_count; i++) {
        if (world.has_position[i] && world.has_selection[i]) {
            SDL_FRect entityrect = {
                world.position_pool[i].x + 105.0f,
                world.position_pool[i].y + 105.0f,
                50.0f, 50.0f
            };
            if (cursorX >= entityrect.x && cursorX <= entityrect.x + entityrect.w &&
                cursorY >= entityrect.y && cursorY <= entityrect.y + entityrect.h) {
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
                    if (!wasSelected) world.selection_pool[topmostEntity].isSelected = true;
                } else if (currentSelectionmode == SelectionMode::MultiSelect) {
                    world.selection_pool[topmostEntity].isSelected = !world.selection_pool[topmostEntity].isSelected;
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
    SDL_SetRenderDrawColor(renderer, 125, 125, 125, 255);
    SDL_RenderRect(renderer, &editorCanvasRect);
}

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
        virtual void setRect(SDL_FRect r) = 0;

        // Unified Lifecycle (Signatures must match exactly)
        virtual void render(float offsetY = 0.0f) = 0;
        virtual bool handleEvent(const SDL_Event& e, SDL_Window* window, float offsetY = 0.0f) = 0;
        virtual void handleGamepad(float cursorX, float cursorY, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) = 0;
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
    // Scrollbar — vertical, draggable thumb, fires onChange(newOffset)
    // Not an IGuiElement: it is owned and driven by Panel internally.
    // ----------------------------------------------------------------
    class Scrollbar {
    public:
        float trackX = 0, trackY = 0, trackW = 10, trackH = 100;
        float contentHeight = 0;   // total scrollable content height
        float viewHeight    = 0;   // visible area height
        float offset        = 0;   // current scroll offset (0 = top)
        std::function<void(float)> onChange;

        void setGeometry(float x, float y, float w, float h,
                         float contentH, float viewH) {
            trackX = x; trackY = y; trackW = w; trackH = h;
            contentHeight = contentH; viewHeight = viewH;
            offset = std::clamp(offset, 0.0f, maxOffset());
        }

        float maxOffset() const {
            return std::max(0.0f, contentHeight - viewHeight);
        }

        // returns true if the event was consumed
        bool handleEvent(const SDL_Event& ev) {
            if (maxOffset() <= 0) return false;
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float mx = ev.button.x, my = ev.button.y;
                SDL_FRect thumb = thumbRect();
                if (inRect(mx, my, thumb)) { dragging = true; dragStartY = my; dragStartOffset = offset; return true; }
            }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP)   { dragging = false; }
            if (ev.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                float dy = ev.motion.y - dragStartY;
                float ratio = dy / (trackH - thumbHeight());
                setOffset(dragStartOffset + ratio * maxOffset());
                return true;
            }
            if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                float mx, my; SDL_GetMouseState(&mx, &my);
                SDL_FRect track = { trackX, trackY, trackW, trackH };
                // caller clips wheel events to panel bounds; we just consume
                setOffset(offset + (ev.wheel.y > 0 ? -40.f : 40.f));
                return true;
            }
            return false;
        }

        void render(SDL_Renderer* renderer) {
            // track
            SDL_FRect track = { trackX, trackY, trackW, trackH };
            SDL_SetRenderDrawColor(renderer, 45, 45, 45, 200);
            SDL_RenderFillRect(renderer, &track);
            if (maxOffset() <= 0) return;
            // thumb
            SDL_FRect thumb = thumbRect();
            bool hov = false;
            float mx, my; SDL_GetMouseState(&mx, &my);
            hov = inRect(mx, my, thumb) || dragging;
            SDL_SetRenderDrawColor(renderer, hov ? 160 : 110, hov ? 160 : 110, hov ? 160 : 110, 255);
            SDL_RenderFillRect(renderer, &thumb);
        }

    private:
        bool  dragging = false;
        float dragStartY = 0, dragStartOffset = 0;

        float thumbHeight() const {
            if (contentHeight <= 0) return trackH;
            return std::max(20.0f, trackH * (viewHeight / contentHeight));
        }
        SDL_FRect thumbRect() const {
            float ratio = (maxOffset() > 0) ? offset / maxOffset() : 0;
            float ty = trackY + ratio * (trackH - thumbHeight());
            return { trackX, ty, trackW, thumbHeight() };
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
        float getMin() const { return minVal; }
        float getMax() const { return maxVal; }
        float getStep() const { return step; }

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

        bool handleEvent(const SDL_Event& ev, SDL_Window* window, float offsetY) {
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

        void handleGamepad(float cursorX, float cursorY, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) {
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

        void render(float offsetY) {
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
        const std::string& getPlaceholder() const { return placeholder; }

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

        void handleGamepad(float cursorX, float cursorY, float offsetY, SDL_Window* window, bool confirmDown, bool confirmDownLastFrame) {
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

        void render(float offsetY) {
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
        void setRect(SDL_FRect r) override { position = {r.x, r.y}; width = r.w; height = r.h; }
        const std::string& getText() const { return textStr; }

        // IGuiElement pure virtual overrides — delegate to Impl helpers
        void render(float offsetY = 0.0f) override { renderImpl(offsetY); }
        bool handleEvent(const SDL_Event& e, SDL_Window* /*window*/, float offsetY = 0.0f) override {
            handleEventImpl(e, offsetY); return false;
        }
        void handleGamepad(float cursorX, float cursorY, float offsetY, SDL_Window* /*window*/,
                           bool confirmDown, bool confirmDownLastFrame) override {
            handleGamepadImpl(cursorX, cursorY, offsetY, confirmDown, confirmDownLastFrame);
        }

        // Old-signature overloads called directly from main.cpp
        void handleEvent(const SDL_Event& e, float offsetY) { handleEventImpl(e, offsetY); }
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

        // Children are owned by the Panel
        void addChild(std::unique_ptr<IGuiElement> child) {
            children.push_back(std::move(child));
            refreshScrollbarGeometry();
        }
        const std::vector<std::unique_ptr<IGuiElement>>& getChildren() const { return children; }

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
}

// ============================================================
// ScriptBase — every scene must reference a script that derives
// from this class.  onStart/onUpdate/onDraw/onEnd are called by
// the runtime; the editor validates that the attachment exists.
// ============================================================
class ScriptBase {
public:
    virtual ~ScriptBase() = default;
    virtual void onStart()            {}
    virtual void onUpdate(float /*dt*/) {}
    virtual void onDraw()             {}
    virtual void onEnd()              {}
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

class SceneParser {
private:
    SDL_Renderer* renderer;
    TTF_TextEngine* textEngine;
    TTF_Font* font;
    SDL_Window* window;

public:
    SceneParser(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_Window* w)
        : renderer(r), textEngine(te), font(f), window(w) {}

    // ==========================================
    // LOAD SCENE
    // ==========================================
    Scene loadFromFile(const std::string& filepath) {
        Scene scene;
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "Failed to open scene file: " << filepath << "\n";
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
            // Check that the referenced script file actually exists
            scene.scriptValid = std::filesystem::exists(scene.scriptAttached);
            if (!scene.scriptValid)
                std::cerr << "[Editor] WARNING: script_attached '"
                          << scene.scriptAttached
                          << "' not found on disk — scene marked INVALID.\n";
            else
                std::cout << "[Editor] Scene '" << scene.name
                          << "' — script '" << scene.scriptAttached << "' OK.\n";
        }

        // --- 1. Parse ECS Entities ---
        if (j.contains("entities")) {
            for (const auto& entityJson : j["entities"]) {
                Entity id = scene.world.create_entity();
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
    std::unique_ptr<Gui::IGuiElement> parseGuiElement(const nlohmann::json& elemJson) {
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
    }};
