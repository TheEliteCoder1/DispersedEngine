#pragma once
// ============================================================================
// lstranspile.h  —  LogicScript (.ls) -> C++ (.cpp) source-to-source transpiler
// ============================================================================
//
// Drop this in the same folder as engine.h. It is a standalone, dependency-
// free single header: it doesn't need physics.h/music.h/texture.h/font.h or
// SDL, so it can be included from the editor build even in translation units
// that never touch the engine runtime.
//
// WHAT THIS IS
//   LogicScript is NOT a virtual machine and NOT interpreted. It is a thin,
//   project-focused *dialect* of C++: shorter keywords, a handful of
//   unambiguous punctuation shorthands, and "@directives" that expand to
//   fixed boilerplate blocks. LSTranspile::transpileFile() turns a .ls file into
//   a real, human-readable .cpp file that is then transpiled by the normal
//   C++ toolchain like any other project script (see REGISTER_SCRIPT in
//   engine.h / game.cpp). There is no runtime cost and no new language to
//   maintain at execution time — only at authoring time.
//
//   Header content (declarations meant for a .h, not a .cpp translation
//   unit) is written the same way but saved with a ".lh" extension
//   instead of ".ls". The keywords, operators, and directives all mean
//   exactly the same thing in a .lh file — content and processing are
//   identical either way — only the extension changes, and that's what
//   tells the transpiler which real-C++ extension to produce:
//     .ls   -> .cpp   (regular LogicScript source)
//     .lh  -> .h     (LogicScript header)
//   Keeping them as distinct source extensions (rather than guessing from
//   file content) means a header and a same-stem .cpp never generate into
//   each other, or into a pre-existing .cpp/.ls of the same stem — see
//   siblingCppPath()/siblingLsPath() below.
//
// DESIGN NOTE (read this before extending the tables below)
//   An earlier hand-written .ls sample in this project was produced by
//   blind find-and-replace across the *entire* file, including comments and
//   prose ("internally" -> "numernally", "pointer" -> "ponumer", "handled"
//   -> "handd", "into" -> "numo"). That happened because the replacement
//   wasn't tokenized: it matched substrings anywhere, including inside
//   English words and inside identifiers that had nothing to do with the
//   abbreviation being applied. It also reused "Rnd" for two unrelated
//   things (the SDL_Renderer* type, and a mangled verb-prefix "RndDo" for
//   "Render...").
//
//   This transpiler fixes both problems structurally:
//     1. Comments and string/char literals are copied to the output
//        byte-for-byte. They are never touched by any substitution table.
//     2. Every substitution (keywords, external API renames) matches a
//        *whole identifier token*, never a substring. "handleEvent" is
//        either a recognized whole word or it passes through unchanged —
//        it can never partially match and get mangled.
//     3. Every abbreviation has exactly one meaning. "Rnd" means
//        SDL_Renderer* and nothing else; project-defined functions like
//        RenderFrame are just identifiers the *author* chose to spell
//        however they like (e.g. "RndDoFrame") — the transpiler does not
//        rename user symbols, only real external API entry points that
//        must resolve to a specific linked SDL/TTF/MIX/Box2D/engine symbol.
//
// LANGUAGE SUMMARY
//   Types:      num->int  spnum->float  flip->bool  chr->char  str->std::string
//               none->void  noptr->nullptr
//   Qualifiers: stay->const  still->static  stayexpr->constexpr
//   Control:    ret->return  fl->for  elif->else if  and->&&  or->||
//   Operators:  ~   assignment            ->  =
//               ~~  equality              ->  ==
//               !~  inequality            ->  !=
//               %   member access via ptr ->  ->
//               :   scope resolution      ->  ::   (only when written tight,
//                                                    e.g. std:clamp; a ':'
//                                                    with surrounding spaces,
//                                                    as in a ternary, is left
//                                                    alone)
//               $   stream insertion      ->  <<   (std_error $"x" $y  ->
//                                                    std::cerr << "x" << y)
//               mod modulo (keyword)      ->  %    ('%' is reserved for
//                                                    member access, so plain
//                                                    modulo is spelled `mod`)
//               JOIN bitwise-or (keyword) ->  |
//   Directives: @name expands to a fixed boilerplate block (see
//               DirectiveTable below). @import "x.h" -> #include "x.h".
//               @if_on_web / @else / @endif -> #ifdef __EMSCRIPTEN__ / #else
//               / #endif. Unknown @directives are left as a commented
//               marker plus a transpiler warning, never silently dropped.
//   Statements: terminated with an explicit ';' — exactly like C++. It is
//               always typed by the author, never inferred. Newlines
//               carry no meaning at all: a statement may be wrapped
//               across as many physical lines as you like, and multiple
//               statements may sit on one line, with no line-continuation
//               backslash required either way. The transpiler doesn't
//               try to guess where a statement ends — it trusts the ';'
//               you wrote, the same way a C++ compiler does.
//
// USAGE
//   #include "lstranspile.h"
//   std::string err;
//   if (!LSTranspile::transpileFile("OpenWorld/scripts/GameScript.ls",
//                                "OpenWorld/scripts/GameScript.cpp", err)) {
//       SDL_Log("LogicScript transpile failed: %s", err.c_str());
//   }
//
//   Or work purely in memory (used by the editor's "Transpile" button so it
//   can show errors without touching disk until the user saves):
//   LSTranspile::TranspileResult r = LSTranspile::transpileSource(lsText, "GameScript.ls");
// ============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>
#include <fstream>
#include <sstream>

