#pragma once
// ============================================================
// visual_logic.h
//
// A node-graph ("blueprint" style) visual script editor that lives
// alongside the existing Gui::TextEditor in the engine's editor.
//
// Design summary (please read before extending):
//  - A Graph is a set of VisualNodes + Connections. Every node has zero
//    or more Pins (Exec pins = control flow arrows, Data pins = value
//    arrows). Nodes are drawn as rectangles; connections are drawn as
//    arrows between pin sockets.
//  - Property editing on a node happens IN the node, using the engine's
//    real Gui::LineEdit / Gui::SpinBox widgets (not custom controls),
//    exactly as requested — the same widgets the rest of the editor
//    already uses.
//  - Code generation walks the graph starting from the four ScriptBase
//    event nodes (OnStart/OnUpdate/OnDraw/OnEnd) and emits plain,
//    readable C++17 that matches the shape of a hand-written
//    ScriptBase subclass (see game.h / game.cpp for the reference
//    shape this mirrors: a *Context struct, a *Script : ScriptBase
//    class, private fields, onStart/onUpdate/onDraw/onEnd bodies).
//  - To guarantee the graph can reproduce ANY statement exactly
//    (including the exact physics/math lines from an existing script,
//    two node kinds are escape hatches:
//    FunctionCall and CustomCode. Both simply hold literal C++ text
//    typed into a LineEdit and emit it verbatim. Structured nodes
//    (events, fields, variables, if/for/while, entity lookup) exist so
//    the *shape* of the program is visible as boxes and arrows, while
//    CustomCode/FunctionCall nodes carry exact statements. This is the
//    same hybrid approach real blueprint tools use (Unreal, etc.) —
//    full "parse arbitrary C++ into 100% semantic nodes" is out of
//    scope for any node system, visual or otherwise.
//  - Saving writes a normal .h and a normal .cpp — no new file
//    extension is introduced. The .cpp additionally carries the graph
//    itself as a base64 JSON blob inside a C++ comment block, so
//    opening that same .cpp back up in the Visual editor restores the
//    exact graph (lossless round trip, single file pair, no sidecar).
//    Opening a .cpp that was NOT authored by this tool (e.g. a hand
//    written game.cpp) is still possible — it just starts as a fresh
//    graph with a note node, since there is no graph to recover; the
//    Text Editor remains the right tool for hand-written files.
// ============================================================

#include "engine.h"
#include <sstream>
#include <set>
#include <random>
#include <regex>

namespace VisualLogic {

// ------------------------------------------------------------------
// Small self-contained base64 (no external deps) — used only to embed
// the graph JSON inside a C++ comment in the generated .cpp file.
// ------------------------------------------------------------------
inline std::string base64Encode(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) { out.push_back(tbl[(val >> valb) & 0x3F]); valb -= 6; }
    }
    if (valb > -6) out.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
inline std::string formatCodeWithNewlines(const std::string& code) {
    std::string result;
    result.reserve(code.size() * 1.2);
    bool inString = false, inChar = false;
    for (size_t i = 0; i < code.size(); ++i) {
        char c = code[i];
        if (c == '"' && (i == 0 || code[i-1] != '\\')) inString = !inString;
        else if (c == '\'' && (i == 0 || code[i-1] != '\\')) inChar = !inChar;
        result.push_back(c);
        if (c == ';' && !inString && !inChar) {
            // Skip if already followed by newline (or whitespace+newline)
            size_t j = i+1;
            while (j < code.size() && std::isspace(code[j])) {
                if (code[j] == '\n') break;
                ++j;
            }
            if (j == code.size() || code[j] != '\n') {
                result.push_back('\n');
            }
        }
    }
    return result;
}
inline std::string base64Decode(const std::string& in) {
    static int T[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i) T[i] = -1;
        std::string tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < (int)tbl.size(); ++i) T[(unsigned char)tbl[i]] = i;
        init = true;
    }
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) { out.push_back(char((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}
inline std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ------------------------------------------------------------------
// Project-wide rename helpers. These are deliberately textual rather than
// a real C++ refactor (this tool doesn't parse C++ — see the file header
// note about CustomCode/FunctionCall) but \b...\b word-boundary matching
// is enough to safely rename a whole identifier like a struct/class name
// wherever it appears (declarations, pointers, "ClassName::method", the
// getName() string literal, etc.) without touching partial matches inside
// longer identifiers.
// ------------------------------------------------------------------
inline std::string regexEscapeIdentifier(const std::string& s) {
    static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
    return std::regex_replace(s, special, R"(\$&)");
}

// Renames one identifier to another inside a single file on disk.
// Returns true if the file's contents changed.
inline bool renameIdentifierInFile(const std::filesystem::path& path,
                                   const std::string& oldName,
                                   const std::string& newName) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::stringstream ss; ss << in.rdbuf();
    in.close();
    std::string text = ss.str();
    
    // Fast string replacement with word boundary checking
    std::string result;
    result.reserve(text.size());
    size_t pos = 0;
    bool changed = false;
    
    while (pos < text.size()) {
        size_t found = text.find(oldName, pos);
        if (found == std::string::npos) {
            result += text.substr(pos);
            break;
        }
        
        // Check word boundaries
        bool leftBoundary = (found == 0) || !std::isalnum((unsigned char)text[found - 1]) && text[found - 1] != '_';
        bool rightBoundary = (found + oldName.size() >= text.size()) || 
                            (!std::isalnum((unsigned char)text[found + oldName.size()]) && 
                             text[found + oldName.size()] != '_');
        
        if (leftBoundary && rightBoundary) {
            result += text.substr(pos, found - pos);
            result += newName;
            pos = found + oldName.size();
            changed = true;
        } else {
            result += text.substr(pos, found - pos + 1);
            pos = found + 1;
        }
    }
    
    if (!changed) return false;
    
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << result;
    return true;
}

// Walks every .h/.hpp/.cpp file under `root` and applies the rename.
inline void renameIdentifierUnderRoot(const std::filesystem::path& root,
                                       const std::string& oldName,
                                       const std::string& newName) {
    if (oldName.empty() || newName.empty() || oldName == newName) return;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) break;
        std::error_code fec;
        if (!it->is_regular_file(fec) || fec) continue;
        std::string ext = it->path().extension().string();
        if (ext != ".h" && ext != ".hpp" && ext != ".cpp") continue;
        renameIdentifierInFile(it->path(), oldName, newName);
    }
}

// ------------------------------------------------------------------
// Data model
// ------------------------------------------------------------------
enum class PinKind { Exec, Data };
enum class PinDir  { Input, Output };

struct Pin {
    std::string name;
    PinKind kind = PinKind::Data;
    PinDir  dir  = PinDir::Input;
    std::string dataType = "float"; // cosmetic only (colors the socket)
};

enum class NodeKind {
    EventOnStart, EventOnUpdate, EventOnDraw, EventOnEnd,
    FieldDecl, ScriptVarDecl, VarGet, VarSet, EntityByName,
    GuardReturn, IfBranch, ForRangeEntities, WhileLoop,
    FunctionCall, BinaryOp, Literal, Comment, CustomCode,
    MethodDecl, MethodDef, IncludeDecl, WebLoop, MainLoop,
    EndFunction
};


inline std::string nodeKindLabel(NodeKind k) {
    switch (k) {
        case NodeKind::EventOnStart:      return "On Start";
        case NodeKind::EventOnUpdate:     return "On Update";
        case NodeKind::EventOnDraw:       return "On Draw";
        case NodeKind::EventOnEnd:        return "On End";
        case NodeKind::FieldDecl:         return "Field";
        case NodeKind::ScriptVarDecl:     return "Script Var";
        case NodeKind::VarGet:            return "Get Var";
        case NodeKind::VarSet:            return "Set Var";
        case NodeKind::EntityByName:      return "Entity By Name";
        case NodeKind::GuardReturn:       return "Guard (return if)";
        case NodeKind::IfBranch:          return "If";
        case NodeKind::ForRangeEntities:  return "For Range";
        case NodeKind::WhileLoop:         return "While";
        case NodeKind::FunctionCall:      return "Call";
        case NodeKind::BinaryOp:          return "Binary Op";
        case NodeKind::Literal:           return "Literal";
        case NodeKind::Comment:           return "Comment";
        case NodeKind::CustomCode:        return "Custom Code";
        case NodeKind::MethodDecl:        return "Method Decl";
        case NodeKind::MethodDef:         return "Method Def";
        case NodeKind::IncludeDecl:       return "Include";
        case NodeKind::WebLoop:           return "Web Loop";
        case NodeKind::MainLoop:          return "Main Loop";
        case NodeKind::EndFunction: return "End Function";
    }
    return "?";
}

// Header-bar color per kind, purely cosmetic.
inline SDL_Color nodeKindColor(NodeKind k) {
    switch (k) {
        case NodeKind::EventOnStart: case NodeKind::EventOnUpdate:
        case NodeKind::EventOnDraw:  case NodeKind::EventOnEnd:
            return SDL_Color{170, 60, 60, 255};
        case NodeKind::FieldDecl: case NodeKind::ScriptVarDecl: case NodeKind::IncludeDecl:
            return SDL_Color{60, 120, 150, 255};
        case NodeKind::MethodDecl:
            return SDL_Color{60, 150, 130, 255};
        case NodeKind::VarGet: case NodeKind::VarSet:
            return SDL_Color{70, 140, 90, 255};
        case NodeKind::EntityByName:
            return SDL_Color{140, 110, 60, 255};
        case NodeKind::GuardReturn: case NodeKind::IfBranch:
        case NodeKind::ForRangeEntities: case NodeKind::WhileLoop:
            return SDL_Color{130, 90, 160, 255};
        case NodeKind::FunctionCall: case NodeKind::CustomCode: case NodeKind::MethodDef:
        case NodeKind::WebLoop: case NodeKind::MainLoop:
            return SDL_Color{90, 90, 170, 255};
        case NodeKind::BinaryOp: case NodeKind::Literal:
            return SDL_Color{80, 80, 90, 255};
        case NodeKind::Comment:
            return SDL_Color{60, 60, 60, 255};
        case NodeKind::EndFunction:
            return SDL_Color{130, 90, 160, 255};
    }
    return SDL_Color{80,80,80,255};
}

