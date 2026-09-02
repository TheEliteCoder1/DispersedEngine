#include "engine.h"
#include "engine3d.h"
#include "physics3d.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp> // glm::quat used by Physics3D bodies

// ===== ADDED: 3D camera and render target globals =====
Engine3D::Camera3D g_editorCamera3D;
Engine3D::CanvasRenderTarget3D g_canvasRenderTarget;

// De-duplicating GPU model cache for entities that have a Mesh3DRef
// assigned (see engine.h's Components::Mesh3DRef and engine3d.h's
// ModelCache3D). Global/long-lived for the same reason
// g_canvasRenderTarget is: it owns GPU resources that must survive
// across frames and get released once, at shutdown, alongside it.
Engine3D::ModelCache3D g_modelCache3D;

bool g_camOrbiting = false;
float g_camLastMouseX = 0.0f, g_camLastMouseY = 0.0f;

SDL_FRect GamepadCursorIconSrcRect =  {0.0f, 0.0f, 32.0f, 32.0f};
SDL_FRect GamepadCursorIconDestRect = {0.0f, 0.0f, 1600.0f, 900.0f};

static glm::vec3 dragStartPos3D(0.0f);

// ===== REMOVED: fixed-size texture constants (no longer needed) =====
// static constexpr uint32_t CANVAS_TEX_WIDTH  = 1390;
// static constexpr uint32_t CANVAS_TEX_HEIGHT = 690;

// Path to the shader binaries used by both the Examples triangle and the
// real obj pipeline. Compile the corresponding .vert/.frag GLSL sources
// (see comment blocks near their load sites below) to SPIR-V with
// glslc/glslangValidator and drop them here.
static const std::string SHADER_DIR = getAssetsPath() + "shaders/";

bool showCanvas = true;

#ifdef __EMSCRIPTEN__
// Web: 2D only – no GPU device, no Vulkan/WebGPU risk
constexpr Engine3D::CanvasMode g_canvasMode = Engine3D::CanvasMode::Mode2D;
#else
// Desktop: you can freely switch between 2D and 3D here
constexpr Engine3D::CanvasMode g_canvasMode = Engine3D::CanvasMode::Mode3D;
#endif

static glm::vec3 g_lastAddedPos3D(0.0f, 0.0f, 0.0f);

// Helper to initialise g_lastAddedPos3D from the current world
void updateLastAddedPosFromWorld(const ECSWorld& world) {
    if (world.entity_count == 0) {
        g_lastAddedPos3D = glm::vec3(0.0f);
        return;
    }
    // Find the entity with the largest X (or just take the last entity in the pool)
    // We'll take the entity with highest index (most recently added) that has position
    for (Entity e = world.entity_count - 1; e != (Entity)-1; --e) {
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
            if (world.has_position3d[e]) {
                g_lastAddedPos3D = glm::vec3(world.position3d_pool[e].x, world.position3d_pool[e].y, world.position3d_pool[e].z);
                return;
            }
        } else {
            if (world.has_position[e]) {
                g_lastAddedPos3D = glm::vec3(world.position_pool[e].x, 0.0f, world.position_pool[e].y);
                return;
            }
        }
    }
    g_lastAddedPos3D = glm::vec3(0.0f);
}

void create_entity_with_user_input(ECSWorld& world, const std::string& name, float w, float h, float depth = 50.0f) {
    Entity e = world.create_entity();
    world.add_metadata(e);
    world.add_rectangle_shape(e);
    world.add_z_index(e);
    world.add_selection(e);
    world.metadata_pool[e] = { name };
    world.rectangle_shape_pool[e] = { w, h };
    world.z_index_pool[e].z = (world.entity_count > 1) ? world.z_index_pool[world.entity_count-2].z + 1 : 1;
    world.selection_pool[e] = { false, {0, 255, 0, 255} };

    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
        world.add_depth3d(e);
        world.depth3d_pool[e] = { depth };
        
        // Add 3D Position Component
        world.add_position3d(e);
        
        // Spawn near origin with slight offset to avoid exact overlap
        // Randomize slightly or use a grid pattern starting at 0,0,0
        static int spawnCounter = 0;
        float offset = 1.5f;
        int row = (int)std::sqrt(spawnCounter);
        int col = spawnCounter - row * row;
        
        world.position3d_pool[e].x = col * offset;
        world.position3d_pool[e].y = 0.0f; // Ground level
        world.position3d_pool[e].z = row * offset;
        
        spawnCounter++;
    } else {
        world.add_position(e);
        // 2D placement logic remains same
        float centerX = editorScrollX + canvasViewW / 2.0f;
        float centerY = editorScrollY + canvasViewH / 2.0f;
        world.position_pool[e] = { centerX - w / 2.0f, centerY - h / 2.0f };
        clamp_entity_position_to_canvas(world.position_pool[e], w, h);
    }
}

