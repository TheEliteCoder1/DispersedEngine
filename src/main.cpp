#include "engine.h"



bool showCanvas = true;


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
    else if (type == "Panel") {
        auto pl = std::make_unique<Gui::Panel>(renderer, textEngine, font, SDL_FRect{0,0,200,500});
        guiElements.push_back(std::move(pl));
    }
    else if (type == "OptionSpinBox") {
        std::vector<std::string> options = {"option1", "option2", "option3"};
        auto osb = std::make_unique<Gui::OptionSpinBox>(renderer, textEngine, font, SDL_FRect{0,0,200,36}, options);
        guiElements.push_back(std::move(osb));
    }
    else if (type == "TextArea") {
        auto ta = std::make_unique<Gui::TextArea>(renderer, textEngine, font, SDL_FRect{0,0,200,500});
        guiElements.push_back(std::move(ta));
    }
    else if (type == "CheckBox") {
        auto cb = std::make_unique<Gui::CheckBox>(renderer, textEngine, font, SDL_FRect{0,0,200,500});
        guiElements.push_back(std::move(cb));
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
        "Button",
        "Panel",
        "OptionSpinBox",
        "TextArea",
        "CheckBox"
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


ECSWorld            g_worldOverlay;
std::vector<Entity> g_worldOverlayEntities;
bool                g_showWorldOverlay = true;
std::string         g_worldOverlayPath;

std::string deriveWorldFilePath(const std::string& sceneFilePath) {
    std::string p = sceneFilePath;
    size_t dot = p.rfind('.');
    if (dot != std::string::npos) p = p.substr(0, dot) + ".world";
    else p += ".world";
    return p;
}

// Deletes a single entity out of the .world overlay and keeps
// g_worldOverlayEntities in sync. ECSWorld::delete_entity() swaps the last
// entity into the deleted slot and shrinks the array, which silently
// reassigns whatever handle equaled `last` to now mean the entity that used
// to live at `id` -- so any other handle in our list still holding the old
// `last` value would go stale. Rewrite those immediately, same pattern
// cleanupOutOfBoundsDecorations() uses in engine.h.
void deleteWorldOverlayEntity(Entity id) {
    if (id >= g_worldOverlay.entity_count) return;
    Entity last = g_worldOverlay.entity_count - 1;

    // Drop this entity's own handle first, before the swap below can give
    // `id` a new meaning -- otherwise the erase-by-value further down would
    // also catch (and wrongly drop) the entity that gets moved into `id`'s
    // slot.
    g_worldOverlayEntities.erase(
        std::remove(g_worldOverlayEntities.begin(), g_worldOverlayEntities.end(), id),
        g_worldOverlayEntities.end());

    g_worldOverlay.delete_entity(id);

    if (id != last) {
        for (Entity& other : g_worldOverlayEntities) if (other == last) other = id;
    }
}

void reloadWorldOverlay(const std::string& sceneFilePath, const std::string& projectRoot) {
    // Reset to a clean world every scene switch so stale entities from a
    // previously-loaded scene never bleed into the one you just opened.
    g_worldOverlay = ECSWorld();
    g_worldOverlayEntities.clear();
    g_worldOverlayPath = deriveWorldFilePath(sceneFilePath);

    bool generatedFlag = false; // unused here, WorldBinary::load requires it
    WorldBinary::load(g_worldOverlayPath, g_worldOverlay, g_worldOverlayEntities,
                       generatedFlag, projectRoot);
    // A missing file just leaves the overlay empty - that scene simply
    // hasn't been generated/run yet, which is fine.
}

void draw_world_overlay(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                         Tools::Camera& camera) {
    if (!g_showWorldOverlay || g_worldOverlayEntities.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (Entity e : g_worldOverlayEntities) {
        if (!g_worldOverlay.has_position[e]) continue;

        // Get entity dimensions
        float w = g_worldOverlay.has_rectangle_shape[e]
                      ? g_worldOverlay.rectangle_shape_pool[e].w
                      : 16.0f;
        float h = g_worldOverlay.has_rectangle_shape[e]
                      ? g_worldOverlay.rectangle_shape_pool[e].h
                      : 16.0f;
        float scaledW = w * camera.zoom;
        float scaledH = h * camera.zoom;

        SDL_FPoint screenPos = camera.worldToScreen(
            g_worldOverlay.position_pool[e].x,
            g_worldOverlay.position_pool[e].y
        );
        float screenX = screenPos.x;
        float screenY = screenPos.y;

        // Try to render texture or animation
        bool rendered = false;
        if (g_worldOverlay.has_texture_ref[e]) {
            rendered = render_entity_texture(renderer, g_worldOverlay, e,
                                             screenX, screenY, camera.zoom);
        }
        if (!rendered && g_worldOverlay.has_animation_state[e]) {
            rendered = render_entity_animation(renderer, g_worldOverlay, e,
                                               screenX, screenY, camera.zoom);
        }

        // Fallback: draw a semi‑transparent rectangle
        if (!rendered) {
            SDL_FRect rect = { screenX, screenY, scaledW, scaledH };
            SDL_SetRenderDrawColor(renderer, 90, 170, 255, 55);
            SDL_RenderFillRect(renderer, &rect);
            SDL_SetRenderDrawColor(renderer, 90, 170, 255, 150);
            SDL_RenderRect(renderer, &rect);
        }

        // Metadata label (only if zoomed in enough)
        if (scaledW > 28.0f && g_worldOverlay.has_metadata[e]) {
            TTF_Text* t = TTF_CreateText(textEngine, font,
                                         g_worldOverlay.metadata_pool[e].name.c_str(), 0);
            if (t) {
                TTF_SetTextColor(t, 150, 210, 255, 200);
                TTF_DrawRendererText(t, screenX + 2.0f, screenY - 14.0f);
                TTF_DestroyText(t);
            }
        }
    }
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
        verticalScrollbar.onChange = [](float v){
            editorScrollY = v;
            g_editorCamera.targetY = v;
        };
        horizontalScrollbar.onChange = [](float v){
            editorScrollX = v;
            g_editorCamera.targetX = v;
        };
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
        fileExplorer.setSaveMode(false, "");
        fileExplorer.setCallback([&](const std::string& path) {
            scene = sceneParser.loadFromFile(path); currentSceneFilePath = path;
            entityInspector.setProjectRoot(scene.projectRoot);
            reloadWorldOverlay(currentSceneFilePath, scene.projectRoot);
            fileExplorer.reset();
        });
        fileExplorer.open();
    });

    
    auto openScriptBtn = makeButton("O-Script", [&]() {
        fileExplorer.setNewTitle("File Explorer - [*.cpp;*.h;*.ls;*.lh;*.txt;*.*]");
        fileExplorer.setFilter("*.cpp;*.h;*.ls;*.txt;*.*;*.json");
        fileExplorer.setSaveMode(false, "");
        fileExplorer.setCallback([&](const std::string& path) {
            textEditor.loadFile(path);
            showCanvas = false;
            textEditor.setVisible(true);
            SDL_StartTextInput(window); fileExplorer.reset();
        });
        fileExplorer.open();
    });

    auto saveBtn = makeButton("Save", [&](){
        inspector.commitAllFields();
        if (!currentSceneFilePath.empty()) {
            sceneParser.saveToFile(scene, currentSceneFilePath);
            entityInspector.setProjectRoot(scene.projectRoot);
            SDL_Log("[Editor] Scene saved to %s", currentSceneFilePath.c_str());
        }
        textEditor.saveFile();
    });

    auto addEntBtn = makeButton("+ Entity", [dialog](){ modeBeforeDialog = currentEditMode; currentEditMode = EditMode::Dialog; dialog->open(); });
    auto addGuiElemBtn = makeButton("+ GuiElem", [dialog2]() { modeBeforeDialog = currentEditMode; currentEditMode = EditMode::Dialog; dialog2->open(); });

    textEditor.onClose = [&]() { showCanvas = true; textEditor.setVisible(false);  SDL_StopTextInput(window); };
   

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
    auto canvasBtn = makeButton("Editor", [&textEditor, window](){
        showCanvas = !showCanvas;
        if (showCanvas) { textEditor.setVisible(false); SDL_StopTextInput(window); }
    });

    auto worldOverlayBtn = makeButton("World Ref", [](){ g_showWorldOverlay = !g_showWorldOverlay; });


    auto transpileLsBtn = makeButton("Ls To C++", [&textEditor]() {
        if (!textEditor.isLogicScript) {
            SDL_Log("[Editor] Transpile .ls: no .ls file is currently open.");
            return;
        }
        textEditor.saveFile(); // transpile whatever's on disk == whatever's on screen
        std::string err;
        if (textEditor.transpileLogicScript(err)) {
            SDL_Log("[Editor] LogicScript transpiled successfully.");
        } else {
            SDL_Log("[Editor] LogicScript transpile failed:\n%s", err.c_str());
        }
    });

    auto transpileCppBtn = makeButton("C++ To LS", [&textEditor]() {
        if (textEditor.isLogicScript) {
            SDL_Log("[Editor] Transpile .cpp: no .cpp file is currently open.");
            return;
        }
        textEditor.saveFile(); // transpile whatever's on disk == whatever's on screen
        std::string err;
        if (textEditor.transpileCPlusPlusScript(err)) {
            SDL_Log("[Editor] C++ Script transpiled successfully.");
        } else {
            SDL_Log("[Editor] C++ Script transpile failed:\n%s", err.c_str());
        }
    });

    auto toolbarContainer = std::make_unique<Gui::ScrollableContainer>(
        renderer, textEngine, g_resources.FontManager.Get("regularFont"),
        Gui::ScrollOrientation::Horizontal);
    auto toolbarBox = std::make_unique<Gui::HBoxContainer>();
    toolbarBox->setPadding(5);
    toolbarBox->setSpacing(5);
    toolbarBox->setPadding(5); toolbarBox->setSpacing(5);
    toolbarBox->addChild(std::move(loadBtn));
    toolbarBox->addChild(std::move(openScriptBtn));
    toolbarBox->addChild(std::move(saveBtn));
    toolbarBox->addChild(std::move(addEntBtn)); 
    toolbarBox->addChild(std::move(addGuiElemBtn));
    toolbarBox->addChild(std::move(selectModeBtn)); 
    toolbarBox->addChild(std::move(moveBtn)); 
    toolbarBox->addChild(std::move(deleteBtn));
    toolbarBox->addChild(std::move(selectSingleBtn)); 
    toolbarBox->addChild(std::move(selectMultiBtn)); 
    toolbarBox->addChild(std::move(deselectBtn));
    toolbarBox->addChild(std::move(inspectorToggleBtn)); 
    toolbarBox->addChild(std::move(canvasBtn)); 
    toolbarBox->addChild(std::move(transpileLsBtn));
    toolbarBox->addChild(std::move(transpileCppBtn));
    toolbarBox->addChild(std::move(worldOverlayBtn));
    toolbarBox->setRect({0, 0, windowWidth, 60});

    float totalW = toolbarBox->getPadding() * 2.0f;
    const auto& children = toolbarBox->getChildren();
    for (auto& child : children) {
        totalW += child->getWidth() + toolbarBox->getSpacing();
    }
    totalW -= toolbarBox->getSpacing();
    toolbarBox->setRect({0.0f, 0.0f, totalW, 60.0f});
    toolbarContainer->setChild(std::move(toolbarBox));
    toolbarContainer->setRect({0.0f, 0.0f, windowWidth, 60.0f});

    auto canvasTools = std::make_unique<Gui::RightAlignedHBoxContainer>();
    canvasTools->setPadding(5); canvasTools->setSpacing(5);
    auto polyBtn = std::make_unique<Gui::Button>(renderer, g_resources.FontManager.Get("regularFont"), "Poly", SDL_FPoint{0,0}, 60, 30);
    polyBtn->onClicked = [&world]() {
        editor_isDrawingPolygon = !editor_isDrawingPolygon;
        if (lastSelectedEntity != (Entity)-1 && world.has_physics_body[lastSelectedEntity]) {
            auto& phys = world.physics_body_pool[lastSelectedEntity];
            if (phys.shapeType != Physics::ShapeType::Polygon) {
                phys.shapeType = Physics::ShapeType::Polygon;
                if (phys.polygonPoints.empty()) {
                    phys.polygonPoints = MakeDefaultTrianglePoints(phys.width, phys.height);
                }
            }
        }
    };
    auto gridBtn = std::make_unique<Gui::Button>(renderer, g_resources.FontManager.Get("regularFont"), "Grid", SDL_FPoint{0,0}, 60, 30);
    gridBtn->onClicked = [](){ editor_showGrid = !editor_showGrid; };
    canvasTools->addChild(std::move(polyBtn));
    canvasTools->addChild(std::move(gridBtn));

    Physics::PhysicsWorld physicsWorld;

    bool running = true; SDL_Event e;
    float scrollOffset = 0.0f; const float maxScrollOffset = 1000.0f, scrollSpeed = 60.0f;
    Uint64 last_time = SDL_GetTicks(); float delta_time = 0.0f;

    // Camera state — P+drag to pan (no more Z gate for zoom)
    static bool cameraPanning = false;
    static float cameraLastMouseX = 0.0f, cameraLastMouseY = 0.0f;

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
                // Camera: P + LMB = pan
                if (e.key.key == SDLK_P) {
                    cameraPanning = true;
                    SDL_GetMouseState(&cameraLastMouseX, &cameraLastMouseY);
                }
            }
            if (e.type == SDL_EVENT_KEY_UP) {
                if (e.key.key == SDLK_P) cameraPanning = false;
            }

            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad) { gamepad = SDL_OpenGamepad(e.gdevice.which); }
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad && SDL_GetGamepadID(gamepad) == e.gdevice.which) { SDL_CloseGamepad(gamepad); gamepad = nullptr; }

            toolbarContainer->handleEvent(e, window, 0.0f, 0.0f);

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
                        // GUI is screen-space: spawn at the screen centre of the canvas view.
                        float centerX = canvasViewX + canvasViewW / 2.0f;
                        float centerY = canvasViewY + canvasViewH / 2.0f;
                        SDL_FPoint pos = { centerX - newElem->getWidth() / 2.0f, centerY - newElem->getHeight() / 2.0f };
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

            if (!showCanvas) {
                if (textEditor.handleEvent(e, window, 0.0f, 0.0f)) continue;
            }

            bool consumedByScrollbar = false;
            if (showCanvas) {
                if (verticalScrollbar.handleEvent(e)) consumedByScrollbar = true;
                if (horizontalScrollbar.handleEvent(e)) consumedByScrollbar = true;
            }

            // ── World Ref: D + LMB = delete an entity from the .world overlay ──
            // Only takes effect while the overlay is actually visible (the
            // "World Ref" button is toggled on) -- when it's hidden there's
            // nothing on screen belonging to it to click, so D+click isn't
            // processed here at all and falls through to whatever normal
            // Select/Move/Delete handling would otherwise do with it.
            if (g_showWorldOverlay && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                e.button.button == SDL_BUTTON_LEFT && isInsideCanvas((float)e.button.x, (float)e.button.y)) {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                bool dHeld = keys && keys[SDL_SCANCODE_D];
                if (dHeld) {
                    SDL_FPoint worldPt = g_editorCamera.screenToWorld((float)e.button.x, (float)e.button.y);
                    Entity topmostOverlayEntity = (Entity)-1;
                    int maxZ = 0; bool found = false;
                    for (Entity oe : g_worldOverlayEntities) {
                        if (!g_worldOverlay.has_position[oe]) continue;
                        float w = g_worldOverlay.has_rectangle_shape[oe] ? g_worldOverlay.rectangle_shape_pool[oe].w : 16.0f;
                        float h = g_worldOverlay.has_rectangle_shape[oe] ? g_worldOverlay.rectangle_shape_pool[oe].h : 16.0f;
                        if (worldPt.x >= g_worldOverlay.position_pool[oe].x && worldPt.x <= g_worldOverlay.position_pool[oe].x + w &&
                            worldPt.y >= g_worldOverlay.position_pool[oe].y && worldPt.y <= g_worldOverlay.position_pool[oe].y + h) {
                            int z = g_worldOverlay.has_z_index[oe] ? g_worldOverlay.z_index_pool[oe].z : 0;
                            if (!found || z > maxZ) { maxZ = z; topmostOverlayEntity = oe; found = true; }
                        }
                    }
                    if (topmostOverlayEntity != (Entity)-1) deleteWorldOverlayEntity(topmostOverlayEntity);
                    // Swallow the click either way while D is held and the
                    // overlay is showing, so it can't also fall through to
                    // select/move/delete something in the real scene
                    // underneath the overlay.
                    continue;
                }
            }

            // ── Camera: P + LMB = pan ───────────────────────────────────────
            if (e.type == SDL_EVENT_MOUSE_MOTION && cameraPanning) {
                float dx = e.motion.x - cameraLastMouseX;
                float dy = e.motion.y - cameraLastMouseY;
                g_editorCamera.pan(dx, dy);
                cameraLastMouseX = e.motion.x;
                cameraLastMouseY = e.motion.y;
            }

            // ── Mouse wheel: scene scrollbars first, then camera zoom ─────
            // No Z key required. Scrollbars inside Panels get first claim
            // so they keep working; everything else zooms the camera.
            if (!consumedByScrollbar && e.type == SDL_EVENT_MOUSE_WHEEL) {
                bool consumedByScene = false;
                for (auto& elem : guiElements) {
                    if (elem->handleEvent(e, window, 0.0f, 0.0f)) { consumedByScene = true; break; }
                }
                if (!consumedByScene) {
                    float mx, my;
                    SDL_GetMouseState(&mx, &my);
                    if (mx >= canvasViewX && mx <= canvasViewX + canvasViewW &&
                        my >= canvasViewY && my <= canvasViewY + canvasViewH) {
                        float factor = (e.wheel.y > 0.0f) ? 1.1f : (1.0f / 1.1f);
                        g_editorCamera.zoomToward(mx, my, factor);
                    }
                }
            }

            if (!consumedByCanvasTools) {
                edit_object_with_editor_mouse(renderer, world, scene.guiElements, selectedGuiElem, e);
            }

            if (selectedGuiElem) { if (inspector.handleEvent(e)) continue; }
            else if (selectedEntity != (Entity)-1) { if (entityInspector.handleEvent(e)) continue; }

            // ── Scene GUI elements are INERT in the editor ────────────────
            // Runtime interaction (LineEdit focus, Button onClicked, SpinBox
            // stepping, etc.) is disabled. Selection and movement go through
            // edit_object_with_editor_mouse/gamepad instead. Scrollbars still
            // receive wheel events in the wheel block above.
            // (The old forwarding loop is intentionally removed.)
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
                toolbarContainer->handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmNow, confirmLastFrame);
                if (!showCanvas) {
                    textEditor.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmNow, confirmLastFrame);
                }
                edit_object_with_editor_gamepad(world, scene.guiElements, selectedGuiElem, cursorX, cursorY, confirmNow, confirmLastFrame);

                // Scene GUI runtime interaction is disabled for gamepad too.
                // (The old handleGamepad forwarding loop is intentionally removed.)

                if (selectedGuiElem) inspector.handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
                else if (selectedEntity != (Entity)-1) entityInspector.handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
            }
            confirmLastFrame = confirmNow;
        }

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
        canvasViewX = 0.0f;
        canvasViewY = toolbarHeight;
        canvasViewW = (float)winW - (inspectorVisible ? inspectorWidth : 0.0f);
        canvasViewH = (float)winH - toolbarHeight;
        if (canvasViewW < 200) canvasViewW = 200;
        if (canvasViewH < 200) canvasViewH = 200;

        const float minLogicalW = 1390.0f;
        const float minLogicalH = 690.0f;
        const float sbSize = 12.0f;
        float contentW = std::max(canvasViewW, minLogicalW);
        float contentH = std::max(canvasViewH, minLogicalH);
        g_canvasLogicalWidth = contentW;
        g_canvasLogicalHeight = contentH;

        verticalScrollbar.setGeometry(
            canvasViewX + canvasViewW - sbSize, canvasViewY, sbSize, canvasViewH,
            contentH, canvasViewH
        );
        horizontalScrollbar.setGeometry(
            canvasViewX, canvasViewY + canvasViewH - sbSize, canvasViewW - sbSize, sbSize,
            contentW, canvasViewW - sbSize
        );

        editorScrollX = g_editorCamera.targetX;
        editorScrollY = g_editorCamera.targetY;

        toolbarContainer->render(0.0f, 0.0f);

        if (showCanvas) {
            render_editor_canvas(renderer);
            draw_world_overlay(renderer, textEngine, g_resources.FontManager.Get("regularFont"), g_editorCamera);
            render_system_and_scene_gui_in_editor(
                renderer, textEngine, g_resources.FontManager.Get("regularFont"),
                world, canvasViewX, canvasViewY,
                editorScrollX, editorScrollY,
                guiElements,
                g_editorCamera
            );
            verticalScrollbar.render(renderer);
            horizontalScrollbar.render(renderer);

            float toolsX = canvasViewX + canvasViewW - 10;
            float toolsY = canvasViewY + 30;
            float containerWidth = 140;
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
                t = TTF_CreateText(textEngine, g_resources.FontManager.Get("regularFont"), "Nothing selected.", 0);
                if (t) { TTF_SetTextColor(t, 100, 100, 120, 255); TTF_DrawRendererText(t, bg.x + 10, bg.y + 40); TTF_DestroyText(t); }
            }
        }

        dialog->tick(delta_time); if (dialog->isOpen()) dialog->render();
        dialog2->tick(delta_time); if (dialog2->isOpen()) dialog2->render();
        addChildDialog.tick(delta_time); if (addChildDialog.isOpen()) addChildDialog.render();
        if (entityInspector.animFrameEditor && entityInspector.animFrameEditor->isOpen()) {
            entityInspector.animFrameEditor->tick(delta_time);
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
    g_resources.AudioManager.Clear();
    TTF_Quit();
    MIX_Quit();

    // icon cleanup
    if (iconSurface) SDL_DestroySurface(iconSurface);

    // systems cleanup
    TTF_DestroyRendererTextEngine(textEngine); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);

    // final exit
    SDL_Quit();
    return 0;
}