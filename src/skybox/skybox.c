#include <string.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "skybox.h"

// ─────────────────────────────────────────────────────────────
// Skybox shader
// Supports both cubemap and equirectangular 2D panoramic textures.
// Translation is stripped from the view matrix so the sky is fixed.
// Depth is forced to 1.0 so sky always renders behind geometry.
// ─────────────────────────────────────────────────────────────

static const char *SKY_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "uniform mat4 matProjection;\n"
    "uniform mat4 matView;\n"
    "out vec3 fragDir;\n"
    "void main() {\n"
    "    fragDir = vertexPosition;\n"
    "    mat4 rotView = mat4(mat3(matView));\n"
    "    vec4 clipPos = matProjection * rotView * vec4(vertexPosition, 1.0);\n"
    "    gl_Position = clipPos.xyww;\n"
    "}\n";

// mode: 0 = gradient, 1 = cubemap, 2 = equirectangular panoramic
static const char *SKY_FS =
    "#version 330\n"
    "in vec3 fragDir;\n"
    "uniform samplerCube cubemap;\n"
    "uniform sampler2D   panorama;\n"
    "uniform int         mode;\n"
    "uniform vec4        topColor;\n"
    "uniform vec4        botColor;\n"
    "out vec4 finalColor;\n"
    "const float PI = 3.14159265358979;\n"
    "void main() {\n"
    "    if (mode == 1) {\n"
    "        finalColor = texture(cubemap, fragDir);\n"
    "    } else if (mode == 2) {\n"
    "        vec3 d = normalize(fragDir);\n"
    "        float u = 0.5 + atan(d.z, d.x) / (2.0 * PI);\n"
    "        float v = 0.5 - asin(clamp(d.y, -1.0, 1.0)) / PI;\n"
    "        finalColor = texture(panorama, vec2(u, v));\n"
    "    } else {\n"
    "        float t = clamp(normalize(fragDir).y * 0.5 + 0.5, 0.0, 1.0);\n"
    "        finalColor = mix(botColor, topColor, t);\n"
    "    }\n"
    "}\n";

// ─────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────

static Shader  sSkyShader  = { 0 };
static Model   sSkybox     = { 0 };
static int     sMode       = 0;   // 0=gradient 1=cubemap 2=panoramic
static int     sModeLoc;
static int     sTopColLoc;
static int     sBotColLoc;

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────

