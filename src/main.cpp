#include "engine.h"
#include <string>

void create_entity_with_user_input(ECSWorld& world, const std::string& name, float w, float h) {
    Entity e = world.create_entity();
    world.add_metadata(e);
    world.add_position(e);
    world.add_rectangle_shape(e);
    world.add_z_index(e);
    world.add_selection(e);
    world.metadata_pool[e] = { name };
    world.position_pool[e] = { 0.0f, 0.0f };
    world.rectangle_shape_pool[e] = { w, h };
    world.z_index_pool[e].z = (world.entity_count > 1) ? world.z_index_pool[world.entity_count-2].z + 1 : 1;
    world.selection_pool[e] = { false, {0, 255, 0, 255} };
}

class AddEntityDialog : public Gui::Dialog {
public:
    Gui::LineEdit nameField;
    Gui::SpinBox  widthBox;
    Gui::SpinBox  heightBox;

    AddEntityDialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font, SDL_Window* window)
        : Gui::Dialog(renderer, textEngine, font, window,
                      {480.0f, 245.0f, 480.0f, 310.0f},
                      "Add Entity", "Create", "Cancel")
        , nameField(renderer, textEngine, font, {0,0,1,1}, "Entity name...")
        , widthBox(renderer, textEngine, font, {0,0,1,1}, 1.0f, 9999.0f, 50.0f)
        , heightBox(renderer, textEngine, font, {0,0,1,1}, 1.0f, 9999.0f, 50.0f)
    {}

    ~AddEntityDialog() override = default;

    Gui::ITextInput* getCurrentTextInput() override {
        if (nameField.isActive()) return &nameField;
        if (widthBox.isActive()) return &widthBox;
        if (heightBox.isActive()) return &heightBox;
        return nullptr;
    }

    bool onHandleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) override {
        bool wasActive = nameField.isActive() || widthBox.isActive() || heightBox.isActive();
        nameField.handleGamepad(cursorX, cursorY, 0.0f, window, confirmDown, confirmDownLastFrame);
        widthBox.handleGamepad(cursorX, cursorY, 0.0f, window, confirmDown, confirmDownLastFrame);
        heightBox.handleGamepad(cursorX, cursorY, 0.0f, window, confirmDown, confirmDownLastFrame);
        bool isActive = nameField.isActive() || widthBox.isActive() || heightBox.isActive();
        if (confirmDown && !confirmDownLastFrame && (isActive || wasActive)) return true;
        return false;
    }

protected:
    void onOpen() override {}

    bool onHandleEvent(const SDL_Event& ev) override {
        if (nameField.handleEvent(ev, window, 0.0f)) return true;
        if (widthBox.handleEvent(ev, window, 0.0f)) return true;
        if (heightBox.handleEvent(ev, window, 0.0f)) return true;
        return false;
    }

    void onRender(SDL_FRect win) override {
        drawText("Name:",   win.x + 14.0f,            win.y + 45.0f, {80,80,80,255});
        drawText("Width:",  win.x + 14.0f,            win.y + 130.0f, {80,80,80,255});
        drawText("Height:", win.x + win.w*0.5f + 8.0f, win.y + 130.0f, {80,80,80,255});
        nameField.setRect({ win.x+14,           win.y+70,  win.w-28,        36 });
        widthBox.setRect ({ win.x+14,           win.y+156, win.w*0.5f-22,   32 });
        heightBox.setRect({ win.x+win.w*0.5f+8, win.y+156, win.w*0.5f-22,   32 });
        nameField.render(0.0f);
        widthBox.render(0.0f);
        heightBox.render(0.0f);
    }

    void onReset() override {
        nameField.clear();
        nameField.deactivate(window);
        widthBox.deactivate(window);
        heightBox.deactivate(window);
    }
};

