#include "engine.h"

bool showCanvas = true;
EngineResources g_resources;


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

void create_gui_element_with_user_input(
    SDL_Renderer* renderer,
    TTF_TextEngine* textEngine,
    TTF_Font* font,
    std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements,
    const std::string& type,
    const std::string& name
) {
    if (type == "Button") {
        auto btn = std::make_unique<Gui::Button>(renderer, font, "btn", SDL_FPoint{0.0f,0.0f}, 100.0f, 50.0f);
        guiElements.push_back(std::move(btn));
    }
    else if (type == "LineEdit") {
        auto le = std::make_unique<Gui::LineEdit>(renderer, textEngine, font, SDL_FRect{0,0,200,36}, "Type here...");
        guiElements.push_back(std::move(le));
    }
    else if (type == "SpinBox") {
        auto sb = std::make_unique<Gui::SpinBox>(renderer, textEngine, font, SDL_FRect{0,0,200,36}, 0.0f, 100.0f, 50.0f, 1.0f);
        guiElements.push_back(std::move(sb));
    }
}

class AddEntityDialog : public Gui::Dialog {
public:
    Gui::LineEdit nameField;
    Gui::SpinBox  widthBox;
    Gui::SpinBox  heightBox;
    AddEntityDialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font, SDL_Window* window)
        : Gui::Dialog(renderer, textEngine, font, window,
                      {480.0f, 245.0f, 480.0f, 310.0f},
                      "+ Entity", "Create", "Cancel")
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
        nameField.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
        widthBox.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
        heightBox.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
        bool isActive = nameField.isActive() || widthBox.isActive() || heightBox.isActive();
        if (confirmDown && !confirmDownLastFrame && (isActive || wasActive)) return true;
        return false;
    }

protected:
    void onOpen() override {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        logicalRect = {
            ((float)w - 480.0f) * 0.5f,
            ((float)h - 310.0f) * 0.5f,
            480.0f, 310.0f
        };
    }
    bool onHandleEvent(const SDL_Event& ev) override {
        if (nameField.handleEvent(ev, window, 0.0f, 0.0f)) return true;
        if (widthBox.handleEvent(ev, window, 0.0f, 0.0f)) return true;
        if (heightBox.handleEvent(ev, window, 0.0f, 0.0f)) return true;
        return false;
    }
    void onRender(SDL_FRect win) override {
        drawText("Name:",   win.x + 14.0f,            win.y + 45.0f, {80,80,80,255});
        drawText("Width:",  win.x + 14.0f,            win.y + 130.0f, {80,80,80,255});
        drawText("Height:", win.x + win.w*0.5f + 8.0f, win.y + 130.0f, {80,80,80,255});
        nameField.setRect({ win.x+14,           win.y+70,  win.w-28,        36 });
        widthBox.setRect ({ win.x+14,           win.y+156, win.w*0.5f-22,   32 });
        heightBox.setRect({ win.x+win.w*0.5f+8, win.y+156, win.w*0.5f-22,   32 });
        nameField.render(0.0f, 0.0f);
        widthBox.render(0.0f, 0.0f);
        heightBox.render(0.0f, 0.0f);
    }
    void onReset() override {
        nameField.clear();
        nameField.deactivate(window);
        widthBox.deactivate(window);
        heightBox.deactivate(window);
    }
};

class AddGuiElemDialog: public Gui::Dialog {
public:
    std::vector<std::string> options = {
        "LineEdit",
        "SpinBox",
        "Button"
    };
    Gui::OptionBox guiElemType;
    AddGuiElemDialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font, SDL_Window* window)
        : Gui::Dialog(renderer, textEngine, font, window,
                      {480.0f, 245.0f, 480.0f, 310.0f},
                      "+ GuiElem", "Create", "Cancel")
        , guiElemType(renderer, textEngine, font, {0,0,1,1}, options)
    {
    }
    ~AddGuiElemDialog() override = default;

    bool onHandleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) override {
        guiElemType.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
        return true;
    }

protected:
    void onOpen() override {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        logicalRect = {
            ((float)w - 480.0f) * 0.5f,
            ((float)h - 310.0f) * 0.5f,
            480.0f, 310.0f
        };
    }
    bool onHandleEvent(const SDL_Event& ev) override {
        if (guiElemType.handleEvent(ev, window, 0.0f, 0.0f)) return true;
        return false;
    }
    void onRender(SDL_FRect win) override {
        drawText("Type:",win.x + 14.0f,win.y + 45.0f, {80,80,80,255});
        guiElemType.setRect({ win.x+14,win.y+70,win.w-28,36 });
        guiElemType.render(0.0f, 0.0f);
    }
};

namespace Gui {