bool intersectRayAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                      const glm::vec3& boxCenter, const glm::vec3& boxHalfExtents, 
                      float& tNear, float& tFar) {
    glm::vec3 invDir = 1.0f / rayDir;
    glm::vec3 tMin = (boxCenter - boxHalfExtents - rayOrigin) * invDir;
    glm::vec3 tMax = (boxCenter + boxHalfExtents - rayOrigin) * invDir;
    
    glm::vec3 t1 = glm::min(tMin, tMax);
    glm::vec3 t2 = glm::max(tMin, tMax);
    
    tNear = glm::max(glm::max(t1.x, t1.y), t1.z);
    tFar = glm::min(glm::min(t2.x, t2.y), t2.z);
    
    return tFar >= std::max(tNear, 0.0f);
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
    // ===== ADDED: depth spinbox, only constructed/used in a 3D build.
    std::optional<Gui::SpinBox> depthBox;

    AddEntityDialog(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font, SDL_Window* window)
        : Gui::Dialog(renderer, textEngine, font, window,
                      {480.0f, 245.0f, 480.0f, 310.0f},
                      "+ Entity", "Create", "Cancel")
        , nameField(renderer, textEngine, font, {0,0,1,1}, "Entity name...")
        , widthBox(renderer, textEngine, font, {0,0,1,1}, 1.0f, 9999.0f, 50.0f)
        , heightBox(renderer, textEngine, font, {0,0,1,1}, 1.0f, 9999.0f, 50.0f)
    {
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
            depthBox.emplace(renderer, textEngine, font, SDL_FRect{0,0,1,1}, 0.1f, 9999.0f, 50.0f);
        }
    }

    ~AddEntityDialog() override = default;

    float getDepthValue() const {
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
            return depthBox.has_value() ? depthBox->getValue() : 50.0f;
        } else {
            return 50.0f;
        }
    }

    Gui::ITextInput* getCurrentTextInput() override {
        if (nameField.isActive()) return &nameField;
        if (widthBox.isActive()) return &widthBox;
        if (heightBox.isActive()) return &heightBox;
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
            if (depthBox.has_value() && depthBox->isActive()) return &(*depthBox);
        }
        return nullptr;
    }

    bool onHandleGamepad(float cursorX, float cursorY, bool confirmDown, bool confirmDownLastFrame) override {
        bool wasActive = nameField.isActive() || widthBox.isActive() || heightBox.isActive();
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
            if (depthBox.has_value()) wasActive = wasActive || depthBox->isActive();
        }
        nameField.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
        widthBox.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
        heightBox.handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
        bool isActive = nameField.isActive() || widthBox.isActive() || heightBox.isActive();
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
            if (depthBox.has_value()) {
                depthBox->handleGamepad(cursorX, cursorY, 0.0f, 0.0f, window, confirmDown, confirmDownLastFrame);
                isActive = isActive || depthBox->isActive();
            }
        }
        if (confirmDown && !confirmDownLastFrame && (isActive || wasActive)) return true;
        return false;
    }

protected:
    void onOpen() override {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        float dialogH = 310.0f;
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) dialogH = 356.0f;
        logicalRect = {
            ((float)w - 480.0f) * 0.5f,
            ((float)h - dialogH) * 0.5f,
            480.0f, dialogH
        };
    }

    bool onHandleEvent(const SDL_Event& ev) override {
        if (nameField.handleEvent(ev, window, 0.0f, 0.0f)) return true;
        if (widthBox.handleEvent(ev, window, 0.0f, 0.0f)) return true;
        if (heightBox.handleEvent(ev, window, 0.0f, 0.0f)) return true;
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
            if (depthBox.has_value() && depthBox->handleEvent(ev, window, 0.0f, 0.0f)) return true;
        }
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
        
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
            if (depthBox.has_value()) {
                drawText("Depth:", win.x + 14.0f, win.y + 200.0f, {80,80,80,255});
                depthBox->setRect({ win.x+14, win.y+226, win.w-28, 32 });
                depthBox->render(0.0f, 0.0f);
            }
        }
    }

    void onReset() override {
        nameField.clear();
        nameField.deactivate(window);
        widthBox.deactivate(window);
        heightBox.deactivate(window);
        if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
            if (depthBox.has_value()) depthBox->deactivate(window);
        }
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

void deleteWorldOverlayEntity(Entity id) {
    if (id >= g_worldOverlay.entity_count) return;
    Entity last = g_worldOverlay.entity_count - 1;
    g_worldOverlayEntities.erase(
        std::remove(g_worldOverlayEntities.begin(), g_worldOverlayEntities.end(), id),
        g_worldOverlayEntities.end());
    g_worldOverlay.delete_entity(id);
    if (id != last) {
        for (Entity& other : g_worldOverlayEntities) if (other == last) other = id;
    }
}

void reloadWorldOverlay(const std::string& sceneFilePath, const std::string& projectRoot) {
    g_worldOverlay = ECSWorld();
    g_worldOverlayEntities.clear();
    g_worldOverlayPath = deriveWorldFilePath(sceneFilePath);
    bool generatedFlag = false; 
    WorldBinary::load(g_worldOverlayPath, g_worldOverlay, g_worldOverlayEntities,
                      generatedFlag, projectRoot);
}

