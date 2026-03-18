#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <GL/gl.h>
#include "worldobject.h"
#include "../globals.h"

// ─────────────────────────────────────────────────────────────
// Frustum Culling
// ─────────────────────────────────────────────────────────────

typedef struct { float x, y, z, w; } Plane;
static Plane frustumPlanes[6];
int framesSinceLoad;

void ExtractFrustumPlanes() {
    Matrix view = rlGetMatrixModelview();
    Matrix proj = rlGetMatrixProjection();
    Matrix vp   = MatrixMultiply(view, proj);

    frustumPlanes[0] = (Plane){ vp.m3 + vp.m0, vp.m7 + vp.m4, vp.m11 + vp.m8,  vp.m15 + vp.m12 };
    frustumPlanes[1] = (Plane){ vp.m3 - vp.m0, vp.m7 - vp.m4, vp.m11 - vp.m8,  vp.m15 - vp.m12 };
    frustumPlanes[2] = (Plane){ vp.m3 + vp.m1, vp.m7 + vp.m5, vp.m11 + vp.m9,  vp.m15 + vp.m13 };
    frustumPlanes[3] = (Plane){ vp.m3 - vp.m1, vp.m7 - vp.m5, vp.m11 - vp.m9,  vp.m15 - vp.m13 };
    frustumPlanes[4] = (Plane){ vp.m3 + vp.m2, vp.m7 + vp.m6, vp.m11 + vp.m10, vp.m15 + vp.m14 };
    frustumPlanes[5] = (Plane){ vp.m3 - vp.m2, vp.m7 - vp.m6, vp.m11 - vp.m10, vp.m15 - vp.m14 };

    for (int i = 0; i < 6; i++) {
        float len = sqrtf(frustumPlanes[i].x * frustumPlanes[i].x +
                          frustumPlanes[i].y * frustumPlanes[i].y +
                          frustumPlanes[i].z * frustumPlanes[i].z);
        if (len > 0.0f) {
            frustumPlanes[i].x /= len; frustumPlanes[i].y /= len;
            frustumPlanes[i].z /= len; frustumPlanes[i].w /= len;
        }
    }
}

