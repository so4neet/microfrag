#include <raylib.h>
#include <raymath.h>
#include "../globals.h"

typedef struct {
    int modelId;
    Vector3 pos;
    Vector3 rotation;
    Vector3 center;
    float radius;
    BoundingBox bb;
    Matrix transform;
    bool useMeshCollision;
    bool active;
    int lightmapBrushIndex;  // index into .lmap entries, -1 for non-brush objects
    Vector3 brushScale;      // only valid when lightmapBrushIndex >= 0
} __attribute__((aligned(16))) WorldObject;

typedef struct WorldLight {
    int id;
    Vector3 pos;
    Color diffuse;
    bool active;
} WorldLight;

typedef struct LoadedModel {
    char path[64];
    Model model;
} LoadedModel;

#define MAX_WORLD_LIGHTS    32
#define MAX_WORLD_OBJECTS   128
extern WorldObject levelObjects[MAX_WORLD_OBJECTS];
extern int objectCount;
extern Vector3 spawnPoint;
extern bool isLevelLoaded;

void AddWorldObject(int modelIndex, Vector3 pos, Vector3 rotation, bool useMesh);
void AddBrushObject(Vector3 pos, Vector3 rotation, Vector3 scale, float texScale, const char *texturePath);
void AddWorldLight(Vector3 pos, Color color);
void LoadLevel(const char *fileName);
void DrawWorld();
bool IsWallAt(Vector3 nextPos, Vector3 currentPos);
float GetGroundHeight(Vector3 playerPos);
int GetOrLoadModel(const char *path);