struct VisualNode {
    int id = 0;
    NodeKind kind = NodeKind::CustomCode;
    float x = 60, y = 60, w = 230, h = 110;
    std::vector<Pin> pins;
    std::unordered_map<std::string, std::string> props;
    std::unordered_map<std::string, std::unique_ptr<Gui::TextArea>> textAreas;
    bool selected = false;

    // Lazily-built real engine widgets, one per editable prop key, in the
    // order they should stack vertically inside the node body.
    std::vector<std::string> widgetOrder;
    std::unordered_map<std::string, std::unique_ptr<Gui::LineEdit>> lineEdits;
    std::unordered_map<std::string, std::unique_ptr<Gui::SpinBox>>  spinBoxes;

    Pin* findPin(const std::string& name) {
        for (auto& p : pins) if (p.name == name) return &p;
        return nullptr;
    }
};

struct Connection {
    int fromNode = -1; std::string fromPin;
    int toNode   = -1; std::string toPin;
};

struct Graph {
    std::string className        = "MyScript";
    std::string contextStructName = "MyContext";
    std::vector<std::unique_ptr<VisualNode>> nodes;
    std::vector<Connection> connections;
    int nextId = 1;

    VisualNode* findNode(int id) {
        for (auto& n : nodes) if (n->id == id) return n.get();
        return nullptr;
    }
    int followExec(int nodeId, const std::string& pinName) const {
        for (auto& c : connections)
            if (c.fromNode == nodeId && c.fromPin == pinName) return c.toNode;
        return -1;
    }
    // returns {producerNode, producerPin} feeding a given input data pin, or nullptr
    const Connection* findDataFeed(int nodeId, const std::string& pinName) const {
        for (auto& c : connections)
            if (c.toNode == nodeId && c.toPin == pinName) return &c;
        return nullptr;
    }
};




// ------------------------------------------------------------------
// Code generation
// ------------------------------------------------------------------
class CodeGenerator {
public:
    explicit CodeGenerator(Graph& g) : graph(g) {}

private:
    Graph& graph;

    std::string emitBody(VisualNode* n, const std::string& indent, bool verbatimFallback = false) {
        if (!n) return "";
        bool usePins = n->props.count("usePins") && n->props["usePins"] == "1";
        if (usePins) {
            return emitChainFrom(graph.followExec(n->id, "body"), indent);
        } else {
            std::string code = n->props.count("code") ? n->props["code"] : "";
            if (verbatimFallback) return code;
            std::string body;
            splitStatements(code, indent, body);
            return body;
        }
    }

public:
    // Expression text for a node acting as a data *producer*.
    std::string exprOfNode(int nodeId, const std::string& outPin) {
        VisualNode* n = graph.findNode(nodeId);
        if (!n) return "/* missing node */";
        switch (n->kind) {
            case NodeKind::VarGet:       return n->props["name"];
            case NodeKind::EntityByName: return "findEntityByName(ctx->scene, \"" + n->props["entityName"] + "\")";
            case NodeKind::Literal:      return n->props["text"];
            case NodeKind::BinaryOp: {
                std::string a = exprFor(n->id, "a");
                std::string b = exprFor(n->id, "b");
                std::string op = n->props.count("op") ? n->props["op"] : "+";
                return "(" + a + " " + op + " " + b + ")";
            }
            case NodeKind::FunctionCall:
            case NodeKind::CustomCode:
                return n->props.count("code") ? n->props["code"] : "";
            default:
                (void)outPin;
                return "/* node kind has no expression form */";
        }
    }

    // Resolves a data-input pin: follow the wire if connected, else fall
    // back to a same-named text property on the consuming node itself
    // (lets a condition/value be typed directly with no wiring needed).
    std::string exprFor(int nodeId, const std::string& pinName) {
        const Connection* c = graph.findDataFeed(nodeId, pinName);
        if (c) return exprOfNode(c->fromNode, c->fromPin);
        VisualNode* n = graph.findNode(nodeId);
        if (n && n->props.count(pinName + "Expr")) return n->props[pinName + "Expr"];
        if (n && n->props.count(pinName)) return n->props[pinName];
        return "true";
    }

    static void splitStatements(const std::string& code, const std::string& indent, std::string& out) {
        std::stringstream ss(code);
        std::string stmt;
        while (std::getline(ss, stmt, ';')) {
            std::string t = trimStr(stmt);
            if (!t.empty()) out += indent + t + ";\n";
        }
    }

    std::string emitChainFrom(int startNodeId, const std::string& indent) {
        std::string out;
        std::set<int> visited;
        int cur = startNodeId;
        while (cur != -1 && !visited.count(cur)) {
            visited.insert(cur);
            VisualNode* n = graph.findNode(cur);
            if (!n) break;
            bool autoAdvance = true;
            switch (n->kind) {
                case NodeKind::VarSet: {
                    std::string val = exprFor(cur, "value");
                    out += indent + "    " + n->props["name"] + " = " + val + ";\n";
                    break;
                }
                case NodeKind::GuardReturn: {
                    std::string cond = exprFor(cur, "cond");
                    std::string ret = n->props.count("returnExpr") ? n->props["returnExpr"] : "";
                    out += indent + "    if (!(" + cond + ")) return" + (ret.empty() ? "" : (" " + ret)) + ";\n";
                    break;
                }
                case NodeKind::IfBranch: {
                    std::string cond = exprFor(cur, "cond");
                    out += indent + "    if (" + cond + ") {\n";
                    out += emitChainFrom(graph.followExec(cur, "true"), indent + "    ");
                    out += indent + "}\n";
                    int f = graph.followExec(cur, "false");
                    if (f != -1) {
                        out += indent + "else {\n";
                        out += emitChainFrom(f, indent + "    ");
                        out += indent + "}\n";
                    }
                    autoAdvance = false; // branches don't rejoin the main chain in this model
                    break;
                }
                case NodeKind::ForRangeEntities: {
                    std::string var = n->props.count("varName") ? n->props["varName"] : "i";
                    std::string cnt = n->props.count("countExpr") ? n->props["countExpr"] : "world.entity_count";
                    out += indent + "    for (Entity " + var + " = 0; " + var + " < " + cnt + "; ++" + var + ") {\n";
                    out += emitChainFrom(graph.followExec(cur, "body"), indent + "    ");
                    out += indent + "}\n";
                    cur = graph.followExec(cur, "after");
                    autoAdvance = false;
                    continue;
                }
                case NodeKind::WhileLoop: {
                    std::string cond = exprFor(cur, "cond");
                    out += indent + "    while (" + cond + ") {\n";
                    out += emitChainFrom(graph.followExec(cur, "body"), indent + "    ");
                    out += indent + "}\n";
                    cur = graph.followExec(cur, "after");
                    autoAdvance = false;
                    continue;
                }
                case NodeKind::FunctionCall:
                case NodeKind::CustomCode: {
                    splitStatements(n->props.count("code") ? "    " + n->props["code"] : "", indent, out);
                    break;
                }
                case NodeKind::Comment: {
                    out += indent + "    // " + n->props["text"] + "\n";
                    break;
                }
                case NodeKind::EndFunction:
                    // Terminator: stop the chain, don't advance
                    autoAdvance = false;
                    break;

                default: break;
            }
            if (autoAdvance) cur = graph.followExec(cur, "next");
        }
        return out;
    }

    int findEventNode(NodeKind k) {
        for (auto& n : graph.nodes) if (n->kind == k) return n->id;
        return -1;
    }

    std::string genHeader() {
        std::stringstream h;
        h << "#pragma once\n";
        h << "#include \"engine.h\"\n\n";
        h << "struct " << graph.contextStructName << " {\n";
        h << "    SDL_Renderer*   renderer    = nullptr;\n";
        h << "    TTF_TextEngine* textEngine  = nullptr;\n";
        h << "    SDL_Window*     window      = nullptr;\n";
        h << "    Scene*          scene       = nullptr;\n";
        h << "};\n\n";
        h << "class " << graph.className << " : public ScriptBase {\n";
        h << "public:\n";
        h << "    std::string getName() const override { return \"" << graph.className << "\"; }\n";
        h << "    " << graph.contextStructName << "* ctx = nullptr;\n";
        h << "    void onStart() override;\n";
        h << "    void onUpdate(float dt) override;\n";
        h << "    void onDraw() override;\n";
        h << "    void onEnd() override;\n";
        for (auto& n : graph.nodes) {
            if (n->kind != NodeKind::MethodDecl) continue;
            std::string retType = n->props.count("returnType") ? n->props["returnType"] : "void";
            std::string name    = n->props.count("name") ? n->props["name"] : "myMethod";
            std::string params  = n->props.count("params") ? n->props["params"] : "";
            h << "    " << retType << " " << name << "(" << params << ");\n";
        }
        h << "private:\n";
        for (auto& n : graph.nodes) {
            if (n->kind != NodeKind::FieldDecl) continue;
            std::string type = n->props.count("type") ? n->props["type"] : "float";
            std::string name = n->props.count("name") ? n->props["name"] : "field";
            std::string init = n->props.count("init") ? n->props["init"] : "";
            h << "    " << type << " " << name << (init.empty() ? "" : (" = " + init)) << ";\n";
        }
        h << "};\n";
        return h.str();
    }

