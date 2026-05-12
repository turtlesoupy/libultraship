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

// Per-alias check: strip the user's `uniform sampler2D <alias>` and
// `uniform vec2 <alias>Size` declarations so the preamble's
// canonical, binding-explicit versions don't collide. The preamble
// injects both the sampler and the matching `<alias>Size` vec2 for
// every alias / external-texture name — chain-side bookkeeping
// populates them from PostProcessExtraBinding::{width,height} every
// frame.
bool IsAliasDeclarationLine(const std::string& line, const std::vector<std::string>& aliases) {
    if (!LineStartsWithKeyword(line, "uniform")) {
        return false;
    }
    for (const std::string& a : aliases) {
        if (a.empty()) {
            continue;
        }
        if (LineHasIdentifier(line, a.c_str())) {
            return true;
        }
        // `<alias>Size` would not whole-word match `<alias>` (the
        // trailing 'S' is an id-char), so check the size identifier
        // explicitly to catch user declarations like
        // `uniform vec2 MyAliasSize;`.
        const std::string sizeName = a + "Size";
        if (LineHasIdentifier(line, sizeName.c_str())) {
            return true;
        }
    }
    return false;
}

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
        LineStartsWithKeyword(line, "out") || LineStartsWithKeyword(line, "OUT") ||
        // libretro's GLSL-version-portability macros. Both expand to
        // varying/in/out/attribute depending on __VERSION__; stripping the
        // line at the macro-prefix avoids a chain `COMPAT_VARYING vec4
        // TEX0;` -> `varying vec4 vTexCoord;` -> collision with the
        // preamble after our TEX0 -> vTexCoord rewrite.
        LineStartsWithKeyword(line, "COMPAT_VARYING") ||
        LineStartsWithKeyword(line, "COMPAT_ATTRIBUTE")) {
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

// Parse a single `#pragma parameter ...` line into `out`. Returns true
// on success. The libretro spec form is:
//   #pragma parameter <name> "<label>" <default> <min> <max> [<step>]
// Quotes around the label are required by spec; some shaders ship them
// without quotes — we accept either to match RetroArch's lenient
// behavior. Unparseable lines log a warning and return false.
bool ParseParameterLine(const std::string& line, PostProcessShaderParameter& out) {
    // Skip "#pragma parameter".
    constexpr const char* kPrefix = "#pragma parameter";
    const size_t firstNonWs = line.find_first_not_of(" \t");
    if (firstNonWs == std::string::npos) {
        return false;
    }
    if (line.compare(firstNonWs, std::strlen(kPrefix), kPrefix) != 0) {
        return false;
    }
    size_t pos = firstNonWs + std::strlen(kPrefix);

    auto skipWs = [&]() {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
            ++pos;
        }
    };
    auto readIdent = [&]() -> std::string {
        skipWs();
        const size_t start = pos;
        while (pos < line.size() && IsIdChar(line[pos])) {
            ++pos;
        }
        return line.substr(start, pos - start);
    };
    auto readLabel = [&]() -> std::string {
        skipWs();
        if (pos >= line.size()) {
            return {};
        }
        if (line[pos] == '"') {
            ++pos;
            const size_t start = pos;
            while (pos < line.size() && line[pos] != '"') {
                ++pos;
            }
            std::string label = line.substr(start, pos - start);
            if (pos < line.size()) {
                ++pos; // consume closing quote
            }
            return label;
        }
        // Unquoted label — read until next whitespace.
        const size_t start = pos;
        while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') {
            ++pos;
        }
        return line.substr(start, pos - start);
    };
    auto readFloat = [&](float& v) -> bool {
        skipWs();
        if (pos >= line.size()) {
            return false;
        }
        const char* p = line.c_str() + pos;
        char* end = nullptr;
        const float parsed = std::strtof(p, &end);
        if (end == p) {
            return false;
        }
        pos += (size_t)(end - p);
        v = parsed;
        return true;
    };

    out.name = readIdent();
    if (out.name.empty()) {
        return false;
    }
    out.label = readLabel();
    if (out.label.empty()) {
        out.label = out.name;
    }
    if (!readFloat(out.defaultValue) || !readFloat(out.minValue) || !readFloat(out.maxValue)) {
        return false;
    }
    float step = 0.0f;
    if (readFloat(step) && step > 0.0f) {
        out.step = step;
    } else {
        const float range = out.maxValue - out.minValue;
        out.step = (range > 0.0f) ? range * 0.01f : 0.01f;
    }
    // Clamp default into the declared range to avoid initial-value
    // assertions downstream. The libretro spec implies the default
    // sits inside the range but doesn't mandate it.
    if (out.defaultValue < out.minValue) {
        out.defaultValue = out.minValue;
    } else if (out.defaultValue > out.maxValue) {
        out.defaultValue = out.maxValue;
    }
    return true;
}

std::vector<PostProcessShaderParameter> ParseShaderParametersImpl(const std::string& src) {
    std::vector<PostProcessShaderParameter> out;
    std::istringstream in(src);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("#pragma parameter") == std::string::npos) {
            continue;
        }
        PostProcessShaderParameter p;
        if (!ParseParameterLine(line, p)) {
            continue;
        }
        bool dup = false;
        for (const auto& existing : out) {
            if (existing.name == p.name) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            out.push_back(std::move(p));
        }
    }
    return out;
}

} // namespace