namespace LSTranspile {

// ----------------------------------------------------------------------
// Result type
// ----------------------------------------------------------------------
struct Diagnostic {
    int line;
    std::string message;
};

struct TranspileResult {
    bool success = false;
    std::string cppSource;
    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> warnings;
};

// ----------------------------------------------------------------------
// Tables (extend these for your own project's abbreviations — see the
// docs above: only real external API names belong in RenameTable; your
// own project functions/variables never need an entry).
// ----------------------------------------------------------------------

// Whole-word LogicScript keywords/types -> C++ text. Applied only to
// identifier tokens (never to substrings, comments, or string literals).
inline std::unordered_map<std::string, std::string>& KeywordTable() {
    static std::unordered_map<std::string, std::string> t = {
        // types
        {"num", "int"}, {"spnum", "float"}, {"flip", "bool"}, {"chr", "char"},
        {"str", "std::string"}, {"none", "void"}, {"noptr", "nullptr"},
        {"unordmap", "std::unordered_map"}, {"dynarr", "std::vector"},
        // qualifiers
        {"stay", "const"}, {"still", "static"}, {"stayexpr", "constexpr"},
        // control flow / block close
        {"ret", "return"}, {"fl", "for"}, {"cont", "continue"},
        {"and", "&&"}, {"or", "||"}, {"mod", "%"}, {"JOIN", "|"},
        // registration macro used by the engine's ScriptRegistry
        {"reg_script", "REGISTER_SCRIPT"},
        // engine/editor short type aliases (SDL wrapper types)
        {"Rnd", "SDL_Renderer"}, {"Window", "SDL_Window"}, {"Event", "SDL_Event"},
        {"Pnt", "SDL_Point"}, {"FPnt", "SDL_FPoint"}, {"FRect", "SDL_FRect"},
        {"JoystickID", "SDL_JoystickID"}, {"GamepadID", "SDL_JoystickID"},
        {"TTF_TxtEng", "TTF_TextEngine"}, {"GameCtx", "GameContext"},
        {"RndFrame", "RenderFrame"}
    };
    return t;
}

// "elif" needs two output tokens ("else" then "if"); handled specially in
// the translator rather than via KeywordTable since it isn't 1:1.

// Real external API renames: LogicScript spelling -> the actual linked
// SDL/TTF/MIX/Box2D/engine symbol. Add to this as your project grows; it
// is the ONLY place where an identifier's meaning is looked up, so it's
// safe to keep large without risking accidental substring corruption.
inline std::unordered_map<std::string, std::string>& RenameTable() {
    static std::unordered_map<std::string, std::string> t = {
        // SDL core
        {"Init", "SDL_Init"}, {"Quit", "SDL_Quit"}, {"FetchError", "SDL_GetError"},
        {"CreateWindow", "SDL_CreateWindow"}, {"CreateRnd", "SDL_CreateRenderer"},
        {"GetWindowSize", "SDL_GetWindowSize"}, {"PollEvent", "SDL_PollEvent"},
        {"GetTicks", "SDL_GetTicks"},
        {"StartVideo", "SDL_INIT_VIDEO"}, {"StartEvents", "SDL_INIT_EVENTS"},
        {"StartGamepad", "SDL_INIT_GAMEPAD"}, {"WINDOW_RESIZABLE", "SDL_WINDOW_RESIZABLE"},
        {"EVENT_QUIT", "SDL_EVENT_QUIT"}, {"EVENT_KEY_DOWN", "SDL_EVENT_KEY_DOWN"},
        {"EVENT_GAMEPAD_ADDED", "SDL_EVENT_GAMEPAD_ADDED"},
        {"EVENT_GAMEPAD_REMOVED", "SDL_EVENT_GAMEPAD_REMOVED"},
        {"GAMEPAD_BUTTON_SOUTH", "SDL_GAMEPAD_BUTTON_SOUTH"},
        {"GAMEPAD_AXIS_LEFTX", "SDL_GAMEPAD_AXIS_LEFTX"},
        {"GAMEPAD_AXIS_LEFTY", "SDL_GAMEPAD_AXIS_LEFTY"},
        {"BLENDMODE_BLEND", "SDL_BLENDMODE_BLEND"},
        {"GetGamepads", "SDL_GetGamepads"}, {"OpenGamepad", "SDL_OpenGamepad"},
        {"CloseGamepad", "SDL_CloseGamepad"}, {"GetGamepadID", "SDL_GetGamepadID"},
        {"GetGamepadButton", "SDL_GetGamepadButton"}, {"GetGamepadAxis", "SDL_GetGamepadAxis"},
        // SDL render
        {"SetRndVSync", "SDL_SetRenderVSync"}, {"SetRndDrwClr", "SDL_SetRenderDrawColor"},
        {"SetRndDrwBlndMode", "SDL_SetRenderDrawBlendMode"},
        {"RdClear", "SDL_RenderClear"}, {"RdPresent", "SDL_RenderPresent"},
        {"RdLine", "SDL_RenderLine"}, {"RdTxture", "SDL_RenderTexture"},
        // TTF / MIX
        {"TTF_CreateRndTxtEng", "TTF_CreateRendererTextEngine"},
        {"TTF_DestroyRndTxtEng", "TTF_DestroyRendererTextEngine"},
        // Box2D
        {"b2Body_IsVal", "b2Body_IsValid"}, {"b2Body_GetRot", "b2Body_GetRotation"},
    };
    return t;
}

inline std::string normalizeBlock(const std::string& block) {
    std::string result;
    std::istringstream stream(block);
    std::string line;
    while (std::getline(stream, line)) {
        size_t start = line.find_first_not_of(" \t\r");
        size_t end = line.find_last_not_of(" \t\r");
        if (start == std::string::npos) continue;
        std::string trimmed = line.substr(start, end - start + 1);
        if (trimmed.empty()) continue;
        if (!result.empty()) result += '\n';
        result += trimmed;
    }
    return result;
}

// @directive -> expansion. A directive followed immediately by "()" in the
// source has the "()" silently consumed (so both "@thing" and "@thing()"
// are accepted). Directives ending in "_LINE" are single-statement; others
// may expand to multiple lines and are emitted with the current line's
// leading indentation applied to every line for readable output.
//
// NOTE: this must be defined before ReverseDirectiveTable() below, since
// that function calls it — C++ does not do whole-file two-pass lookup for
// ordinary function calls, only the declarations already seen at the call
// site are visible.
inline std::unordered_map<std::string, std::string>& DirectiveTable() {
    static std::unordered_map<std::string, std::string> t = {
        {"@check_web_get_web",
            "#ifdef __EMSCRIPTEN__\n#include <emscripten/emscripten.h>\n#endif"},

        {"@init_running_and_last_time",
            "bool running = true;\nUint64 lastTime = SDL_GetTicks();"},

        {"@now_dt_lastime_calc",
            "Uint64 now = SDL_GetTicks();\n"
            "float dt = (float)(now - lastTime) / 1000.0f;\n"
            "lastTime = now;"},

        {"@check_quit",
            "if (e.type == SDL_EVENT_QUIT) running = false;\n"
            "if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;"},

        {"@script_events",
            "if (ctx.scene && ctx.scene->script) {\n"
            "    ctx.scene->script->onEvent(e);\n"
            "}"},

        {"@hand_gui_elements_events",
            "for (auto& elem : ctx.scene->guiElements) {\n"
            "    elem->handleEvent(e, window, 0.0f, 0.0f);\n"
            "}"},

        {"@check_gamepad_add_gamepad",
            "if (e.type == SDL_EVENT_GAMEPAD_ADDED && !ctx.gamepad)\n"
            "    ctx.gamepad = SDL_OpenGamepad(e.gdevice.which);\n"
            "else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && ctx.gamepad &&\n"
            "         SDL_GetGamepadID(ctx.gamepad) == e.gdevice.which) {\n"
            "    SDL_CloseGamepad(ctx.gamepad);\n"
            "    ctx.gamepad = nullptr;\n"
            "}"},

        {"@hand_gamepad_events",
            "GameScript* gs = ActiveGameScript(ctx);\n"
            "if (!gs) {\n"
            "    float deadzone = 0.2f;\n"
            "    float ax = SDL_GetGamepadAxis(ctx.gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;\n"
            "    float ay = SDL_GetGamepadAxis(ctx.gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;\n"
            "    if (std::fabs(ax) < deadzone) ax = 0.0f;\n"
            "    if (std::fabs(ay) < deadzone) ay = 0.0f;\n"
            "    const float cursorSpeed = 500.0f;\n"
            "    ctx.gamepadCursorX += ax * cursorSpeed * dt;\n"
            "    ctx.gamepadCursorY += ay * cursorSpeed * dt;\n"
            "    int winW, winH;\n"
            "    SDL_GetWindowSize(ctx.window, &winW, &winH);\n"
            "    ctx.gamepadCursorX = std::clamp(ctx.gamepadCursorX, 0.0f, (float)winW);\n"
            "    ctx.gamepadCursorY = std::clamp(ctx.gamepadCursorY, 0.0f, (float)winH);\n"
            "    bool confirmDown = SDL_GetGamepadButton(ctx.gamepad, SDL_GAMEPAD_BUTTON_SOUTH) != 0;\n"
            "    for (auto& elem : ctx.scene->guiElements) {\n"
            "        elem->handleGamepad(ctx.gamepadCursorX, ctx.gamepadCursorY, 0.0f, 0.0f,\n"
            "                             ctx.window, confirmDown, ctx.confirmDownLastFrame);\n"
            "    }\n"
            "    ctx.confirmDownLastFrame = confirmDown;\n"
            "} else {\n"
            "    ctx.confirmDownLastFrame = false;\n"
            "}"},

        {"@web_set_main_loops",
            "emscripten_set_main_loop(main_loop_callback, 0, 1);\n"
            "emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);"},

        {"@destroy_rnd_and_win",
            "SDL_DestroyRenderer(renderer);\nSDL_DestroyWindow(window);"},
    };
    return t;
}

// Builds the reverse lookup: normalized C++ block -> @directive name.
// The map is built once, on first use, via an immediately-invoked lambda:
// C++11/17 guarantee function-local static initialization is thread-safe
// (no two threads can run the initializer concurrently, and every thread
// sees the fully-built map), so no manual "init" flag is needed — a
// hand-rolled `static bool init; if (!init) {...}` guard is a data race if
// two threads call this for the first time at once, since one thread can
// observe `init == true` while another thread's writes into `t` haven't
// become visible yet.
inline std::unordered_map<std::string, std::string>& ReverseDirectiveTable() {
    static std::unordered_map<std::string, std::string> t = [] {
        std::unordered_map<std::string, std::string> m;
        for (auto& [directive, expansion] : DirectiveTable()) {
            m[normalizeBlock(expansion)] = directive;
        }
        return m;
    }();
    return t;
}

// Attempts to match a block of C++ source lines starting at `startLine`
// against known directive expansions. Returns the @directive name if
// matched (and sets `linesConsumed`), or empty string if no match.
inline std::string matchDirectiveAtLine(
    const std::vector<std::string>& sourceLines,
    size_t startLine,
    int& linesConsumed)
{
    // Try matching progressively larger blocks (up to the longest directive)
    static const int MAX_DIRECTIVE_LINES = 20; // longest directive block
    auto& revTable = ReverseDirectiveTable();

    for (int len = 1; len <= MAX_DIRECTIVE_LINES && startLine + (size_t)len <= sourceLines.size(); ++len) {
        std::string block;
        for (int i = 0; i < len; ++i) {
            if (!block.empty()) block += '\n';
            block += sourceLines[startLine + i];
        }
        std::string normalized = normalizeBlock(block);
        auto it = revTable.find(normalized);
        if (it != revTable.end()) {
            linesConsumed = len;
            return it->second;
        }
    }
    linesConsumed = 0;
    return "";
}

// ----------------------------------------------------------------------
// Tokenizer
// ----------------------------------------------------------------------
namespace detail {

enum class TokKind { Whitespace, Newline, LineComment, BlockComment, StringLit,
                      CharLit, Preprocessor, Directive, Identifier, Number, Punct };

struct Tok {
    TokKind kind;
    std::string text;  // exact source text (used verbatim for comments/strings)
    int line;
};

inline bool isIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
inline bool isIdentChar(char c)  { return std::isalnum((unsigned char)c) || c == '_'; }

inline std::vector<Tok> tokenize(const std::string& src) {
    std::vector<Tok> out;
    size_t i = 0, n = src.size();
    int line = 1;

    while (i < n) {
        char c = src[i];

        // Newline
        if (c == '\n') { out.push_back({TokKind::Newline, "\n", line}); line++; i++; continue; }
        if (c == '\r') { i++; continue; } // normalize CRLF -> LF

        // Whitespace (not newline)
        if (c == ' ' || c == '\t') {
            size_t j = i;
            while (j < n && (src[j] == ' ' || src[j] == '\t')) j++;
            out.push_back({TokKind::Whitespace, src.substr(i, j - i), line});
            i = j; continue;
        }

        // Line comment
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            size_t j = i;
            while (j < n && src[j] != '\n') j++;
            out.push_back({TokKind::LineComment, src.substr(i, j - i), line});
            i = j; continue;
        }

        // Block comment
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < n && !(src[j] == '*' && src[j + 1] == '/')) {
                if (src[j] == '\n') line++;
                j++;
            }
            j = (j + 1 < n) ? j + 2 : n;
            out.push_back({TokKind::BlockComment, src.substr(i, j - i), line});
            i = j; continue;
        }