    // Normalizes a user-typed header ("cmath", "<cmath>", "\"foo.h\"") down
    // to the bare name, for deduping, and reports whether it was originally
    // wrapped in <> or "" (so the wrapping typed by the user is respected).
    static std::string bareHeaderName(const std::string& typed) {
        std::string h = trimStr(typed);
        if (!h.empty() && (h.front() == '<' || h.front() == '"')) h.erase(h.begin());
        if (!h.empty() && (h.back() == '>' || h.back() == '"')) h.pop_back();
        return h;
    }


    std::string genSource(const std::string& headerName) {
        std::stringstream s;
        s << "#include \"" << headerName << "\"\n";

        // ---- Include nodes: each one is added on the next line after the
        // last include line, in graph order. A header typed without <> or ""
        // defaults to angle brackets (system-style); the Emscripten header is
        // skipped here since the automatic guard block below always supplies
        // it exactly once, so a stray Include node for it can't duplicate it.
        std::set<std::string> seenHeaders;
        for (auto& n : graph.nodes) {
            if (n->kind != NodeKind::IncludeDecl) continue;
            std::string typed = n->props.count("header") ? n->props["header"] : "";
            if (trimStr(typed).empty()) continue;
            std::string bare = bareHeaderName(typed);
            if (bare == "emscripten/emscripten.h" || seenHeaders.count(bare)) continue;
            seenHeaders.insert(bare);
            std::string t = trimStr(typed);
            if (!t.empty() && (t.front() == '<' || t.front() == '"')) s << "#include " << t << "\n";
            else s << "#include <" << t << ">\n";
        }

        // ---- Automatic Emscripten include guard: added to every script
        // this editor generates, exactly once, so a Web Loop node always has
        // what it needs without the user having to type the guard by hand.
        s << "\n#ifdef __EMSCRIPTEN__\n";
        s << "#include <emscripten/emscripten.h>\n";
        s << "#endif\n\n";

        // ---- Script Var nodes: plain file-scope variables (not class
        // members) declared right after the includes, e.g.
        // `static SDL_Renderer* g_renderer = nullptr;` in game.cpp.
        bool anyScriptVar = false;
        for (auto& n : graph.nodes) {
            if (n->kind != NodeKind::ScriptVarDecl) continue;
            std::string type = n->props.count("type") ? n->props["type"] : "float";
            std::string name = n->props.count("name") ? n->props["name"] : "myVar";
            std::string init = n->props.count("init") ? n->props["init"] : "";
            s << type << " " << name << (init.empty() ? "" : (" = " + init)) << ";\n";
            anyScriptVar = true;
        }
        if (anyScriptVar) s << "\n";

        // ---- Web Loop: holds only the *body* of main_loop_callback — the
        // "#ifdef __EMSCRIPTEN__ / void main_loop_callback() { ... } / #endif"
        // signature and tags are written here, verbatim and unindented, so
        // the person's own formatting/indentation in the node is preserved
        // exactly as typed (no automatic indentation is added).
        for (auto& n : graph.nodes) {
            if (n->kind != NodeKind::WebLoop) continue;
            std::string code = emitBody(n.get(), "", true); // <-- CHANGED (true = verbatim fallback)
            s << "#ifdef __EMSCRIPTEN__\n";
            s << "void main_loop_callback() {\n";
            s << code;
            if (!code.empty() && code.back() != '\n') s << "\n";
            s << "}\n";
            s << "#endif\n\n";
            break;
        }

        auto emitMethod = [&](const char* sig, NodeKind evt) {
            s << "void " << graph.className << "::" << sig << " {\n";
            int evNode = findEventNode(evt);
            if (evNode != -1) s << emitChainFrom(graph.followExec(evNode, "next"), "    ");
            s << "}\n\n";
        };
        emitMethod("onStart()",        NodeKind::EventOnStart);
        emitMethod("onUpdate(float dt)", NodeKind::EventOnUpdate);
        emitMethod("onDraw()",         NodeKind::EventOnDraw);
        emitMethod("onEnd()",          NodeKind::EventOnEnd);

        // User-declared methods: each Method Def node is a standalone,
        // self-contained body (like Custom Code) rather than a chain to
        // walk, so it's emitted directly instead of via emitChainFrom.
        // Its signature is pulled from the Method Decl node sharing its
        // "name" — if none is found, falls back to void/no-args and a
        // warning comment so the mismatch is visible in the output.
        for (auto& n : graph.nodes) {
            if (n->kind != NodeKind::MethodDef) continue;
            std::string name = n->props.count("name") ? n->props["name"] : "myMethod";
            std::string retType = "void";
            std::string params  = "";
            bool foundDecl = false;
            for (auto& d : graph.nodes) {
                if (d->kind == NodeKind::MethodDecl && d->props.count("name") && d->props["name"] == name) {
                    retType = d->props.count("returnType") ? d->props["returnType"] : "void";
                    params  = d->props.count("params") ? d->props["params"] : "";
                    foundDecl = true;
                    break;
                }
            }
            if (!foundDecl) {
                s << "// warning: no matching Method Decl node found for \"" << name << "\"; defaulting to void " << name << "()\n";
            }
            s << retType << " " << graph.className << "::" << name << "(" << params << ") {\n";
            s << emitBody(n.get(), "    "); // <-- CHANGED
            s << "}\n\n";
        }

        // ---- Main Loop: holds only the *body* of int main(argc, argv) —
        // the signature and braces are written here; the node's own text is
        // inserted verbatim and unindented (no automatic "return 0;" is
        // added — write it yourself inside the node if this script is a
        // standalone entry point).
        for (auto& n : graph.nodes) {
            if (n->kind != NodeKind::MainLoop) continue;
            std::string code = emitBody(n.get(), "", true); // <-- CHANGED (true = verbatim fallback)
            s << "int main(int argc, char* argv[]) {\n";
            s << code;
            if (!code.empty() && code.back() != '\n') s << "\n";
            s << "}\n\n";
            break;
        }
        return s.str();
    }
};

// ------------------------------------------------------------------
// JSON round-trip (embedded inside the generated .cpp so the .cpp/.h
// pair remain the only two files on disk)
// ------------------------------------------------------------------
inline nlohmann::json pinToJson(const Pin& p) {
    return { {"name", p.name}, {"kind", p.kind == PinKind::Exec ? "exec" : "data"},
             {"dir", p.dir == PinDir::Input ? "in" : "out"}, {"dataType", p.dataType} };
}
inline Pin pinFromJson(const nlohmann::json& j) {
    Pin p;
    p.name = j.value("name", "");
    p.kind = j.value("kind", "data") == "exec" ? PinKind::Exec : PinKind::Data;
    p.dir  = j.value("dir", "in") == "out" ? PinDir::Output : PinDir::Input;
    p.dataType = j.value("dataType", "float");
    return p;
}
inline int nodeKindToInt(NodeKind k) { return (int)k; }
inline NodeKind nodeKindFromInt(int i) { return (NodeKind)i; }

inline std::string graphToJsonString(Graph& g) {
    nlohmann::json j;
    j["className"] = g.className;
    j["contextStructName"] = g.contextStructName;
    j["nextId"] = g.nextId;
    nlohmann::json nodesJ = nlohmann::json::array();
    for (auto& n : g.nodes) {
        nlohmann::json nj;
        nj["id"] = n->id;
        nj["kind"] = nodeKindToInt(n->kind);
        nj["x"] = n->x; nj["y"] = n->y; nj["w"] = n->w; nj["h"] = n->h;
        nj["props"] = n->props;
        nlohmann::json pinsJ = nlohmann::json::array();
        for (auto& p : n->pins) pinsJ.push_back(pinToJson(p));
        nj["pins"] = pinsJ;
        nodesJ.push_back(nj);
    }
    j["nodes"] = nodesJ;
    nlohmann::json connJ = nlohmann::json::array();
    for (auto& c : g.connections)
        connJ.push_back({ {"fromNode", c.fromNode}, {"fromPin", c.fromPin},
                           {"toNode", c.toNode}, {"toPin", c.toPin} });
    j["connections"] = connJ;
    return j.dump();
}

inline std::vector<std::string> widgetOrderForKind(NodeKind kind) {
    switch (kind) {
        case NodeKind::FieldDecl:         return {"type", "name", "init"};
        case NodeKind::ScriptVarDecl:     return {"type", "name", "init"};
        case NodeKind::IncludeDecl:       return {"header"};
        case NodeKind::WebLoop:           return {"code"};
        case NodeKind::MainLoop:          return {"code"};
        case NodeKind::VarGet:            return {"name"};
        case NodeKind::VarSet:            return {"name"};
        case NodeKind::EntityByName:      return {"entityName"};
        case NodeKind::GuardReturn:       return {"condExpr", "returnExpr"};
        case NodeKind::IfBranch:          return {"condExpr"};
        case NodeKind::ForRangeEntities:  return {"varName", "countExpr"};
        case NodeKind::WhileLoop:         return {"condExpr"};
        case NodeKind::FunctionCall:      return {"code"};
        case NodeKind::BinaryOp:          return {"op"};
        case NodeKind::Literal:           return {"text"};
        case NodeKind::Comment:           return {"text"};
        case NodeKind::CustomCode:        return {"code"};
        case NodeKind::MethodDecl:        return {"returnType", "name", "params"};
        case NodeKind::MethodDef:         return {"name", "code"};
        case NodeKind::EndFunction:       return {}; 
        default:                          return {};
    }
}


