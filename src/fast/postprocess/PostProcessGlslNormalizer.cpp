// Implemented from the plan in docs/crt_shader_plan_2026-05-11.md §3 / §4
// and the libretro single-file GLSL conventions documented at
// https://github.com/libretro/glsl-shaders/blob/master/README.md. No code
// copied from RetroArch or any GPL-licensed shader runtime.

#include "fast/postprocess/PostProcessGlslNormalizer.h"

#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace Fast {

namespace {

bool IsIdChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// In-place whole-word replace. Identifier-style boundary check on both
// sides — `Texture` matches `Texture` and `Texture.xy` but not
// `TextureSize` or `MyTexture`.
void RewriteIdentifier(std::string& src, const std::string& from, const std::string& to) {
    if (from.empty() || from == to) {
        return;
    }
    size_t pos = 0;
    while ((pos = src.find(from, pos)) != std::string::npos) {
        const bool leftOk = (pos == 0) || !IsIdChar(src[pos - 1]);
        const size_t end = pos + from.size();
        const bool rightOk = (end >= src.size()) || !IsIdChar(src[end]);
        if (leftOk && rightOk) {
            src.replace(pos, from.size(), to);
            pos += to.size();
        } else {
            pos += 1;
        }
    }
}

// Token-aware "does this line contain identifier X as a whole word".
bool LineHasIdentifier(const std::string& line, const char* token) {
    const size_t n = std::strlen(token);
    size_t pos = 0;
    while ((pos = line.find(token, pos)) != std::string::npos) {
        const bool leftOk = (pos == 0) || !IsIdChar(line[pos - 1]);
        const bool rightOk = (pos + n >= line.size()) || !IsIdChar(line[pos + n]);
        if (leftOk && rightOk) {
            return true;
        }
        pos += 1;
    }
    return false;
}

bool LineStartsWithKeyword(const std::string& line, const char* keyword) {
    const size_t firstNonWs = line.find_first_not_of(" \t");
    if (firstNonWs == std::string::npos) {
        return false;
    }
    const size_t n = std::strlen(keyword);
    if (line.size() - firstNonWs < n) {
        return false;
    }
    if (line.compare(firstNonWs, n, keyword) != 0) {
        return false;
    }
    const size_t after = firstNonWs + n;
    if (after >= line.size()) {
        return true;
    }
    const char c = line[after];
    // Keyword must terminate (whitespace / EOL / common punctuation). This
    // avoids matching `in` inside `int` or `varying` inside `varying2`.
    return !IsIdChar(c);
}

// Schema names AFTER identifier rewrites — these are the canonical
// identifiers our preamble declares. Any line that declares one of them
// is dropped so we can re-introduce them with consistent types and
// (in the transpile path) explicit Vulkan bindings.
constexpr const char* kSchemaIdentifiers[] = {
    "Source", "SourceSize", "OutputSize", "InputSize", "FrameCount",
    "FrameDirection", "MVPMatrix", "vTexCoord", "fragColor",
    // Phase 2D: libretro multipass-shader bindings.
    "Original", "OriginalSize",
};

bool IsSchemaDeclarationLine(const std::string& line) {
    // Strip every top-level `in` / `varying` / `IN` / `attribute` / `out`
    // / `OUT` declaration. A fragment-shader post-process pass only has
    // one varying input (vTexCoord) and one output (fragColor) — both
    // are provided by the runtime preamble. Anything else the user
    // declared either matches that role (and would duplicate our
    // declaration) or is wired to a vertex stage that doesn't exist in
    // our fixed stock VS. Either way, dropping the user line and
    // letting the preamble win is the correct outcome.
    //
    // This catches edge cases like `IN vec2 Coord;` paired with
    // `#define Coord TEX0` — after the text-rewrite stage above
    // `TEX0 → vTexCoord` would make the user's declaration expand to
    // `in vec2 vTexCoord;` and collide with our preamble. By stripping
    // the declaration unconditionally we side-step the macro-chain.
    if (LineStartsWithKeyword(line, "in") || LineStartsWithKeyword(line, "varying") ||
        LineStartsWithKeyword(line, "IN") || LineStartsWithKeyword(line, "attribute") ||
        LineStartsWithKeyword(line, "out") || LineStartsWithKeyword(line, "OUT")) {
        return true;
    }
    // Uniforms: only strip the schema-aliased ones — the user is
    // allowed to keep their own custom uniforms (shader parameters
    // etc.) so the preamble doesn't have to enumerate them.
    if (!LineStartsWithKeyword(line, "uniform")) {
        return false;
    }
    for (const char* id : kSchemaIdentifiers) {
        if (LineHasIdentifier(line, id)) {
            return true;
        }
    }
    return false;
}

// Preamble emitted at the top of the normalized output. Order matters
// only for legibility; the `#define FRAGMENT` makes libretro single-file
// shaders select their FRAGMENT branch when compiled under our forced
// `#version 330 core`.
constexpr const char* kPreamble =
    "#version 330 core\n"
    "// Preamble injected by LUS PostProcessGlslNormalizer.\n"
    "#define FRAGMENT 1\n"
    // PRECISION is libretro's portability shim for GLSL-ES precision
    // qualifiers; outside ES it expands to nothing. Declaring it here
    // (rather than text-rewriting) keeps the user's own `#define
    // PRECISION` lines syntactically valid — they just redefine an
    // already-empty macro, which the GLSL preprocessor permits.
    "#define PRECISION\n"
    "uniform sampler2D Source;\n"
    "uniform sampler2D Original;\n"
    "uniform vec2 SourceSize;\n"
    "uniform vec2 OutputSize;\n"
    "uniform vec2 InputSize;\n"
    "uniform vec2 OriginalSize;\n"
    "uniform int FrameCount;\n"
    "uniform float FrameDirection;\n"
    "in vec2 vTexCoord;\n"
    "out vec4 fragColor;\n";

} // namespace

