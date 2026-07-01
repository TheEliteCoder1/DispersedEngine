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
    void onOpen() override {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        
        // Recalculate logicalRect to be perfectly centered
        logicalRect = {
            ((float)w - 480.0f) * 0.5f,  // X: (Window Width - Dialog Width) / 2
            ((float)h - 310.0f) * 0.5f,  // Y: (Window Height - Dialog Height) / 2
            480.0f,                      // Width
            310.0f                       // Height
        };
    }
    
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

// Handle Windows vs POSIX naming compliance
#if defined(_WIN32) || defined(_WIN64)
#define popen _popen
#define pclose _pclose
#endif


std::string get_system_output(const char* cmd) {
    std::array<char, 256> buffer;
    std::string result;
    
    // Open the pipe for reading (using POSIX popen)
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    
    // Read the command output into the buffer
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

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
    
    SDL_Window* window = SDL_CreateWindow("Dispersed Engine", (int)windowWidth, (int)windowHeight, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        TTF_Quit(); SDL_Quit(); return 1;
    }


    #ifdef __EMSCRIPTEN__
        SDL_Log("PLATFORM: Emscripten detected");
        SDL_Log("Projects path: %s", getProjectsPath().c_str());
        SDL_Log("Assets path: %s", getAssetsPath().c_str());
    #else
        SDL_Log("PLATFORM: Native build");
    #endif
        
    SDL_Surface* iconSurface = IMG_Load((getAssetsPath() + "icon.svg").c_str());
    if (!iconSurface) SDL_Log("Failed to load icon: %s", SDL_GetError());
    else SDL_SetWindowIcon(window, iconSurface);
    
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1;
    }
    
    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);
    TTF_Font* font = TTF_OpenFont((getAssetsPath() + "fredoka.ttf").c_str(), 20);
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
    float lastTriggerValue = 0.0f;
    
    AddEntityDialog* dialog = new AddEntityDialog(renderer, textEngine, font, window);
    Gui::VirtualKeyboard* virtualKeyboard = new Gui::VirtualKeyboard(renderer, textEngine, font);
    bool showVirtualKeyboard = false;
    
    // ── Load scene from file ─────────────────────────────────────────────────
    SceneParser sceneParser(renderer, textEngine, font, window);
    Scene scene;
    ECSWorld& world = scene.world;
    std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements = scene.guiElements;
    
    // ── Inspectors ─────────────────────────────────────────────────────────
    Gui::SceneInspector inspector(renderer, textEngine, font, window);
    Gui::IGuiElement* selectedGuiElem = nullptr;
    Gui::EntityInspector entityInspector(renderer, textEngine, font, window);
    Entity selectedEntity = (Entity)-1;
    
    auto selectEntity = [&](Entity e) {
        if (selectedEntity != e) {
            selectedEntity = e;
            if (e != (Entity)-1) {
                entityInspector.setTarget(world, e);
                if (selectedGuiElem) {
                    selectedGuiElem->editorSelected = false;
                    selectedGuiElem = nullptr;
                    inspector.setTarget(nullptr);
                }
            } else {
                entityInspector.clearTarget();
            }
        }
    };
    
    auto selectGuiElem = [&](Gui::IGuiElement* elem) {
        if (selectedGuiElem) selectedGuiElem->editorSelected = false;
        selectedGuiElem = elem;
        if (selectedGuiElem) selectedGuiElem->editorSelected = true;
        inspector.setTarget(elem);
        lastSelectedEntity = (Entity)-1;
        selectEntity((Entity)-1);
    };
    
    // ── Scrollbars for the editor canvas ──────────────────────────────────
    Gui::Scrollbar verticalScrollbar;
    Gui::Scrollbar horizontalScrollbar;
    verticalScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
    horizontalScrollbar.setOrientation(Gui::ScrollOrientation::Horizontal);
    
    static bool scrollConnected = false;
    if (!scrollConnected) {
        verticalScrollbar.onChange = [](float v){ editorScrollY = v; };
        horizontalScrollbar.onChange = [](float v){ editorScrollX = v; };
        scrollConnected = true;
    }
    
    // ── Build toolbar using layout containers ────────────────────────────
    // Helper to create buttons with original size (100x50)
    auto makeButton = [&](const std::string& label, std::function<void()> cb) {
        auto btn = std::make_unique<Gui::Button>(renderer, font, label, SDL_FPoint{0,0}, 100, 50);
        btn->onClicked = cb;
        return btn;
    };
    
    // Create all buttons
    auto loadBtn = makeButton("Open", [&](){
        if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
        inspector.setTarget(nullptr);
        scene = sceneParser.loadFromFile("projects/FirstProject/scenes/mainmenu.json");
        if (!scene.scriptValid)
            SDL_Log("[Editor] Scene reloaded but INVALID — missing or not-found script '%s'",
                    scene.scriptAttached.c_str());
        else
            SDL_Log("[Editor] Scene reloaded OK — script: %s", scene.scriptAttached.c_str());
    });
    
    auto saveBtn = makeButton("Save", [&](){
        inspector.commitAllFields();
        sceneParser.saveToFile(scene, getProjectsPath() + "FirstProject/scenes/mainmenu.json");
        SDL_Log("[Editor] Scene saved to mainmenu.json");
    });
    
    auto addEntBtn = makeButton("Add Entity", [dialog](){
        currentEditMode = EditMode::Dialog;
        dialog->open();
    });
    
    auto selectModeBtn = makeButton("Select", [](){ currentEditMode = EditMode::Select; });
    auto moveBtn = makeButton("Move", [](){ currentEditMode = EditMode::MoveWithMouse; });
    auto deleteBtn = makeButton("Delete", [](){ currentEditMode = EditMode::Delete; });
    
    auto selectSingleBtn = makeButton("Single Sel", [](){ currentSelectionmode = SelectionMode::SingleSelect; });
    auto selectMultiBtn = makeButton("Multi Sel", [](){ currentSelectionmode = SelectionMode::MultiSelect; });
    
    auto deselectBtn = makeButton("Deselect", [&world](){ deselect_all(world); });
    auto inspectorToggleBtn = makeButton("Inspector", [](){ inspectorVisible = !inspectorVisible; });

    // ── Layout: Single horizontal toolbar at the top ─────────────────────
    auto toolbar = std::make_unique<Gui::HBoxContainer>();
    toolbar->setPadding(5);
    toolbar->setSpacing(5);
    toolbar->addChild(std::move(loadBtn));
    toolbar->addChild(std::move(saveBtn));
    toolbar->addChild(std::move(addEntBtn));
    toolbar->addChild(std::move(selectModeBtn));
    toolbar->addChild(std::move(moveBtn));
    toolbar->addChild(std::move(deleteBtn));
    toolbar->addChild(std::move(selectSingleBtn));
    toolbar->addChild(std::move(selectMultiBtn));
    toolbar->addChild(std::move(deselectBtn));
    toolbar->addChild(std::move(inspectorToggleBtn));
    
    // Set toolbar rect at the top of the window
    toolbar->setRect({0, 0, windowWidth, 60});
    
    bool running = true;
    SDL_Event e;
    float scrollOffset           = 0.0f;
    const float maxScrollOffset  = 1000.0f;
    const float scrollSpeed      = 60.0f;
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
            
            // Toggle inspector with 'I' key (keep as backup)
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_I) {
                inspectorVisible = !inspectorVisible;
            }
            
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
            
            // Editor scrollbars get first chance at mouse wheel
            bool consumedByScrollbar = false;
            if (verticalScrollbar.handleEvent(e)) consumedByScrollbar = true;
            if (horizontalScrollbar.handleEvent(e)) consumedByScrollbar = true;
            
            if (!consumedByScrollbar && e.type == SDL_EVENT_MOUSE_WHEEL) {
                bool consumedByScene = false;
                for (auto& elem : guiElements) {
                    if (elem->handleEvent(e, window, 0.0f)) {
                        consumedByScene = true;
                        break;
                    }
                }
                if (!consumedByScene) {
                    scrollOffset += (e.wheel.y > 0.0f) ? -scrollSpeed : scrollSpeed;
                    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);
                }
            }
            
            // Forward events to toolbar
            toolbar->handleEvent(e, window, 0.0f);
            
            // ── Dialog handling ─────────────────────────────────────────────
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
            
            // ── Normal editor events ────────────────────────────────────────
            bool isSelectLeftClick = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                                     e.button.button == SDL_BUTTON_LEFT &&
                                     currentEditMode == EditMode::Select);
            
            if (!isSelectLeftClick) {
                edit_object_with_editor_mouse(renderer, world, scene.guiElements, e);
            }
            
            // ── Inspector event handling ────────────────────────────────────
            if (selectedGuiElem) {
                if (inspector.handleEvent(e)) continue;
            } else if (selectedEntity != (Entity)-1) {
                if (entityInspector.handleEvent(e)) continue;
            }
            
            // ── GUI element picking / entity selection ──────────────────────
            if (isSelectLeftClick) {
                float mx = e.button.x, my = e.button.y;
                bool guiHit = false;
                
                for (auto& elem : guiElements) {
                    if (elem->getType() == "Panel") {
                        auto* panel = static_cast<Gui::Panel*>(elem.get());
                        auto* child = panel->hitTest(mx, my);
                        if (child) {
                            selectGuiElem(child);
                            guiHit = true;
                            break;
                        }
                    } else {
                        SDL_FRect r = { elem->getX(), elem->getY(),
                                       elem->getWidth(), elem->getHeight() };
                        if (mx >= r.x && mx <= r.x+r.w && my >= r.y && my <= r.y+r.h) {
                            selectGuiElem(elem.get());
                            guiHit = true;
                            break;
                        }
                    }
                }
                
                if (!guiHit) {
                    edit_object_with_editor_mouse(renderer, world, guiElements, e);
                    if (lastSelectedEntity != (Entity)-1) {
                        selectEntity(lastSelectedEntity);
                    } else {
                        selectGuiElem(nullptr);
                    }
                } else {
                    lastSelectedEntity = (Entity)-1;
                    selectEntity((Entity)-1);
                }
            }
            
            // ── GUI element drag‑move ──────────────────────────────────────
            static float guiDragStartMouseX = 0, guiDragStartMouseY = 0;
            static float guiDragStartElemX  = 0, guiDragStartElemY  = 0;
            static bool  guiDragging = false;
            
            if (currentEditMode == EditMode::MoveWithMouse && selectedGuiElem) {
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                    float mx = e.button.x, my = e.button.y;
                    SDL_FRect r = { selectedGuiElem->getX(), selectedGuiElem->getY(),
                                   selectedGuiElem->getWidth(), selectedGuiElem->getHeight() };
                    if (mx >= r.x && mx <= r.x+r.w && my >= r.y && my <= r.y+r.h) {
                        guiDragging = true;
                        guiDragStartMouseX = mx; guiDragStartMouseY = my;
                        guiDragStartElemX = selectedGuiElem->getX();
                        guiDragStartElemY = selectedGuiElem->getY();
                    }
                }
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) guiDragging = false;
                
                if (e.type == SDL_EVENT_MOUSE_MOTION && guiDragging) {
                    float dx = e.motion.x - guiDragStartMouseX;
                    float dy = e.motion.y - guiDragStartMouseY;
                    SDL_FRect nr = {
                        guiDragStartElemX + dx,
                        guiDragStartElemY + dy,
                        selectedGuiElem->getWidth(),
                        selectedGuiElem->getHeight()
                    };
                    selectedGuiElem->setRect(nr);
                    inspector.commitAllFields();
                    inspector.setTarget(nullptr);
                    inspector.setTarget(selectedGuiElem);
                }
            }
            
            // ── Scene GUI elements — forward non‑wheel events ───────────────
            if (e.type != SDL_EVENT_MOUSE_WHEEL) {
                for (auto& elem : guiElements)
                    elem->handleEvent(e, window, 0.0f);
            }
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
            
            if (showVirtualKeyboard) {
                float trigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
                bool triggerPressed = trigger > 0.5f;
                if (triggerPressed && lastTriggerValue <= 0.5f) {
                    virtualKeyboard->toggleShift();
                }
                lastTriggerValue = trigger;
                
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
                
                // Toolbar buttons get gamepad events via container
                toolbar->handleGamepad(cursorX, cursorY, 0.0f, window, confirmNow, confirmLastFrame);
                
                edit_object_with_editor_gamepad(world, cursorX, cursorY, confirmNow, confirmLastFrame);
                
                for (auto& elem : guiElements)
                    elem->handleGamepad(cursorX, cursorY, 0.0f, window, confirmNow, confirmLastFrame);
                
                if (selectedGuiElem)
                    inspector.handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
                else if (selectedEntity != (Entity)-1)
                    entityInspector.handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
            }
            
            confirmLastFrame = confirmNow;
        }
        
        // ── Sync entity selection with lastSelectedEntity ──────────────────
        static Entity prevSelectedEntity = (Entity)-1;
        if (lastSelectedEntity != prevSelectedEntity) {
            if (lastSelectedEntity != (Entity)-1)
                selectEntity(lastSelectedEntity);
            else
                selectEntity((Entity)-1);
            prevSelectedEntity = lastSelectedEntity;
        }
        
        // ── Render ──────────────────────────────────────────────────────────
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);
        
        // ── Compute dynamic viewport ────────────────────────────────────────
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);
        
        const float toolbarHeight = 60.0f;
        const float inspectorWidth = inspectorVisible ? Gui::SceneInspector::PANEL_W : 0.0f;
        
        canvasViewX = 0.0f;
        canvasViewY = toolbarHeight;
        canvasViewW = (float)winW - (inspectorVisible ? inspectorWidth : 0.0f);
        canvasViewH = (float)winH - toolbarHeight;
        
        if (canvasViewW < 200) canvasViewW = 200;
        if (canvasViewH < 200) canvasViewH = 200;
        
        const float logicalCanvasWidth = 1390.0f;
        const float logicalCanvasHeight = 690.0f;
        
        // ── Update scrollbar geometries ─────────────────────────────────────
        const float sbSize = 12.0f;
        verticalScrollbar.setGeometry(
            canvasViewX + canvasViewW - sbSize,
            canvasViewY,
            sbSize,
            canvasViewH,
            logicalCanvasHeight,
            canvasViewH
        );
        
        horizontalScrollbar.setGeometry(
            canvasViewX,
            canvasViewY + canvasViewH - sbSize,
            canvasViewW - sbSize,
            sbSize,
            logicalCanvasWidth,
            canvasViewW - sbSize
        );
        
        // ── Render toolbar ──────────────────────────────────────────────────
        toolbar->render(0.0f);
        
        // ── Canvas and entities ─────────────────────────────────────────────
        render_editor_canvas(renderer);
        render_system_in_editor(renderer, textEngine, font, world,
                               canvasViewX, canvasViewY, editorScrollX, editorScrollY);
        
        // ── Scene GUI elements ──────────────────────────────────────────────
        for (auto& elem : guiElements)
            elem->render(0.0f);
        
        // ── Render scrollbars (drawn on top of everything) ─────────────────
        verticalScrollbar.render(renderer);
        horizontalScrollbar.render(renderer);
        
        // ── Inspector ────────────────────────────────────────────────────────
        if (inspectorVisible) {
            if (selectedGuiElem) {
                inspector.render((float)winH);
            } else if (selectedEntity != (Entity)-1) {
                entityInspector.commitAllFields(); // Write any pending user edits to the world
                entityInspector.syncFromWorld();   // Update fields to match external changes (like dragging)
                entityInspector.render((float)winH);
            } else {
                float rightX = (float)winW - Gui::SceneInspector::PANEL_W;
                SDL_FRect bg = { rightX, toolbarHeight, Gui::SceneInspector::PANEL_W, (float)winH - toolbarHeight };
                SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245);
                SDL_RenderFillRect(renderer, &bg);
                SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255);
                SDL_RenderRect(renderer, &bg);
                
                TTF_Text* t = TTF_CreateText(textEngine, font, "Inspector", 0);
                if (t) {
                    TTF_SetTextColor(t, 180, 180, 200, 255);
                    TTF_DrawRendererText(t, bg.x + 10, bg.y + 10);
                    TTF_DestroyText(t);
                }
                
                t = TTF_CreateText(textEngine, font, "Click an entity \n or GUI element.", 0);
                if (t) {
                    TTF_SetTextColor(t, 100, 100, 120, 255);
                    TTF_DrawRendererText(t, bg.x + 10, bg.y + 40);
                    TTF_DestroyText(t);
                }
            }
        }
        
        dialog->tick(delta_time);
        if (dialog->isOpen()) dialog->render();
        
        // Gamepad crosshair
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