// Forward declare so graphFromJsonString can attach widgets lazily on demand.
inline void graphFromJsonString(Graph& g, const std::string& jsonStr) {
    g.nodes.clear();
    g.connections.clear();
    nlohmann::json j = nlohmann::json::parse(jsonStr, nullptr, false);
    if (j.is_discarded()) return;
    g.className = j.value("className", "MyScript");
    g.contextStructName = j.value("contextStructName", "MyContext");
    g.nextId = j.value("nextId", 1);
    for (auto& nj : j.value("nodes", nlohmann::json::array())) {
        auto n = std::make_unique<VisualNode>();
        n->id = nj.value("id", 0);
        n->kind = nodeKindFromInt(nj.value("kind", (int)NodeKind::CustomCode));
        n->widgetOrder = widgetOrderForKind(n->kind);
        n->x = nj.value("x", 60.0f); n->y = nj.value("y", 60.0f);
        n->w = nj.value("w", 230.0f); n->h = nj.value("h", 110.0f);
        if (nj.contains("props"))
            for (auto it = nj["props"].begin(); it != nj["props"].end(); ++it)
                n->props[it.key()] = it.value().get<std::string>();
        for (auto& pj : nj.value("pins", nlohmann::json::array()))
            n->pins.push_back(pinFromJson(pj));
        if ((n->kind == NodeKind::CustomCode || n->kind == NodeKind::MethodDef) && n->props.count("code")) {
            n->props["code"] = formatCodeWithNewlines(n->props["code"]);
        }
        g.nodes.push_back(std::move(n));
    }
    for (auto& cj : j.value("connections", nlohmann::json::array())) {
        Connection c;
        c.fromNode = cj.value("fromNode", -1);
        c.fromPin  = cj.value("fromPin", "");
        c.toNode   = cj.value("toNode", -1);
        c.toPin    = cj.value("toPin", "");
        g.connections.push_back(c);
    }
}

const std::string kGraphBeginMarker = "/* === VISUALLOGIC_GRAPH_BEGIN ===";
const std::string kGraphEndMarker   = "=== VISUALLOGIC_GRAPH_END === */";

// ------------------------------------------------------------------
// Node factory — sets up pins/props/widget order per kind.
// ------------------------------------------------------------------
inline std::unique_ptr<VisualNode> makeNode(Graph& g, NodeKind kind, float x, float y) {
    auto n = std::make_unique<VisualNode>();
    n->id = g.nextId++;
    n->kind = kind;
    n->x = x; n->y = y;

    auto exec = [&](const char* nm, PinDir d) { n->pins.push_back({nm, PinKind::Exec, d, "exec"}); };
    auto data = [&](const char* nm, PinDir d, const char* ty = "float") { n->pins.push_back({nm, PinKind::Data, d, ty}); };

    auto addBodyPin = [&]() { exec("body", PinDir::Output); };

    switch (kind) {
        case NodeKind::EventOnStart:  n->w=180; n->h=60; exec("next", PinDir::Output); break;
        case NodeKind::EventOnUpdate: n->w=180; n->h=60; exec("next", PinDir::Output); break;
        case NodeKind::EventOnDraw:   n->w=180; n->h=60; exec("next", PinDir::Output); break;
        case NodeKind::EventOnEnd:    n->w=180; n->h=60; exec("next", PinDir::Output); break;

        case NodeKind::FieldDecl:
            n->props["type"] = "float"; n->props["name"] = "myField"; n->props["init"] = "0.0f";
            n->w = 230; n->h = 150;
            break;

        case NodeKind::ScriptVarDecl:
            // Same shape as Field, but emitted as a plain file-scope variable
            // right after the includes in the .cpp, not as a class member —
            // e.g. `static SDL_Renderer* g_renderer = nullptr;` in game.cpp.
            n->props["type"] = "static float"; n->props["name"] = "myVar"; n->props["init"] = "0.0f";
            n->w = 250; n->h = 150;
            break;

        case NodeKind::IncludeDecl:
            // No pins — like Field/Comment, this just contributes a line to
            // the generated .cpp. Type a bare name ("cmath") for <cmath>, or
            // include your own <>/"" to control the style exactly.
            n->props["header"] = "cmath";
            n->w = 230; n->h = 95;
            break;

        case NodeKind::VarGet:
            n->props["name"] = "score1";
            data("value", PinDir::Output);
            n->w = 200; n->h = 95;
            break;

        case NodeKind::VarSet:
            n->props["name"] = "score1";
            exec("in", PinDir::Input); exec("next", PinDir::Output);
            data("value", PinDir::Input);
            n->w = 210; n->h = 115;
            break;

        case NodeKind::EntityByName:
            n->props["entityName"] = "Ball";
            data("entity", PinDir::Output, "Entity");
            n->w = 210; n->h = 95;
            break;

        case NodeKind::GuardReturn:
            n->props["condExpr"] = "ctx && ctx->scene";
            n->props["returnExpr"] = "";
            exec("in", PinDir::Input); exec("next", PinDir::Output);
            data("cond", PinDir::Input, "bool");
            n->w = 260; n->h = 135;
            break;

        case NodeKind::IfBranch:
            n->props["condExpr"] = "true";
            exec("in", PinDir::Input); exec("true", PinDir::Output); exec("false", PinDir::Output);
            data("cond", PinDir::Input, "bool");
            n->w = 230; n->h = 120;
            break;

        case NodeKind::ForRangeEntities:
            n->props["varName"] = "i"; n->props["countExpr"] = "world.entity_count";
            exec("in", PinDir::Input); exec("body", PinDir::Output); exec("after", PinDir::Output);
            n->w = 250; n->h = 140;
            break;

        case NodeKind::WhileLoop:
            n->props["condExpr"] = "true";
            exec("in", PinDir::Input); exec("body", PinDir::Output); exec("after", PinDir::Output);
            data("cond", PinDir::Input, "bool");
            n->w = 230; n->h = 120;
            break;

        case NodeKind::FunctionCall:
            n->props["code"] = "SDL_Log(\"hello\")";
            exec("in", PinDir::Input); exec("next", PinDir::Output);
            data("result", PinDir::Output, "auto");
            n->w = 260; n->h = 120;
            break;

        case NodeKind::BinaryOp:
            n->props["op"] = "+";
            data("a", PinDir::Input); data("b", PinDir::Input); data("result", PinDir::Output);
            n->w = 200; n->h = 130;
            break;

        case NodeKind::Literal:
            n->props["text"] = "0";
            n->props["numeric"] = "1"; // drives a real Gui::SpinBox for this node's value
            data("value", PinDir::Output);
            n->w = 190; n->h = 95;
            break;

        case NodeKind::Comment:
            n->props["text"] = "note...";
            n->w = 220; n->h = 80;
            break;

        case NodeKind::CustomCode:
            n->props["code"] = formatCodeWithNewlines("// raw statement(s), separated by ';'");
            exec("in", PinDir::Input); exec("next", PinDir::Output);
            n->w = 280; n->h = 450;
            break;

        case NodeKind::MethodDecl:
            // Declares a method's signature only — like Field, this contributes
            // to the generated header (a public member function declaration)
            // and has no exec/data pins of its own; nothing wires into or out
            // of it. Pair it with a Method Def node of the same "name" to also
            // emit the implementation.
            n->props["returnType"] = "void";
            n->props["name"] = "myMethod";
            n->props["params"] = "";
            n->w = 260; n->h = 165;
            break;

        case NodeKind::MethodDef:
            // Defines a method's body verbatim, the same way Custom Code holds
            // raw statements — but as a standalone top-level definition (like
            // On Start/On Update/etc.) rather than a mid-chain step. "name"
            // must match a Method Decl node's "name" so codegen can pull the
            // matching returnType/params for the ClassName::name(...) line.
            n->props["name"] = "myMethod";
            n->props["code"] = formatCodeWithNewlines("// method body statement(s), separated by ';'");
            n->props["usePins"] = "0";  // default: textarea mode
            n->w = 300; n->h = 400;
            break;

        case NodeKind::WebLoop:
            // Body-only: no signature/braces to write. This is inserted
            // verbatim (no auto-formatting, no forced indentation) inside
            // "#ifdef __EMSCRIPTEN__ / void main_loop_callback() { ... } / #endif".
            n->props["code"] = "// main_loop_callback body - write statements exactly as you want them to appear, this text is not reformatted or indented for you";
            n->props["usePins"] = "0";
            n->w = 340; n->h = 380;
            break;

        case NodeKind::MainLoop:
            // Body-only: no signature/braces, and no automatic "return 0;" -
            // add your own return statement inside this node if needed.
            n->props["code"] = "// int main(argc, argv) body - write statements exactly as you want them to appear; remember to write your own return statement";
            n->props["usePins"] = "0";
            n->w = 340; n->h = 380;
            break;
    }
    n->widgetOrder = widgetOrderForKind(kind);
    return n;
}

// ------------------------------------------------------------------
// The editor widget itself
// ------------------------------------------------------------------
class VisualScriptEditor : public Gui::IGuiElement {
public:
    VisualScriptEditor(SDL_Renderer* r, TTF_TextEngine* te, TTF_Font* f, SDL_Window* win, SDL_FRect rect)
        : renderer(r), textEngine(te), font(f), window(win), rect(rect) {
        buildPalette();
    }

    std::function<void()> onClose;


    Gui::TextArea* m_activeTextArea = nullptr;


    void deactivateAllTextAreasExcept(Gui::TextArea* keep) {
        for (auto& node : graph.nodes) {
            for (auto& kv : node->textAreas) {
                if (kv.second.get() != keep && kv.second->isActive()) {
                    kv.second->setActive(false);
                    SDL_StopTextInput(window); // optional, but safe
                }
            }
        }
    }