std::string NormalizeUserGlsl(const std::string& src) {
    // Step 1 — rewrite libretro / legacy identifier names to our schema.
    // Done as a whole-buffer pass before line-walking so that downstream
    // strip decisions see canonical names regardless of which alias the
    // author used.
    std::string body = src;
    // Sampler aliases. `s_p` is the older libretro variable name still
    // used by Hyllian's CRT shaders; `Texture` is the modern one.
    // OrigTexture is libretro's pre-2018 spelling for the game-FB
    // sampler now standardized as Original. Order matters: the longer
    // identifier ("OrigTexture") must be rewritten before its prefix
    // pattern ("Texture") so the whole-word match grabs the longer
    // form first.
    RewriteIdentifier(body, "OrigTexture", "Original");
    RewriteIdentifier(body, "OrigInputSize", "OriginalSize");
    RewriteIdentifier(body, "Texture", "Source");
    RewriteIdentifier(body, "s_p", "Source");
    // `Source` reads against a `SourceSize` query — but if the file used
    // `TextureSize` we already rewrote it twice (Texture → Source then
    // SourceSize already present). The order matters: rewrite the
    // composite name BEFORE the substring it contains so we don't
    // produce `SourceSize` from `Texture` + later code. The
    // RewriteIdentifier function is whole-word so this is safe — listed
    // for clarity:
    RewriteIdentifier(body, "TextureSize", "SourceSize");
    // Texture-coordinate varyings. libretro VS exports `TexCoord`; FS
    // sees it as `texCoord` (older naming) or `TEX0` (newer).
    RewriteIdentifier(body, "TEX0", "vTexCoord");
    RewriteIdentifier(body, "TexCoord", "vTexCoord");
    RewriteIdentifier(body, "texCoord", "vTexCoord");
    // Fragment output. libretro's combined-file convention uses
    // `FragColor` (named `out`) at __VERSION__ >= 130, falling back to
    // `gl_FragColor` (built-in) at < 130. We force #version 330 so the
    // `out FragColor` path is selected; rewrite that name to ours.
    RewriteIdentifier(body, "FragColor", "fragColor");
    // gl_FragColor: removed in GLSL 330 core, so any user shader that
    // still references it would fail to compile under our forced version
    // anyway. We rewrite for consistency when authors fall through to
    // the legacy branch by accident.
    RewriteIdentifier(body, "gl_FragColor", "fragColor");
    // Legacy GLSL <= 130 / ES sampler-typed lookup names. GLSL 330 core
    // has the unified `texture()` overload set. The libretro `tex2D` /
    // `IN` / `OUT` macros are handled by the shader's own `#if
    // __VERSION__ >= 130` block, which selects the modern names under
    // our forced version. Direct uses still need rewriting:
    RewriteIdentifier(body, "texture2D", "texture");
    RewriteIdentifier(body, "texture3D", "texture");
    RewriteIdentifier(body, "textureCube", "texture");

    // Step 2 — walk lines, drop our own future preamble's worth of
    // declarations plus user `#version` / `#pragma parameter` directives.
    std::istringstream in(body);
    std::string line;
    std::vector<std::string> out;
    out.reserve(body.size() / 32 + 16);
    while (std::getline(in, line)) {
        const size_t firstNonWs = line.find_first_not_of(" \t");
        if (firstNonWs != std::string::npos) {
            // Strip user `#version`; we force our own.
            if (line.compare(firstNonWs, 8, "#version") == 0) {
                continue;
            }
            // Strip `#pragma parameter`; the UI doesn't surface these
            // yet, and glslang warns on unknown pragmas. The `#define`
            // fall-back blocks in the shaders themselves carry the
            // default values, so nothing visual changes.
            if (line.compare(firstNonWs, 17, "#pragma parameter") == 0) {
                continue;
            }
        }
        if (IsSchemaDeclarationLine(line)) {
            continue;
        }
        out.push_back(line);
    }

    std::string normalized;
    normalized.reserve(body.size() + 512);
    normalized += kPreamble;
    for (const auto& l : out) {
        normalized += l;
        normalized += '\n';
    }
    return normalized;
}

} // namespace Fast