void draw_world_overlay(SDL_Renderer* renderer, TTF_TextEngine* textEngine, TTF_Font* font,
                        Tools::Camera& camera) {
    if (!g_showWorldOverlay || g_worldOverlayEntities.empty()) return;
    float viewX = canvasViewX;
    float viewY = canvasViewY;
    float viewW = canvasViewW;
    float viewH = canvasViewH;
    float wL, wT, wR, wB;
    camera.getVisibleWorldBounds(viewX, viewY, viewW, viewH, wL, wT, wR, wB);
    const float cullMargin = 20.0f; 
    SDL_FRect visibleRect = { wL - cullMargin, wT - cullMargin,
                              (wR - wL) + 2.0f * cullMargin,
                              (wB - wT) + 2.0f * cullMargin };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (Entity e : g_worldOverlayEntities) {
        if (!g_worldOverlay.has_position[e]) continue;
        float w = g_worldOverlay.has_rectangle_shape[e]
                  ? g_worldOverlay.rectangle_shape_pool[e].w
                  : 16.0f;
        float h = g_worldOverlay.has_rectangle_shape[e]
                  ? g_worldOverlay.rectangle_shape_pool[e].h
                  : 16.0f;
        float ex = g_worldOverlay.position_pool[e].x;
        float ey = g_worldOverlay.position_pool[e].y;
        if (ex + w < visibleRect.x || ex > visibleRect.x + visibleRect.w ||
            ey + h < visibleRect.y || ey > visibleRect.y + visibleRect.h) {
            continue;
        }
        float scaledW = w * camera.zoom;
        float scaledH = h * camera.zoom;
        SDL_FPoint screenPos = camera.worldToScreen(ex, ey);
        float screenX = screenPos.x;
        float screenY = screenPos.y;
        bool rendered = false;
        if (g_worldOverlay.has_texture_ref[e]) {
            rendered = render_entity_texture(renderer, g_worldOverlay, e,
                                             screenX, screenY, camera.zoom);
        }
        if (!rendered && g_worldOverlay.has_animation_state[e]) {
            rendered = render_entity_animation(renderer, g_worldOverlay, e,
                                               screenX, screenY, camera.zoom);
        }
        if (!rendered) {
            SDL_FRect rect = { screenX, screenY, scaledW, scaledH };
            SDL_SetRenderDrawColor(renderer, 90, 170, 255, 55);
            SDL_RenderFillRect(renderer, &rect);
            SDL_SetRenderDrawColor(renderer, 90, 170, 255, 150);
            SDL_RenderRect(renderer, &rect);
        }
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

std::vector<unsigned char> loadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        SDL_Log("Could not open shader file: %s", path.c_str());
        return {};
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }
    SDL_Log("Failed to read shader file: %s", path.c_str());
    return {};
}

static SDL_GPUShader* CreateShaderFromSPV(SDL_GPUDevice* device,
                                          const unsigned char* bytecode,
                                          size_t bytecode_len,
                                          SDL_GPUShaderStage stage,
                                          SDL_GPUShaderFormat format) {
    SDL_GPUShaderCreateInfo info = {};
    info.code = bytecode;
    info.code_size = bytecode_len;
    info.stage = stage;
    info.format = format;
    info.entrypoint = "main";
    info.num_samplers = 0;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    if (!shader) {
        SDL_Log("Failed to create shader: %s", SDL_GetError());
    }
    return shader;
}