    // ---- IGuiElement ----
    std::string getType() const override { return "VisualScriptEditor"; }
    float getX() const override { return rect.x; }
    float getY() const override { return rect.y; }
    float getWidth() const override { return rect.w; }
    float getHeight() const override { return rect.h; }
    void setPos(SDL_Point p) override { rect.x = (float)p.x; rect.y = (float)p.y; }
    void setRect(SDL_FRect r) override { rect = r; }

    void setVisible(bool v) { visible = v; }
    bool isVisible() const { return visible; }
    bool isFileLoaded() const { return !cppFilePath.empty(); }
    const std::string& getFilePath() const { return cppFilePath; }

    // ---- file I/O ----
    void newGraph() {
        graph = Graph{};
        graph.className = "GameScript";
        graph.contextStructName = "GameContext";
        cppFilePath.clear(); headerFilePath.clear();
        selectedNodeId = -1;
        zoom = 1.0f;
        panX = panY = 0;
        syncRenameFields();
    }

    void loadFromFile(const std::string& cppPath) {
        cppFilePath = cppPath;
        std::filesystem::path p(cppPath);
        headerFilePath = (p.parent_path() / (p.stem().string() + ".h")).string();

        std::ifstream in(cppPath);
        if (!in.is_open()) { newGraph(); return; }
        std::stringstream ss; ss << in.rdbuf();
        std::string text = ss.str();

        size_t b = text.find(kGraphBeginMarker);
        size_t e = text.find(kGraphEndMarker);
        if (b != std::string::npos && e != std::string::npos && e > b) {
            std::string b64 = text.substr(b + kGraphBeginMarker.size(),
                                           e - (b + kGraphBeginMarker.size()));
            b64 = trimStr(b64);
            std::string json = base64Decode(b64);
            graph = Graph{};
            graphFromJsonString(graph, json);
        } else {
            // Not authored by this tool — start clean with an explanatory note
            // rather than pretending to parse arbitrary C++.
            newGraph(); // NOTE: this clears cppFilePath/headerFilePath, so...
            cppFilePath = cppPath; headerFilePath = (p.parent_path() / (p.stem().string() + ".h")).string(); // ...restore them: the user still opened *this* file
            auto note = makeNode(graph, NodeKind::Comment, 60, 60);
            note->props["text"] = "This file has no Visual Editor graph embedded. "
                                   "Use the Text Editor to view its existing code; "
                                   "building here will start a brand-new script.";
            graph.nodes.push_back(std::move(note));
        }
        selectedNodeId = -1;
        zoom = 1.0f;
        panX = panY = 0;
        syncRenameFields();
    }

    // ---- Context/Script renaming ----
    // Renames the context struct or script class in the in-memory graph
    // AND, if this graph is tied to a file on disk, everywhere else it is
    // used across the project (other .h/.cpp files that reference the old
    // struct/class name), then re-saves the currently open pair so it picks
    // up the new name too. See renameIdentifierUnderRoot() near the top of
    // this file for how the project root is found and how the rename is
    // applied (a word-boundary text substitution, not a C++ parse).
    void renameContextStruct(const std::string& typedName) {
        std::string newName = trimStr(typedName);
        if (newName.empty() || newName == graph.contextStructName) return;
        std::string oldName = graph.contextStructName;
        renameAcrossProject(oldName, newName);
        graph.contextStructName = newName;
        if (!cppFilePath.empty()) saveFile();
        syncRenameFields();
    }

    void renameScriptClass(const std::string& typedName) {
        std::string newName = trimStr(typedName);
        if (newName.empty() || newName == graph.className) return;
        std::string oldName = graph.className;
        renameAcrossProject(oldName, newName);
        graph.className = newName;
        if (!cppFilePath.empty()) saveFile();
        syncRenameFields();
    }

    void saveFile() {
        if (cppFilePath.empty()) return;
        CodeGenerator gen(graph);
        std::filesystem::path p(cppFilePath);
        std::string headerName = p.stem().string() + ".h";
        headerFilePath = (p.parent_path() / headerName).string();

        std::ofstream hOut(headerFilePath);
        hOut << gen.genHeader();
        hOut.close();

        std::string src = gen.genSource(headerName);
        std::string json = graphToJsonString(graph);
        std::string b64 = base64Encode(json);

        std::ofstream cOut(cppFilePath);
        cOut << src << "\n" << kGraphBeginMarker << "\n" << b64 << "\n" << kGraphEndMarker << "\n";
        cOut.close();

        SDL_Log("[VisualScriptEditor] Saved %s and %s", headerFilePath.c_str(), cppFilePath.c_str());
    }

    void setSaveTargetIfEmpty(const std::string& cppPath) {
        if (cppFilePath.empty()) cppFilePath = cppPath;
    }

    void note(const std::string& text, float x, float y) {
        auto n = makeNode(graph, NodeKind::Comment, x, y);
        n->w = 420; n->h = 100;
        n->props["text"] = text;
        graph.nodes.push_back(std::move(n));
    }

    // ---- rendering ----
    void render(float, float) override {
        if (!visible) return;
        SDL_SetRenderDrawColor(renderer, 24, 24, 30, 255);
        SDL_RenderFillRect(renderer, &rect);

        float paletteW = 170.0f;
        SDL_FRect paletteRect = { rect.x, rect.y, paletteW, rect.h };
        paletteContainer->setRect(paletteRect);
        
        // Update the inner box's X/Y to match the container so children don't draw at y=0
        // and overlap the main toolbar. Preserve paletteTotalH so scrolling still works.
        if (paletteBox) {
            SDL_FRect boxRect = { rect.x, rect.y, paletteW - 12.0f, paletteTotalH };
            paletteBox->setRect(boxRect);
        }
        
        paletteContainer->render(0.0f, 0.0f);

        canvasRect = { rect.x + paletteW, rect.y, rect.w - paletteW, rect.h };
        SDL_SetRenderDrawColor(renderer, 20, 20, 26, 255);
        SDL_RenderFillRect(renderer, &canvasRect);
        SDL_Rect clip = { (int)canvasRect.x, (int)canvasRect.y, (int)canvasRect.w, (int)canvasRect.h };
        SDL_SetRenderClipRect(renderer, &clip);

        // faint grid (zoom-independent spacing in graph space)
        float gridStep = 40.0f;
        float zoomedGrid = gridStep * zoom;
        for (float gx = fmodf(-panX * zoom, zoomedGrid); gx < canvasRect.w; gx += zoomedGrid)
            SDL_RenderLine(renderer, canvasRect.x + gx, canvasRect.y, canvasRect.x + gx, canvasRect.y + canvasRect.h);
        for (float gy = fmodf(-panY * zoom, zoomedGrid); gy < canvasRect.h; gy += zoomedGrid)
            SDL_RenderLine(renderer, canvasRect.x, canvasRect.y + gy, canvasRect.x + canvasRect.w, canvasRect.y + gy);

        // connections
        for (auto& c : graph.connections) drawConnection(c);
        if (draggingPinFromNode != -1) {
            float sx, sy; pinScreenPos(draggingPinFromNode, draggingPinName, draggingPinIsOutput, sx, sy);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
            SDL_RenderLine(renderer, sx, sy, mouseX, mouseY);
        }

        for (auto& n : graph.nodes) drawNode(*n);

        SDL_SetRenderClipRect(renderer, nullptr);

        // // footer status bar
        // SDL_FRect footer = { rect.x, rect.y + rect.h - 26, rect.w, 26 };
        // SDL_SetRenderDrawColor(renderer, 32, 32, 40, 255);
        // SDL_RenderFillRect(renderer, &footer);
        // std::string status = "Class: " + graph.className + "   File: " +
        //     (cppFilePath.empty() ? "(unsaved)" : cppFilePath) +
        //     "   Zoom: " + std::to_string((int)(zoom * 100)) + "%  [Ctrl+Wheel]=zoom  [Del]=delete selected  [MMB drag]=pan  [drag pin->pin]=connect  [RMB on wire]=disconnect";
        // TTF_Text* t = TTF_CreateText(textEngine, font, status.c_str(), 0);
        // if (t) { TTF_SetTextColor(t, 170, 170, 190, 255); TTF_DrawRendererText(t, footer.x + 8, footer.y + 4); TTF_DestroyText(t); }
    }

