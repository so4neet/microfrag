#include <math.h>
#include <string.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include "weapon.h"
#include "game_state.h"
#include "../globals.h"
#include "../world/worldobject.h"
#include "../net/net_host.h"
#include "../net/net.h"
#include "tracer.h"
#include "../audio/audio.h"

// ─────────────────────────────────────────────────────────────
// Weapon definition table
// ─────────────────────────────────────────────────────────────

const WeaponDef weapons[WEAPON_COUNT] = {
    [WEAPON_SHOTGUN] = {
        .name         = "Shotgun",
        .shape        = WSHAPE_BOX,
        .bodyColor    = { 60,  60,  60,  255 },   // dark grey body
        .accentColor  = { 30,  30,  30,  255 },   // near-black barrel
        .bodyLength   = 0.28f,   // length along barrel axis
        .bodyWidth    = 0.04f,   // lateral thickness
        .bodyHeight   = 0.05f,   // vertical thickness
        .barrelLength = 0.12f,
        .barrelRadius = 0.014f,
        .fireRate     = 0.2f,
        .damage       = 8,
        .spread       = 0.01f,
        .pellets      = 8,
        .range        = 30.0f,
        .screenShake  = 0.12f,
        .recoilPitch  = 0.03f,
    },
    [WEAPON_ROCKET] = {
        .name         = "Rocket Launcher",
        .shape        = WSHAPE_TUBE,
        .bodyColor    = { 180, 60,  20,  255 },   // burnt orange tube
        .accentColor  = { 100, 30,  10,  255 },   // darker barrel mouth
        .bodyLength   = 0.32f,
        .bodyWidth    = 0.035f,  // tube radius
        .bodyHeight   = 0.035f,
        .barrelLength = 0.06f,
        .barrelRadius = 0.026f,
        .fireRate     = 0.9f,
        .damage       = 80,
        .spread       = 0.0f,
        .pellets      = 1,
        .range        = 200.0f,
        .screenShake  = 0.2f,
        .recoilPitch  = 0.05f,
    },
};

// ─────────────────────────────────────────────────────────────
// Procedural mesh helpers
// ─────────────────────────────────────────────────────────────