// Load a cubemap from six separate face images by arranging them into a
// vertical cross layout and using LoadTextureCubemap.
//
// Vertical cross layout (4 wide x 3 tall in face-size units):
//         [top]
// [left] [front] [right] [back]
//         [bottom]
//
static TextureCubemap LoadCubemapFromFaces(void) {
    const char *paths[6] = {
        "assets/skybox/right.png",  "assets/skybox/left.png",
        "assets/skybox/top.png",    "assets/skybox/bottom.png",
        "assets/skybox/front.png",  "assets/skybox/back.png",
    };
    for (int i = 0; i < 6; i++)
        if (!FileExists(paths[i])) return (TextureCubemap){ 0 };

    Image faces[6];
    for (int i = 0; i < 6; i++) {
        faces[i] = LoadImage(paths[i]);
        ImageFormat(&faces[i], PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }
    int fw = faces[0].width, fh = faces[0].height;

    // Build a 4×3 cross image
    Image cross = GenImageColor(fw * 4, fh * 3, BLANK);

    // face index → (col, row) in the cross
    // right=0, left=1, top=2, bottom=3, front=4, back=5
    int col[6] = { 2, 0, 1, 1, 1, 3 };
    int row[6] = { 1, 1, 0, 2, 1, 1 };

    for (int i = 0; i < 6; i++) {
        Rectangle dst = { col[i] * fw, row[i] * fh, fw, fh };
        ImageDraw(&cross, faces[i],
                  (Rectangle){ 0, 0, fw, fh }, dst, WHITE);
        UnloadImage(faces[i]);
    }

    // LoadTextureCubemap auto-detects the cross layout
    TextureCubemap cube = LoadTextureCubemap(cross, CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE);
    UnloadImage(cross);

    if (cube.id) TraceLog(LOG_INFO, "SKYBOX: loaded from 6 face images (%dx%d)", fw, fh);
    return cube;
}

// Load a 2D equirectangular panoramic texture (no cubemap conversion needed).
// The fragment shader samples it using spherical UV mapping.
static Texture2D LoadPanorama(const char *path) {
    Image img = LoadImage(path);
    if (!img.data) return (Texture2D){ 0 };
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    TraceLog(LOG_INFO, "SKYBOX: loaded panoramic %s", path);
    return tex;
}

// ─────────────────────────────────────────────────────────────
// SkyboxInit
// ─────────────────────────────────────────────────────────────

void SkyboxInit(void) {
    sSkyShader = LoadShaderFromMemory(SKY_VS, SKY_FS);

    Mesh cubeMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    sSkybox       = LoadModelFromMesh(cubeMesh);
    sSkybox.materials[0].shader = sSkyShader;

    sModeLoc   = GetShaderLocation(sSkyShader, "mode");
    sTopColLoc = GetShaderLocation(sSkyShader, "topColor");
    sBotColLoc = GetShaderLocation(sSkyShader, "botColor");

    // Tell raylib to auto-populate matView and matProjection each draw call
    sSkyShader.locs[SHADER_LOC_MATRIX_VIEW]        = GetShaderLocation(sSkyShader, "matView");
    sSkyShader.locs[SHADER_LOC_MATRIX_PROJECTION]  = GetShaderLocation(sSkyShader, "matProjection");
    // Register cubemap and panorama sampler locations
    sSkyShader.locs[SHADER_LOC_MAP_CUBEMAP]        = GetShaderLocation(sSkyShader, "cubemap");
    sSkyShader.locs[SHADER_LOC_MAP_DIFFUSE]        = GetShaderLocation(sSkyShader, "panorama");

    // Priority: 6 face cubemap → panoramic HDR → panoramic PNG → gradient
    TextureCubemap cube = LoadCubemapFromFaces();
    if (cube.id) {
        sSkybox.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = cube;
        sMode = 1;
        return;
    }

    const char *panoPaths[] = { "assets/skybox/sky.hdr", "assets/skybox/sky.png", NULL };
    for (int i = 0; panoPaths[i]; i++) {
        if (!FileExists(panoPaths[i])) continue;
        Texture2D pano = LoadPanorama(panoPaths[i]);
        if (pano.id) {
            sSkybox.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = pano;
            sMode = 2;
            return;
        }
    }

    TraceLog(LOG_WARNING, "SKYBOX: no assets found, using gradient fallback");
    sMode = 0;
}

void SkyboxShutdown(void) {
    if (sMode == 1)
        UnloadTexture(sSkybox.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture);
    else if (sMode == 2)
        UnloadTexture(sSkybox.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture);
    UnloadModel(sSkybox);
    UnloadShader(sSkyShader);
}

// ─────────────────────────────────────────────────────────────
// SkyboxDraw
// ─────────────────────────────────────────────────────────────

void SkyboxDraw(Camera3D cam) {
    SetShaderValue(sSkyShader, sModeLoc, &sMode, SHADER_UNIFORM_INT);

    if (sMode == 0) {
        // Gradient fallback — deep blue top, near-black horizon
        float top[4] = { 0.05f, 0.08f, 0.18f, 1.0f };
        float bot[4] = { 0.01f, 0.02f, 0.06f, 1.0f };
        SetShaderValue(sSkyShader, sTopColLoc, top, SHADER_UNIFORM_VEC4);
        SetShaderValue(sSkyShader, sBotColLoc, bot, SHADER_UNIFORM_VEC4);
    }

    // Don't touch backface culling state — the game disables it globally
    // and re-enabling it here would break brush top faces.
    rlDisableDepthMask();
    DrawModel(sSkybox, cam.position, 1.0f, WHITE);
    rlEnableDepthMask();
}