    AddChildDialog::AddChildDialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                                   SDL_Window* window, Panel* parentPanel)
        : Dialog(renderer, textEngine, font, window, {0,0,400,200}, "Add Child", "Add", "Cancel"),
          parentPanel(parentPanel),
          typeOption(renderer, textEngine, font, {0,0,1,1}, options)
    {}

    bool AddChildDialog::onHandleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) {
        typeOption.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
        return true; // consume event
    }

    void AddChildDialog::onOpen() {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        logicalRect = {
            ((float)w - 400.0f) * 0.5f,
            ((float)h - 200.0f) * 0.5f,
            400.0f, 200.0f
        };
    }

    bool AddChildDialog::onHandleEvent(const SDL_Event& ev) {
        if (typeOption.handleEvent(ev, window, 0.0f, 0.0f))
            return true;
        return false;
    }

    void AddChildDialog::onRender(SDL_FRect win) {
        drawText("Child Type:", win.x + 14, win.y + 45, {80,80,80,255});
        typeOption.setRect({ win.x + 14, win.y + 70, win.w - 28, 36 });
        typeOption.render(0.0f, 0.0f);
    }

    void AddChildDialog::onReset() {
        // Nothing to reset – the parent panel is set externally
    }

} // namespace Gui


#if defined(_WIN32) || defined(_WIN64)
#define popen _popen
#define pclose _pclose
#endif