// Build a flat box mesh centered at origin, pointing along +Z.
// Width = X, height = Y, depth = Z (length).
static Mesh MakeBoxMesh(float w, float h, float d, Color col) {
    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;

    // 6 faces * 4 verts = 24 verts, 6 faces * 2 tris * 3 idx = 36 indices
    Mesh mesh = { 0 };
    mesh.vertexCount   = 24;
    mesh.triangleCount = 12;
    mesh.vertices  = (float *)MemAlloc(24 * 3 * sizeof(float));
    mesh.normals   = (float *)MemAlloc(24 * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(24 * 2 * sizeof(float));
    mesh.colors    = (unsigned char *)MemAlloc(24 * 4);
    mesh.indices   = (unsigned short *)MemAlloc(36 * sizeof(unsigned short));

    // face: origin, normal, u-axis, v-axis
    typedef struct { float ox,oy,oz, nx,ny,nz, ux,uy,uz, vx,vy,vz, us,vs; } Face;
    Face faces[6] = {
        { -hw,-hh, hd,  0, 0, 1,  1,0,0,  0,1,0,  w,h },  // front
        {  hw,-hh,-hd,  0, 0,-1, -1,0,0,  0,1,0,  w,h },  // back
        { -hw,-hh,-hd, -1, 0, 0,  0,0,1,  0,1,0,  d,h },  // left
        {  hw,-hh, hd,  1, 0, 0,  0,0,-1, 0,1,0,  d,h },  // right
        { -hw, hh,-hd,  0, 1, 0,  1,0,0,  0,0,1,  w,d },  // top
        { -hw,-hh, hd,  0,-1, 0,  1,0,0,  0,0,-1, w,d },  // bottom
    };

    int vi = 0, ii = 0;
    for (int f = 0; f < 6; f++) {
        Face *fc = &faces[f];
        float corners[4][3] = {
            { fc->ox,                   fc->oy,                   fc->oz                   },
            { fc->ox+fc->ux*fc->us,     fc->oy+fc->uy*fc->us,     fc->oz+fc->uz*fc->us     },
            { fc->ox+fc->ux*fc->us+fc->vx*fc->vs,
              fc->oy+fc->uy*fc->us+fc->vy*fc->vs,
              fc->oz+fc->uz*fc->us+fc->vz*fc->vs },
            { fc->ox+fc->vx*fc->vs,     fc->oy+fc->vy*fc->vs,     fc->oz+fc->vz*fc->vs     },
        };
        float uvs[4][2] = { {0,0},{1,0},{1,1},{0,1} };
        for (int v = 0; v < 4; v++) {
            mesh.vertices[vi*3+0] = corners[v][0];
            mesh.vertices[vi*3+1] = corners[v][1];
            mesh.vertices[vi*3+2] = corners[v][2];
            mesh.normals [vi*3+0] = fc->nx;
            mesh.normals [vi*3+1] = fc->ny;
            mesh.normals [vi*3+2] = fc->nz;
            mesh.texcoords[vi*2+0] = uvs[v][0];
            mesh.texcoords[vi*2+1] = uvs[v][1];
            mesh.colors[vi*4+0] = col.r;
            mesh.colors[vi*4+1] = col.g;
            mesh.colors[vi*4+2] = col.b;
            mesh.colors[vi*4+3] = col.a;
            vi++;
        }
        int base = f * 4;
        mesh.indices[ii++] = base+0; mesh.indices[ii++] = base+1; mesh.indices[ii++] = base+2;
        mesh.indices[ii++] = base+0; mesh.indices[ii++] = base+2; mesh.indices[ii++] = base+3;
    }

    UploadMesh(&mesh, false);
    return mesh;
}

// Build a cylinder mesh along +Z axis, centered at origin.
static Mesh MakeCylinderMesh(float radius, float length, int slices, Color col) {
    // Exact counts — written out explicitly to avoid allocation bugs:
    //   Side:  2 rings * (slices+1) verts,  slices*2 tris
    //   Caps:  2 caps * (1 center + slices+1 ring) verts,  2*slices tris
    int sideVerts  = 2 * (slices + 1);
    int capVerts   = 2 * (1 + slices + 1);   // center + ring (slices+1) per cap
    int totalVerts = sideVerts + capVerts;

    int sideTris   = slices * 2;
    int capTris    = slices * 2;
    int totalTris  = sideTris + capTris;
    int totalIdx   = totalTris * 3;

    Mesh mesh = { 0 };
    mesh.vertexCount   = totalVerts;
    mesh.triangleCount = totalTris;
    mesh.vertices  = (float *)MemAlloc(totalVerts * 3 * sizeof(float));
    mesh.normals   = (float *)MemAlloc(totalVerts * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(totalVerts * 2 * sizeof(float));
    mesh.colors    = (unsigned char *)MemAlloc(totalVerts * 4);
    mesh.indices   = (unsigned short *)MemAlloc(totalIdx  * sizeof(unsigned short));

    float hz = length * 0.5f;
    int   vi = 0, ii = 0;

    // ── Side rings ────────────────────────────────────────────
    int vertsPerRing = slices + 1;
    for (int r = 0; r < 2; r++) {
        float z = (r == 0) ? -hz : hz;
        for (int s = 0; s <= slices; s++) {
            float angle = (float)s / slices * 2.0f * PI;
            float nx = cosf(angle), ny = sinf(angle);
            mesh.vertices[vi*3+0] = nx * radius;
            mesh.vertices[vi*3+1] = ny * radius;
            mesh.vertices[vi*3+2] = z;
            mesh.normals [vi*3+0] = nx;
            mesh.normals [vi*3+1] = ny;
            mesh.normals [vi*3+2] = 0.0f;
            mesh.texcoords[vi*2+0] = (float)s / slices;
            mesh.texcoords[vi*2+1] = (float)r;
            mesh.colors[vi*4+0]=col.r; mesh.colors[vi*4+1]=col.g;
            mesh.colors[vi*4+2]=col.b; mesh.colors[vi*4+3]=col.a;
            vi++;
        }
    }
    // Side indices
    for (int s = 0; s < slices; s++) {
        unsigned short a = (unsigned short)s;
        unsigned short b = (unsigned short)(s + 1);
        unsigned short c = (unsigned short)(vertsPerRing + s);
        unsigned short d = (unsigned short)(vertsPerRing + s + 1);
        mesh.indices[ii++]=a; mesh.indices[ii++]=c; mesh.indices[ii++]=b;
        mesh.indices[ii++]=b; mesh.indices[ii++]=c; mesh.indices[ii++]=d;
    }

    // ── End caps ──────────────────────────────────────────────
    for (int cap = 0; cap < 2; cap++) {
        float z  = (cap == 0) ? -hz : hz;
        float nz = (cap == 0) ? -1.0f : 1.0f;
        int capBase = vi;

        // Center vertex
        mesh.vertices[vi*3+0] = 0.0f;
        mesh.vertices[vi*3+1] = 0.0f;
        mesh.vertices[vi*3+2] = z;
        mesh.normals [vi*3+0] = 0.0f;
        mesh.normals [vi*3+1] = 0.0f;
        mesh.normals [vi*3+2] = nz;
        mesh.texcoords[vi*2+0] = 0.5f;
        mesh.texcoords[vi*2+1] = 0.5f;
        mesh.colors[vi*4+0]=col.r; mesh.colors[vi*4+1]=col.g;
        mesh.colors[vi*4+2]=col.b; mesh.colors[vi*4+3]=col.a;
        vi++;

        // Ring verts (slices+1 so first==last seam closes cleanly)
        for (int s = 0; s <= slices; s++) {
            float angle = (float)s / slices * 2.0f * PI;
            mesh.vertices[vi*3+0] = cosf(angle) * radius;
            mesh.vertices[vi*3+1] = sinf(angle) * radius;
            mesh.vertices[vi*3+2] = z;
            mesh.normals [vi*3+0] = 0.0f;
            mesh.normals [vi*3+1] = 0.0f;
            mesh.normals [vi*3+2] = nz;
            mesh.texcoords[vi*2+0] = 0.5f + cosf(angle) * 0.5f;
            mesh.texcoords[vi*2+1] = 0.5f + sinf(angle) * 0.5f;
            mesh.colors[vi*4+0]=col.r; mesh.colors[vi*4+1]=col.g;
            mesh.colors[vi*4+2]=col.b; mesh.colors[vi*4+3]=col.a;
            vi++;
        }

        // Fan indices — slices tris per cap
        for (int s = 0; s < slices; s++) {
            unsigned short ctr  = (unsigned short)capBase;
            unsigned short cur  = (unsigned short)(capBase + 1 + s);
            unsigned short next = (unsigned short)(capBase + 2 + s);
            if (cap == 0) {
                mesh.indices[ii++]=ctr; mesh.indices[ii++]=next; mesh.indices[ii++]=cur;
            } else {
                mesh.indices[ii++]=ctr; mesh.indices[ii++]=cur;  mesh.indices[ii++]=next;
            }
        }
    }

    UploadMesh(&mesh, false);
    return mesh;
}

// Combine two meshes into one Model (body + barrel).
// Both meshes are already uploaded; we store them as separate meshes in the Model
// so they can have different colours baked into vertex colors.
static Model MakeCombinedModel(Mesh body, Mesh barrel) {
    Model m = { 0 };
    m.meshCount     = 2;
    m.materialCount = 1;
    m.meshes        = (Mesh *)MemAlloc(2 * sizeof(Mesh));
    m.meshMaterial  = (int  *)MemAlloc(2 * sizeof(int));
    m.materials     = (Material *)MemAlloc(1 * sizeof(Material));
    m.meshes[0]     = body;
    m.meshes[1]     = barrel;
    m.meshMaterial[0] = 0;
    m.meshMaterial[1] = 0;
    m.materials[0]  = LoadMaterialDefault();
    m.transform     = MatrixIdentity();
    return m;
}

// Build view + world models for one weapon definition.
// The view model is scaled down slightly and offset so it looks right in first-person.
static void BuildModels(const WeaponDef *wep, Model *outView, Model *outWorld) {
    Mesh body, barrel;

    if (wep->shape == WSHAPE_BOX) {
        body   = MakeBoxMesh(wep->bodyLength, wep->bodyHeight, wep->bodyWidth,
                             wep->bodyColor);
        barrel = MakeCylinderMesh(wep->barrelRadius, wep->barrelLength, 8,
                                  wep->accentColor);
    } else {
        // WSHAPE_TUBE — body is also a cylinder
        body   = MakeCylinderMesh(wep->bodyWidth, wep->bodyLength, 10,
                                  wep->bodyColor);
        barrel = MakeCylinderMesh(wep->barrelRadius, wep->barrelLength, 10,
                                  wep->accentColor);
    }

    *outView  = MakeCombinedModel(body, barrel);
    // World model reuses the same meshes (same GPU buffers, different Model shell)
    // We make a second set of meshes for the world model so transforms are independent.
    Mesh body2, barrel2;
    if (wep->shape == WSHAPE_BOX) {
        body2   = MakeBoxMesh(wep->bodyLength, wep->bodyHeight, wep->bodyWidth,
                              wep->bodyColor);
        barrel2 = MakeCylinderMesh(wep->barrelRadius, wep->barrelLength, 8,
                                   wep->accentColor);
    } else {
        body2   = MakeCylinderMesh(wep->bodyWidth, wep->bodyLength, 10,
                                   wep->bodyColor);
        barrel2 = MakeCylinderMesh(wep->barrelRadius, wep->barrelLength, 10,
                                   wep->accentColor);
    }
    *outWorld = MakeCombinedModel(body2, barrel2);
}

// ─────────────────────────────────────────────────────────────
// Model storage
// ─────────────────────────────────────────────────────────────

static Model sViewModels [WEAPON_COUNT];
static Model sWorldModels[WEAPON_COUNT];

void WeaponsInit(void) {
    for (int i = 0; i < WEAPON_COUNT; i++)
        BuildModels(&weapons[i], &sViewModels[i], &sWorldModels[i]);
}

void WeaponsShutdown(void) {
    for (int i = 0; i < WEAPON_COUNT; i++) {
        UnloadModel(sViewModels[i]);
        UnloadModel(sWorldModels[i]);
    }
}

Model *WeaponGetViewModel(uint8_t id) {
    if (id >= WEAPON_COUNT) return &sViewModels[0];
    return &sViewModels[id];
}

Model *WeaponGetWorldModel(uint8_t id) {
    if (id >= WEAPON_COUNT) return &sWorldModels[0];
    return &sWorldModels[id];
}

// ─────────────────────────────────────────────────────────────
// Cycle
// ─────────────────────────────────────────────────────────────

void WeaponCycleNext(int slot) {
    PlayerState *p = &gameState.players[slot];
    p->weapon = (p->weapon + 1) % WEAPON_COUNT;
    p->fireCooldown = 0.0f;
}

void WeaponCyclePrev(int slot) {
    PlayerState *p = &gameState.players[slot];
    p->weapon = (p->weapon + WEAPON_COUNT - 1) % WEAPON_COUNT;
    p->fireCooldown = 0.0f;
}

// ─────────────────────────────────────────────────────────────
// Hitscan helpers
// ─────────────────────────────────────────────────────────────

static float RandF(void) {
    static unsigned int seed = 12345;
    seed = seed * 1664525u + 1013904223u;
    return ((float)(seed & 0xFFFF) / 32767.5f) - 1.0f;
}

// ─────────────────────────────────────────────────────────────
// WeaponTickCooldown — call once per frame for each active player
// ─────────────────────────────────────────────────────────────

void WeaponTickCooldown(int slot, float dt) {
    PlayerState *p = &gameState.players[slot];
    if (p->fireCooldown > 0.0f) {
        p->fireCooldown -= dt;
        if (p->fireCooldown < 0.0f) p->fireCooldown = 0.0f;
    }
}

// ─────────────────────────────────────────────────────────────
// WeaponTryFire
// ─────────────────────────────────────────────────────────────

bool WeaponTryFire(int shooterSlot, float dt) {
    PlayerState *shooter = &gameState.players[shooterSlot];
    if (!shooter->active) return false;

    if (shooter->fireCooldown > 0.0f) return false;

    const WeaponDef *wep = &weapons[shooter->weapon];
    Vector3 origin = shooter->pos;

    float cp = cosf(shooter->pitch);
    Vector3 aimDir = {
        sinf(shooter->yaw) * cp,
       -sinf(shooter->pitch),
        cosf(shooter->yaw) * cp,
    };

    Vector3 right = { cosf(shooter->yaw), 0.0f, -sinf(shooter->yaw) };
    Vector3 up    = Vector3Normalize(Vector3CrossProduct(right, aimDir));

    float   closestDist = wep->range;
    int     hitSlot     = -1;
    Vector3 hitPoint    = Vector3Add(origin, Vector3Scale(aimDir, wep->range));

    // Muzzle position — slightly in front of the eye so tracer starts at the gun
    Vector3 muzzle = Vector3Add(origin, Vector3Scale(aimDir, 0.4f));

    for (int pellet = 0; pellet < wep->pellets; pellet++) {
        Vector3 dir = aimDir;
        if (wep->spread > 0.0f) {
            float sr = RandF() * wep->spread;
            float su = RandF() * wep->spread;
            dir = Vector3Normalize(Vector3Add(dir,
                    Vector3Add(Vector3Scale(right, sr),
                               Vector3Scale(up,    su))));
        }

        Ray ray = { origin, dir };
        float   pelletDist = wep->range;
        int     pelletHit  = -1;
        Vector3 pelletEnd  = Vector3Add(origin, Vector3Scale(dir, wep->range));

        for (int s = 0; s < MAX_PLAYERS; s++) {
            if (s == shooterSlot) continue;
            PlayerState *target = &gameState.players[s];
            if (!target->active) continue;
            float cr = 0.3f;
            BoundingBox bb = {
                { target->pos.x - cr, target->pos.y - playerHeight, target->pos.z - cr },
                { target->pos.x + cr, target->pos.y + cr,           target->pos.z + cr },
            };
            RayCollision col = GetRayCollisionBox(ray, bb);
            if (col.hit && col.distance < pelletDist) {
                pelletDist = col.distance;
                pelletHit  = s;
                pelletEnd  = col.point;
                // Track overall closest for damage
                if (col.distance < closestDist) {
                    closestDist = col.distance;
                    hitSlot     = s;
                    hitPoint    = col.point;
                }
            }
        }

        // Spawn one tracer per pellet on the host
        TracerSpawn(muzzle, pelletEnd, pelletHit >= 0);

        // Broadcast tracer to clients
        HitMsg hit = {
            .type    = MSG_HIT,
            .shooter = (uint8_t)shooterSlot,
            .ox = muzzle.x,   .oy = muzzle.y,   .oz = muzzle.z,
            .hx = pelletEnd.x,.hy = pelletEnd.y, .hz = pelletEnd.z,
            .hit = (pelletHit >= 0) ? 1 : 0,
        };
        HostBroadcastRaw(&hit, sizeof(hit), CHAN_RELIABLE);
    }

    if (hitSlot >= 0) {
        PlayerState *target = &gameState.players[hitSlot];
        target->health -= wep->damage;
        if (target->health <= 0) {
            target->health      = 0;
            target->active      = false;
            target->respawnTimer = 3.0f;
            HostBroadcastEvent(EVENT_FRAG, (uint8_t)shooterSlot, (uint8_t)hitSlot, 0);
            AudioPlayDeath(hitSlot == 0);
        } else {
            HostBroadcastEvent(EVENT_DAMAGE, (uint8_t)shooterSlot, (uint8_t)hitSlot,
                               (int16_t)wep->damage);
        }
    }

    shooter->fireCooldown = wep->fireRate;
    return true;
}