        // String literal
        if (c == '"') {
            size_t j = i + 1;
            while (j < n && src[j] != '"') {
                if (src[j] == '\\' && j + 1 < n) j++;
                j++;
            }
            j = (j < n) ? j + 1 : n;
            out.push_back({TokKind::StringLit, src.substr(i, j - i), line});
            i = j; continue;
        }

        // Char literal
        if (c == '\'') {
            size_t j = i + 1;
            while (j < n && src[j] != '\'') {
                if (src[j] == '\\' && j + 1 < n) j++;
                j++;
            }
            j = (j < n) ? j + 1 : n;
            out.push_back({TokKind::CharLit, src.substr(i, j - i), line});
            i = j; continue;
        }

        // Preprocessor line (raw '#...' — passed through untouched so
        // hand-written #include/#define/#pragma still work verbatim)
        if (c == '#') {
            size_t j = i;
            while (j < n && src[j] != '\n') j++;
            out.push_back({TokKind::Preprocessor, src.substr(i, j - i), line});
            i = j; continue;
        }

        // Directive '@name'
        if (c == '@') {
            size_t j = i + 1;
            while (j < n && isIdentChar(src[j])) j++;
            out.push_back({TokKind::Directive, src.substr(i, j - i), line});
            i = j; continue;
        }