int main(int argc, char* argv[]) {
    const float windowWidth  = 1600.0f;
    const float windowHeight = 900.0f;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << "SDL Initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    if (!TTF_Init()) {
        std::cerr << "TTF Initialization failed: " << SDL_GetError() << std::endl;
        SDL_Quit(); return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Dispersed Engine", (int)windowWidth, (int)windowHeight, 0);
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        TTF_Quit(); SDL_Quit(); return 1;
    }

    SDL_Surface* iconSurface = IMG_Load("assets/icon.svg");
    if (!iconSurface) SDL_Log("Failed to load icon: %s", SDL_GetError());
    else SDL_SetWindowIcon(window, iconSurface);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1;
    }

    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);
    TTF_Font* font = TTF_OpenFont("assets/fredoka.ttf", 20);
    if (!font) std::cerr << "Failed to load font: " << SDL_GetError() << std::endl;

    // ── Gamepad ─────────────────────────────────────────────────────────────
    SDL_Gamepad* gamepad = nullptr;
    const float deadzone         = 0.2f;
    const float stickScrollSpeed = 400.0f;
    const float stickCursorSpeed = 800.0f;

    int gamepadCount = 0;
    SDL_JoystickID* gamepadIDs = SDL_GetGamepads(&gamepadCount);
    if (gamepadIDs && gamepadCount > 0) {
        gamepad = SDL_OpenGamepad(gamepadIDs[0]);
        if (gamepad) SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(gamepad));
        else SDL_Log("Failed to open gamepad: %s", SDL_GetError());
        SDL_free(gamepadIDs);
    } else {
        SDL_Log("No gamepads found at startup.");
    }

    float cursorX = windowWidth  / 2.0f;
    float cursorY = windowHeight / 2.0f;
    bool confirmLastFrame = false;
    float lastTriggerValue = 0.0f; // for L2 rising edge detection

    AddEntityDialog* dialog = new AddEntityDialog(renderer, textEngine, font, window);
    Gui::VirtualKeyboard* virtualKeyboard = new Gui::VirtualKeyboard(renderer, textEngine, font);
    bool showVirtualKeyboard = false;

    // ── Load scene from file ─────────────────────────────────────────────────
    SceneParser sceneParser(renderer, textEngine, font, window);
    Scene scene = sceneParser.loadFromFile("../projects/FirstProject/scenes/mainmenu.json");
    ECSWorld& world = scene.world;

    if (!scene.scriptValid)
        SDL_Log("[Editor] Scene '%s' is INVALID — script_attached='%s' missing or empty.",
                scene.name.c_str(), scene.scriptAttached.c_str());
    else
        SDL_Log("[Editor] Scene '%s' loaded OK — script: %s",
                scene.name.c_str(), scene.scriptAttached.c_str());

    // ── Toolbar buttons ──────────────────────────────────────────────────────
    Gui::Button loadBtn(renderer, font, "Open", { 5.0f, 15.0f }, 100.0f, 80.0f);
    loadBtn.onClicked = [&scene, &sceneParser](){
        scene = sceneParser.loadFromFile("mainmenu.json");
        if (!scene.scriptValid)
            SDL_Log("[Editor] Scene reloaded but INVALID — missing or not-found script '%s'",
                    scene.scriptAttached.c_str());
        else
            SDL_Log("[Editor] Scene reloaded OK — script: %s", scene.scriptAttached.c_str());
    };

    Gui::Button addEntBtn(renderer, font, "AddEnt", { 5.0f + 80.0f+45.0f, 15.0f }, 100.0f, 80.0f);
    addEntBtn.onClicked = [dialog](){
        currentEditMode = EditMode::Dialog;
        dialog->open();
    };

    Gui::Button selectModeBtn(renderer, font, "Sel", { 5.0f + 80.0f+45.0f+80.0f+45.0f, 15.0f }, 100.0f, 80.0f);
    selectModeBtn.onClicked = [](){ currentEditMode = EditMode::Select; };

    Gui::Button moveBtn(renderer, font, "Move", { 5.0f, 15.0f+80.0f+30.0f }, 100.0f, 80.0f);
    moveBtn.onClicked = [](){ currentEditMode = EditMode::MoveWithMouse; };

    Gui::Button selectSingleBtn(renderer, font, "Sel-0", { 5.0f, 15.0f+80.0f+30.0f+80.0f+30.0f }, 100.0f, 80.0f);
    selectSingleBtn.onClicked = [](){ currentSelectionmode = SelectionMode::SingleSelect; };

    Gui::Button selectMultiBtn(renderer, font, "Sel-1", { 5.0f, 15.0f+80.0f+30.0f+80.0f+30.0f+80.0f+30.0f }, 100.0f, 80.0f);
    selectMultiBtn.onClicked = [](){ currentSelectionmode = SelectionMode::MultiSelect; };

    Gui::Button deleteBtn(renderer, font, "DelEnt",
        { 5.0f, 15.0f+80.0f+30.0f+80.0f+30.0f+80.0f+30.0f+80.0f+30.0f+80.0f+30.0f },
        100.0f, 80.0f);
    deleteBtn.onClicked = [](){ currentEditMode = EditMode::Delete; };

    Gui::Button deselectBtn(renderer, font, "DeSel*",
        { 5.0f, 15.0f+80.0f+30.0f+80.0f+30.0f+80.0f+30.0f+80.0f+30.0f },
        100.0f, 80.0f);
    deselectBtn.onClicked = [&world](){ deselect_all(world); };

    bool running = true;
    SDL_Event e;
    float scrollOffset           = 0.0f;
    const float maxScrollOffset  = 1000.0f;
    const float scrollSpeed      = 60.0f;
    const float scrollbarWidth   = 8.0f;
    const float scrollbarPadding = 4.0f;
    const float totalContentHeight = windowHeight + maxScrollOffset;

    Uint64 last_time = SDL_GetTicks();
    float  delta_time = 0.0f;

    while (running) {
        Uint64 current_time = SDL_GetTicks();
        delta_time = (float)(current_time - last_time) / 1000.0f;
        last_time = current_time;

        // ── Events ───────────────────────────────────────────────────────────
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;

            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad) {
                gamepad = SDL_OpenGamepad(e.gdevice.which);
                if (gamepad) SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(gamepad));
            }
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (gamepad && SDL_GetGamepadID(gamepad) == e.gdevice.which) {
                    SDL_CloseGamepad(gamepad);
                    gamepad = nullptr;
                    SDL_Log("Gamepad disconnected.");
                }
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                scrollOffset += (e.wheel.y > 0.0f) ? -scrollSpeed : scrollSpeed;
                scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);
            }

            if (dialog->isOpen()) {
                if (showVirtualKeyboard && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    Gui::ITextInput* currentInput = dialog->getCurrentTextInput();
                    virtualKeyboard->handleMouse(e, currentInput);
                    if (virtualKeyboard->isCloseRequested()) {
                        showVirtualKeyboard = false;
                        virtualKeyboard->resetCloseRequest();
                        if (currentInput) currentInput->setActive(false);
                    }
                }

                auto action = dialog->handleEvent(e);
                if (action == Gui::Dialog::Action::Confirm) {
                    const std::string& name = dialog->nameField.getText();
                    if (!name.empty())
                        create_entity_with_user_input(world, name,
                            dialog->widthBox.getValue(),
                            dialog->heightBox.getValue());
                    dialog->reset();
                    showVirtualKeyboard = false;
                } else if (action == Gui::Dialog::Action::Cancel) {
                    dialog->reset();
                    showVirtualKeyboard = false;
                }
                continue;
            }

            // Normal editor events
            loadBtn.handleEvent(e, scrollOffset);
            addEntBtn.handleEvent(e, scrollOffset);
            selectModeBtn.handleEvent(e, scrollOffset);
            moveBtn.handleEvent(e, scrollOffset);
            selectSingleBtn.handleEvent(e, scrollOffset);
            selectMultiBtn.handleEvent(e, scrollOffset);
            deselectBtn.handleEvent(e, scrollOffset);
            deleteBtn.handleEvent(e, scrollOffset);
            edit_object_with_editor_mouse(renderer, world, e);

            // ── Scene GUI elements — always at fixed screen positions (offsetY=0)
            for (auto& elem : scene.guiElements)
                elem->handleEvent(e, window, 0.0f);
        }

        // ── Gamepad ──────────────────────────────────────────────────────────
        if (gamepad) {
            auto applyAxis = [&](SDL_GamepadAxis axis) -> float {
                float raw = SDL_GetGamepadAxis(gamepad, axis) / 32767.0f;
                if (std::abs(raw) <= deadzone) return 0.0f;
                float sign = raw > 0.0f ? 1.0f : -1.0f;
                return sign * ((std::abs(raw) - deadzone) / (1.0f - deadzone));
            };

            float lx = applyAxis(SDL_GAMEPAD_AXIS_LEFTX);
            float ly = applyAxis(SDL_GAMEPAD_AXIS_LEFTY);
            float ry = applyAxis(SDL_GAMEPAD_AXIS_RIGHTY);

            cursorX = std::clamp(cursorX + lx * stickCursorSpeed * delta_time, 0.0f, windowWidth);
            cursorY = std::clamp(cursorY + ly * stickCursorSpeed * delta_time, 0.0f, windowHeight);
            scrollOffset = std::clamp(scrollOffset + ry * stickScrollSpeed * delta_time, 0.0f, maxScrollOffset);

            bool confirmNow = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);

            // ── Virtual keyboard gamepad extras ────────────────────────────
            if (showVirtualKeyboard) {
                // L2 trigger toggles Shift (rising edge)
                float trigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
                bool triggerPressed = trigger > 0.5f;
                if (triggerPressed && lastTriggerValue <= 0.5f) {
                    virtualKeyboard->toggleShift();
                }
                lastTriggerValue = trigger;

                // East button (B/Circle) closes the keyboard
                if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST) && !confirmLastFrame) {
                    virtualKeyboard->requestClose();
                }
            }

            if (dialog->isOpen()) {
                Gui::ITextInput* currentInput = dialog->getCurrentTextInput();
                bool wasActive = (currentInput != nullptr);

                if (showVirtualKeyboard) {
                    virtualKeyboard->handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame, currentInput);
                    if (virtualKeyboard->isCloseRequested()) {
                        showVirtualKeyboard = false;
                        virtualKeyboard->resetCloseRequest();
                        if (currentInput) currentInput->setActive(false);
                        confirmLastFrame = confirmNow;
                        continue;
                    }
                } else {
                    auto action = dialog->handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
                    Gui::ITextInput* newInput = dialog->getCurrentTextInput();
                    if (!wasActive && newInput != nullptr && action == Gui::Dialog::Action::None) {
                        showVirtualKeyboard = true;
                        virtualKeyboard->layoutKeys(newInput->getRect(), windowWidth, windowHeight,
                                                    newInput->isNumericOnly());
                    }
                    if (action == Gui::Dialog::Action::Confirm) {
                        const std::string& name = dialog->nameField.getText();
                        if (!name.empty())
                            create_entity_with_user_input(world, name,
                                dialog->widthBox.getValue(),
                                dialog->heightBox.getValue());
                        dialog->reset();
                        showVirtualKeyboard = false;
                    } else if (action == Gui::Dialog::Action::Cancel) {
                        dialog->reset();
                        showVirtualKeyboard = false;
                    }
                }
            } else {
                showVirtualKeyboard = false;
                loadBtn.handleGamepad(cursorX, cursorY, scrollOffset, confirmNow, confirmLastFrame);
                addEntBtn.handleGamepad(cursorX, cursorY, scrollOffset, confirmNow, confirmLastFrame);
                selectModeBtn.handleGamepad(cursorX, cursorY, scrollOffset, confirmNow, confirmLastFrame);
                moveBtn.handleGamepad(cursorX, cursorY, scrollOffset, confirmNow, confirmLastFrame);
                selectSingleBtn.handleGamepad(cursorX, cursorY, scrollOffset, confirmNow, confirmLastFrame);
                selectMultiBtn.handleGamepad(cursorX, cursorY, scrollOffset, confirmNow, confirmLastFrame);
                deselectBtn.handleGamepad(cursorX, cursorY, scrollOffset, confirmNow, confirmLastFrame);
                deleteBtn.handleGamepad(cursorX, cursorY, scrollOffset, confirmNow, confirmLastFrame);
                edit_object_with_editor_gamepad(world, cursorX, cursorY, confirmNow, confirmLastFrame);

                // ── Scene GUI elements (gamepad) — fixed screen positions
                for (auto& elem : scene.guiElements)
                    elem->handleGamepad(cursorX, cursorY, 0.0f, window, confirmNow, confirmLastFrame);
            }

            confirmLastFrame = confirmNow;
        }

        // ── Render ───────────────────────────────────────────────────────────
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        loadBtn.render(scrollOffset, cursorX, cursorY);
        addEntBtn.render(scrollOffset, cursorX, cursorY);
        selectModeBtn.render(scrollOffset, cursorX, cursorY);
        moveBtn.render(scrollOffset, cursorX, cursorY);
        selectSingleBtn.render(scrollOffset, cursorX, cursorY);
        selectMultiBtn.render(scrollOffset, cursorX, cursorY);
        deselectBtn.render(scrollOffset, cursorX, cursorY);
        deleteBtn.render(scrollOffset, cursorX, cursorY);

        // Scrollbar
        SDL_FRect scrollTrack = {
            windowWidth - scrollbarWidth - scrollbarPadding, scrollbarPadding,
            scrollbarWidth, windowHeight - (scrollbarPadding * 2.0f)
        };
        SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
        SDL_RenderFillRect(renderer, &scrollTrack);

        float thumbHeight = std::max(30.0f, scrollTrack.h * (windowHeight / totalContentHeight));
        float thumbY = scrollTrack.y + (scrollOffset / maxScrollOffset) * (scrollTrack.h - thumbHeight);
        SDL_FRect scrollThumb = { scrollTrack.x, thumbY, scrollbarWidth, thumbHeight };
        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        SDL_RenderFillRect(renderer, &scrollThumb);

        render_editor_canvas(renderer);
        movement_system(world, delta_time);
        render_system_in_editor(renderer, textEngine, font, world);

        // ── Scene GUI elements — rendered at fixed screen positions (no editor scroll)
        for (auto& elem : scene.guiElements)
            elem->render(0.0f);

        // ── Script validity banner ─────────────────────────────────────────
        if (!scene.scriptValid && font && textEngine) {
            SDL_SetRenderDrawColor(renderer, 180, 30, 30, 200);
            SDL_FRect banner = { editorCanvasRect.x, editorCanvasRect.y,
                                 editorCanvasRect.w, 28.0f };
            SDL_RenderFillRect(renderer, &banner);
            std::string msg = "INVALID SCENE — script_attached missing or not found: \"" +
                              scene.scriptAttached + "\"";
            TTF_Text* t = TTF_CreateText(textEngine, font, msg.c_str(), 0);
            if (t) {
                TTF_SetTextColor(t, 255, 220, 220, 255);
                TTF_DrawRendererText(t, banner.x + 8.0f, banner.y + 5.0f);
                TTF_DestroyText(t);
            }
        }

        dialog->tick(delta_time);
        if (dialog->isOpen()) dialog->render();

        // Gamepad crosshair (always on top)
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_FRect crosshair_firstrect = { cursorX - 10.0f, cursorY - 1.0f, 20.0f, 2.0f };
        SDL_FRect crosshair_secondrect = { cursorX - 1.0f, cursorY - 10.0f, 2.0f, 20.0f };
        SDL_RenderFillRect(renderer, &crosshair_firstrect);
        SDL_RenderFillRect(renderer, &crosshair_secondrect);

        if (showVirtualKeyboard) {
            virtualKeyboard->render();
        }

        SDL_RenderPresent(renderer);
    }

    delete dialog;
    delete virtualKeyboard;

    if (gamepad) SDL_CloseGamepad(gamepad);
    if (font) TTF_CloseFont(font);
    if (iconSurface) SDL_DestroySurface(iconSurface);

    TTF_DestroyRendererTextEngine(textEngine);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}