    // ---- events ----
    bool handleEvent(const SDL_Event& e, SDL_Window* win, float, float) override {
        if (!visible) return false;

        if (paletteContainer->handleEvent(e, win, 0, 0)) return true;

        if (e.type == SDL_EVENT_MOUSE_MOTION) { mouseX = e.motion.x; mouseY = e.motion.y; }

        // --- Zoom with Ctrl+Wheel ---
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            SDL_Keymod mod = SDL_GetModState();
            if (mod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL)) {
                float factor = (e.wheel.y > 0) ? 1.1f : (1.0f / 1.1f);
                float oldZoom = zoom;
                float newZoom = std::clamp(zoom * factor, ZOOM_MIN, ZOOM_MAX);
                if (newZoom == zoom) return true;
                // Keep world point under mouse fixed
                float mx = e.wheel.mouse_x, my = e.wheel.mouse_y;
                float worldX = (mx - canvasRect.x) / oldZoom + panX;
                float worldY = (my - canvasRect.y) / oldZoom + panY;
                zoom = newZoom;
                panX = worldX - (mx - canvasRect.x) / zoom;
                panY = worldY - (my - canvasRect.y) / zoom;
                return true;
            }
        }
        // Note: only SDLK_DELETE (not Backspace) triggers node deletion, since
        // Backspace is consumed by an active node LineEdit while editing text.
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_DELETE) {
            if (selectedNodeId != -1) { deleteNode(selectedNodeId); return true; }
        }

        if (!inCanvas(mouseX, mouseY) && e.type != SDL_EVENT_MOUSE_BUTTON_UP && e.type != SDL_EVENT_MOUSE_MOTION)
            return false;

        Gui::TextArea* newlyActive = nullptr;

        // Let node property widgets (LineEdit/SpinBox) try first, topmost node first.
        for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it) {
            VisualNode& n = **it;
            for (auto& key : n.widgetOrder) {
                if (n.lineEdits.count(key) && n.lineEdits[key]->handleEvent(e, win, 0.0f, 0.0f)) {
                    n.props[key] = n.lineEdits[key]->getText();
                    return true;
                }
                if (n.spinBoxes.count(key) && n.spinBoxes[key]->handleEvent(e, win, 0.0f, 0.0f)) {
                    std::stringstream ss; ss << n.spinBoxes[key]->getValue();
                    n.props[key] = ss.str();
                    return true;
                }
                if (n.textAreas.count(key) && n.textAreas[key]->handleEvent(e, win, 0.0f, 0.0f)) {
                    n.props[key] = n.textAreas[key]->getText();
                    if (n.textAreas[key]->isActive()) {
                        newlyActive = n.textAreas[key].get();
                    }
                    return true; // still return, but we need to deactivate others after
                }
            }
        }

        if (newlyActive) {
            deactivateAllTextAreasExcept(newlyActive);
            m_activeTextArea = newlyActive;
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            // If we clicked somewhere that wasn't a TextArea, deactivate all
            if (m_activeTextArea) {
                m_activeTextArea->setActive(false);
                SDL_StopTextInput(window);
                m_activeTextArea = nullptr;
            }
        }

        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            float mx = e.button.x, my = e.button.y;
            if (e.button.button == SDL_BUTTON_MIDDLE) { panning = true; panStartX = mx; panStartY = my; panOrigX = panX; panOrigY = panY; return true; }

            // pin hit test (topmost first)
            for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it) {
                VisualNode& n = **it;
                for (auto& p : n.pins) {
                    float px, py; pinScreenPos(n.id, p.name, p.dir == PinDir::Output, px, py);
                    if (dist(mx, my, px, py) < 8.0f) {
                        if (e.button.button == SDL_BUTTON_LEFT) {
                            draggingPinFromNode = n.id;
                            draggingPinName = p.name;
                            draggingPinIsOutput = (p.dir == PinDir::Output);
                            draggingPinKind = p.kind;
                            return true;
                        }
                    }
                }
            }
            // resize handle hit test (topmost first) — only nodes with a
            // TextArea (Custom Code / Method Def) expose one, in their
            // bottom-right corner; grabbing it resizes instead of moving.
            if (e.button.button == SDL_BUTTON_LEFT) {
                for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it) {
                    VisualNode& n = **it;
                    if (n.kind != NodeKind::CustomCode && n.kind != NodeKind::MethodDef &&
                        n.kind != NodeKind::WebLoop && n.kind != NodeKind::MainLoop) continue;
                    SDL_FRect sr = screenRect(n);
                    SDL_FRect handle = { sr.x + sr.w - 14.0f, sr.y + sr.h - 14.0f, 14.0f, 14.0f };
                    if (inRect(mx, my, handle)) {
                        selectNode(n.id);
                        resizingNodeId = n.id;
                        resizeStartMouseX = mx; resizeStartMouseY = my;
                        resizeStartW = n.w; resizeStartH = n.h;
                        return true;
                    }
                }
            }
            // right click near a connection: delete it
            if (e.button.button == SDL_BUTTON_RIGHT) {
                for (size_t i = 0; i < graph.connections.size(); ++i) {
                    if (hitTestConnection(graph.connections[i], mx, my)) {
                        graph.connections.erase(graph.connections.begin() + i);
                        return true;
                    }
                }
            }
            // node header drag / select
            if (e.button.button == SDL_BUTTON_LEFT) {
                for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it) {
                    VisualNode& n = **it;
                    SDL_FRect sr = screenRect(n);
                    SDL_FRect header = { sr.x, sr.y, sr.w, 22 };
                    // --- Checkbox toggle for special body nodes ---
                    bool isSpecialBody = (n.kind == NodeKind::MethodDef || 
                                        n.kind == NodeKind::WebLoop || 
                                        n.kind == NodeKind::MainLoop);
                    if (isSpecialBody && inRect(mx, my, header)) {
                        float cbSize = 14.0f * std::max(0.7f, zoom);
                        float cbX = sr.x + sr.w - cbSize - 6.0f;
                        float cbY = sr.y + 4.0f;
                        SDL_FRect cbRect = {cbX, cbY, cbSize, cbSize};
                        if (inRect(mx, my, cbRect)) {
                            // Toggle usePins
                            bool cur = n.props.count("usePins") && n.props["usePins"] == "1";
                            n.props["usePins"] = cur ? "0" : "1";
                            // Clear widgets so they rebuild in new mode
                            n.textAreas.clear();
                            n.lineEdits.clear();
                            n.spinBoxes.clear();
                            selectNode(n.id);
                            return true;  // consume click, don't start drag
                        }
                    }
                    if (inRect(mx, my, sr)) {
                        selectNode(n.id);
                        if (inRect(mx, my, header)) {
                            draggingNodeId = n.id;
                            dragOffX = mx - sr.x; dragOffY = my - sr.y;
                        }
                        return true;
                    }
                }
                selectNode(-1);
            }
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            if (panning) { panX = panOrigX + (e.motion.x - panStartX) / zoom; panY = panOrigY + (e.motion.y - panStartY) / zoom; return true; }
            if (resizingNodeId != -1) {
                VisualNode* n = graph.findNode(resizingNodeId);
                if (n) {
                    float dx = (e.motion.x - resizeStartMouseX) / zoom;
                    float dy = (e.motion.y - resizeStartMouseY) / zoom;
                    n->w = std::max(160.0f, resizeStartW + dx);
                    n->h = std::max(100.0f, resizeStartH + dy);
                }
                return true;
            }
            if (draggingNodeId != -1) {
                VisualNode* n = graph.findNode(draggingNodeId);
                if (n) {
                    float graphX = (e.motion.x - canvasRect.x) / zoom + panX;
                    float graphY = (e.motion.y - canvasRect.y) / zoom + panY;
                    n->x = graphX - dragOffX / zoom;
                    n->y = graphY - dragOffY / zoom;
                }
                return true;
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (e.button.button == SDL_BUTTON_MIDDLE) panning = false;
            draggingNodeId = -1;
            resizingNodeId = -1;
            if (draggingPinFromNode != -1) {
                float mx = e.button.x, my = e.button.y;
                for (auto& n : graph.nodes) {
                    for (auto& p : n->pins) {
                        if (p.kind != draggingPinKind) continue;
                        if ((p.dir == PinDir::Output) == draggingPinIsOutput) continue; // need opposite direction
                        float px, py; pinScreenPos(n->id, p.name, p.dir == PinDir::Output, px, py);
                        if (dist(mx, my, px, py) < 10.0f) {
                            Connection c;
                            if (draggingPinIsOutput) { c.fromNode = draggingPinFromNode; c.fromPin = draggingPinName; c.toNode = n->id; c.toPin = p.name; }
                            else { c.fromNode = n->id; c.fromPin = p.name; c.toNode = draggingPinFromNode; c.toPin = draggingPinName; }
                            // an input can only have one incoming data/exec wire
                            for (size_t i = 0; i < graph.connections.size(); ++i)
                                if (graph.connections[i].toNode == c.toNode && graph.connections[i].toPin == c.toPin)
                                    { graph.connections.erase(graph.connections.begin() + i); break; }
                            graph.connections.push_back(c);
                        }
                    }
                }
                draggingPinFromNode = -1;
            }
        }
        return false;
    }

    void handleGamepad(float cursorX, float cursorY, float, float, SDL_Window* win, bool confirmDown, bool confirmDownLastFrame) override {
        if (!visible) return;
        paletteContainer->handleGamepad(cursorX, cursorY, 0, 0, win, confirmDown, confirmDownLastFrame);
    }