        // Identifier / keyword
        if (isIdentStart(c)) {
            size_t j = i;
            while (j < n && isIdentChar(src[j])) j++;
            out.push_back({TokKind::Identifier, src.substr(i, j - i), line});
            i = j; continue;
        }

        // Number
        if (std::isdigit((unsigned char)c)) {
            size_t j = i;
            while (j < n && (std::isalnum((unsigned char)src[j]) || src[j] == '.')) j++;
            out.push_back({TokKind::Number, src.substr(i, j - i), line});
            i = j; continue;
        }

        // Punctuation / operators — greedily grab the LogicScript multi-
        // char operators we recognize (~~, !~, ::, ->, <<, >>, &&, ||,
        // ==, !=, <=, >=, +=, -=, etc.) so downstream translation sees
        // whole operator tokens, not lone characters.
        {
            static const char* multi[] = {
                "~~", "!~", "::", "->", "<<", ">>", "&&", "||", "==", "!=",
                "<=", ">=", "+=", "-=", "*=", "/=", "++", "--"
            };
            bool matched = false;
            for (const char* m : multi) {
                size_t len = std::char_traits<char>::length(m);
                if (i + len <= n && src.compare(i, len, m) == 0) {
                    out.push_back({TokKind::Punct, src.substr(i, len), line});
                    i += len; matched = true; break;
                }
            }
            if (matched) continue;
        }

        out.push_back({TokKind::Punct, std::string(1, c), line});
        i++;
    }
    return out;
}

} // namespace detail