int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    const float windowWidth  = 1600.0f;
    const float windowHeight = 900.0f;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { std::cerr << "SDL Initialization failed: " << SDL_GetError() << std::endl; return 1; }
    if (!TTF_Init()) { std::cerr << "TTF Initialization failed: " << SDL_GetError() << std::endl; SDL_Quit(); return 1; }
    if (!MIX_Init()) { SDL_Log("Could not initialize mixer: %s", SDL_GetError()); SDL_Quit(); return -1; }
    SDL_Window* window = SDL_CreateWindow("Dispersed Engine", (int)windowWidth, (int)windowHeight, SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "Window creation failed: " << SDL_GetError() << std::endl; TTF_Quit(); SDL_Quit(); return 1; }
    SDL_Surface* iconSurface = IMG_Load((getAssetsPath() + "icon.svg").c_str());
    if (!iconSurface) SDL_Log("Failed to load icon: %s", SDL_GetError()); else SDL_SetWindowIcon(window, iconSurface);
    SDL_GPUDevice* gpuDevice = nullptr;
    SDL_Renderer* renderer   = nullptr;
    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_VERBOSE);
    SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, "C:/Windows/System32/vulkan-1.dll");
    gpuDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, "vulkan");
    if (!gpuDevice) {
    SDL_Log("GPU device creation failed: %s", SDL_GetError());
    SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1;
    }
    SDL_ClaimWindowForGPUDevice(gpuDevice, window);
    renderer = SDL_CreateGPURenderer(gpuDevice, window);
    if (!renderer) {
    SDL_Log("GPURenderer creation failed: %s", SDL_GetError());
    SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1;
    }
    } else {
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
    SDL_Log("Renderer creation failed: %s", SDL_GetError());
    SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1;
    }
    }
    g_resources.TextureManager.SetRenderer(renderer);
    std::string GamepadCursorIconPath = getAssetsPath() + "icon.ico";
    g_resources.TextureManager.Load("GamePadCursorIcon", GamepadCursorIconPath);
    Engine3D::Examples::TriangleResources triangleExample;
    SDL_GPUShader* objVertexShader = nullptr;
    SDL_GPUShader* objFragmentShader = nullptr;
    SDL_GPUGraphicsPipeline* objPipeline = nullptr;
    Engine3D::DefaultGpuResources gpuDefaults;
    Engine3D::ObjModel cubeObjModel;
    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
    if (!Engine3D::Examples::setupTriangleExample(gpuDevice, SHADER_DIR,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, triangleExample)) {
    SDL_Log("Triangle example setup failed (missing shader binaries?) -- continuing without it.");
    }
    objVertexShader = Engine3D::loadShaderSPV(gpuDevice, SHADER_DIR + "obj.vert.spv",
    SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    objFragmentShader = Engine3D::loadShaderSPV(gpuDevice, SHADER_DIR + "obj.frag.spv",
    SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (objVertexShader && objFragmentShader) {
    objPipeline = Engine3D::createObjPipeline(gpuDevice, objVertexShader, objFragmentShader,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    } else {
    SDL_Log("Obj shaders failed to load -- renderObj() will be skipped this run.");
    }
    if (!Engine3D::loadObjModel(gpuDevice, getAssetsPath() + "models/cube.obj", gpuDefaults, cubeObjModel)) {
    SDL_Log("Failed to load GPU obj model for cube.obj");
    }
    }
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
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
    Engine3D::Transform3D testTransform;
    Engine3D::Mesh3D testMesh;
    bool meshLoaded = Engine3D::loadMesh(getAssetsPath() + "models/cube.obj", testMesh);
    Physics3D::InitJolt();
    auto g_physicsWorld3D = std::make_unique<Physics3D::PhysicsWorld3D>(glm::vec3(0.0f, -2.0f, 0.0f));
    Physics3D::PhysicsBody3D* cubePhysicsBody = nullptr;
    {
    auto cubeBody = std::make_unique<Physics3D::RigidBody3D>(
    g_physicsWorld3D->GetSystem(), glm::vec3(0.0f, 2.0f, 0.0f));
    cubeBody->AddBox(glm::vec3(0.5f, 0.5f, 0.5f));
    cubePhysicsBody = cubeBody.get();
    g_physicsWorld3D->AddOwned(std::move(cubeBody));
    auto floorBody = std::make_unique<Physics3D::StaticBody3D>(
    g_physicsWorld3D->GetSystem(), glm::vec3(0.0f, -1.0f, 0.0f));
    floorBody->AddBox(glm::vec3(10.0f, 0.5f, 10.0f));
    g_physicsWorld3D->AddOwned(std::move(floorBody));
    }
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
    fileExplorer.setCallback([&](const std::string& path) {
    scene = sceneParser.loadFromFile(path);
    currentSceneFilePath = path;
    entityInspector.setProjectRoot(scene.projectRoot);
    reloadWorldOverlay(currentSceneFilePath, scene.projectRoot);
    updateLastAddedPosFromWorld(scene.world);
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
    textEditor.saveFile();
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
    textEditor.saveFile();
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
    static bool cameraPanning = false;
    static float cameraLastMouseX = 0.0f, cameraLastMouseY = 0.0f;
    SDL_Log("Entering main loop now");
    while (running) {
    Uint64 current_time = SDL_GetTicks();
    delta_time = (float)(current_time - last_time) / 1000.0f; last_time = current_time;
    {
    int winW0, winH0; SDL_GetWindowSize(window, &winW0, &winH0);
    const float toolbarHeight0 = 60.0f;
    const float inspectorWidth0 = inspectorVisible ? Gui::SceneInspector::PANEL_W : 0.0f;
    float cvX = 0.0f;
    float cvY = toolbarHeight0;
    float cvW = (float)winW0 - (inspectorVisible ? inspectorWidth0 : 0.0f);
    float cvH = (float)winH0 - toolbarHeight0;
    if (cvW < 200) cvW = 200;
    if (cvH < 200) cvH = 200;
    canvasViewX = cvX; canvasViewY = cvY; canvasViewW = cvW; canvasViewH = cvH;
    g_editorCamera.setupForCanvas(canvasViewX, canvasViewY, canvasViewW, canvasViewH);
    toolbarContainer->setRect({0.0f, 0.0f, (float)winW0, 60.0f});
    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
    g_canvasRenderTarget.resize(gpuDevice, renderer,
    (Uint32)canvasViewW, (Uint32)canvasViewH);
    g_editorCamera3D.setupForCanvas(canvasViewX, canvasViewY, canvasViewW, canvasViewH);
    }
    }
    while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT) { SDL_Log("QUIT event received - exiting"); running = false; }
    if (e.type == SDL_EVENT_KEY_DOWN) {
    bool ctrlDown = (e.key.mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL));
    if (ctrlDown && e.key.key == SDLK_I) inspectorVisible = !inspectorVisible;
    if (ctrlDown && e.key.key == SDLK_S) {
    inspector.commitAllFields();
    if (!currentSceneFilePath.empty()) sceneParser.saveToFile(scene, currentSceneFilePath);
    textEditor.saveFile();
    }
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
    create_entity_with_user_input(world, name, dialog->widthBox.getValue(), dialog->heightBox.getValue(), dialog->getDepthValue());
    if constexpr (g_canvasMode != Engine3D::CanvasMode::Mode3D) {
    Entity newEnt = world.entity_count - 1;
    float w = dialog->widthBox.getValue(), h = dialog->heightBox.getValue();
    float centerX = editorScrollX + canvasViewW / 2.0f, centerY = editorScrollY + canvasViewH / 2.0f;
    world.position_pool[newEnt] = { centerX - w / 2.0f, centerY - h / 2.0f };
    clamp_entity_position_to_canvas(world.position_pool[newEnt], w, h);
    }
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
    continue;
    }
    }
    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
    float mx = e.button.x, my = e.button.y;
    if (isInsideCanvas(mx, my)) {
    SDL_HideCursor();
    SDL_SetWindowRelativeMouseMode(window, true);
    g_camOrbiting = true;
    }
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) {
    g_camOrbiting = false;
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_ShowCursor();
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION && g_camOrbiting) {
    g_editorCamera3D.lookMouseDelta((float)e.motion.xrel, (float)e.motion.yrel);
    }
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
    float mx, my; SDL_GetMouseState(&mx, &my);
    if (isInsideCanvas(mx, my)) {
    bool rmbHeld = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_RMASK) != 0;
    g_editorCamera3D.scroll((float)e.wheel.y, rmbHeld);
    }
    }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION && cameraPanning) {
    float dx = e.motion.x - cameraLastMouseX;
    float dy = e.motion.y - cameraLastMouseY;
    g_editorCamera.pan(dx, dy);
    cameraLastMouseX = e.motion.x;
    cameraLastMouseY = e.motion.y;
    }
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
    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT &&
    isInsideCanvas((float)e.button.x, (float)e.button.y) &&
    (currentEditMode == EditMode::Select || currentEditMode == EditMode::MoveWithMouse ||
    currentEditMode == EditMode::Delete)) {
    float mx = (float)e.button.x;
    float my = (float)e.button.y;
    glm::vec3 rayOrigin = g_editorCamera3D.position;
    float aspect = (canvasViewH > 0.0f) ? (canvasViewW / canvasViewH) : 1.0f;
    glm::mat4 view = g_editorCamera3D.getViewMatrix();
    glm::mat4 proj = g_editorCamera3D.getProjectionMatrix(aspect, false);
    float nx = (2.0f * (mx - canvasViewX)) / canvasViewW - 1.0f;
    float ny = 1.0f - (2.0f * (my - canvasViewY)) / canvasViewH;
    glm::vec4 rayClip = glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 rayEye = glm::inverse(proj) * rayClip;
    rayEye.z = -1.0f; rayEye.w = 0.0f;
    glm::vec3 rayDirWorld = glm::normalize(glm::vec3(glm::inverse(view) * rayEye));
    Entity picked = (Entity)-1;
    float closestDist = std::numeric_limits<float>::max();
    for (Entity ent = 0; ent < world.entity_count; ++ent) {
    if (!world.has_position3d[ent]) continue;
    float w = world.has_rectangle_shape[ent] ? world.rectangle_shape_pool[ent].w : 1.0f;
    float h = world.has_rectangle_shape[ent] ? world.rectangle_shape_pool[ent].h : 1.0f;
    float d = world.has_depth3d[ent] ? world.depth3d_pool[ent].depth : 1.0f;
    glm::vec3 center(world.position3d_pool[ent].x, world.position3d_pool[ent].y, world.position3d_pool[ent].z);
    glm::vec3 halfExtents(w * 0.5f, h * 0.5f, d * 0.5f);
    float tNear, tFar;
    if (intersectRayAABB(rayOrigin, rayDirWorld, center, halfExtents, tNear, tFar)) {
    if (tNear < closestDist) {
    closestDist = tNear;
    picked = ent;
    }
    }
    }
    if (currentEditMode == EditMode::Delete) {
    if (picked != (Entity)-1) {
    if (selectedEntity == picked) { selectedEntity = (Entity)-1; entityInspector.clearTarget(); }
    if (lastSelectedEntity == picked) lastSelectedEntity = (Entity)-1;
    world.delete_entity(picked);
    }
    } else if (picked != (Entity)-1) {
    if (selectedEntity != (Entity)-1 && world.has_selection[selectedEntity]) {
    world.selection_pool[selectedEntity].isSelected = false;
    }
    if (world.has_selection[picked]) {
    world.selection_pool[picked].isSelected = true;
    }
    lastSelectedEntity = picked;
    selectEntity(picked);
    if (currentEditMode == EditMode::MoveWithMouse) {
    isDraggingLeftMouse = true;
    lastDragX = e.button.x;
    lastDragY = e.button.y;
    dragStartPos3D = glm::vec3(
    world.position3d_pool[picked].x,
    world.position3d_pool[picked].y,
    world.position3d_pool[picked].z
    );
    }
    } else {
    if (selectedEntity != (Entity)-1 && world.has_selection[selectedEntity]) {
    world.selection_pool[selectedEntity].isSelected = false;
    }
    lastSelectedEntity = (Entity)-1;
    selectEntity((Entity)-1);
    }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION && isDraggingLeftMouse &&
    currentEditMode == EditMode::MoveWithMouse &&
    lastSelectedEntity != (Entity)-1 && lastSelectedEntity < world.entity_count &&
    world.has_position3d[lastSelectedEntity]) {
    int dx = e.motion.x - lastDragX;
    int dy = e.motion.y - lastDragY;
    if (dx != 0 || dy != 0) {
    glm::vec3 entityPos(world.position3d_pool[lastSelectedEntity].x,
    world.position3d_pool[lastSelectedEntity].y,
    world.position3d_pool[lastSelectedEntity].z);
    float distance = glm::length(entityPos - g_editorCamera3D.position);
    float worldUnitsPerPixel = (canvasViewH > 0.0f)
    ? (2.0f * distance * std::tan(glm::radians(g_editorCamera3D.fovDeg * 0.5f)) / canvasViewH)
    : 0.0f;
    glm::vec3 right = g_editorCamera3D.getRight();
    glm::vec3 up = g_editorCamera3D.getUp();
    glm::vec3 worldDelta = right * ((float)dx * worldUnitsPerPixel) - up * ((float)dy * worldUnitsPerPixel);
    world.position3d_pool[lastSelectedEntity].x += worldDelta.x;
    world.position3d_pool[lastSelectedEntity].y += worldDelta.y;
    world.position3d_pool[lastSelectedEntity].z += worldDelta.z;
    lastDragX = e.motion.x;
    lastDragY = e.motion.y;
    }
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
    isDraggingLeftMouse = false;
    }
    }
    if (!consumedByCanvasTools && g_canvasMode == Engine3D::CanvasMode::Mode2D) {
    edit_object_with_editor_mouse(renderer, world, scene.guiElements, selectedGuiElem, e);
    }
    if (selectedGuiElem) { if (inspector.handleEvent(e)) continue; }
    else if (selectedEntity != (Entity)-1) { if (entityInspector.handleEvent(e)) continue; }
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
    if (!dialog->nameField.getText().empty()) create_entity_with_user_input(world, dialog->nameField.getText(), dialog->widthBox.getValue(), dialog->heightBox.getValue(), dialog->getDepthValue());
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
    if (selectedGuiElem) inspector.handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
    else if (selectedEntity != (Entity)-1) entityInspector.handleGamepad(cursorX, cursorY, confirmNow, confirmLastFrame);
    }
    confirmLastFrame = confirmNow;
    }
    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
    if (showCanvas) {
    const auto* keys = SDL_GetKeyboardState(nullptr);
    bool forward = keys[SDL_SCANCODE_W];
    bool back    = keys[SDL_SCANCODE_S];
    bool left    = keys[SDL_SCANCODE_A];
    bool right   = keys[SDL_SCANCODE_D];
    bool up      = keys[SDL_SCANCODE_Q];
    bool down    = keys[SDL_SCANCODE_E];
    bool fast    = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    if (forward || back || left || right || up || down) {
    g_editorCamera3D.flyMove(delta_time, forward, back, left, right, up, down, fast);
    g_editorCamera3D.clampToBounds();
    }
    }
    }
    animation_system(world, delta_time);
    if (selectedEntity != (Entity)-1) {
    if (selectedEntity >= world.entity_count ||
    (!world.has_position[selectedEntity] && !world.has_position3d[selectedEntity])) {
    selectedEntity = (Entity)-1;
    entityInspector.clearTarget();
    if (lastSelectedEntity != (Entity)-1) {
    lastSelectedEntity = (Entity)-1;
    }
    }
    }
    if (lastSelectedEntity != (Entity)-1) {
    if (lastSelectedEntity >= world.entity_count ||
    (!world.has_position[lastSelectedEntity] && !world.has_position3d[lastSelectedEntity])) {
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
    SDL_SetRenderDrawColor(renderer, 20, 20, 24, 255);
    SDL_RenderClear(renderer);
    int winW, winH; SDL_GetWindowSize(window, &winW, &winH);
    const float toolbarHeight = 60.0f;
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
    SDL_FRect canvasRect = { canvasViewX, canvasViewY, canvasViewW, canvasViewH };
    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode3D) {
    float aspect = (canvasViewH > 0.0f) ? (canvasViewW / canvasViewH) : 1.0f;
    glm::mat4 view = g_editorCamera3D.getViewMatrix();
    glm::mat4 proj = g_editorCamera3D.getProjectionMatrix(aspect, false);
    SDL_GPUCommandBuffer* gpuCmd = SDL_AcquireGPUCommandBuffer(gpuDevice);
    if (gpuCmd && g_canvasRenderTarget.isValid()) {
    SDL_GPUColorTargetInfo colorInfo = {};
    colorInfo.texture = g_canvasRenderTarget.colorTexture;
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;
    colorInfo.clear_color = {0.08f, 0.08f, 0.12f, 1.0f};
    SDL_GPUDepthStencilTargetInfo depthInfo = {};
    depthInfo.texture = g_canvasRenderTarget.depthTexture;
    depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    depthInfo.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthInfo.clear_depth = 1.0f;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(gpuCmd, &colorInfo, 1, &depthInfo);
    if (pass) {
    if (objPipeline) {
    for (Entity e = 0; e < world.entity_count; ++e) {
    if (!world.has_position3d[e] || !world.has_mesh3d[e]) continue;
    const std::string& path = world.mesh3d_pool[e].modelPath;
    if (path.empty()) continue;
    Engine3D::ObjModel* model = g_modelCache3D.GetOrLoad(gpuDevice, gpuDefaults, path);
    if (!model || !model->isLoaded) continue;

    Engine3D::Transform3D t;
    t.position = glm::vec3(world.position3d_pool[e].x, world.position3d_pool[e].y, world.position3d_pool[e].z);

    // Apply rotation if component exists
    if (world.has_rotation[e]) {
        t.rotation.y = world.rotation_pool[e].degrees;
    }

    // Mesh dimensions (meshW/meshH/meshD on Mesh3DRef) are deliberately
    // independent from the outline's width/height/depth (RectangleShape's
    // w/h + Depth3D's depth) -- they scale the *loaded mesh* relative to
    // its own native bounding box, so the mesh can be resized without
    // touching the outline drawn for the entity below. 0 on any axis
    // means "keep the model's native size on that axis".
    const auto& meshRef = world.mesh3d_pool[e];
    glm::vec3 nativeSize = model->boundsMax - model->boundsMin;
    float sx = 1.0f, sy = 1.0f, sz = 1.0f;
    if (meshRef.meshW > 0.0f && nativeSize.x > 0.0001f) sx = meshRef.meshW / nativeSize.x;
    if (meshRef.meshH > 0.0f && nativeSize.y > 0.0001f) sy = meshRef.meshH / nativeSize.y;
    if (meshRef.meshD > 0.0f && nativeSize.z > 0.0001f) sz = meshRef.meshD / nativeSize.z;

    // Generic Scale component (if present) still applies as an extra
    // x/y multiplier on top of the mesh's own dimensions, kept for
    // backward-compat with existing Scale usage.
    if (world.has_scale[e]) {
        sx *= world.scale_pool[e].x;
        sy *= world.scale_pool[e].y;
    }

    t.scale = glm::vec3(sx, sy, sz);

    Engine3D::renderObj(gpuCmd, pass, objPipeline, *model, t, view, proj, gpuDefaults);
    }
    }
    SDL_EndGPURenderPass(pass);
    }
    SDL_SubmitGPUCommandBuffer(gpuCmd);
    SDL_WaitForGPUIdle(gpuDevice);
    } else if (gpuCmd) {
    SDL_CancelGPUCommandBuffer(gpuCmd);
    }
    if (g_canvasRenderTarget.sdlTexture) {
    SDL_RenderTexture(renderer, g_canvasRenderTarget.sdlTexture, nullptr, &canvasRect);
    }
    Engine3D::renderAxisRulerGizmo3D(renderer, textEngine, g_resources.FontManager.Get("regularFont"), g_editorCamera3D);

    // Render wireframe outlines for every 3D entity, mesh or not. This
    // is the entity's own bounding-box outline (driven by RectangleShape's
    // w/h + Depth3D's depth) and is intentionally kept visible even once
    // a mesh is loaded -- the mesh's own size is controlled separately by
    // Mesh3DRef's meshW/meshH/meshD (see the renderObj loop above), so the
    // outline and the mesh no longer fight over the same dimensions.
    for (Entity e = 0; e < world.entity_count; ++e) {
    if (!world.has_position3d[e]) continue;

    float w = world.has_rectangle_shape[e] ? world.rectangle_shape_pool[e].w : 100.0f;
    float h = world.has_rectangle_shape[e] ? world.rectangle_shape_pool[e].h : 100.0f;
    float d = world.has_depth3d[e] ? world.depth3d_pool[e].depth : 50.0f;

    // Apply scale
    float sx = world.has_scale[e] ? world.scale_pool[e].x : 1.0f;
    float sy = world.has_scale[e] ? world.scale_pool[e].y : 1.0f;
    w *= sx;
    h *= sy;

    glm::vec3 center(world.position3d_pool[e].x, world.position3d_pool[e].y, world.position3d_pool[e].z);
    glm::vec3 half(w * 0.5f, h * 0.5f, d * 0.5f);

    SDL_Color color = {255, 255, 255, 255};
    if (world.has_selection[e] && world.selection_pool[e].isSelected) {
    color = world.selection_pool[e].selectionColor;
    }
    Engine3D::renderEntityCubeOutline3D(renderer, g_editorCamera3D, center, half, color);
    }

    // Render Collider3D outlines. Kept as its own pass, in its own color
    // family, so a fresh collider is immediately visible/distinguishable
    // from the entity's own cube outline above:
    //   - default (not selected): cyan   -- vs. the cube outline's white default
    //   - collider selected:      pink   -- vs. the cube outline's selectionColor (blue/green/etc.)
    // Only Shape::Box gets a simple box wireframe here, matching
    // "adding a collider shows a new basic box collider" -- other shapes
    // (Sphere/Capsule/ConvexHull/Mesh/RiggedApprox) aren't cubes and
    // aren't covered by this simple box gizmo.
    for (Entity e = 0; e < world.entity_count; ++e) {
    if (!world.has_position3d[e] || !world.has_collider3d[e]) continue;
    const auto& col = world.collider3d_pool[e];
    if (col.shape != Components::Collider3DRef::Shape::Box) continue;

    glm::vec3 colliderCenter(world.position3d_pool[e].x, world.position3d_pool[e].y, world.position3d_pool[e].z);
    glm::vec3 colliderHalf(col.halfExtentsX, col.halfExtentsY, col.halfExtentsZ);

    bool colliderSelected = world.has_selection[e] && world.selection_pool[e].isSelected;
    SDL_Color colliderColor = colliderSelected
        ? SDL_Color{255, 105, 180, 255}  // pink: collider selection
        : SDL_Color{0, 255, 255, 255};   // cyan: basic box collider default

    Engine3D::renderEntityCubeOutline3D(renderer, g_editorCamera3D, colliderCenter, colliderHalf, colliderColor);
    }

    // Render GridMap3D entities
    for (Entity e = 0; e < world.entity_count; ++e) {
    if (!world.has_position3d[e] || !world.has_gridmap3d[e]) continue;
    auto& gm = world.gridmap3d_pool[e];
    if (gm.cells.empty()) continue;

    glm::vec3 entityPos(world.position3d_pool[e].x, world.position3d_pool[e].y, world.position3d_pool[e].z);
    for (const auto& cell : gm.cells) {
        float cx = entityPos.x + cell.x * gm.cellWidth + cell.offX;
        float cy = entityPos.y + cell.y * gm.cellHeight + cell.offY;
        float cz = entityPos.z + cell.z * gm.cellDepth + cell.offZ;
        glm::vec3 cellCenter(cx, cy, cz);
        glm::vec3 cellHalf(gm.cellWidth * 0.5f, gm.cellHeight * 0.5f, gm.cellDepth * 0.5f);
        Engine3D::renderEntityCubeOutline3D(renderer, g_editorCamera3D, cellCenter, cellHalf, {100, 200, 255, 255});
    }
    }
    }
    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode2D) {
    SDL_SetRenderDrawColor(renderer, 30, 30, 35, 255);
    SDL_RenderFillRect(renderer, &canvasRect);
    render_editor_canvas(renderer);
    draw_world_overlay(renderer, textEngine, g_resources.FontManager.Get("regularFont"), g_editorCamera);
    render_system_and_scene_gui_in_editor(
    renderer, textEngine, g_resources.FontManager.Get("regularFont"),
    world, canvasViewX, canvasViewY,
    editorScrollX, editorScrollY,
    guiElements,
    g_editorCamera
    );
    }
    verticalScrollbar.render(renderer);
    horizontalScrollbar.render(renderer);
    float toolsX = canvasViewX + canvasViewW - 10;
    float toolsY = canvasViewY + 30;
    float containerWidth = 140;
    canvasTools->setRect({ toolsX - containerWidth, toolsY, containerWidth, 40 });
    canvasTools->render(0.0f, 0.0f);
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
    if constexpr (g_canvasMode == Engine3D::CanvasMode::Mode2D) {
    if (meshLoaded) {
    testTransform.rotation.y += delta_time * 30.0f;
    testTransform.position.z = 3.0f;
    Engine3D::renderMeshWireFrame(renderer, testTransform, testMesh,
    canvasViewW, canvasViewH);
    }
    }
    GamepadCursorIconSrcRect.x = cursorX;
    GamepadCursorIconSrcRect.y = cursorY;
    GamepadCursorIconDestRect.w = winW;
    GamepadCursorIconDestRect.h = winH;
    SDL_RenderTexture(renderer, g_resources.TextureManager.Get("GamePadCursorIcon"), &GamepadCursorIconSrcRect, &GamepadCursorIconDestRect);
    if (showVirtualKeyboard) virtualKeyboard->render();
    SDL_RenderPresent(renderer);
    }
    delete dialog; delete dialog2; delete virtualKeyboard;
    if (gamepad) SDL_CloseGamepad(gamepad);
    cubePhysicsBody = nullptr;
    g_physicsWorld3D.reset();
    Physics3D::ShutdownJolt();
    if (gpuDevice) {
    Engine3D::destroyObjModel(gpuDevice, cubeObjModel);
    Engine3D::destroyDefaultGpuResources(gpuDevice, gpuDefaults);
    if (objPipeline) SDL_ReleaseGPUGraphicsPipeline(gpuDevice, objPipeline);
    if (objVertexShader) SDL_ReleaseGPUShader(gpuDevice, objVertexShader);
    if (objFragmentShader) SDL_ReleaseGPUShader(gpuDevice, objFragmentShader);
    Engine3D::Examples::destroyTriangleExample(gpuDevice, triangleExample);
    g_canvasRenderTarget.destroy(gpuDevice);
    }
    g_resources.FontManager.Clear();
    g_resources.TextureManager.Clear();
    g_resources.AudioManager.Clear();
    TTF_Quit();
    MIX_Quit();
    if (iconSurface) SDL_DestroySurface(iconSurface);
    TTF_DestroyRendererTextEngine(textEngine);
    SDL_DestroyRenderer(renderer);
    if (gpuDevice) {
    SDL_ReleaseWindowFromGPUDevice(gpuDevice, window);
    SDL_DestroyGPUDevice(gpuDevice);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}