private:
    SDL_Renderer* renderer; TTF_TextEngine* textEngine; TTF_Font* font; SDL_Window* window;
    SDL_FRect rect;
    SDL_FRect canvasRect{};
    bool visible = true;

    Graph graph;
    std::string cppFilePath, headerFilePath;

    float zoom = 1.0f;
    static constexpr float ZOOM_MIN = 0.5f, ZOOM_MAX = 2.0f;

    float panX = 0, panY = 0;
    bool panning = false; float panStartX=0, panStartY=0, panOrigX=0, panOrigY=0;

    int draggingNodeId = -1; float dragOffX=0, dragOffY=0;
    int resizingNodeId = -1; float resizeStartMouseX=0, resizeStartMouseY=0, resizeStartW=0, resizeStartH=0;
    int draggingPinFromNode = -1; std::string draggingPinName; bool draggingPinIsOutput = false; PinKind draggingPinKind = PinKind::Data;
    int selectedNodeId = -1;
    float mouseX = 0, mouseY = 0;

    std::unique_ptr<Gui::ScrollableContainer> paletteContainer;
    Gui::VBoxContainer* paletteBox = nullptr;
    float paletteTotalH = 0.0f;
    Gui::LineEdit* contextNameEditPtr = nullptr; // owned by palette; used to read/refresh the "Context name" field
    Gui::LineEdit* classNameEditPtr   = nullptr; // owned by palette; used to read/refresh the "Script class name" field

    void syncRenameFields() {
        if (contextNameEditPtr) contextNameEditPtr->setText(graph.contextStructName);
        if (classNameEditPtr)   classNameEditPtr->setText(graph.className);
    }

    // Finds the project root by walking up from the currently open .cpp
    // looking for CMakeLists.txt (every project this engine creates has one
    // at its root — see createNewConfig() in engine.h), then rewrites every
    // .h/.hpp/.cpp file under it. Falls back to just the open file's folder
    // if no CMakeLists.txt is found (e.g. a lone script outside a project),
    // so the rename still applies to at least that file's neighbors. If no
    // file has been saved yet, only the in-memory graph name is changed.
    inline void renameAcrossProject(const std::string& oldName, const std::string& newName) {
        if (cppFilePath.empty()) return;
        std::filesystem::path start = std::filesystem::path(cppFilePath).parent_path();
        std::filesystem::path root = start;
        std::filesystem::path search = start;
        
        // Search up to 4 levels (was 8) - enough for typical project structure
        for (int depth = 0; depth < 4; ++depth) {
            if (std::filesystem::exists(search / "CMakeLists.txt")) { 
                root = search; 
                break; 
            }
            std::filesystem::path parent = search.parent_path();
            if (parent == search) break; // reached filesystem root
            search = parent;
        }
        
        renameIdentifierUnderRoot(root, oldName, newName);
        SDL_Log("[VisualScriptEditor] Renamed '%s' -> '%s' under %s", oldName.c_str(), newName.c_str(), root.string().c_str());
    }


    bool inCanvas(float x, float y) const { return inRect(x, y, canvasRect); }
    static bool inRect(float x, float y, SDL_FRect r) { return x>=r.x && x<=r.x+r.w && y>=r.y && y<=r.y+r.h; }
    static float dist(float x1,float y1,float x2,float y2){ float dx=x1-x2, dy=y1-y2; return std::sqrt(dx*dx+dy*dy); }

    void selectNode(int id) {
        for (auto& n : graph.nodes) n->selected = (n->id == id);
        selectedNodeId = id;
    }

    void deleteNode(int id) {
        graph.connections.erase(std::remove_if(graph.connections.begin(), graph.connections.end(),
            [&](const Connection& c) { return c.fromNode == id || c.toNode == id; }), graph.connections.end());
        graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(),
            [&](const std::unique_ptr<VisualNode>& n) { return n->id == id; }), graph.nodes.end());
        selectedNodeId = -1;
    }

    SDL_FRect screenRect(const VisualNode& n) const {
        return { canvasRect.x + (n.x - panX) * zoom, canvasRect.y + (n.y - panY) * zoom, n.w * zoom, n.h * zoom };
    }

    void pinScreenPos(int nodeId, const std::string& pinName, bool isOutput, float& outX, float& outY) {
        VisualNode* n = graph.findNode(nodeId);
        if (!n) { outX = outY = 0; return; }
        SDL_FRect sr = screenRect(*n);
        std::vector<Pin*> side;
        for (auto& p : n->pins) if ((p.dir == PinDir::Output) == isOutput) side.push_back(&p);
        int idx = 0;
        for (size_t i = 0; i < side.size(); ++i) if (side[i]->name == pinName) { idx = (int)i; break; }
        float spacing = 20.0f * zoom;
        float yy = sr.y + 34.0f + idx * spacing;
        outX = isOutput ? sr.x + sr.w - 2.0f : sr.x + 2.0f;
        outY = yy;
    }

    bool hitTestConnection(const Connection& c, float mx, float my) {
        float x1,y1,x2,y2;
        pinScreenPos(c.fromNode, c.fromPin, true, x1, y1);
        pinScreenPos(c.toNode, c.toPin, false, x2, y2);
        float midX = (x1 + x2) * 0.5f;
        auto segDist = [&](float ax,float ay,float bx,float by){
            float dx=bx-ax, dy=by-ay; float len2 = dx*dx+dy*dy;
            float t = len2 > 0 ? std::clamp(((mx-ax)*dx+(my-ay)*dy)/len2, 0.0f, 1.0f) : 0.0f;
            float px=ax+t*dx, py=ay+t*dy;
            return dist(mx,my,px,py);
        };
        return segDist(x1,y1,midX,y1) < 6.0f || segDist(midX,y1,midX,y2) < 6.0f || segDist(midX,y2,x2,y2) < 6.0f;
    }

    void drawConnection(const Connection& c) {
        float x1,y1,x2,y2;
        pinScreenPos(c.fromNode, c.fromPin, true, x1, y1);
        pinScreenPos(c.toNode, c.toPin, false, x2, y2);
        float midX = (x1 + x2) * 0.5f;
        VisualNode* fn = graph.findNode(c.fromNode);
        bool isExec = false;
        if (fn) if (Pin* p = fn->findPin(c.fromPin)) isExec = (p->kind == PinKind::Exec);
        if (isExec) SDL_SetRenderDrawColor(renderer, 255, 210, 90, 255);
        else        SDL_SetRenderDrawColor(renderer, 120, 190, 255, 255);
        SDL_RenderLine(renderer, x1, y1, midX, y1);
        SDL_RenderLine(renderer, midX, y1, midX, y2);
        SDL_RenderLine(renderer, midX, y2, x2, y2);
        // arrowhead
        SDL_RenderLine(renderer, x2, y2, x2 - 6, y2 - 4);
        SDL_RenderLine(renderer, x2, y2, x2 - 6, y2 + 4);
    }

    void drawScaledText(const std::string& text, float x, float y, SDL_Color color, float scale) {
        if (text.empty()) return;
        SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), (int)text.size(), color);
        if (!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        if (tex) {
            SDL_FRect dst = { x, y, surf->w * scale, surf->h * scale };
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_DestroySurface(surf);
    }

    void drawNode(VisualNode& n) {
        SDL_FRect sr = screenRect(n);
        if (sr.x + sr.w < canvasRect.x || sr.x > canvasRect.x + canvasRect.w ||
            sr.y + sr.h < canvasRect.y || sr.y > canvasRect.y + canvasRect.h) return;
        
        // --- Dynamically ensure pins match usePins mode ---
        bool usePins = n.props.count("usePins") && n.props["usePins"] == "1";
        bool isSpecialBody = (n.kind == NodeKind::MethodDef || 
                            n.kind == NodeKind::WebLoop || 
                            n.kind == NodeKind::MainLoop);
        if (isSpecialBody) {
            bool hasBodyPin = false;
            for (auto& p : n.pins) if (p.name == "body") { hasBodyPin = true; break; }
            if (usePins && !hasBodyPin) {
                n.pins.push_back({"body", PinKind::Exec, PinDir::Output, "exec"});
            } else if (!usePins && hasBodyPin) {
                n.pins.erase(std::remove_if(n.pins.begin(), n.pins.end(),
                    [](const Pin& p){ return p.name == "body"; }), n.pins.end());
            }
        }
        
        SDL_SetRenderDrawColor(renderer, 45, 45, 55, 235);
        SDL_RenderFillRect(renderer, &sr);
        SDL_Color hc = nodeKindColor(n.kind);
        SDL_FRect header = { sr.x, sr.y, sr.w, 22 * zoom };
        SDL_SetRenderDrawColor(renderer, hc.r, hc.g, hc.b, 255);
        SDL_RenderFillRect(renderer, &header);
        SDL_SetRenderDrawColor(renderer, n.selected ? 255 : 120, n.selected ? 230 : 120, n.selected ? 90 : 140, 255);
        SDL_RenderRect(renderer, &sr);
        
        std::string title = nodeKindLabel(n.kind);
        drawScaledText(title, sr.x + 6, sr.y + (22 * zoom - 20 * zoom) * 0.5f, {255,255,255,255}, zoom);
        
        // --- Draw "use pins" checkbox for special body nodes ---
        if (isSpecialBody) {
            float cbSize = 14.0f * std::max(0.7f, zoom);
            float cbX = sr.x + sr.w - cbSize - 6.0f;
            float cbY = sr.y + 4.0f;
            SDL_FRect cbRect = {cbX, cbY, cbSize, cbSize};
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderRect(renderer, &cbRect);
            if (usePins) {
                SDL_SetRenderDrawColor(renderer, 80, 220, 120, 255);
                SDL_RenderFillRect(renderer, &cbRect);
                // Draw a small checkmark
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                float mx = cbX + cbSize*0.25f, my = cbY + cbSize*0.5f;
                SDL_RenderLine(renderer, mx, my, mx + cbSize*0.3f, my + cbSize*0.3f);
                SDL_RenderLine(renderer, mx + cbSize*0.3f, my + cbSize*0.3f, mx + cbSize*0.75f, my - cbSize*0.25f);
            }
            // Tooltip-like label
            drawScaledText(usePins ? "pins" : "txt", cbX - 28*zoom, cbY + 1, {180,180,200,255}, std::min(1.0f, zoom));
        }
        
        // pins (only drawn if they exist)
        for (auto& p : n.pins) {
            float px, py; pinScreenPos(n.id, p.name, p.dir == PinDir::Output, px, py);
            SDL_FRect dot = { px - 4, py - 4, 8, 8 };
            if (p.kind == PinKind::Exec) SDL_SetRenderDrawColor(renderer, 255, 210, 90, 255);
            else SDL_SetRenderDrawColor(renderer, 120, 190, 255, 255);
            SDL_RenderFillRect(renderer, &dot);
            float lx = (p.dir == PinDir::Output) ? px - 60.0f : px + 8.0f;
            drawScaledText(p.name, lx, py - 8, {200,200,210,255}, zoom);
        }
        
        // property widgets
        float wy = sr.y + 34.0f + (float)std::max(inCount(n), outCount(n)) * 20.0f * zoom + 4.0f;
        for (auto& key : n.widgetOrder) {
            // Skip "code" textarea when in pins mode
            if (isSpecialBody && key == "code" && usePins) continue;
            
            ensureWidget(n, key);
            SDL_FRect wr = { sr.x + 8, wy, sr.w - 16, 24.0f };
            bool isTextArea = n.textAreas.count(key) > 0;
            if (isTextArea) {
                float remaining = sr.y + sr.h - wy - 8;
                wr.h = std::max(remaining, 100.0f);
            }
            if (n.lineEdits.count(key)) {
                n.lineEdits[key]->setRect(wr);
                n.lineEdits[key]->render(0.0f, 0.0f);
            } else if (n.spinBoxes.count(key)) {
                n.spinBoxes[key]->setRect(wr);
                n.spinBoxes[key]->render(0.0f, 0.0f);
            } else if (n.textAreas.count(key)) {
                n.textAreas[key]->setRect(wr);
                n.textAreas[key]->setZoom(zoom);
                n.textAreas[key]->render(0.0f, 0.0f);
            }
            if (isTextArea) wy += wr.h + 4;
            else wy += 28.0f;
        }
        
        // Resize grip
        if (n.kind == NodeKind::CustomCode || n.kind == NodeKind::MethodDef ||
            n.kind == NodeKind::WebLoop || n.kind == NodeKind::MainLoop) {
            SDL_SetRenderDrawColor(renderer, 200, 200, 210, 200);
            float gx = sr.x + sr.w - 4.0f, gy = sr.y + sr.h - 4.0f;
            for (float o = 4.0f; o <= 12.0f; o += 4.0f) {
                SDL_RenderLine(renderer, gx - o, gy, gx, gy - o);
            }
        }
    }

    int inCount(VisualNode& n) { int c=0; for (auto& p : n.pins) if (p.dir==PinDir::Input) c++; return c; }
    int outCount(VisualNode& n) { int c=0; for (auto& p : n.pins) if (p.dir==PinDir::Output) c++; return c; }

    void ensureWidget(VisualNode& n, const std::string& key) {
        if (n.lineEdits.count(key) || n.spinBoxes.count(key) || n.textAreas.count(key)) return;
        
        bool usePins = n.props.count("usePins") && n.props["usePins"] == "1";
        bool isSpecialBody = (n.kind == NodeKind::MethodDef || 
                            n.kind == NodeKind::WebLoop || 
                            n.kind == NodeKind::MainLoop);
        
        // In pins mode, skip the "code" textarea entirely
        if (isSpecialBody && key == "code" && usePins) return;
        
        bool numeric = (n.kind == NodeKind::Literal && key == "text" && n.props.count("numeric"));
        if (numeric) {
            float initVal = 0.0f;
            try { initVal = std::stof(n.props[key]); } catch (...) {}
            auto sb = std::make_unique<Gui::SpinBox>(renderer, textEngine, font, 
                SDL_FRect{0,0,100,24}, -1000000.0f, 1000000.0f, initVal, 0.5f);
            n.spinBoxes[key] = std::move(sb);
        } else {
            if ((n.kind == NodeKind::CustomCode || n.kind == NodeKind::MethodDef ||
                n.kind == NodeKind::WebLoop || n.kind == NodeKind::MainLoop) && key == "code") {
                auto ta = std::make_unique<Gui::TextArea>(renderer, textEngine, font, SDL_FRect{0,0,100,300});
                ta->setText(n.props[key]);
                n.textAreas[key] = std::move(ta);
            } else {
                auto le = std::make_unique<Gui::LineEdit>(renderer, textEngine, font, SDL_FRect{0,0,100,24}, key);
                le->setText(n.props[key]);
                n.lineEdits[key] = std::move(le);
            }
        }
    }

    void buildPalette() {
        // Create the scrollable container (vertical).
        paletteContainer = std::make_unique<Gui::ScrollableContainer>(
            renderer, textEngine, font, Gui::ScrollOrientation::Vertical);

        // Create the inner VBox and add all widgets.
        auto box = std::make_unique<Gui::VBoxContainer>();
        box->setPadding(4);
        box->setSpacing(6);

        // Rename controls
        auto contextNameEdit = std::make_unique<Gui::LineEdit>(renderer, textEngine, font,
                                                            SDL_FRect{0,0,150,24}, "Context name");
        contextNameEdit->setText(graph.contextStructName);
        contextNameEditPtr = contextNameEdit.get();
        box->addChild(std::move(contextNameEdit));

        auto renameContextBtn = std::make_unique<Gui::Button>(renderer, font, "Rename Ctx",
                                                            SDL_FPoint{0,0}, 150, 26);
        renameContextBtn->onClicked = [this]() { renameContextStruct(contextNameEditPtr->getText()); };
        box->addChild(std::move(renameContextBtn));

        auto classNameEdit = std::make_unique<Gui::LineEdit>(renderer, textEngine, font,
                                                            SDL_FRect{0,0,150,24}, "Script class name");
        classNameEdit->setText(graph.className);
        classNameEditPtr = classNameEdit.get();
        box->addChild(std::move(classNameEdit));

        auto renameClassBtn = std::make_unique<Gui::Button>(renderer, font, "Rename Sct",
                                                            SDL_FPoint{0,0}, 150, 26);
        renameClassBtn->onClicked = [this]() { renameScriptClass(classNameEditPtr->getText()); };
        box->addChild(std::move(renameClassBtn));

        // All other "+ ..." buttons
        auto addBtn = [&](const std::string& label, NodeKind kind) {
            auto btn = std::make_unique<Gui::Button>(renderer, font, label, SDL_FPoint{0,0}, 150, 26);
            btn->onClicked = [this, kind]() {
                float spawnX = panX + 60.0f + 20.0f * (float)(graph.nodes.size() % 5);
                float spawnY = panY + 60.0f + 20.0f * (float)(graph.nodes.size() % 5);
                graph.nodes.push_back(makeNode(graph, kind, spawnX, spawnY));
            };
            box->addChild(std::move(btn));
        };
        addBtn("+ On Start",    NodeKind::EventOnStart);
        addBtn("+ On Update",   NodeKind::EventOnUpdate);
        addBtn("+ On Draw",     NodeKind::EventOnDraw);
        addBtn("+ On End",      NodeKind::EventOnEnd);
        addBtn("+ Field",       NodeKind::FieldDecl);
        addBtn("+ Script Var",  NodeKind::ScriptVarDecl);
        addBtn("+ Include",     NodeKind::IncludeDecl);
        addBtn("+ Method Decl", NodeKind::MethodDecl);
        addBtn("+ Method Def",  NodeKind::MethodDef);
        addBtn("+ Web Loop",    NodeKind::WebLoop);
        addBtn("+ Main Loop",   NodeKind::MainLoop);
        addBtn("+ Get Var",     NodeKind::VarGet);
        addBtn("+ Set Var",     NodeKind::VarSet);
        addBtn("+ Entity Ref",  NodeKind::EntityByName);
        addBtn("+ Guard",       NodeKind::GuardReturn);
        addBtn("+ If",          NodeKind::IfBranch);
        addBtn("+ For Range",   NodeKind::ForRangeEntities);
        addBtn("+ While",       NodeKind::WhileLoop);
        addBtn("+ Call",        NodeKind::FunctionCall);
        addBtn("+ Binary Op",   NodeKind::BinaryOp);
        addBtn("+ Literal",     NodeKind::Literal);
        addBtn("+ Comment",     NodeKind::Comment);
        addBtn("+ Custom Code", NodeKind::CustomCode);
        addBtn("+ End Func", NodeKind::EndFunction);

        auto delBtn = std::make_unique<Gui::Button>(renderer, font, "Delete Sel",
                                                    SDL_FPoint{0,0}, 150, 26);
        delBtn->onClicked = [this]() { if (selectedNodeId != -1) deleteNode(selectedNodeId); };
        box->addChild(std::move(delBtn));

        auto closeBtn = std::make_unique<Gui::Button>(renderer, font, "Close",
                                                    SDL_FPoint{0,0}, 150, 26);
        closeBtn->onClicked = [this]() { if (onClose) onClose(); };
        box->addChild(std::move(closeBtn));

        auto formatBtn = std::make_unique<Gui::Button>(renderer, font, "Format Code",
                                                    SDL_FPoint{0,0}, 150, 26);
        formatBtn->onClicked = [this]() {
            for (auto& n : graph.nodes) {
                if ((n->kind == NodeKind::CustomCode || n->kind == NodeKind::MethodDef) && n->props.count("code")) {
                    n->props["code"] = formatCodeWithNewlines(n->props["code"]);
                    if (n->textAreas.count("code")) {
                        n->textAreas["code"]->setText(n->props["code"]);
                    }
                }
            }
        };
        box->addChild(std::move(formatBtn));

        // Compute the total height of the VBox's content.
        // Since we haven't set the box's rect yet, we manually sum child heights + spacing + padding.
        float totalH = box->getPadding() * 2.0f;
        const auto& children = box->getChildren();
        for (auto& child : children) {
            totalH += child->getHeight() + box->getSpacing();
        }
        totalH -= box->getSpacing(); // remove last spacing
        paletteTotalH = totalH;

        // Set the VBox's rect to (0,0, width, totalH).
        float paletteWidth = 170.0f - 12.0f; // account for scrollbar width
        box->setRect({0.0f, 0.0f, paletteWidth, totalH});

        // Store the raw pointer before moving into the container.
        paletteBox = box.get();
        paletteContainer->setChild(std::move(box));
    }
};
} // namespace VisualLogic