// ----------------------------------------------------------------------
// Translator
// ----------------------------------------------------------------------
inline TranspileResult transpileSource(const std::string& lsSource, const std::string& sourceFilename = "<source>") {
    using namespace detail;
    TranspileResult result;
    std::vector<Tok> toks = tokenize(lsSource);
    std::string out;
    out.reserve(lsSource.size() * 2);

    // indent tracking so multi-line directive expansions read naturally —
    // purely cosmetic, unrelated to statement boundaries.
    std::string currentIndent;
    std::string lastIdent;

    // First identifier seen since the start of the current statement (reset
    // at ';', '{', '}' — the only three tokens that can end a statement,
    // since newlines carry no meaning). Unlike lastIdent — which is
    // overwritten by every identifier, including a case label's value —
    // this reliably answers "does this statement start with case/default/
    // public/private/protected/class/struct?" for the ':' handling below.
    bool firstColonInStatement = false;
    std::string stmtLeadIdent;

    // True at the start of a new statement: right after ';', '{', '}', a
    // preprocessor/directive line, or the start of the file. Used only to
    // disambiguate a leading '~' as a destructor rather than assignment.
    // Every statement boundary here is something the *author* wrote
    // explicitly (a real ';', '{', or '}') — there is no inference step.
    bool atStmtStart = true;

    for (size_t idx = 0; idx < toks.size(); ++idx) {
        Tok& t = toks[idx];

        switch (t.kind) {
            case TokKind::Newline:
                // Newlines are purely cosmetic in LogicScript: they don't
                // end statements and they don't need escaping to continue
                // one, so they just pass through unchanged.
                out += '\n';
                currentIndent.clear();
                continue;

            case TokKind::Whitespace:
                out += t.text;
                currentIndent += t.text;
                continue;

            case TokKind::LineComment:
            case TokKind::BlockComment:
                out += t.text; // verbatim; doesn't change statement state
                continue;

            case TokKind::StringLit:
            case TokKind::CharLit:
                out += t.text;
                atStmtStart = false;
                continue;

            case TokKind::Preprocessor:
                out += t.text;
                atStmtStart = true; // a #include/#define/#pragma line is self-contained
                continue;

            case TokKind::Number:
                out += t.text;
                atStmtStart = false;
                continue;

            case TokKind::Identifier: {
                const std::string& w = t.text;

                if (w == "elif") {
                    out += "else if";
                    atStmtStart = false;
                    continue;
                }
                if (w == "else") {
                    out += "else";
                    atStmtStart = false;
                    continue;
                }

                std::string translated = w;
                auto kw = KeywordTable().find(w);
                if (kw != KeywordTable().end()) { translated = kw->second; }
                else {
                    auto rn = RenameTable().find(w);
                    if (rn != RenameTable().end()) translated = rn->second;
                    else if (w.rfind("PS_", 0) == 0) translated = "ProjectScript_" + w.substr(3);
                }
                out += translated;
                lastIdent = w;
                if (stmtLeadIdent.empty()) stmtLeadIdent = w;
                atStmtStart = false;
                continue;
            }

            case TokKind::Directive: {
                const std::string& d = t.text;

                if (d == "@import") {
                    size_t k = idx + 1;
                    while (k < toks.size() && toks[k].kind == TokKind::Whitespace) k++;
                    if (k < toks.size() && toks[k].kind == TokKind::StringLit) {
                        out += "#include " + toks[k].text;
                        idx = k;
                    } else {
                        result.errors.push_back({t.line, "@import expects a following string, e.g. @import \"game.h\""});
                        out += "/* @import: missing header string */";
                    }
                    atStmtStart = true;
                    continue;
                }
                if (d == "@if_on_web") { out += "#ifdef __EMSCRIPTEN__"; atStmtStart = true; continue; }
                if (d == "@else")      { out += "#else";                atStmtStart = true; continue; }
                if (d == "@endif")     { out += "#endif";               atStmtStart = true; continue; }

                auto exp = DirectiveTable().find(d);
                if (exp != DirectiveTable().end()) {
                    size_t k = idx + 1;
                    if (k < toks.size() && toks[k].kind == TokKind::Punct && toks[k].text == "(") {
                        size_t k2 = k + 1;
                        if (k2 < toks.size() && toks[k2].kind == TokKind::Punct && toks[k2].text == ")") {
                            idx = k2;
                        }
                    }
                    std::string expansion = exp->second;
                    std::string indented;
                    size_t p = 0;
                    bool first = true;
                    while (p <= expansion.size()) {
                        size_t nl = expansion.find('\n', p);
                        std::string chunk = (nl == std::string::npos) ? expansion.substr(p) : expansion.substr(p, nl - p);
                        if (!first) indented += "\n" + currentIndent;
                        indented += chunk;
                        first = false;
                        if (nl == std::string::npos) break;
                        p = nl + 1;
                    }
                    out += indented;
                } else {
                    result.warnings.push_back({t.line, "Unknown directive '" + d + "' — left as-is; add it to DirectiveTable() in lstranspile.h"});
                    out += "/* " + d + ": unknown LogicScript directive, not expanded */";
                }
                atStmtStart = true; // expansions carry their own terminators
                continue;
            }

            case TokKind::Punct: {
                const std::string& p = t.text;

                if (p == "(") { out += "("; atStmtStart = false; continue; }
                if (p == ")") { out += ")"; atStmtStart = false; continue; }
                if (p == "[") { out += "["; atStmtStart = false; continue; }
                if (p == "]") { out += "]"; atStmtStart = false; continue; }
                if (p == "\\") { out += p; atStmtStart = false; continue; } // macro line-splice, passed through as-is
                if (p == "~~") { out += "=="; atStmtStart = false; continue; }
                if (p == "!~") { out += "!="; atStmtStart = false; continue; }
                if (p == "%")  { out += "->"; atStmtStart = false; continue; }
                if (p == "$")  { out += "<<"; atStmtStart = false; continue; }
                if (p == "~") {
                    // Skip whitespace to find the next token
                    size_t nextNonWs = idx + 1;
                    while (nextNonWs < toks.size() && (toks[nextNonWs].kind == TokKind::Whitespace || toks[nextNonWs].kind == TokKind::Newline))
                        ++nextNonWs;
                    bool followsIdentifier = (nextNonWs < toks.size() &&
                                            toks[nextNonWs].kind == TokKind::Identifier);
                    bool looksLikeDestructor = followsIdentifier && (lastIdent == "virtual" || atStmtStart);
                    out += looksLikeDestructor ? "~" : "=";
                    atStmtStart = false;
                    continue;
                }
                if (p == ":") {
                    bool tightBefore = !out.empty() && isIdentChar(out.back());
                    bool tightAfter = (idx + 1 < toks.size()) &&
                                    (toks[idx + 1].kind == TokKind::Identifier || toks[idx + 1].kind == TokKind::Number);

                    // Case 1: access specifier or label (case/default)
                    if (stmtLeadIdent == "case" || stmtLeadIdent == "default" ||
                        stmtLeadIdent == "public" || stmtLeadIdent == "private" || stmtLeadIdent == "protected") {
                        out += ":";
                        // Do NOT set firstColonInStatement because these are not inheritance colons.
                        // The label is a complete construct on its own — clear stmtLeadIdent so it
                        // doesn't leak into the next statement (otherwise a later 'std:function' on
                        // the very next line would be misread as another label colon instead of '::').
                        stmtLeadIdent.clear();
                    }
                    // Case 2: inheritance colon (first colon after class/struct keyword)
                    else if ((stmtLeadIdent == "class" || stmtLeadIdent == "struct") && !firstColonInStatement) {
                        out += ":";
                        firstColonInStatement = true;  // mark that we've seen the inheritance colon
                    }
                    // Case 3: everything else → scope resolution or ternary/other
                    else {
                        // Only output '::' if both sides are tight (no spaces)
                        out += (tightBefore && tightAfter) ? "::" : ":";
                    }
                    atStmtStart = false;
                    continue;
                }
                if (p == "{") {
                    out += p;
                    atStmtStart = true;
                    stmtLeadIdent.clear();
                    firstColonInStatement = false;
                    continue;
                }
                if (p == "}") {
                    out += p;
                    atStmtStart = true;
                    stmtLeadIdent.clear();
                    firstColonInStatement = false;
                    continue;
                }
                if (p == ";") {
                    // The author's own explicit statement terminator — passed
                    // through verbatim, exactly like C++, whether it closes an
                    // ordinary statement or sits inside a for-loop header.
                    out += ";";
                    atStmtStart = true;
                    stmtLeadIdent.clear();
                    firstColonInStatement = false;
                    continue;
                }
                if (p == "," || p == "&&" || p == "||" || p == "==" || p == "!=" ||
                    p == "<=" || p == ">=" || p == "+" || p == "-" || p == "*" || p == "/" ||
                    p == "=" || p == "<" || p == ">" || p == "?" || p == "&" || p == "|" ||
                    p == "+=" || p == "-=" || p == "*=" || p == "/=") {
                    out += p; atStmtStart = false; continue;
                }
                out += p; // '.' '++' '--' etc. — ordinary, passed through as-is
                atStmtStart = false;
                continue;
            }
        }
    }

    result.cppSource = out;
    result.success = result.errors.empty();
    (void)sourceFilename;
    return result;
}