std::string get_system_output(const char* cmd) {
    std::array<char, 256> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

int main(int argc, char* argv[]) {
    const float windowWidth  = 1600.0f;
    const float windowHeight = 900.0f;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { std::cerr << "SDL Initialization failed: " << SDL_GetError() << std::endl; return 1; }
    if (!TTF_Init()) { std::cerr << "TTF Initialization failed: " << SDL_GetError() << std::endl; SDL_Quit(); return 1; }
    if (!MIX_Init()) { SDL_Log("Could not initialize mixer: %s", SDL_GetError()); SDL_Quit(); return -1; }
    
    SDL_Window* window = SDL_CreateWindow("Dispersed Engine", (int)windowWidth, (int)windowHeight, SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "Window creation failed: " << SDL_GetError() << std::endl; TTF_Quit(); SDL_Quit(); return 1; }
    SDL_SetWindowOpacity(window, 0.95f);
    
    SDL_Surface* iconSurface = IMG_Load((getAssetsPath() + "icon.svg").c_str());
    if (!iconSurface) SDL_Log("Failed to load icon: %s", SDL_GetError()); else SDL_SetWindowIcon(window, iconSurface);
    
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) { std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl; SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1; }
    
    g_resources.TextureManager.SetRenderer(renderer);
    if (!g_resources.AudioManager.CreateMixerDevice()) { SDL_Log("Audio mixer init failed"); }
    
    TTF_TextEngine* textEngine = TTF_CreateRendererTextEngine(renderer);
    g_resources.FontManager.Load("regularFont", getAssetsPath() + "fredoka.ttf", 20);
    g_resources.FontManager.Load("largeFont", getAssetsPath() + "fira.ttf", 23);
    
    Gui::TextEditor textEditor(renderer, textEngine, g_resources.FontManager.Get("largeFont"), {0, 0, 100, 100});
    textEditor.setVisible(false);
    
    std::filesystem::path projectsAbsPath = std::filesystem::absolute(getProjectsPath());
    Gui::FileExplorer fileExplorer(renderer, textEngine, g_resources.FontManager.Get("regularFont"), window, projectsAbsPath.string(), "*.json");
    
    SDL_Gamepad* gamepad = nullptr;
    const float deadzone = 0.2f, stickScrollSpeed = 400.0f, stickCursorSpeed = 800.0f;
    int gamepadCount = 0;
    SDL_JoystickID* gamepadIDs = SDL_GetGamepads(&gamepadCount);
    if (gamepadIDs && gamepadCount > 0) {
        gamepad = SDL_OpenGamepad(gamepadIDs[0]);
        if (gamepad) SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(gamepad)); else SDL_Log("Failed to open gamepad: %s", SDL_GetError());
        SDL_free(gamepadIDs);
    } else { SDL_Log("No gamepads found at startup."); }
    
    float cursorX = windowWidth / 2.0f, cursorY = windowHeight / 2.0f;
    bool confirmLastFrame = false; float lastTriggerValue = 0.0f;
    Gui::VirtualKeyboard* virtualKeyboard = new Gui::VirtualKeyboard(renderer, textEngine, g_resources.FontManager.Get("regularFont"));
    bool showVirtualKeyboard = false;
    
    AddEntityDialog* dialog = new AddEntityDialog(renderer, textEngine, g_resources.FontManager.Get("regularFont"), window);
    AddGuiElemDialog* dialog2 = new AddGuiElemDialog(renderer, textEngine, g_resources.FontManager.Get("regularFont"), window);
    Gui::AddChildDialog addChildDialog(renderer, textEngine, g_resources.FontManager.Get("regularFont"), window, nullptr);
    
    SceneParser sceneParser(renderer, textEngine, g_resources.FontManager.Get("regularFont"), window);
    Scene scene;
    ECSWorld& world = scene.world;
    std::vector<std::unique_ptr<Gui::IGuiElement>>& guiElements = scene.guiElements;
    std::string currentSceneFilePath;
    
    Gui::SceneInspector inspector(renderer, textEngine, g_resources.FontManager.Get("regularFont"), window);
    Gui::IGuiElement* selectedGuiElem = nullptr;
    inspector.setGuiElementsVector(&guiElements);
    inspector.setAddChildDialog(&addChildDialog);
    
    Gui::EntityInspector entityInspector(renderer, textEngine, g_resources.FontManager.Get("regularFont"), window, fileExplorer);
    Entity selectedEntity = (Entity)-1;
    auto selectEntity = [&](Entity e) {
        if (selectedEntity != e) {
            selectedEntity = e;
            if (e != (Entity)-1) {
                entityInspector.setTarget(world, e);
                if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; inspector.setTarget(nullptr); }
            } else { entityInspector.clearTarget(); }
        }
    };
    
    Gui::Scrollbar verticalScrollbar, horizontalScrollbar;
    verticalScrollbar.setOrientation(Gui::ScrollOrientation::Vertical);
    horizontalScrollbar.setOrientation(Gui::ScrollOrientation::Horizontal);
    static bool scrollConnected = false;
    if (!scrollConnected) {
        verticalScrollbar.onChange = [](float v){ editorScrollY = v; };
        horizontalScrollbar.onChange = [](float v){ editorScrollX = v; };
        scrollConnected = true;
    }
    
    auto makeButton = [&](const std::string& label, std::function<void()> cb) {
        auto btn = std::make_unique<Gui::Button>(renderer, g_resources.FontManager.Get("regularFont"), label, SDL_FPoint{0,0}, 100, 50);
        btn->onClicked = cb; return btn;
    };
    
    auto loadBtn = makeButton("O-Scene", [&]() {
        if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
        inspector.setTarget(nullptr);
        fileExplorer.setNewTitle("File Explorer - [*.json]"); fileExplorer.setFilter("*.json");
        fileExplorer.setCallback([&](const std::string& path) {
            scene = sceneParser.loadFromFile(path); currentSceneFilePath = path;
            for (auto& elem : scene.guiElements) {
                SDL_FPoint pos = { elem->getX(), elem->getY() };
                clamp_guiElem_position_to_canvas(pos, elem->getWidth(), elem->getHeight());
                elem->setPos({ static_cast<int>(pos.x), static_cast<int>(pos.y) });
            }
            fileExplorer.reset();
        });
        fileExplorer.open();
    });
    
    auto openScriptBtn = makeButton("O-Script", [&]() {
        fileExplorer.setNewTitle("File Explorer - [*.cpp;*.h;*.txt;*.*]"); fileExplorer.setFilter("*.cpp;*.h;*.txt;*.*;*.json");
        fileExplorer.setCallback([&](const std::string& path) {
            textEditor.loadFile(path); showCanvas = false; textEditor.setVisible(true); SDL_StartTextInput(window); fileExplorer.reset();
        });
        fileExplorer.open();
    });
    
    auto saveBtn = makeButton("Save", [&](){
        inspector.commitAllFields();
        if (!currentSceneFilePath.empty()) { sceneParser.saveToFile(scene, currentSceneFilePath); SDL_Log("[Editor] Scene saved to %s", currentSceneFilePath.c_str()); }
        textEditor.saveFile();
    });
    
    auto addEntBtn = makeButton("+ Entity", [dialog](){ modeBeforeDialog = currentEditMode; currentEditMode = EditMode::Dialog; dialog->open(); });
    auto addGuiElemBtn = makeButton("+ GuiElem", [dialog2]() { modeBeforeDialog = currentEditMode; currentEditMode = EditMode::Dialog; dialog2->open(); });
    
    textEditor.onClose = [&]() { showCanvas = true; textEditor.setVisible(false); SDL_StopTextInput(window); };
    
    auto selectModeBtn = makeButton("Select", [](){ currentEditMode = EditMode::Select; });
    auto moveBtn = makeButton("Move", [](){ currentEditMode = EditMode::MoveWithMouse; });
    auto deleteBtn = makeButton("Delete", [](){ currentEditMode = EditMode::Delete; });
    auto selectSingleBtn = makeButton("Single Sel", [](){ currentSelectionmode = SelectionMode::SingleSelect; });
    auto selectMultiBtn = makeButton("Multi Sel", [](){ currentSelectionmode = SelectionMode::MultiSelect; });
    auto deselectBtn = makeButton("Deselect", [&world, &selectedGuiElem](){
        deselect_all(world);
        if (selectedGuiElem) { selectedGuiElem->editorSelected = false; selectedGuiElem = nullptr; }
    });
    auto inspectorToggleBtn = makeButton("Inspector", [](){ inspectorVisible = !inspectorVisible; });
    auto canvasBtn = makeButton("Editor", [&textEditor](){ showCanvas = !showCanvas; textEditor.setVisible(!showCanvas); });
    
    auto toolbar = std::make_unique<Gui::HBoxContainer>();
    toolbar->setPadding(5); toolbar->setSpacing(5);
    toolbar->addChild(std::move(loadBtn)); toolbar->addChild(std::move(openScriptBtn)); toolbar->addChild(std::move(saveBtn));
    toolbar->addChild(std::move(addEntBtn)); toolbar->addChild(std::move(addGuiElemBtn));
    toolbar->addChild(std::move(selectModeBtn)); toolbar->addChild(std::move(moveBtn)); toolbar->addChild(std::move(deleteBtn));
    toolbar->addChild(std::move(selectSingleBtn)); toolbar->addChild(std::move(selectMultiBtn)); toolbar->addChild(std::move(deselectBtn));
    toolbar->addChild(std::move(inspectorToggleBtn)); toolbar->addChild(std::move(canvasBtn));
    toolbar->setRect({0, 0, windowWidth, 60});

    auto canvasTools = std::make_unique<Gui::RightAlignedHBoxContainer>();
    canvasTools->setPadding(5); canvasTools->setSpacing(5);

    auto polyBtn = std::make_unique<Gui::Button>(renderer, g_resources.FontManager.Get("regularFont"), "Poly", SDL_FPoint{0,0}, 60, 30);
    polyBtn->onClicked = [&world]() {
        editor_isDrawingPolygon = !editor_isDrawingPolygon;
        
        // Automatically switch the selected entity to Polygon mode
        if (lastSelectedEntity != (Entity)-1 && world.has_physics_body[lastSelectedEntity]) {
            auto& phys = world.physics_body_pool[lastSelectedEntity];
            if (phys.shapeType != Physics::ShapeType::Polygon) {
                phys.shapeType = Physics::ShapeType::Polygon;
                // Default to a basic triangle if no points exist yet. Points must be
                // TOP-LEFT relative (range [0,w] x [0,h]) to match how the mouse
                // polygon editor stores clicks and how render_physics_shape_overlay
                // reads them back (both subtract width*0.5/height*0.5) - the old
                // centered square here ({-hw,-hh}...) used the wrong convention,
                // which is why the shape came out offset/mis-sized.
                if (phys.polygonPoints.empty()) {
                    phys.polygonPoints = MakeDefaultTrianglePoints(phys.width, phys.height);
                }
            }
        }
    };

    auto gridBtn = std::make_unique<Gui::Button>(renderer, g_resources.FontManager.Get("regularFont"),
                                                 "Grid", SDL_FPoint{0,0}, 60, 30);
    gridBtn->onClicked = [](){ editor_showGrid = !editor_showGrid; };
    canvasTools->addChild(std::move(polyBtn));
    canvasTools->addChild(std::move(gridBtn));

    Physics::PhysicsWorld physicsWorld;

    bool running = true; SDL_Event e;
    float scrollOffset = 0.0f; const float maxScrollOffset = 1000.0f, scrollSpeed = 60.0f;
    Uint64 last_time = SDL_GetTicks(); float delta_time = 0.0f;

    while (running) {
        Uint64 current_time = SDL_GetTicks();
        delta_time = (float)(current_time - last_time) / 1000.0f; last_time = current_time;
        
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN) {
                bool ctrlDown = (e.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL));
                if (ctrlDown && e.key.key == SDLK_I) inspectorVisible = !inspectorVisible;
                if (ctrlDown && e.key.key == SDLK_S) {
                    inspector.commitAllFields();
                    if (!currentSceneFilePath.empty()) sceneParser.saveToFile(scene, currentSceneFilePath);
                    textEditor.saveFile();
                }
            }
            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad) { gamepad = SDL_OpenGamepad(e.gdevice.which); }
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad && SDL_GetGamepadID(gamepad) == e.gdevice.which) { SDL_CloseGamepad(gamepad); gamepad = nullptr; }
            
            toolbar->handleEvent(e, window, 0.0f, 0.0f);
            
            // --- NEW: Handle canvas tools buttons (Poly/Grid) ---
            bool consumedByCanvasTools = false;
            if (showCanvas) {
                consumedByCanvasTools = canvasTools->handleEvent(e, window, 0.0f, 0.0f);
            }
            
            if (fileExplorer.isOpen()) {
                auto action = fileExplorer.handleEvent(e);
                if (action == Gui::Dialog::Action::Confirm) {
                    if (fileExplorer.isSaveMode()) fileExplorer.triggerSaveCallback();
                    else fileExplorer.triggerCallback();
                }
                else if (action == Gui::Dialog::Action::Cancel) fileExplorer.reset();
                continue;
            }
            if (dialog->isOpen()) {
                if (showVirtualKeyboard && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    Gui::ITextInput* currentInput = dialog->getCurrentTextInput();
                    virtualKeyboard->handleMouse(e, currentInput);
                    if (virtualKeyboard->isCloseRequested()) { showVirtualKeyboard = false; virtualKeyboard->resetCloseRequest(); if (currentInput) currentInput->setActive(false); }
                }
                auto action = dialog->handleEvent(e);
                if (action == Gui::Dialog::Action::Confirm) {
                    const std::string& name = dialog->nameField.getText();
                    if (!name.empty()) {
                        create_entity_with_user_input(world, name, dialog->widthBox.getValue(), dialog->heightBox.getValue());
                        Entity newEnt = world.entity_count - 1;
                        float w = dialog->widthBox.getValue(), h = dialog->heightBox.getValue();
                        float centerX = editorScrollX + canvasViewW / 2.0f, centerY = editorScrollY + canvasViewH / 2.0f;
                        world.position_pool[newEnt] = { centerX - w / 2.0f, centerY - h / 2.0f };
                        clamp_entity_position_to_canvas(world.position_pool[newEnt], w, h);
                    }
                    dialog->reset(); showVirtualKeyboard = false; currentEditMode = modeBeforeDialog;
                } else if (action == Gui::Dialog::Action::Cancel) { dialog->reset(); showVirtualKeyboard = false; currentEditMode = modeBeforeDialog; }
                continue;
            }
            if (dialog2->isOpen()) {
                auto action = dialog2->handleEvent(e);
                if (action == Gui::Dialog::Action::Confirm) {
                    size_t countBefore = scene.guiElements.size();
                    create_gui_element_with_user_input(renderer, textEngine, g_resources.FontManager.Get("regularFont"), scene.guiElements, dialog2->guiElemType.getCurrentOption(), "untitled");
                    if (scene.guiElements.size() > countBefore) {
                        auto& newElem = scene.guiElements.back();
                        float centerX = editorScrollX + canvasViewW / 2.0f, centerY = editorScrollY + canvasViewH / 2.0f;
                        SDL_FPoint pos = { centerX - newElem->getWidth() / 2.0f, centerY - newElem->getHeight() / 2.0f };
                        clamp_guiElem_position_to_canvas(pos, newElem->getWidth(), newElem->getHeight());
                        newElem->setPos({ static_cast<int>(pos.x), static_cast<int>(pos.y) });
                    }
                    dialog2->reset(); showVirtualKeyboard = false; currentEditMode = modeBeforeDialog;
                } else if (action == Gui::Dialog::Action::Cancel) { dialog2->reset(); showVirtualKeyboard = false; currentEditMode = modeBeforeDialog; }
                continue;
            }
            if (addChildDialog.isOpen()) {
                auto action = addChildDialog.handleEvent(e);
                if (action == Gui::Dialog::Action::Confirm) {
                    Gui::Panel* panel = addChildDialog.parentPanel;
                    if (panel) {
                        std::string childType = addChildDialog.typeOption.getCurrentOption();
                        std::unique_ptr<Gui::IGuiElement> child;
                        if (childType == "Button") child = std::make_unique<Gui::Button>(renderer, g_resources.FontManager.Get("regularFont"), "Button", SDL_FPoint{0,0}, 100, 50);
                        else if (childType == "LineEdit") child = std::make_unique<Gui::LineEdit>(renderer, textEngine, g_resources.FontManager.Get("regularFont"), SDL_FRect{0,0,200,36}, "Type here...");
                        else if (childType == "SpinBox") child = std::make_unique<Gui::SpinBox>(renderer, textEngine, g_resources.FontManager.Get("regularFont"), SDL_FRect{0,0,200,36}, 0.0f, 100.0f, 50.0f, 1.0f);
                        if (child) {
                            int childY = (int)panel->getY() + 10;
                            const auto& existingChildren = panel->getChildren();
                            if (!existingChildren.empty()) childY = (int)(existingChildren.back()->getY() + existingChildren.back()->getHeight() + 5);
                            int childX = (int)(panel->getX() + (panel->getWidth() - child->getWidth()) / 2.0f);
                            child->setPos({childX, childY}); panel->addChild(std::move(child));
                            if (selectedGuiElem == panel) inspector.setTarget(panel);
                        }
                    }
                    addChildDialog.reset(); currentEditMode = modeBeforeDialog;
                } else if (action == Gui::Dialog::Action::Cancel) { addChildDialog.reset(); currentEditMode = modeBeforeDialog; }
                continue;
            }

            if (entityInspector.animFrameEditor && entityInspector.animFrameEditor->isOpen()) {
                auto action = entityInspector.animFrameEditor->handleEvent(e);
                if (action == Gui::Dialog::Action::Confirm) {
                    entityInspector.animFrameEditor->reset();
                    currentEditMode = modeBeforeDialog;
                } else if (action == Gui::Dialog::Action::Cancel) {
                    entityInspector.animFrameEditor->reset();
                    currentEditMode = modeBeforeDialog;
                }
                continue;
            }
            
            if (!showCanvas) { if (textEditor.handleEvent(e, window, 0.0f, 0.0f)) continue; }
            
            bool consumedByScrollbar = false;
            if (showCanvas) {
                if (verticalScrollbar.handleEvent(e)) consumedByScrollbar = true;
                if (horizontalScrollbar.handleEvent(e)) consumedByScrollbar = true;
            }
            if (!consumedByScrollbar && e.type == SDL_EVENT_MOUSE_WHEEL) {
                bool consumedByScene = false;
                float guiOffsetX = editorScrollX - canvasViewX, guiOffsetY = editorScrollY - canvasViewY;
                for (auto& elem : guiElements) { if (elem->handleEvent(e, window, guiOffsetX, guiOffsetY)) { consumedByScene = true; break; } }
                if (!consumedByScene) {
                    scrollOffset += (e.wheel.y > 0.0f) ? -scrollSpeed : scrollSpeed;
                    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);
                }
            }
            
            if (!consumedByCanvasTools) {
                edit_object_with_editor_mouse(renderer, world, scene.guiElements, selectedGuiElem, e);
            }
            
            if (selectedGuiElem) { if (inspector.handleEvent(e)) continue; }
            else if (selectedEntity != (Entity)-1) { if (entityInspector.handleEvent(e)) continue; }
            
            if (e.type != SDL_EVENT_MOUSE_WHEEL) {
                float guiOffsetX = editorScrollX - canvasViewX, guiOffsetY = editorScrollY - canvasViewY;
                for (auto& elem : guiElements) elem->handleEvent(e, window, guiOffsetX, guiOffsetY);
            }
        }
        
        if (gamepad) {
            auto applyAxis = [&](SDL_GamepadAxis axis) -> float {
                float raw = SDL_GetGamepadAxis(gamepad, axis) / 32767.0f;
                if (std::abs(raw) <= deadzone) return 0.0f;
                float sign = raw > 0.0f ? 1.0f : -1.0f;
                return sign * ((std::abs(raw) - deadzone) / (1.0f - deadzone));
            };
            float lx = applyAxis(SDL_GAMEPAD_AXIS_LEFTX), ly = applyAxis(SDL_GAMEPAD_AXIS_LEFTY), ry = applyAxis(SDL_GAMEPAD_AXIS_RIGHTY);
            cursorX = std::clamp(cursorX + lx * stickCursorSpeed * delta_time, 0.0f, windowWidth);
            cursorY = std::clamp(cursorY + ly * stickCursorSpeed * delta_time, 0.0f, windowHeight);
            scrollOffset = std::clamp(scrollOffset + ry * stickScrollSpeed * delta_time, 0.0f, maxScrollOffset);
            bool confirmNow = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
            
            if (showVirtualKeyboard) {
                float trigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
                if (trigger > 0.5f && lastTriggerValue <= 0.5f) virtualKeyboard->toggleShift();
                lastTriggerValue = trigger;
                if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST) && !confirmLastFrame) virtualKeyboard->requestClose();
            }
            
            if (dialog->isOpen()) {
                Gui::ITextInput* currentInput = dialog->getCurrentTextInput();
                bool wasActive = (currentInput != nullptr);
                if (showVirtualKeyboard) {
                    virtualKeyboard->handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame, currentInput);
                    if (virtualKeyboard->isCloseRequested()) { showVirtualKeyboard = false; virtualKeyboard->resetCloseRequest(); if (currentInput) currentInput->setActive(false); confirmLastFrame = confirmNow; continue; }
                } else {
                    auto action = dialog->handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
                    Gui::ITextInput* newInput = dialog->getCurrentTextInput();
                    if (!wasActive && newInput != nullptr && action == Gui::Dialog::Action::None) {
                        showVirtualKeyboard = true;
                        virtualKeyboard->layoutKeys(newInput->getRect(), windowWidth, windowHeight, newInput->isNumericOnly());
                    }
                    if (action == Gui::Dialog::Action::Confirm) {
                        if (!dialog->nameField.getText().empty()) create_entity_with_user_input(world, dialog->nameField.getText(), dialog->widthBox.getValue(), dialog->heightBox.getValue());
                        dialog->reset(); showVirtualKeyboard = false; currentEditMode = modeBeforeDialog;
                    } else if (action == Gui::Dialog::Action::Cancel) { dialog->reset(); showVirtualKeyboard = false; currentEditMode = modeBeforeDialog; }
                }
            } else if (dialog2->isOpen()) {
                auto action = dialog2->handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
                if (action == Gui::Dialog::Action::Confirm) {
                    create_gui_element_with_user_input(renderer, textEngine, g_resources.FontManager.Get("regularFont"), scene.guiElements, dialog2->guiElemType.getCurrentOption(), "untitled");
                    dialog2->reset(); showVirtualKeyboard = false; currentEditMode = modeBeforeDialog;
                } else if (action == Gui::Dialog::Action::Cancel) { dialog2->reset(); showVirtualKeyboard = false; currentEditMode = modeBeforeDialog; }
            } else if (addChildDialog.isOpen()) {
                auto action = addChildDialog.handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
                if (action == Gui::Dialog::Action::Confirm) { addChildDialog.reset(); currentEditMode = modeBeforeDialog; }
                else if (action == Gui::Dialog::Action::Cancel) { addChildDialog.reset(); currentEditMode = modeBeforeDialog; }
            } else {
                showVirtualKeyboard = false;
                toolbar->handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmNow, confirmLastFrame);
                if (!showCanvas) textEditor.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmNow, confirmLastFrame);
                edit_object_with_editor_gamepad(world, scene.guiElements, selectedGuiElem, cursorX, cursorY, confirmNow, confirmLastFrame);
                float guiOffsetX = editorScrollX - canvasViewX, guiOffsetY = editorScrollY - canvasViewY;
                for (auto& elem : guiElements) elem->handleGamepad(cursorX, cursorY, guiOffsetX, guiOffsetY, window, confirmNow, confirmLastFrame);
                if (selectedGuiElem) inspector.handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
                else if (selectedEntity != (Entity)-1) entityInspector.handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
            }
            confirmLastFrame = confirmNow;
        }

        // Advance animation clips every frame. This runs unconditionally
        // (not just in some future "play-test" mode) so that toggling
        // Play/Stop in the Inspector is visible immediately while editing,
        // rather than requiring a separate runtime/play mode.
        animation_system(world, delta_time);

        if (selectedEntity != (Entity)-1) {
            if (selectedEntity >= world.entity_count || !world.has_position[selectedEntity]) {
                selectedEntity = (Entity)-1;
                entityInspector.clearTarget();
                if (lastSelectedEntity != (Entity)-1) {
                    lastSelectedEntity = (Entity)-1;
                }
            }
        }
        if (lastSelectedEntity != (Entity)-1) {
            if (lastSelectedEntity >= world.entity_count || !world.has_position[lastSelectedEntity]) {
                lastSelectedEntity = (Entity)-1;
                if (selectedEntity != (Entity)-1) {
                    selectedEntity = (Entity)-1;
                    entityInspector.clearTarget();
                }
            }
        }

        
        static Entity prevSelectedEntity = (Entity)-1;
        if (lastSelectedEntity != prevSelectedEntity) {
            if (lastSelectedEntity != (Entity)-1) selectEntity(lastSelectedEntity); else selectEntity((Entity)-1);
            prevSelectedEntity = lastSelectedEntity;
        }
        static Gui::IGuiElement* prevSelectedGuiElem = nullptr;
        if (selectedGuiElem != prevSelectedGuiElem) {
            if (selectedGuiElem) {
                inspector.setTarget(selectedGuiElem);
                if (selectedEntity != (Entity)-1) { selectedEntity = (Entity)-1; entityInspector.clearTarget(); }
            } else inspector.setTarget(nullptr);
            prevSelectedGuiElem = selectedGuiElem;
        }
        
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255); SDL_RenderClear(renderer);
        int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
        const float toolbarHeight = 60.0f;
        const float inspectorWidth = inspectorVisible ? Gui::SceneInspector::PANEL_W : 0.0f;
        canvasViewX = 0.0f; canvasViewY = toolbarHeight;
        canvasViewW = (float)winW - (inspectorVisible ? inspectorWidth : 0.0f); canvasViewH = (float)winH - toolbarHeight;
        if (canvasViewW < 200) canvasViewW = 200; if (canvasViewH < 200) canvasViewH = 200;
        
        const float logicalCanvasWidth = 1390.0f, logicalCanvasHeight = 690.0f, sbSize = 12.0f;
        verticalScrollbar.setGeometry(canvasViewX + canvasViewW - sbSize, canvasViewY, sbSize, canvasViewH, logicalCanvasHeight, canvasViewH);
        horizontalScrollbar.setGeometry(canvasViewX, canvasViewY + canvasViewH - sbSize, canvasViewW - sbSize, sbSize, logicalCanvasWidth, canvasViewW - sbSize);
        
        toolbar->render(0.0f, 0.0f);
        if (showCanvas) {
            render_editor_canvas(renderer);
            // --- Draw the scene and GUI elements (includes the old Poly/Grid buttons) ---
            render_system_and_scene_gui_in_editor(renderer, textEngine, g_resources.FontManager.Get("regularFont"), world, canvasViewX, canvasViewY, editorScrollX, editorScrollY, guiElements);
            verticalScrollbar.render(renderer); horizontalScrollbar.render(renderer);
            
            // --- NEW: Render canvas tools ON TOP of everything else in the canvas area ---
            float toolsX = canvasViewX + canvasViewW - 10; // right margin
            float toolsY = canvasViewY + 10;
            float containerWidth = 140;                   // enough to hold both buttons (2*60 + spacing)
            canvasTools->setRect({ toolsX - containerWidth, toolsY, containerWidth, 40 });
            canvasTools->render(0.0f, 0.0f);

        } else {
            textEditor.setRect({canvasViewX, canvasViewY, canvasViewW, canvasViewH}); textEditor.render(0.0f, 0.0f);
        }
        
        if (inspectorVisible) {
            if (selectedGuiElem) { inspector.syncFromTarget(); inspector.render((float)winH); }
            else if (selectedEntity != (Entity)-1) { entityInspector.commitAllFields(); entityInspector.syncFromWorld(); entityInspector.render((float)winH); }
            else {
                float rightX = (float)winW - Gui::SceneInspector::PANEL_W;
                SDL_FRect bg = { rightX, toolbarHeight, Gui::SceneInspector::PANEL_W, (float)winH - toolbarHeight };
                SDL_SetRenderDrawColor(renderer, 28, 28, 35, 245); SDL_RenderFillRect(renderer, &bg);
                SDL_SetRenderDrawColor(renderer, 60, 60, 75, 255); SDL_RenderRect(renderer, &bg);
                TTF_Text* t = TTF_CreateText(textEngine, g_resources.FontManager.Get("regularFont"), "Inspector", 0);
                if (t) { TTF_SetTextColor(t, 180, 180, 200, 255); TTF_DrawRendererText(t, bg.x + 10, bg.y + 10); TTF_DestroyText(t); }
                t = TTF_CreateText(textEngine, g_resources.FontManager.Get("regularFont"), "Click an entity or GUI element.", 0);
                if (t) { TTF_SetTextColor(t, 100, 100, 120, 255); TTF_DrawRendererText(t, bg.x + 10, bg.y + 40); TTF_DestroyText(t); }
            }
        }
        
        dialog->tick(delta_time); if (dialog->isOpen()) dialog->render();
        dialog2->tick(delta_time); if (dialog2->isOpen()) dialog2->render();
        addChildDialog.tick(delta_time); if (addChildDialog.isOpen()) addChildDialog.render();

        if (entityInspector.animFrameEditor && entityInspector.animFrameEditor->isOpen()) {
            entityInspector.animFrameEditor->tick(delta_time);
            // Don't render if file explorer is open on top of it
            if (!fileExplorer.isOpen()) {
                entityInspector.animFrameEditor->render();
            }
        }

        fileExplorer.tick(delta_time); if (fileExplorer.isOpen()) fileExplorer.render();
        
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_FRect crosshair_firstrect = { cursorX - 10.0f, cursorY - 1.0f, 20.0f, 2.0f };
        SDL_FRect crosshair_secondrect = { cursorX - 1.0f, cursorY - 10.0f, 2.0f, 20.0f };
        SDL_RenderFillRect(renderer, &crosshair_firstrect); SDL_RenderFillRect(renderer, &crosshair_secondrect);
        if (showVirtualKeyboard) virtualKeyboard->render();
        
        SDL_RenderPresent(renderer);
    }
    
    // delete pointers
    delete dialog; delete dialog2; delete virtualKeyboard;
    // gamepad cleanup
    if (gamepad) SDL_CloseGamepad(gamepad);
    // clears all allocations and also calls library quit functions unless that is internal
    g_resources.FontManager.Clear();
    g_resources.TextureManager.Clear();
    g_resources.AudioManager.~Manager();
    // icon cleanup
    if (iconSurface) SDL_DestroySurface(iconSurface);
    // systems cleanup
    TTF_DestroyRendererTextEngine(textEngine); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    // final exit
    SDL_Quit();
    return 0;
}