bool IsSphereInFrustum(Vector3 center, float radius) {
    for (int i = 0; i < 6; i++) {
        float d = frustumPlanes[i].x * center.x + frustumPlanes[i].y * center.y +
                  frustumPlanes[i].z * center.z + frustumPlanes[i].w;
        if (d < -radius) return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────

static Shader lightShader;
static int lightCountLoc, ambientLoc, viewPosLoc;

static float globalAmbientR = 0.2f;
static float globalAmbientG = 0.2f;
static float globalAmbientB = 0.2f;

int objectCount = 0;
int lightCount  = 0;
WorldObject levelObjects[MAX_WORLD_OBJECTS] = { 0 };
WorldLight  levelLights[MAX_WORLD_LIGHTS]   = { 0 };

static LoadedModel modelCache[MAX_WORLD_OBJECTS];
static int         cachedModelCount = 0;

typedef struct { char path[256]; Texture2D texture; } CachedTexture;
#define MAX_TEXTURE_CACHE 64
static CachedTexture textureCache[MAX_TEXTURE_CACHE];
static int           cachedTextureCount = 0;

Vector3   spawnPoint    = { 0 };
bool      isLevelLoaded = false;
Texture2D missingTex;

// ─────────────────────────────────────────────────────────────
// Lightmap bake state
// Loaded before brushes are spawned; consumed by AddBrushObject.
// ─────────────────────────────────────────────────────────────

static bool               lightmapBaked  = false;
static int                brushObjIndex  = 0;

#define MAX_BRUSH_BAKE 256
static Image              sBakeAtlas                  = { 0 };
static LightmapBrushEntry sBakeEntries[MAX_BRUSH_BAKE];
static int                sBakeCount                  = 0;

// ─────────────────────────────────────────────────────────────
// Texture cache
// ─────────────────────────────────────────────────────────────

static Texture2D GetOrLoadTexture(const char *path) {
    for (int i = 0; i < cachedTextureCount; i++)
        if (strcmp(textureCache[i].path, path) == 0) return textureCache[i].texture;
    if (cachedTextureCount < MAX_TEXTURE_CACHE) {
        Texture2D tex = LoadTexture(path);
        SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
        strncpy(textureCache[cachedTextureCount].path, path, 255);
        textureCache[cachedTextureCount].texture = tex;
        cachedTextureCount++;
        return tex;
    }
    return LoadTexture(path);
}

// ─────────────────────────────────────────────────────────────
// Lighting
// ─────────────────────────────────────────────────────────────

void SetGlobalAmbient(float r, float g, float b) {
    globalAmbientR = r; globalAmbientG = g; globalAmbientB = b;
    if (lightShader.id != 0) {
        float a[4] = { r, g, b, 1.0f };
        SetShaderValue(lightShader, ambientLoc, a, SHADER_UNIFORM_VEC4);
    }
}

void AddWorldLight(Vector3 pos, Color color) {
    if (lightCount < MAX_WORLD_LIGHTS) {
        levelLights[lightCount].id      = GL_LIGHT0 + lightCount;
        levelLights[lightCount].pos     = pos;
        levelLights[lightCount].diffuse = color;
        levelLights[lightCount].active  = true;
        lightCount++;
    }
}

// ─────────────────────────────────────────────────────────────
// Atlas path helpers
// ─────────────────────────────────────────────────────────────

static void GetAtlasLayoutPath(const char *lp, char *out, int n) {
    strncpy(out, lp, n - 1);
    char *dot = strrchr(out, '.'); if (dot) strcpy(dot, ".lmap"); else strncat(out, ".lmap", n - strlen(out) - 1);
}
static void GetAtlasPngPath(const char *lp, char *out, int n) {
    strncpy(out, lp, n - 1);
    char *dot = strrchr(out, '.'); if (dot) strcpy(dot, ".lmap.png"); else strncat(out, ".lmap.png", n - strlen(out) - 1);
}

// ─────────────────────────────────────────────────────────────
// CPU atlas sampler (bilinear, RGBA8)
// ─────────────────────────────────────────────────────────────

static Color SampleAtlasCPU(float u, float v) {
    Image *img = &sBakeAtlas;
    if (u < 0.0f) u = 0.0f; if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;

    // Correct coordinate mapping for pixel centers
    float px = u * img->width - 0.5f;
    float py = v * img->height - 0.5f;

    // Clamp lower bounds
    if (px < 0.0f) px = 0.0f;
    if (py < 0.0f) py = 0.0f;

    int x0 = (int)px, y0 = (int)py;

    // Clamp upper bounds bounds explicitly to width-1
    int x1 = x0 + 1; if (x1 >= img->width)  x1 = img->width - 1;
    int y1 = y0 + 1; if (y1 >= img->height) y1 = img->height - 1;

    float fx = px - x0, fy = py - y0;
    unsigned char *p = (unsigned char *)img->data;
    int s = img->width * 4;
    #define PX(x,y) (p+(y)*s+(x)*4)
    float r = PX(x0,y0)[0]*(1-fx)*(1-fy) + PX(x1,y0)[0]*fx*(1-fy) + PX(x0,y1)[0]*(1-fx)*fy + PX(x1,y1)[0]*fx*fy;
    float g = PX(x0,y0)[1]*(1-fx)*(1-fy) + PX(x1,y0)[1]*fx*(1-fy) + PX(x0,y1)[1]*(1-fx)*fy + PX(x1,y1)[1]*fx*fy;
    float b = PX(x0,y0)[2]*(1-fx)*(1-fy) + PX(x1,y0)[2]*fx*(1-fy) + PX(x0,y1)[2]*(1-fx)*fy + PX(x1,y1)[2]*fx*fy;
    #undef PX
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
}

// ─────────────────────────────────────────────────────────────
// Build per-brush RGBA vertex color array from atlas.
// Vertex order must match GenerateBrushModel exactly.
// Returns heap buffer (caller frees), or NULL if not available.
// ─────────────────────────────────────────────────────────────

static unsigned char *BuildBrushColors(int brushIdx, Vector3 scale) {
    if (!lightmapBaked || brushIdx >= sBakeCount || !sBakeAtlas.data) return NULL;

    LightmapBrushEntry *entry = &sBakeEntries[brushIdx];
    float sx = scale.x, sy = scale.y, sz = scale.z;
    float uLens[6] = { sx, sx, sz, sz, sx, sx };
    float vLens[6] = { sy, sy, sy, sy, sz, sz };

    int totalVerts = 0;
    for (int f = 0; f < 6; f++) {
        int su = (int)ceilf(uLens[f] / BRUSH_SUBDIV_SIZE); if (su < BRUSH_MIN_STEPS) su = BRUSH_MIN_STEPS;
        int sv = (int)ceilf(vLens[f] / BRUSH_SUBDIV_SIZE); if (sv < BRUSH_MIN_STEPS) sv = BRUSH_MIN_STEPS;
        totalVerts += (su + 1) * (sv + 1);
    }

    unsigned char *colors = (unsigned char *)MemAlloc(totalVerts * 4);
    int vOff = 0;

    for (int f = 0; f < 6; f++) {
        LightmapFaceRegion *reg = &entry->faces[f];
        int su = (int)ceilf(uLens[f] / BRUSH_SUBDIV_SIZE); if (su < BRUSH_MIN_STEPS) su = BRUSH_MIN_STEPS;
        int sv = (int)ceilf(vLens[f] / BRUSH_SUBDIV_SIZE); if (sv < BRUSH_MIN_STEPS) sv = BRUSH_MIN_STEPS;

        for (int row = 0; row <= sv; row++) {
            float tv = (float)row / sv;
            for (int col = 0; col <= su; col++) {
                float tu = (float)col / su;
                // v0 = top of atlas region, tv=0 = top of face row — no flip
                Color c = SampleAtlasCPU(
                    reg->u0 + tu * (reg->u1 - reg->u0),
                    reg->v0 + tv * (reg->v1 - reg->v0));
                colors[vOff*4+0] = c.r;
                colors[vOff*4+1] = c.g;
                colors[vOff*4+2] = c.b;
                colors[vOff*4+3] = 255;
                vOff++;
            }
        }
    }

    return colors;
}

// ─────────────────────────────────────────────────────────────
// Load lightmap — MUST be called before brushes are spawned.
// Fills sBakeAtlas + sBakeEntries for BuildBrushColors to consume.
// ─────────────────────────────────────────────────────────────

static void DoLoadLightmap(const char *levelPath) {
    lightmapBaked = false;
    sBakeCount    = 0;
    brushObjIndex = 0;

    if (sBakeAtlas.data) { UnloadImage(sBakeAtlas); sBakeAtlas = (Image){ 0 }; }

    char layoutPath[256];
    GetAtlasLayoutPath(levelPath, layoutPath, sizeof(layoutPath));

    FILE *f = fopen(layoutPath, "rb");
    if (!f) { TraceLog(LOG_WARNING, "No lightmap: %s", layoutPath); return; }

    int brushCount = 0;
    fread(&brushCount, sizeof(int), 1, f);
    if (brushCount <= 0 || brushCount > MAX_BRUSH_BAKE) {
        TraceLog(LOG_WARNING, "Bad lightmap brush count: %d", brushCount);
        fclose(f); return;
    }
    for (int i = 0; i < brushCount; i++)
        fread(&sBakeEntries[i], sizeof(LightmapBrushEntry), 1, f);
    fclose(f);

    char pngPath[256];
    GetAtlasPngPath(levelPath, pngPath, sizeof(pngPath));
    sBakeAtlas = LoadImage(pngPath);
    if (!sBakeAtlas.data) {
        TraceLog(LOG_WARNING, "Could not load lightmap PNG: %s", pngPath);
        return;
    }
    ImageFormat(&sBakeAtlas, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    sBakeCount    = brushCount;
    lightmapBaked = true;
    TraceLog(LOG_INFO, "Lightmap ready: %d brushes", brushCount);
}

// ─────────────────────────────────────────────────────────────
// World Objects
// ─────────────────────────────────────────────────────────────

void AddWorldObject(int modelIndex, Vector3 pos, Vector3 rotation, bool useMesh) {
    if (objectCount >= MAX_WORLD_OBJECTS) return;

    WorldObject *obj   = &levelObjects[objectCount];
    Model       *model = &modelCache[modelIndex].model;

    obj->modelId            = modelIndex;
    obj->pos                = pos;
    obj->rotation           = rotation;
    obj->useMeshCollision   = useMesh;
    obj->active             = true;
    obj->lightmapBrushIndex = -1;
    obj->brushScale         = (Vector3){ 0, 0, 0 };

    obj->transform = MatrixMultiply(
        MatrixRotateXYZ((Vector3){ rotation.x*DEG2RAD, rotation.y*DEG2RAD, rotation.z*DEG2RAD }),
        MatrixTranslate(pos.x, pos.y, pos.z));

    BoundingBox localBB = GetModelBoundingBox(*model);
    Vector3 corners[8] = {
        {localBB.min.x,localBB.min.y,localBB.min.z},{localBB.min.x,localBB.min.y,localBB.max.z},
        {localBB.min.x,localBB.max.y,localBB.min.z},{localBB.min.x,localBB.max.y,localBB.max.z},
        {localBB.max.x,localBB.min.y,localBB.min.z},{localBB.max.x,localBB.min.y,localBB.max.z},
        {localBB.max.x,localBB.max.y,localBB.min.z},{localBB.max.x,localBB.max.y,localBB.max.z}
    };
    Vector3 wMin = { 1e6f, 1e6f, 1e6f }, wMax = { -1e6f, -1e6f, -1e6f };
    for (int i = 0; i < 8; i++) {
        corners[i] = Vector3Transform(corners[i], obj->transform);
        if (corners[i].x < wMin.x) wMin.x = corners[i].x;
        if (corners[i].y < wMin.y) wMin.y = corners[i].y;
        if (corners[i].z < wMin.z) wMin.z = corners[i].z;
        if (corners[i].x > wMax.x) wMax.x = corners[i].x;
        if (corners[i].y > wMax.y) wMax.y = corners[i].y;
        if (corners[i].z > wMax.z) wMax.z = corners[i].z;
    }
    obj->bb     = (BoundingBox){ wMin, wMax };
    obj->center = (Vector3){ (wMin.x+wMax.x)*0.5f, (wMin.y+wMax.y)*0.5f, (wMin.z+wMax.z)*0.5f };
    obj->radius = Vector3Distance(obj->center, wMax);
    objectCount++;
}

// ─────────────────────────────────────────────────────────────
// Brush mesh generation
// ─────────────────────────────────────────────────────────────

static int EmitFace(
    float *vBuf, float *nBuf, float *uvBuf, unsigned short *iBuf, int baseVert,
    float ox, float oy, float oz,
    float uxX, float uxY, float uxZ,
    float vxX, float vxY, float vxZ,
    float uLen, float vLen, float nx, float ny, float nz, float texScale)
{
    int su = (int)ceilf(uLen/BRUSH_SUBDIV_SIZE); if (su < BRUSH_MIN_STEPS) su = BRUSH_MIN_STEPS;
    int sv = (int)ceilf(vLen/BRUSH_SUBDIV_SIZE); if (sv < BRUSH_MIN_STEPS) sv = BRUSH_MIN_STEPS;
    int vu = su + 1, vv = sv + 1;

    int vi = 0;
    for (int row = 0; row < vv; row++) {
        float tv = (float)row / sv;
        for (int col = 0; col < vu; col++) {
            float tu = (float)col / su;
            vBuf[vi*3+0] = ox + uxX*tu + vxX*tv;
            vBuf[vi*3+1] = oy + uxY*tu + vxY*tv;
            vBuf[vi*3+2] = oz + uxZ*tu + vxZ*tv;
            nBuf[vi*3+0] = nx; nBuf[vi*3+1] = ny; nBuf[vi*3+2] = nz;
            uvBuf[vi*2+0] = tu * uLen * texScale;
            uvBuf[vi*2+1] = (1.0f - tv) * vLen * texScale;
            vi++;
        }
    }
    int ii = 0;
    for (int row = 0; row < sv; row++)
        for (int col = 0; col < su; col++) {
            unsigned short tl = baseVert + row*vu + col;
            unsigned short tr = tl+1, bl = tl+vu, br = bl+1;
            iBuf[ii++]=tl; iBuf[ii++]=bl; iBuf[ii++]=tr;
            iBuf[ii++]=tr; iBuf[ii++]=bl; iBuf[ii++]=br;
        }
    return vu * vv;
}

// bakedColors: pre-built RGBA buffer (totalVerts*4), or NULL for white.
// Embedded into mesh.colors BEFORE UploadMesh so no UpdateMeshBuffer is needed —
// avoids relying on glBufferSubData which KOS does not support reliably.
static Model GenerateBrushModel(Vector3 scale, float texScale, const char *texturePath,
                                unsigned char *bakedColors)
{
    float hw = scale.x*0.5f, hh = scale.y*0.5f, hd = scale.z*0.5f;
    float sx = scale.x, sy = scale.y, sz = scale.z;
    float uLens[6] = { sx, sx, sz, sz, sx, sx };
    float vLens[6] = { sy, sy, sy, sy, sz, sz };

    int totalVerts = 0, totalTris = 0;
    for (int f = 0; f < 6; f++) {
        int su = (int)ceilf(uLens[f]/BRUSH_SUBDIV_SIZE); if (su<BRUSH_MIN_STEPS) su=BRUSH_MIN_STEPS;
        int sv = (int)ceilf(vLens[f]/BRUSH_SUBDIV_SIZE); if (sv<BRUSH_MIN_STEPS) sv=BRUSH_MIN_STEPS;
        totalVerts += (su+1)*(sv+1);
        totalTris  += su*sv*2;
    }

    float         *vBuf  = (float *)MemAlloc(totalVerts * 3 * sizeof(float));
    float         *nBuf  = (float *)MemAlloc(totalVerts * 3 * sizeof(float));
    float         *uvBuf = (float *)MemAlloc(totalVerts * 2 * sizeof(float));
    unsigned short *iBuf = (unsigned short *)MemAlloc(totalTris * 3 * sizeof(unsigned short));

    int vOff = 0, iOff = 0;
    #define EMIT(ox,oy,oz, uxX,uxY,uxZ, vxX,vxY,vxZ, uL,vL, nx,ny,nz) do { \
        int nv = EmitFace(vBuf+vOff*3, nBuf+vOff*3, uvBuf+vOff*2, iBuf+iOff, vOff, \
                          ox,oy,oz, uxX,uxY,uxZ, vxX,vxY,vxZ, uL,vL, nx,ny,nz, texScale); \
        int su=(int)ceilf((uL)/BRUSH_SUBDIV_SIZE); if(su<BRUSH_MIN_STEPS)su=BRUSH_MIN_STEPS; \
        int sv=(int)ceilf((vL)/BRUSH_SUBDIV_SIZE); if(sv<BRUSH_MIN_STEPS)sv=BRUSH_MIN_STEPS; \
        iOff+=su*sv*6; vOff+=nv; } while(0)

    EMIT(-hw,-hh, hd,  sx,0,0,  0,sy,0,  sx,sy,  0, 0, 1);
    EMIT( hw,-hh,-hd, -sx,0,0,  0,sy,0,  sx,sy,  0, 0,-1);
    EMIT(-hw,-hh,-hd,  0,0,sz,  0,sy,0,  sz,sy, -1, 0, 0);
    EMIT( hw,-hh, hd,  0,0,-sz, 0,sy,0,  sz,sy,  1, 0, 0);
    EMIT(-hw, hh, hd,  sx,0,0,  0,0,-sz, sx,sz,  0, 1, 0);
    EMIT(-hw,-hh,-hd,  sx,0,0,  0,0,sz,  sx,sz,  0,-1, 0);
    #undef EMIT

    unsigned char *cBuf = (unsigned char *)MemAlloc(totalVerts * 4);
    if (bakedColors) memcpy(cBuf, bakedColors, totalVerts * 4);
    else             memset(cBuf, 255,          totalVerts * 4);

    Mesh mesh = { 0 };
    mesh.vertexCount   = totalVerts;
    mesh.triangleCount = totalTris;
    mesh.vertices      = vBuf;
    mesh.normals       = nBuf;
    mesh.texcoords     = uvBuf;
    mesh.indices       = iBuf;
    mesh.colors        = cBuf;  // baked in before UploadMesh

    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);

    if (texturePath && texturePath[0] != '\0') {
        char fullTexPath[256];
        snprintf(fullTexPath, sizeof(fullTexPath), "%s", texturePath);
        Texture2D tex = GetOrLoadTexture(fullTexPath);
        if (tex.id != 0) model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    }
    if (lightShader.id != 0) model.materials[0].shader = lightShader;
    return model;
}

void AddBrushObject(Vector3 pos, Vector3 rotation, Vector3 scale,
                    float texScale, const char *texturePath)
{
    if (objectCount >= MAX_WORLD_OBJECTS || cachedModelCount >= MAX_WORLD_OBJECTS) return;

    // Sample lightmap colors for this brush from the pre-loaded atlas
    unsigned char *colors = BuildBrushColors(brushObjIndex, scale);

    Model brushModel = GenerateBrushModel(scale, texScale, texturePath, colors);
    if (colors) MemFree(colors);

    char key[64];
    snprintf(key, sizeof(key), "__brush_%d", objectCount);
    strncpy(modelCache[cachedModelCount].path, key, sizeof(modelCache[0].path) - 1);
    modelCache[cachedModelCount].model = brushModel;
    int mIdx = cachedModelCount++;

    AddWorldObject(mIdx, pos, rotation, true);
    levelObjects[objectCount - 1].lightmapBrushIndex = brushObjIndex;
    levelObjects[objectCount - 1].brushScale         = scale;
    brushObjIndex++;
}

// ─────────────────────────────────────────────────────────────
// Collision
// ─────────────────────────────────────────────────────────────

float GetGroundHeight(Vector3 playerPos) {
    if (!isLevelLoaded) return spawnPoint.y;

    float groundY = -1000.0f;
    float feetY   = playerPos.y - playerHeight;
    // Only accept surfaces within this distance below the player's feet.
    // This is the same step-up limit as the original AABB version:
    //   limit = feetY + 1.2  =>  surface must be <= 1.2 units above feet.
    // Expressed as a downward ray distance from playerPos.y:
    //   min ray dist = playerHeight - 1.2  (surface just above feet)
    //   max ray dist = playerHeight + 0.2  (just below feet, step-down)
    // Accept a surface as ground only if surfaceY <= feetY + 1.2.
    // This is identical to the original AABB limit — it allows step-ups
    // but rejects surfaces above the player's feet (ceiling, mid-air).
    float stepLimit = (framesSinceLoad < 60) ? feetY + 0.5f : feetY + 1.2f;

    Ray downRay = {
        .position  = { playerPos.x, playerPos.y, playerPos.z },
        .direction = { 0.0f, -1.0f, 0.0f }
    };

    for (int i = 0; i < objectCount; i++) {
        WorldObject *o = &levelObjects[i];
        if (!o->active) continue;

        // XZ pre-filter
        if (playerPos.x < o->bb.min.x - playerRadius ||
            playerPos.x > o->bb.max.x + playerRadius ||
            playerPos.z < o->bb.min.z - playerRadius ||
            playerPos.z > o->bb.max.z + playerRadius) continue;

        float surfaceY;

        if (o->useMeshCollision) {
            float bestDist = 1e6f;
            bool  found    = false;
            Model m = modelCache[o->modelId].model;
            for (int mm = 0; mm < m.meshCount; mm++) {
                RayCollision rc = GetRayCollisionMesh(downRay, m.meshes[mm], o->transform);
                if (rc.hit && rc.distance < bestDist) {
                    bestDist = rc.distance;
                    found    = true;
                }
            }
            if (!found) continue;
            surfaceY = playerPos.y - bestDist;
        } else {
            if (playerPos.x < o->bb.min.x || playerPos.x > o->bb.max.x ||
                playerPos.z < o->bb.min.z || playerPos.z > o->bb.max.z) continue;
            surfaceY = o->bb.max.y;
        }

        // Same limit as original: only ground if surface is at or below step threshold
        if (surfaceY > stepLimit) continue;
        if (surfaceY > groundY) groundY = surfaceY;
    }

    if (framesSinceLoad < 61) framesSinceLoad++;
    return (groundY == -1000.0f) ? feetY : groundY;
}

bool IsWallAt(Vector3 nextPos, Vector3 currentPos) {
    Vector3 moveVec  = Vector3Subtract(nextPos, currentPos);
    float   moveDist = Vector3Length(moveVec);
    if (moveDist < 0.001f) return false;
    Vector3 dir        = Vector3Normalize(moveVec);
    Vector3 orthoRight = Vector3Normalize(Vector3CrossProduct(dir, (Vector3){0,1,0}));
    Vector3 checkPos   = { nextPos.x, nextPos.y + (maxStepHeight + 0.2f), nextPos.z };
    float   searchDist = moveDist + playerRadius + 0.2f;
    for (int i = 0; i < objectCount; i++) {
        if (!levelObjects[i].active) continue;
        if (CheckCollisionBoxSphere(levelObjects[i].bb, checkPos, playerRadius)) {
            if (levelObjects[i].useMeshCollision) {
                Ray rays[3] = {
                    { Vector3Subtract(currentPos, Vector3Scale(dir, 0.1f)), dir },
                    { Vector3Add(rays[0].position, Vector3Scale(orthoRight, 0.2f)), dir },
                    { Vector3Subtract(rays[0].position, Vector3Scale(orthoRight, 0.2f)), dir }
                };
                Model m = modelCache[levelObjects[i].modelId].model;
                for (int r = 0; r < 3; r++)
                    for (int mm = 0; mm < m.meshCount; mm++) {
                        RayCollision hit = GetRayCollisionMesh(rays[r], m.meshes[mm], levelObjects[i].transform);
                        if (hit.hit && hit.distance <= searchDist) return true;
                    }
                continue;
            }
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// Draw World
// ─────────────────────────────────────────────────────────────

void DrawWorld(void) {
    ExtractFrustumPlanes();
    Matrix  invView = MatrixInvert(rlGetMatrixModelview());
    Vector3 camPos  = { invView.m12, invView.m13, invView.m14 };

    BeginShaderMode(lightShader);
    SetShaderValue(lightShader, viewPosLoc, &camPos, SHADER_UNIFORM_VEC3);

    if (lightmapBaked) {
        float white[4] = { 1,1,1,1 }; int zero = 0;
        SetShaderValue(lightShader, ambientLoc,    white, SHADER_UNIFORM_VEC4);
        SetShaderValue(lightShader, lightCountLoc, &zero, SHADER_UNIFORM_INT);
    } else {
        float amb[4] = { globalAmbientR, globalAmbientG, globalAmbientB, 1.0f };
        SetShaderValue(lightShader, ambientLoc,     amb,         SHADER_UNIFORM_VEC4);
        SetShaderValue(lightShader, lightCountLoc, &lightCount,  SHADER_UNIFORM_INT);
        for (int i = 0; i < lightCount; i++) {
            char pn[32], cn[32];
            sprintf(pn, "lightPositions[%i]", i); sprintf(cn, "lightColors[%i]", i);
            float pos[3] = { levelLights[i].pos.x, levelLights[i].pos.y, levelLights[i].pos.z };
            float col[4] = { levelLights[i].diffuse.r/255.0f, levelLights[i].diffuse.g/255.0f,
                             levelLights[i].diffuse.b/255.0f, 1.0f };
            SetShaderValue(lightShader, GetShaderLocation(lightShader, pn), pos, SHADER_UNIFORM_VEC3);
            SetShaderValue(lightShader, GetShaderLocation(lightShader, cn), col, SHADER_UNIFORM_VEC4);
        }
    }

    for (int i = 0; i < objectCount; i++) {
        if (!levelObjects[i].active) continue;
        if (!IsSphereInFrustum(levelObjects[i].center, levelObjects[i].radius)) continue;
        rlPushMatrix();
            rlMultMatrixf(MatrixToFloat(levelObjects[i].transform));
            DrawModel(modelCache[levelObjects[i].modelId].model, (Vector3){0,0,0}, 1.0f, WHITE);
        rlPopMatrix();
    }
    EndShaderMode();
}

// ─────────────────────────────────────────────────────────────
// Level Loading
// ─────────────────────────────────────────────────────────────

int GetOrLoadModel(const char *path) {
    for (int i = 0; i < cachedModelCount; i++)
        if (strcmp(modelCache[i].path, path) == 0) return i;
    if (cachedModelCount < MAX_WORLD_OBJECTS) {
        modelCache[cachedModelCount].model = LoadModel(path);
        strcpy(modelCache[cachedModelCount].path, path);
        for (int m = 0; m < modelCache[cachedModelCount].model.materialCount; m++)
            modelCache[cachedModelCount].model.materials[m].shader = lightShader;
        return cachedModelCount++;
    }
    return -1;
}

void LoadLevel(const char *fileName) {
    if (missingTex.id == 0) missingTex = LoadTexture(APPENDPATH("assets/tex/gravel.png"));

    isLevelLoaded   = false;
    objectCount     = 0;
    lightCount      = 0;
    framesSinceLoad = 0;
    spawnPoint      = (Vector3){ 0, 1.0f, 0 };

    if (lightShader.id == 0) {
        lightShader   = LoadShader("assets/shaders/lighting.vs", "assets/shaders/lighting.fs");
        lightCountLoc = GetShaderLocation(lightShader, "lightCount");
        ambientLoc    = GetShaderLocation(lightShader, "ambientColor");
        viewPosLoc    = GetShaderLocation(lightShader, "viewPos");
    }

    // Load lightmap atlas BEFORE spawning brushes so colors are available
    // during mesh generation — avoids needing glBufferSubData (unreliable on KOS).
    DoLoadLightmap(fileName);

    FILE *file = fopen(fileName, "r");
    if (!file) { TraceLog(LOG_ERROR, "Could not open level: %s", fileName); return; }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char type; if (sscanf(line, "%c", &type) != 1) continue;

        if (type == 'A') {
            float r, g, b; sscanf(line, "A %f %f %f", &r, &g, &b); SetGlobalAmbient(r, g, b);
        }
        else if (type == 'S') {
            sscanf(line, "S %f %f %f", &spawnPoint.x, &spawnPoint.y, &spawnPoint.z);
        }
        else if (type == 'O') {
            char modelPath[128]; Vector3 pos, rot; int meshFlag = 0;
            sscanf(line, "O %s %f %f %f %f %f %f %d",
                   modelPath, &pos.x, &pos.y, &pos.z, &rot.x, &rot.y, &rot.z, &meshFlag);
            char fullPath[256];
            snprintf(fullPath, sizeof(fullPath), "%s", modelPath);
            int mIdx = GetOrLoadModel(fullPath);
            if (mIdx != -1) AddWorldObject(mIdx, pos, rot, (meshFlag == 1));
        }
        else if (type == 'L') {
            Vector3 pos; int r, g, b;
            sscanf(line, "L %f %f %f %d %d %d", &pos.x, &pos.y, &pos.z, &r, &g, &b);
            AddWorldLight(pos, (Color){ r, g, b, 255 });
        }
        else if (type == 'B') {
            Vector3 pos, rot, scale; float texScale = 1.0f; char texPath[256] = { 0 };
            int parsed = sscanf(line, "B %f %f %f %f %f %f %f %f %f %f %255s",
                                &pos.x, &pos.y, &pos.z, &rot.x, &rot.y, &rot.z,
                                &scale.x, &scale.y, &scale.z, &texScale, texPath);
            if (parsed >= 9) {
                const char *tex = (parsed >= 11 && strcmp(texPath, "none") != 0) ? texPath : "";
                AddBrushObject(pos, rot, scale, texScale, tex);
            }
        }
    }
    fclose(file);

    // Atlas no longer needed — all brush colors are embedded in their VBOs
    if (sBakeAtlas.data) { UnloadImage(sBakeAtlas); sBakeAtlas = (Image){ 0 }; }

    isLevelLoaded = true;
}