// ----------------------------------------------------------------------
// File-based convenience wrapper
// ----------------------------------------------------------------------

// True if `path` ends in `ext` (`ext` includes the leading dot, e.g. ".h").
inline bool hasExt(const std::string& path, const std::string& ext) {
    return path.size() >= ext.size() &&
           path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
}

inline bool transpileFile(const std::string& lsPath, const std::string& outCppPath, std::string& errorOut) {
    std::ifstream in(lsPath, std::ios::binary);
    if (!in) { errorOut = "Could not open " + lsPath; return false; }
    std::stringstream ss;
    ss << in.rdbuf();

    TranspileResult r = transpileSource(ss.str(), lsPath);
    if (!r.errors.empty()) {
        std::ostringstream msg;
        for (auto& e : r.errors) msg << lsPath << ":" << e.line << ": error: " << e.message << "\n";
        errorOut = msg.str();
        return false;
    }

    std::ofstream out(outCppPath, std::ios::binary);
    if (!out) { errorOut = "Could not write " + outCppPath; return false; }
    // Header LogicScript (.lh) produces a real header (.h); everything
    // else (.ls, or an unrecognized extension) produces a .cpp. Name the
    // right source extension here so the note in the generated file
    // actually points back at the file the author should edit.
    const char* srcExt = hasExt(lsPath, ".lh") ? ".lh" : ".ls";
    out << "// Auto-generated by lstranspile.h from " << lsPath
        << " — do not hand-edit; edit the " << srcExt << " source instead.\n";
    out << r.cppSource;

    if (!r.warnings.empty()) {
        std::ostringstream msg;
        for (auto& w : r.warnings) msg << lsPath << ":" << w.line << ": warning: " << w.message << "\n";
        errorOut = msg.str(); // non-fatal: caller may still log this even though success == true
    }
    return true;
}

// Derives the sibling C++ output path for a LogicScript path:
//   "OpenWorld/scripts/GameScript.ls"  -> "OpenWorld/scripts/GameScript.cpp"
//   "OpenWorld/scripts/GameScript.lh" -> "OpenWorld/scripts/GameScript.h"
//
// LogicScript headers use a distinct ".lh" source extension (rather than
// plain ".ls") specifically so this function can tell "this authors a
// header" from "this authors a .cpp" and route to the matching real-C++
// extension. Without that split, a ".ls" file and a ".lh" file that
// happen to share a stem (e.g. both named "Types") would both want to
// produce output next to it, and a header-producing .ls could silently
// overwrite — or be overwritten by — an unrelated .cpp of the same stem.
// If lsPath doesn't end in ".ls" or ".lh", ".cpp" is simply appended (so
// it never collides with the input file even when called on the wrong
// extension by mistake).
inline std::string siblingCppPath(const std::string & lsPath) {
    if (hasExt(lsPath, ".lh"))
        return lsPath.substr(0, lsPath.size() - 3) + ".h"; // FIXED
    if (hasExt(lsPath, ".ls"))
        return lsPath.substr(0, lsPath.size() - 3) + ".cpp";
    return lsPath + ".cpp";
}