std::vector<PostProcessShaderParameter> ParseShaderParameters(const std::string& src) {
    return ParseShaderParametersImpl(src);
}

std::string NormalizeUserGlsl(const std::string& src) {
    return NormalizeUserGlsl(src, {}, {});
}

std::string NormalizeUserGlsl(const std::string& src,
                              const std::vector<std::string>& aliasNames) {
    return NormalizeUserGlsl(src, aliasNames, {});
}

std::string NormalizeUserGlsl(const std::string& src,
                              const std::vector<std::string>& aliasNames,
                              const std::vector<PostProcessShaderParameter>& parameters) {
    // Step 1 — rewrite libretro / legacy identifier names to our schema.
    // Done as a whole-buffer pass before line-walking so that downstream
    // strip decisions see canonical names regardless of which alias the
    // author used.
    std::string body = src;
    // Sampler-name aliases drawn from the public libretro shader-format
    // portability conventions (libretro/glsl-shaders README + the
    // libretro shader-spec doc pages). `Texture` and `OrigTexture` are
    // the documented legacy spellings of what later became `Source` and
    // `Original`. `s_p` is an older alternate spelling for the source
    // sampler that some shaders still ship. Listed here for portability;
    // every rewrite is identifier-by-identifier and not derived from
    // reading any specific shader's source.
    //
    // Order matters: the longer identifier (`OrigTexture`) must be
    // rewritten before its prefix pattern (`Texture`) so the whole-word
    // match grabs the longer form first.
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
    // Build a quick "is this token a parameter name" lookup for the
    // #define-stripping pass below. Linear scan is fine — shaders
    // declare at most a few dozen parameters and we walk each line
    // once.
    auto isParamName = [&parameters](const std::string& ident) -> bool {
        for (const auto& p : parameters) {
            if (p.name == ident) {
                return true;
            }
        }
        return false;
    };

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
            // Strip `#pragma parameter`; we re-emit each parameter as
            // a real `uniform float` in the preamble below.
            if (line.compare(firstNonWs, 17, "#pragma parameter") == 0) {
                continue;
            }
            // Strip `#define <param_name> <value>` for any parsed
            // parameter — the GLSL preprocessor would otherwise
            // expand our `uniform float <name>` declaration to
            // `uniform float <value>` (invalid). The shader's
            // fallback default is replaced by the live uniform
            // value the chain pushes each frame.
            if (line.compare(firstNonWs, 7, "#define") == 0) {
                size_t p = firstNonWs + 7;
                while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) {
                    ++p;
                }
                const size_t identStart = p;
                while (p < line.size() && IsIdChar(line[p])) {
                    ++p;
                }
                if (p > identStart) {
                    const std::string ident = line.substr(identStart, p - identStart);
                    if (isParamName(ident)) {
                        continue;
                    }
                }
            }
        }
        if (IsSchemaDeclarationLine(line)) {
            continue;
        }
        if (!aliasNames.empty() && IsAliasDeclarationLine(line, aliasNames)) {
            continue;
        }
        out.push_back(line);
    }

    std::string normalized;
    normalized.reserve(body.size() + 512);
    normalized += kPreamble;
    // Append per-alias sampler declarations after the canonical
    // preamble so they share the schema's strip / re-inject contract.
    // Names come from the .glslp `aliasN` keys (plus any
    // `textures = "..."` external-texture names) via the chain, in
    // declaration order — slot binding (2 + i) follows the same
    // ordering, so the transpiler can match without an out-of-band
    // sync. The matching `<alias>Size` vec2 is emitted alongside the
    // sampler so shaders that read filtered dimensions (libretro
    // multipass / lookup-texture shaders) see real values; the
    // transpiler appends the same names to the UBO block, and the
    // backends populate them from PostProcessExtraBinding::{width,
    // height} each frame.
    for (const std::string& alias : aliasNames) {
        if (alias.empty()) {
            continue;
        }
        normalized += "uniform sampler2D ";
        normalized += alias;
        normalized += ";\n";
        normalized += "uniform vec2 ";
        normalized += alias;
        normalized += "Size;\n";
    }
    // libretro `#pragma parameter` declarations land here as plain
    // `uniform float <name>` slots. The shader body's `#define
    // <name> <fallback>` blocks remain in-source — the GLSL spec
    // lets `uniform float X` and `#define X 0.5` coexist (the
    // uniform wins because it is the actual identifier; the
    // #define gets re-applied to literal text matches outside the
    // declaration, which is fine for the shader). The transpiled
    // HLSL/MSL gets a matching UBO tail member from the transpiler.
    // Strip user-declared duplicates so we own the canonical decl.
    for (const auto& p : parameters) {
        if (p.name.empty()) {
            continue;
        }
        normalized += "uniform float ";
        normalized += p.name;
        normalized += ";\n";
    }
    for (const auto& l : out) {
        normalized += l;
        normalized += '\n';
    }
    return normalized;
}

} // namespace Fast