// Convenience one-shot: transpiles a .ls (or .lh) file and writes the
// result right next to it, same stem, same folder — "GameScript.ls" in ->
// "GameScript.cpp" out, or "Types.lh" in -> "Types.h" out — overwriting
// any previous file of that derived name. This is what the editor's
// "Transpile .ls" button calls; use it directly too for build-time
// regeneration (e.g. a pre-build step that walks a scripts/ folder and
// transpiles every .ls/.lh it finds).
inline bool transpileFileToSibling(const std::string& lsPath, std::string& errorOut) {
    return transpileFile(lsPath, siblingCppPath(lsPath), errorOut);
}

// Same as transpileFileToSibling, but also reports the output path it wrote
// to (or would have written to on failure) — handy for logging/UI, e.g.
// SDL_Log("transpiled -> %s", outPathOut.c_str()).
inline bool transpileFileToSibling(const std::string& lsPath, std::string& errorOut, std::string& outPathOut) {
    outPathOut = siblingCppPath(lsPath);
    return transpileFile(lsPath, outPathOut, errorOut);
}

// ----------------------------------------------------------------------
// Reverse transpiler: C++ (.cpp) -> LogicScript (.ls)
// ----------------------------------------------------------------------

inline std::unordered_map<std::string, std::string>& ReverseKeywordTable() {
    static std::unordered_map<std::string, std::string> t = [] {
        std::unordered_map<std::string, std::string> m;
        // Build by swapping KeywordTable keys/values
        for (auto& [ls, cpp] : KeywordTable()) {
            // Skip multi-char operator entries (handled in punct section)
            if (cpp == "&&" || cpp == "||" || cpp == "%" || cpp == "|")
                continue;
            m[cpp] = ls;
        }
        // Manual overrides for types that appear as multi-token in C++
        m["std::string"] = "str";
        m["REGISTER_SCRIPT"] = "reg_script";
        return m;
    }();
    return t;
}


inline std::unordered_map<std::string, std::string>& ReverseRenameTable() {
    static std::unordered_map<std::string, std::string> t = [] {
        std::unordered_map<std::string, std::string> m;
        for (auto& [ls, cpp] : RenameTable()) {
            m[cpp] = ls;
        }
        return m;
    }();
    return t;
}


inline TranspileResult transpileCppToLogicScript(
    const std::string& cppSource,
    const std::string& sourceFilename = "<source>")
{
    using namespace detail;
    TranspileResult result;

    // ── Pre-pass: collapse directive blocks and #include ──
    std::vector<std::string> sourceLines;
    {
        std::istringstream ss(cppSource);
        std::string line;
        while (std::getline(ss, line)) sourceLines.push_back(line);
    }

    auto& revDirectives = ReverseDirectiveTable();
    std::string preProcessed;
    preProcessed.reserve(cppSource.size());

    for (size_t i = 0; i < sourceLines.size(); ) {
        bool matched = false;
        for (int len = 20; len >= 1 && !matched; --len) {
            if (i + (size_t)len > sourceLines.size()) continue;
            std::string block;
            for (int k = 0; k < len; ++k) {
                if (!block.empty()) block += '\n';
                block += sourceLines[i + k];
            }
            std::string normalized = normalizeBlock(block);
            auto it = revDirectives.find(normalized);
            if (it != revDirectives.end()) {
                preProcessed += it->second + "\n";
                i += len;
                matched = true;
            }
        }
        if (matched) continue;

        // #include "..." -> @import "..."
        std::string trimmed = sourceLines[i];
        size_t s = trimmed.find_first_not_of(" \t");
        if (s != std::string::npos) trimmed = trimmed.substr(s);
        if (trimmed.rfind("#include", 0) == 0) {
            size_t q1 = trimmed.find('"');
            size_t q2 = trimmed.rfind('"');
            if (q1 != std::string::npos && q2 > q1) {
                preProcessed += "@import " + trimmed.substr(q1, q2 - q1 + 1) + "\n";
            } else {
                preProcessed += sourceLines[i] + "\n";
            }
        } else {
            preProcessed += sourceLines[i] + "\n";
        }
        i++;
    }

    // ── Token pass: reverse keywords, operators, identifiers ──
    std::vector<Tok> toks = tokenize(preProcessed);
    std::string out;
    out.reserve(preProcessed.size() * 2);

    auto& revKw = ReverseKeywordTable();
    auto& revRn = ReverseRenameTable();
    for (size_t idx = 0; idx < toks.size(); ++idx) {
        Tok& t = toks[idx];

        switch (t.kind) {
            case TokKind::Newline:     out += '\n'; continue;
            case TokKind::Whitespace:  out += t.text; continue;
            case TokKind::LineComment:
            case TokKind::BlockComment: out += t.text; continue;
            case TokKind::StringLit:
            case TokKind::CharLit:     out += t.text; continue;
            case TokKind::Preprocessor: out += t.text; continue;
            case TokKind::Number:      out += t.text; continue;
            case TokKind::Directive:   out += t.text; continue;

            case TokKind::Identifier: {
                const std::string& w = t.text;

                // "else if" -> "elif"
                if (w == "else") {
                    size_t k = idx + 1;
                    while (k < toks.size() && toks[k].kind == TokKind::Whitespace) k++;
                    if (k < toks.size() && toks[k].kind == TokKind::Identifier &&
                        toks[k].text == "if") {
                        out += "elif";
                        idx = k;
                        continue;
                    }
                    out += "else";
                    continue;
                }

                auto kwIt = revKw.find(w);
                if (kwIt != revKw.end()) {
                    out += kwIt->second;
                    continue;
                }

                auto rnIt = revRn.find(w);
                if (rnIt != revRn.end()) { out += rnIt->second; continue; }

                if (w.rfind("ProjectScript_", 0) == 0) {
                    out += "PS_" + w.substr(14);
                    continue;
                }

                out += w;
                continue;
            }

            case TokKind::Punct: {
                const std::string& p = t.text;

                if (p == "==") { out += "~~";  continue; }
                if (p == "!=") { out += "!~";  continue; }
                if (p == "->") { out += "%";   continue; }
                if (p == "::") { out += ":";   continue; }
                if (p == "<<") { out += "$";   continue; }
                if (p == "&&") { out += "and"; continue; }
                if (p == "||") { out += "or";  continue; }
                if (p == "=")  { out += "~";   continue; }
                if (p == "%")  { out += "mod"; continue; }
                if (p == "|")  { out += "JOIN"; continue; }
                if (p == ";") {
                    // LogicScript statements are terminated with an explicit
                    // ';', the same as C++ — so a C++ ';' round-trips to a
                    // LogicScript ';' verbatim, whether it's an ordinary
                    // statement terminator or sits inside a for-loop header.
                    out += ";";
                    continue;
                }
                // ... (other operators remain unchanged) ...
                if (p == "{") {
                    out += p;
                    continue;
                }
                if (p == "}") {
                    out += p;
                    continue;
                }
                if (p == "(") {
                    out += p;
                    continue;
                }
                if (p == ")") {
                    out += p;
                    continue;
                }

                out += p;
                continue;
            }
        }
    }

    result.cppSource = out;
    result.success = result.errors.empty();
    (void)sourceFilename;
    return result;
}


// File-based convenience: reads a .cpp (or .h), writes a .ls (or .lh) next to it
inline bool transpileCppFileToLs(
    const std::string& cppPath,
    const std::string& outLsPath,
    std::string& errorOut)
{
    std::ifstream in(cppPath, std::ios::binary);
    if (!in) { errorOut = "Could not open " + cppPath; return false; }
    std::stringstream ss;
    ss << in.rdbuf();

    TranspileResult r = transpileCppToLogicScript(ss.str(), cppPath);
    if (!r.errors.empty()) {
        std::ostringstream msg;
        for (auto& e : r.errors)
            msg << cppPath << ":" << e.line << ": error: " << e.message << "\n";
        errorOut = msg.str();
        return false;
    }

    std::ofstream out(outLsPath, std::ios::binary);
    if (!out) { errorOut = "Could not write " + outLsPath; return false; }
    out << "// Auto-generated from " << cppPath << "\n";
    out << r.cppSource;
    return true;
}

// Derives the sibling LogicScript output path for a C++ source path — the
// mirror image of siblingCppPath() above:
//   "OpenWorld/scripts/GameScript.cpp" -> "OpenWorld/scripts/GameScript.ls"
//   "OpenWorld/engine/Types.h"         -> "OpenWorld/engine/Types.lh"
//
// A header (.h) reverse-transpiles to ".lh", never plain ".ls", for the
// same reason siblingCppPath() routes ".lh" to ".h": so a header and a
// same-stem .cpp never fight over one generated LogicScript file. Any
// other extension (.cpp, or an unrecognized one) reverse-transpiles to
// ".ls" — stripping ".cpp" if present, otherwise appending ".ls" so it
// never collides with the input file.
inline std::string siblingLsPath(const std::string& cppPath) {
    if (hasExt(cppPath, ".h"))
        return cppPath.substr(0, cppPath.size() - 2) + ".lh";
    if (hasExt(cppPath, ".cpp"))
        return cppPath.substr(0, cppPath.size() - 4) + ".ls";
    return cppPath + ".ls";
}

// Convenience one-shot: reverse-transpiles a .cpp (or .h) file and writes
// the result right next to it, same stem, same folder — "GameScript.cpp"
// in -> "GameScript.ls" out, or "Types.h" in -> "Types.lh" out —
// overwriting any previous file of that derived name. Mirror of
// transpileFileToSibling() for the reverse direction.
inline bool transpileCppFileToLsSibling(const std::string& cppPath, std::string& errorOut) {
    return transpileCppFileToLs(cppPath, siblingLsPath(cppPath), errorOut);
}

// Same as transpileCppFileToLsSibling, but also reports the output path it
// wrote to (or would have written to on failure) — handy for logging/UI.
inline bool transpileCppFileToLsSibling(const std::string& cppPath, std::string& errorOut, std::string& outPathOut) {
    outPathOut = siblingLsPath(cppPath);
    return transpileCppFileToLs(cppPath, outPathOut, errorOut);
}

} // namespace LSTranspile

// ----------------------------------------------------------------------
// Optional standalone CLI: g++ -std=c++20 -DLSCOMPILE_STANDALONE_MAIN lstranspile.h -x c++ -o lstranspile -
//   lstranspile GameScript.ls GameScript.cpp
// ----------------------------------------------------------------------
#ifdef LSCOMPILE_STANDALONE_MAIN
#include <iostream>
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: lstranspile <input.ls> <output.cpp>\n";
        return 1;
    }
    std::string err;
    if (!LSTranspile::transpileFile(argv[1], argv[2], err)) {
        std::cerr << err;
        return 1;
    }
    if (!err.empty()) std::cerr << err; // warnings
    std::cout << "Transpiled " << argv[1] << " -> " << argv[2] << "\n";
    return 0;
}
#endif
