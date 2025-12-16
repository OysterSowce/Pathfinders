// Pathfinders_v6_10_unified.cpp
// hit-zones and damage rig
// last build was faction weapons etc//

#include <SDL.h>
#include <SDL_ttf.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <unordered_map>
#include <stack>
#include <algorithm>
#include <random>
#include <chrono>
#include <array>
#include <cstdint>


// -----------------------------------------------------------
// Config
// -----------------------------------------------------------

namespace cfg {
    constexpr int ScreenW = 1280;
    constexpr int ScreenH = 720;

    constexpr int TileSize = 32;
    constexpr int MapCols = 80;
    constexpr int MapRows = 80;

    constexpr float ZoomPlayer = 1.5f;
    constexpr float ZoomSandbox = 0.8f;

    // Pawn size (new)
    constexpr float PawnSize = 14.0f;

    constexpr float PlayerWalkSpeed = 60.0f; // was faster //
    constexpr float PlayerSprintSpeed = 100.0f; // was faster //
    constexpr float PlayerSneakSpeed = 30.0f; // new: sneak = slow & quiet //

    constexpr float BulletSpeed = 520.0f;
    constexpr float BulletMaxRange = 800.0f;

    // AI movement
    constexpr float AIWalkSpeed = 40.0f;
    constexpr float AISprintSpeed = 80.0f;
    
    constexpr float GunshotHearTiles = 18.0f;
    constexpr float FootstepHearWalkTiles = 2.0f;
    constexpr float FootstepHearSprintTiles = 5.0f;
    constexpr float HearDecayS = 3.0f;

    constexpr float TreeFoliageAlpha = 0.4f;

    constexpr float TrunkMin = 6.0f;
    constexpr float TrunkMax = 14.0f;
    constexpr float FoliageRadiusPx = 40.0f;
    constexpr int   FoliagePerTrunk = 12;
    constexpr float FoliageMinSize = 14.0f;
    constexpr float FoliageMaxSize = 26.0f;

    constexpr float StandoffRange = 180.0f;

    // Colors
    constexpr SDL_Color ColBg{ 5,  10,  16, 255 };
    constexpr SDL_Color ColLand{ 40, 55,  40, 255 };
    constexpr SDL_Color ColWater{ 20, 40,  80, 255 };
    constexpr SDL_Color ColWall{ 80, 80,  90, 255 };
    constexpr SDL_Color ColLeaf{ 35, 80,  35, 255 };
    constexpr SDL_Color ColTrunk{ 60, 40,  25, 255 };
    constexpr SDL_Color ColLine{ 250, 250, 250, 255 };
    constexpr SDL_Color ColCorpse{ 80,  0,   0, 255 };
    constexpr SDL_Color ColBullet{ 250, 230, 180, 255 };
    constexpr SDL_Color ColPing{ 200, 200, 255, 160 };

    constexpr SDL_Color ColUI{ 230, 230, 230, 255 };
    constexpr SDL_Color ColUIAlt{ 180, 220, 255, 255 };
    constexpr SDL_Color ColSelect{ 250, 230, 120, 255 };
}

// -----------------------------------------------------------
// RNG helpers
// -----------------------------------------------------------

static std::mt19937& rng() {
    static std::mt19937 g(
        (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return g;
}

static int irand(int a, int b) {
    std::uniform_int_distribution<int> d(a, b);
    return d(rng());
}
static float frand(float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(rng());
}

// -----------------------------------------------------------
// Weapons (Phase W1 - Data model + table)
// -----------------------------------------------------------

enum class WeaponTier : uint8_t {
    Basic = 0,
    Intermediate,
    Advanced
};

enum class AmmoType : uint8_t {
    None = 0,
    ACP_45,
    PARA_9MM,
    TOK_762x25,
    CARB_30,
    NATO_556,
    NATO_762,
    RUS_762x39,
    RUS_762x54R,
    SHOT_12G,
    RIM_22
};

enum class WeaponId : uint16_t {
    None = 0,

    // Pistols
    M1911,
    LUGER_P08,
    TOKAREV_TT33,
    WELROD,

    // SMG
    STEN,
    MP40,
    THOMPSON,
    PPSH41,
    SUOMI,

    // Rifles / Carbines
    M1_CARBINE,
    KAR98K,
    MOSIN,
    SMLE,
    GARAND,
    SVT40,

    // Assault-ish / battle rifles (for later eras)
    AK47,
    FAL,

    // Shotguns
    DB_SHOTGUN,
    PUMP_SHOTGUN,

    // LMG
    DP27,
    BREN,
    MG34
};

static inline const char* tierName(WeaponTier t) {
    switch (t) {
    case WeaponTier::Basic: return "Basic";
    case WeaponTier::Intermediate: return "Intermediate";
    case WeaponTier::Advanced: return "Advanced";
    default: return "Unknown";
    }
}

static inline const char* ammoName(AmmoType a) {
    switch (a) {
    case AmmoType::ACP_45:      return ".45 ACP";
    case AmmoType::PARA_9MM:    return "9mm";
    case AmmoType::TOK_762x25:  return "7.62x25";
    case AmmoType::CARB_30:     return ".30 Carbine";
    case AmmoType::NATO_556:    return "5.56";
    case AmmoType::NATO_762:    return "7.62";
    case AmmoType::RUS_762x39:  return "7.62x39";
    case AmmoType::RUS_762x54R: return "7.62x54R";
    case AmmoType::SHOT_12G:    return "12G";
    case AmmoType::RIM_22:      return ".22";
    default:                    return "None";
    }
}

struct WeaponDef {
    WeaponId   id = WeaponId::None;
    const char* name = "None";
    WeaponTier tier = WeaponTier::Basic;
    AmmoType   ammo = AmmoType::None;

    int   magSize = 0;
    int   reserveDefault = 0;

    float fireCooldownS = 0.25f;   // time between shots
    float reloadTimeS = 1.2f;

    float baseDamage = 10.0f;      // pre-hit-zone multiplier
    float spreadDeg = 2.0f;        // random cone
    int   pellets = 1;             // shotgun style

    float projectileSpeed = 520.0f;
    float maxRange = 800.0f;
};

// runtime mutable weapon state living on an Actor
struct WeaponInstance {
    WeaponId id = WeaponId::None;

    int magAmmo = 0;
    int reserveAmmo = 0;

    float fireTimer = 0.0f;
    bool  reloading = false;
    float reloadTimer = 0.0f;

    void clearTimers() {
        fireTimer = 0.0f;
        reloading = false;
        reloadTimer = 0.0f;
    }
};

static constexpr WeaponDef kWeaponTable[] = {
    // id, name, tier, ammo, mag, reserve, fireCD, reload, dmg, spread, pellets, speed, range

    { WeaponId::None, "Unarmed", WeaponTier::Basic, AmmoType::None,
      0, 0, 0.25f, 1.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f },

      // Pistols
      { WeaponId::M1911, "M1911", WeaponTier::Basic, AmmoType::ACP_45,
        7, 21, 0.20f, 1.25f, 18.0f, 4.0f, 1, 560.0f, 520.0f },

      { WeaponId::LUGER_P08, "Luger P08", WeaponTier::Basic, AmmoType::PARA_9MM,
        8, 24, 0.18f, 1.20f, 16.0f, 4.5f, 1, 600.0f, 520.0f },

      { WeaponId::TOKAREV_TT33, "TT-33", WeaponTier::Basic, AmmoType::TOK_762x25,
        8, 24, 0.17f, 1.15f, 15.0f, 4.5f, 1, 620.0f, 520.0f },

      { WeaponId::WELROD, "Welrod (Silenced)", WeaponTier::Intermediate, AmmoType::PARA_9MM,
        6, 18, 0.35f, 1.40f, 20.0f, 3.5f, 1, 520.0f, 560.0f },

        // SMG
        { WeaponId::STEN, "Sten", WeaponTier::Basic, AmmoType::PARA_9MM,
          32, 96, 0.08f, 1.60f, 12.0f, 7.5f, 1, 560.0f, 520.0f },

        { WeaponId::MP40, "MP40", WeaponTier::Intermediate, AmmoType::PARA_9MM,
          32, 96, 0.075f, 1.55f, 12.0f, 6.5f, 1, 580.0f, 540.0f },

        { WeaponId::THOMPSON, "Thompson", WeaponTier::Intermediate, AmmoType::ACP_45,
          30, 90, 0.075f, 1.70f, 13.0f, 6.0f, 1, 560.0f, 520.0f },

        { WeaponId::PPSH41, "PPSh-41", WeaponTier::Intermediate, AmmoType::TOK_762x25,
          35, 105, 0.060f, 1.80f, 11.0f, 8.0f, 1, 620.0f, 520.0f },

        { WeaponId::SUOMI, "Suomi", WeaponTier::Advanced, AmmoType::PARA_9MM,
          36, 108, 0.065f, 1.75f, 12.0f, 6.0f, 1, 600.0f, 540.0f },

          // Rifles / Carbines
          { WeaponId::M1_CARBINE, "M1 Carbine", WeaponTier::Basic, AmmoType::CARB_30,
            15, 60, 0.14f, 1.55f, 15.0f, 3.5f, 1, 780.0f, 820.0f },

          { WeaponId::KAR98K, "Kar98k", WeaponTier::Basic, AmmoType::NATO_762,
            5, 25, 0.55f, 2.10f, 42.0f, 2.0f, 1, 980.0f, 1100.0f },

          { WeaponId::MOSIN, "Mosin-Nagant", WeaponTier::Basic, AmmoType::RUS_762x54R,
            5, 25, 0.58f, 2.10f, 44.0f, 2.2f, 1, 980.0f, 1150.0f },

          { WeaponId::SMLE, "Lee-Enfield", WeaponTier::Intermediate, AmmoType::NATO_762,
            10, 40, 0.45f, 2.05f, 40.0f, 2.2f, 1, 980.0f, 1100.0f },

          { WeaponId::GARAND, "M1 Garand", WeaponTier::Advanced, AmmoType::NATO_762,
            8, 32, 0.30f, 2.10f, 38.0f, 2.6f, 1, 980.0f, 1050.0f },

          { WeaponId::SVT40, "SVT-40", WeaponTier::Advanced, AmmoType::RUS_762x54R,
            10, 40, 0.30f, 2.15f, 36.0f, 2.8f, 1, 980.0f, 1050.0f },

            // Assault-ish (later-era feel knobs)
            { WeaponId::AK47, "AK-47", WeaponTier::Advanced, AmmoType::RUS_762x39,
              30, 90, 0.10f, 1.85f, 20.0f, 4.5f, 1, 820.0f, 900.0f },

            { WeaponId::FAL, "FAL", WeaponTier::Advanced, AmmoType::NATO_762,
              20, 80, 0.12f, 1.95f, 26.0f, 4.0f, 1, 900.0f, 980.0f },

              // Shotguns
              { WeaponId::DB_SHOTGUN, "Double Barrel", WeaponTier::Basic, AmmoType::SHOT_12G,
                2, 20, 0.75f, 2.40f, 10.0f, 10.0f, 8, 520.0f, 420.0f },

              { WeaponId::PUMP_SHOTGUN, "Pump Shotgun", WeaponTier::Intermediate, AmmoType::SHOT_12G,
                6, 24, 0.55f, 2.60f, 10.0f, 9.0f, 8, 520.0f, 460.0f },

                // LMG
                { WeaponId::DP27, "DP-27", WeaponTier::Intermediate, AmmoType::RUS_762x54R,
                  47, 141, 0.11f, 2.35f, 18.0f, 6.0f, 1, 880.0f, 950.0f },

                { WeaponId::BREN, "Bren", WeaponTier::Advanced, AmmoType::NATO_762,
                  30, 120, 0.11f, 2.25f, 18.0f, 5.5f, 1, 900.0f, 980.0f },

                { WeaponId::MG34, "MG34", WeaponTier::Advanced, AmmoType::NATO_762,
                  50, 150, 0.085f, 2.60f, 18.0f, 7.0f, 1, 920.0f, 980.0f },
};

static inline const WeaponDef& weaponDef(WeaponId id) {
    const int n = (int)(sizeof(kWeaponTable) / sizeof(kWeaponTable[0]));
    for (int i = 0; i < n; ++i) {
        if (kWeaponTable[i].id == id) return kWeaponTable[i];
    }
    return kWeaponTable[0]; // Unarmed fallback
}

static inline const char* weaponName(WeaponId id) {
    return weaponDef(id).name;
}



// -----------------------------------------------------------
// Math helpers
// -----------------------------------------------------------

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float X, float Y) : x(X), y(Y) {}
};

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return { a.x + b.x, a.y + b.y }; }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return { a.x - b.x, a.y - b.y }; }
inline Vec2 operator*(const Vec2& a, float s) { return { a.x * s, a.y * s }; }
inline Vec2 operator*(float s, const Vec2& a) { return { a.x * s, a.y * s }; }

inline float lenSq(const Vec2& v) { return v.x * v.x + v.y * v.y; }
inline float length(const Vec2& v) { return std::sqrt(lenSq(v)); }

inline Vec2 normalize(const Vec2& v) {
    float L = length(v);
    if (L < 1e-4f) return Vec2(1, 0);
    return Vec2(v.x / L, v.y / L);
}

inline float deg2rad(float d) { return d * 3.14159265f / 180.0f; }

// -----------------------------------------------------------
// Hit zones + HitBoxes (Phase W5 Option A - proper rectangles)
// -----------------------------------------------------------
enum class HitZone : uint8_t { Head = 0, Torso, Legs, ArmL, ArmR };

static inline const char* hitZoneName(HitZone z) {
    switch (z) {
    case HitZone::Head:  return "Head";
    case HitZone::Torso: return "Torso";
    case HitZone::Legs:  return "Legs";
    case HitZone::ArmL:  return "L Arm";
    case HitZone::ArmR:  return "R Arm";
    default:             return "?";
    }
}

static inline Vec2 perpRight(const Vec2& fwd) { return Vec2(-fwd.y, fwd.x); }
static inline float dot2(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

struct HitBox {
    HitZone zone = HitZone::Torso;
    // Local space in (forward,right) axes where:
    // local.x = dot(rel, forward), local.y = dot(rel, right)
    float x = 0, y = 0, w = 0, h = 0; // rect min(x,y), size(w,h)
};

static inline bool pointInRect(float px, float py, const HitBox& r) {
    return (px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h);
}

static inline float zoneMultiplier(HitZone z) {
    switch (z) {
    case HitZone::Head:  return 1.8f;
    case HitZone::Torso: return 0.50f;
    case HitZone::Legs:  return 0.35f;
    case HitZone::ArmL:  return 0.40f;
    case HitZone::ArmR:  return 0.40f;
    default:             return 1.0f;
    }
}

// Optional weapon “flavour” hook (disabling vs lethal)
static inline float weaponZoneBias(WeaponId wid, HitZone z) {
    if (wid == WeaponId::M1_CARBINE) {
        if (z == HitZone::Head) return 0.90f;
        if (z == HitZone::Legs) return 1.15f;
    }
    return 1.0f;
}

// Forward decls (implemented after Actor exists)
static void buildDefaultHitRig(struct Actor& a);
static bool resolveHitZone(const struct Actor& a, const Vec2& hitPos, HitZone& outZone);


// -----------------------------------------------------------
// Enums & small helpers
// -----------------------------------------------------------

enum class Tile : uint8_t {
    Land = 0,
    Water,
    Wall,
    Tree
};

enum class Faction : uint8_t {
    Allies = 0,
    Axis,
    Militia,
    Rebels
};

enum class AIState : uint8_t {
    Idle = 0,
    Patrol,
    Search,
    Seek,
    Attack,
    HoldCover,
    Hunker,
    Investigate,
    Flank,
    Flee
};

enum class Mode : uint8_t {
    Player = 0,
    Control,
    Paint
};

enum class MissionPhase : uint8_t {
    None = 0,
    Ingress,
    Exfil,
    Complete,
    Failed
};

enum class MissionKind : uint8_t {
    Intel = 0,
    HVT,
    Sabotage,
    Rescue,
    Sweep
};

enum class MissionRole : uint8_t {
    None = 0,
    ObjectiveGuard,
    AreaPatrol,
    Reserve
};

enum class SquadMode : uint8_t {
    Calm = 0,           // idle around anchor; slow patrol + scan
    PatrolSweep,        // 1–2 units walk out to a nearby point ("go look")
    ReturnToAnchor,     // walkers return to anchor
    CombatContact,      // active firefight / threat present
    Defensive,          // under fire but no LOS (future use)
    Redeploying         // moving to a new anchor (future use)
};

enum class SquadIntent : uint8_t {
    Hold = 0,       // defend / overwatch current area
    Advance,        // push toward enemy / last known contact
    Flank,          // split into support + flanking elements
    Retreat,        // break contact toward cover / guard anchor
    Search          // move carefully toward last known contact
};


inline const char* stateName(AIState s) {
    switch (s) {
    case AIState::Idle:        return "Idle";
    case AIState::Patrol:      return "Patrol";
    case AIState::Search:      return "Search";
    case AIState::Seek:        return "Seek";
    case AIState::Attack:      return "Attack";
    case AIState::HoldCover:   return "Hold";
    case AIState::Hunker:      return "Hunker";
    case AIState::Investigate: return "Investigate";
    case AIState::Flank:       return "Flank";
    case AIState::Flee:        return "Flee";
    default:                   return "?";
    }
}

// -----------------------------------------------------------
// Map
// -----------------------------------------------------------

struct Map {
    int cols = 0;
    int rows = 0;
    std::vector<Tile> tiles;

    void init(int c, int r) {
        cols = c;
        rows = r;
        tiles.assign(cols * rows, Tile::Land);
    }

    bool inBounds(int c, int r) const {
        return (c >= 0 && r >= 0 && c < cols && r < rows);
    }

    Tile at(int c, int r) const {
        if (!inBounds(c, r)) return Tile::Wall;
        return tiles[r * cols + c];
    }

    void set(int c, int r, Tile t) {
        if (!inBounds(c, r)) return;
        tiles[r * cols + c] = t;
    }
};

// -----------------------------------------------------------
// Gun / Bullet / Audio
// -----------------------------------------------------------

struct Bullet {
    Vec2 pos;
    Vec2 dir;
    float traveled = 0.0f;
    float speed = 0.0f;
    float maxRange = 0.0f;

    WeaponId wid = WeaponId::None; // Phase W5: damage flavour + logging
    int dmg = 1;
    Faction src = Faction::Axis;
};


struct SoundPing {
    Vec2  pos;
    float radiusPx = 0.f;
    float ttl = 0.f;
};

struct Bark {
    Vec2  pos;
    std::string text;
    float ttl = 0.0f;
};

struct Gun {
    int   inMag = 8;
    int   magSize = 8;
    float fireCooldownS = 0.3f;
    float fireTimer = 0.0f;
    float reloadTimeS = 1.6f;
    float reloadTimer = 0.0f;
    bool  reloading = false;
    int   bulletDamage = 1;

    void update(float dt) {
        if (fireTimer > 0.0f)
            fireTimer = std::max(0.0f, fireTimer - dt);
        if (reloading) {
            reloadTimer -= dt;
            if (reloadTimer <= 0.0f) {
                reloading = false;
                inMag = magSize;
            }
        }
    }

    bool canFire() const {
        return !reloading && fireTimer <= 0.0f && inMag > 0;
    }

    void startReload() {
        if (reloading) return;
        if (inMag == magSize) return;
        reloading = true;
        reloadTimer = reloadTimeS;
    }
};

// -----------------------------------------------------------
// Actor / Squads
// -----------------------------------------------------------

struct Actor {
    Vec2 pos;
    Vec2 facing{ 1, 0 };
    float w = 18.0f;
    float h = 12.0f;

    int hp = 2;
    int hpMax = 2;

    // Phase W5: last hit info (for debug + AI reactions later)
    HitZone lastHitZone = HitZone::Torso;
    float   lastHitTime = -999.0f;
    Vec2    lastShotOrigin{ 0,0 };

    // Proper local-space hit rig (forward/right axes)
    std::array<HitBox, 5> hitRig; // Head/Torso/Legs/ArmL/ArmR

    // Wounds (simple but meaningful)
    float legWoundS = 0.0f; // slows movement
    float armWoundS = 0.0f; // worsens spread

    // Player feedback throttle (used for barks)
    float nextCalloutS = 0.0f;


    Faction team = Faction::Axis;
    Gun gun; // legacy (v6.10 firing uses this). We'll bridge to WeaponInstance in Part 3.

    // Phase W1+ : table-driven weapon state (not yet wired into firing until Part 3)
    WeaponInstance weapon;


    AIState state = AIState::Patrol;
    float  visionRange = 240.0f;
    float  visionFOVDeg = 90.0f;

    float moveWalkSpeed = 70.0f;
    float moveSprintSpeed = 110.0f;

    bool selected = false;
    bool isLeader = false;
    bool isHVT = false;

    int squadId = -1;

    // AI timers
    float nextThink = 0.0f;
    float idleTimer = 0.0f;
    float barkCooldown = 0.0f;
    float repathTimer = 0.0f;

    // For "hit" reaction
    bool  recentlyHit = false;
    float recentlyHitTimer = 0.0f;

    // For search/seek
    Vec2 patrolTarget{ 0,0 };
    Vec2 investigatePos{ 0,0 };
    bool hasOrder = false;
    Vec2 orderPos{ 0,0 };

    // Pathfollowing
    std::vector<Vec2> path;
    int pathIndex = -1;

    // Squad scan behaviour
    bool inScan = false;     // true if this actor currently has an assigned scan slot
    Vec2 scanPos{ 0,0 };     // where to stand while scanning

    bool alive() const { return hp > 0; }
};

// --- Build a default hit rig based on pawn size (local forward/right space)
static void buildDefaultHitRig(Actor& a) {
    // Use w as “length along forward”, h as “width across right”
    float halfF = a.w * 0.5f;
    float halfR = a.h * 0.5f;

    // Layout in forward/right axes:
    // Forward is +x, Back is -x, Right is +y, Left is -y (relative to facing)
    // Make head slightly forward, legs backward, arms lateral.
    a.hitRig = {
        HitBox{ HitZone::Head,  +halfF * 0.35f, -halfR * 0.18f,  halfF * 0.28f,  halfR * 0.36f },
        HitBox{ HitZone::Torso, -halfF * 0.15f, -halfR * 0.45f,  halfF * 0.55f,  halfR * 0.90f },
        HitBox{ HitZone::Legs,  -halfF * 0.80f, -halfR * 0.40f,  halfF * 0.55f,  halfR * 0.80f },
        HitBox{ HitZone::ArmL,  -halfF * 0.10f, -halfR * 0.95f,  halfF * 0.40f,  halfR * 0.45f },
        HitBox{ HitZone::ArmR,  -halfF * 0.10f, +halfR * 0.50f,  halfF * 0.40f,  halfR * 0.45f },
    };
}

static bool resolveHitZone(const Actor& a, const Vec2& hitPos, HitZone& outZone) {
    Vec2 fwd = normalize(a.facing);
    Vec2 right = perpRight(fwd);
    Vec2 rel = hitPos - a.pos;

    // Actor-local coordinates (forward,right)
    float lx = dot2(rel, fwd);
    float ly = dot2(rel, right);

    for (const HitBox& hb : a.hitRig) {
        if (pointInRect(lx, ly, hb)) {
            outZone = hb.zone;
            return true;
        }
    }

    // Fallback: treat as torso if none matched
    outZone = HitZone::Torso;
    return false;
}


// --- Hit zone classification (implementation after Actor is defined)
static HitZone classifyHitZone(const Actor& a, const Vec2& hitPos) {
    Vec2 fwd = normalize(a.facing);
    Vec2 rel = hitPos - a.pos;

    // Use actor "length" along facing = a.w
    float halfF = a.w * 0.5f;
    float f = dot2(rel, fwd);

    // front third => Head, middle => Torso, rear => Legs
    float headCut = halfF * 0.33f;
    float legsCut = -halfF * 0.20f;

    if (f > headCut) return HitZone::Head;
    if (f < legsCut) return HitZone::Legs;
    return HitZone::Torso;
}


// -----------------------------------------------------------
// Weapons (Phase W2 - Loadouts: player + faction tier rules)
// -----------------------------------------------------------

static float gDamageScale = 0.3f;  // 0.6–0.85 is a good range


struct FactionWeaponRule {
    WeaponTier minTier = WeaponTier::Basic;
    WeaponTier maxTier = WeaponTier::Basic;
};

// --- Player start knobs (edit these)
static WeaponId gPlayerStartWeapon = WeaponId::WELROD;
static int gPlayerStartMagOverride = -1;     // -1 = use weapon default mag size
static int gPlayerStartReserveOverride = -1; // -1 = use weapon default reserve

// --- Faction tier knobs ("soft difficulty")
static inline FactionWeaponRule weaponRuleForFaction(Faction f) {
    switch (f) {
    case Faction::Militia: return { WeaponTier::Basic,        WeaponTier::Basic };
    case Faction::Rebels:  return { WeaponTier::Basic,        WeaponTier::Intermediate };
    case Faction::Axis:    return { WeaponTier::Intermediate, WeaponTier::Advanced };
    case Faction::Allies:  return { WeaponTier::Intermediate, WeaponTier::Advanced };
    default:               return { WeaponTier::Basic,        WeaponTier::Intermediate };
    }
}

static inline bool tierInRange(WeaponTier t, WeaponTier lo, WeaponTier hi) {
    return (int)t >= (int)lo && (int)t <= (int)hi;
}

// Faction-flavoured pools (kept small + readable; easy to tune later)
static inline WeaponId pickWeaponFromFactionPool(Faction f, WeaponTier lo, WeaponTier hi) {
    // NOTE: These are *preferences*, not hard restrictions.
    // If a pool can't satisfy the tier rule, we fall back to "any weapon in table within tier".
    static const WeaponId axisPool[] = {
        WeaponId::LUGER_P08, WeaponId::MP40, WeaponId::KAR98K, WeaponId::MG34
    };
    static const WeaponId alliesPool[] = {
        WeaponId::M1911, WeaponId::STEN, WeaponId::THOMPSON, WeaponId::M1_CARBINE, WeaponId::GARAND, WeaponId::BREN, WeaponId::SMLE
    };
    static const WeaponId rebelsPool[] = {
        WeaponId::M1911, WeaponId::STEN, WeaponId::DB_SHOTGUN, WeaponId::MOSIN, WeaponId::PPSH41
    };
    static const WeaponId militiaPool[] = {
        WeaponId::TOKAREV_TT33, WeaponId::PPSH41, WeaponId::MOSIN, WeaponId::DP27, WeaponId::AK47
    };

    auto pickFrom = [&](const WeaponId* pool, int count) -> WeaponId {
        WeaponId candidates[32];
        int n = 0;
        for (int i = 0; i < count; ++i) {
            const WeaponDef& d = weaponDef(pool[i]);
            if (d.id == WeaponId::None) continue;
            if (!tierInRange(d.tier, lo, hi)) continue;
            candidates[n++] = d.id;
        }
        if (n > 0) return candidates[irand(0, n - 1)];
        return WeaponId::None;
        };

    switch (f) {
    case Faction::Axis: { WeaponId w = pickFrom(axisPool, (int)(sizeof(axisPool) / sizeof(axisPool[0])));    if (w != WeaponId::None) return w; } break;
    case Faction::Allies: { WeaponId w = pickFrom(alliesPool, (int)(sizeof(alliesPool) / sizeof(alliesPool[0])));  if (w != WeaponId::None) return w; } break;
    case Faction::Rebels: { WeaponId w = pickFrom(rebelsPool, (int)(sizeof(rebelsPool) / sizeof(rebelsPool[0])));  if (w != WeaponId::None) return w; } break;
    case Faction::Militia: { WeaponId w = pickFrom(militiaPool, (int)(sizeof(militiaPool) / sizeof(militiaPool[0]))); if (w != WeaponId::None) return w; } break;
    default: break;
    }

    // Fallback: choose ANY weapon from the global table within tier range
    WeaponId fallback[128];
    int n = 0;
    const int total = (int)(sizeof(kWeaponTable) / sizeof(kWeaponTable[0]));
    for (int i = 0; i < total; ++i) {
        const WeaponDef& d = kWeaponTable[i];
        if (d.id == WeaponId::None) continue;
        if (!tierInRange(d.tier, lo, hi)) continue;
        fallback[n++] = d.id;
    }
    if (n > 0) return fallback[irand(0, n - 1)];
    return WeaponId::M1911; // very safe fallback
}

static inline void applyWeaponInstance(Actor& a, WeaponId wid, int magOverride = -1, int reserveOverride = -1) {
    const WeaponDef& d = weaponDef(wid);
    a.weapon.id = wid;
    a.weapon.clearTimers();
    a.weapon.magAmmo = (magOverride >= 0) ? magOverride : d.magSize;
    a.weapon.reserveAmmo = (reserveOverride >= 0) ? reserveOverride : d.reserveDefault;

    // Bridge: sync legacy gun stats from the weapon table so existing firing uses WeaponDef.
    a.gun.magSize = d.magSize;
    a.gun.inMag = a.weapon.magAmmo;
    a.gun.fireCooldownS = d.fireCooldownS;
    a.gun.reloadTimeS = d.reloadTimeS;
    a.gun.bulletDamage = (int)std::round(d.baseDamage);

    // Reset gun timers so a fresh spawn doesn't inherit prior state
    a.gun.fireTimer = 0.0f;
    a.gun.reloading = false;
    a.gun.reloadTimer = 0.0f;

}

static inline void applyFactionLoadout(Actor& a) {
    FactionWeaponRule rule = weaponRuleForFaction(a.team);
    WeaponId wid = pickWeaponFromFactionPool(a.team, rule.minTier, rule.maxTier);
    applyWeaponInstance(a, wid);
}

static inline void applyPlayerLoadout(Actor& player) {
    applyWeaponInstance(player, gPlayerStartWeapon, gPlayerStartMagOverride, gPlayerStartReserveOverride);
}

static inline bool weaponCanFire(const Actor& a) {
    return !a.weapon.reloading && a.weapon.fireTimer <= 0.0f && a.weapon.magAmmo > 0;
}

static inline void weaponStartReload(Actor& a) {
    if (a.weapon.reloading) return;
    const WeaponDef& d = weaponDef(a.weapon.id);
    if (d.magSize <= 0) return;
    if (a.weapon.magAmmo >= d.magSize) return;
    if (a.weapon.reserveAmmo <= 0) return;

    a.weapon.reloading = true;
    a.weapon.reloadTimer = d.reloadTimeS;

    // Keep legacy gun flags roughly in sync for now (UI/older code)
    a.gun.reloading = true;
    a.gun.reloadTimer = d.reloadTimeS;
}

static inline void weaponUpdate(Actor& a, float dt) {
    if (a.weapon.fireTimer > 0.0f) a.weapon.fireTimer = std::max(0.0f, a.weapon.fireTimer - dt);

    if (a.weapon.reloading) {
        a.weapon.reloadTimer -= dt;
        if (a.weapon.reloadTimer <= 0.0f) {
            const WeaponDef& d = weaponDef(a.weapon.id);
            int need = std::max(0, d.magSize - a.weapon.magAmmo);
            int take = std::min(need, a.weapon.reserveAmmo);
            a.weapon.magAmmo += take;
            a.weapon.reserveAmmo -= take;
            a.weapon.reloading = false;
            a.weapon.reloadTimer = 0.0f;
        }
    }

    // Bridge: keep legacy gun magazine number aligned so existing HUD bits remain sane
    a.gun.inMag = a.weapon.magAmmo;
    a.gun.magSize = weaponDef(a.weapon.id).magSize;

    // Keep gun reload flags aligned (optional, harmless)
    a.gun.reloading = a.weapon.reloading;
    a.gun.reloadTimer = a.weapon.reloadTimer;
}



struct SquadMemory {
    std::vector<Vec2> sightings;
    float decayS = 8.0f;
};

struct Squad {
    int   id = -1;
    Faction side = Faction::Axis;
    std::vector<int> members;

    int leader = -1;
    Vec2 home{ 0,0 };

    float patrolRadius = 220.0f;
    MissionRole role = MissionRole::None;
    Vec2 roleAnchor{ 0,0 };
    float roleRadius = 220.0f;

    SquadMemory mem;

    // NEW: lightweight squad behaviour state
    SquadMode mode = SquadMode::Calm;
    float     modeTimer = 0.0f;       // when <= 0, re-evaluate utility choice
    Vec2      currentGoal{ 0,0 };     // high-level movement target

      
    //DELETE???//
    // NEW: simple checkpoint patrol loop
    std::vector<Vec2> checkpoints;
    int currentCheckpoint = -1;


    float calmTimerS = 0.0f;       // idle time before next calm movement
    float calmBarkTimerS = 0.0f;   // time until next calm bark is allowed
    float idleScanCooldownS = 0.0f; // idle scan cooldown
    bool  calmGoalActive = false;

    // NEW: squad-level scan state
    bool  scanning = false;
    Vec2  scanCenter{ 0,0 };
    float scanRadius = 120.0f;
    float scanTimer = 0.0f;

    // --- Patch S: squad-level reasoning state ---
    SquadIntent intent = SquadIntent::Hold;
    bool intentExecuting = false;     // false = coordinating / scanning, true = orders live
    float intentTimer = 0.0f;        // commitment window (cannot re-plan while > 0)
    float coordTimer = 0.0f;         // short delay between "plan chosen" and "go!"

    float timeSinceContact = 999.0f; // seconds since last enemy sight/shot
    Vec2  lastKnownEnemy{ 0,0 };
    bool  hasLastKnownEnemy = false;

    int   lastAliveCount = 0;        // for casualty detection
    int   recentCasualties = 0;

    bool  underFire = false;         // any member recentlyHit
    bool  hadFlankLOS = false;       // did flank element gain LOS during this plan?

    // Morale / suppression
    int   initialCount = 0;      // set when squad is created
    float suppression = 0.0f;    // 0..N, decays
    float confidence = 1.0f;     // 0..1, derived each frame


    // --- Visual debug for Patch S ---
    bool debugHasEnemy = false;
    bool debugHasCover = false;
    bool debugHasFlank = false;
    Vec2 debugEnemyPos{ 0,0 };
    Vec2 debugCoverPos{ 0,0 };
    Vec2 debugFlankPos{ 0,0 };
};


// -----------------------------------------------------------
// Mission
// -----------------------------------------------------------

struct MissionParams {
    int   enemySquadsBase = 2;
    int   sweepSquadsBase = 3;
    int   respawnWaves = 0;
    float patrolRadiusScale = 1.0f;
    float sweepFraction = 0.5f;

    bool useAxis = true;
    bool useMilitia = true;
    bool useRebels = true;
    bool useAllies = false; // NEW: allow Allies as a spawnable faction
};


struct MissionState {
    bool   active = false;
    MissionKind kind = MissionKind::Intel;
    MissionPhase phase = MissionPhase::Ingress;

    int alarmLevel = 0;

    // Intel
    Vec2 docPos{ 0,0 };
    bool docPresent = false;
    bool docTaken = false;

    // HVT
    Vec2 hvtPos{ 0,0 };
    bool hvtPresent = false;
    bool hvtKilled = false;
    int  hvtIndex = -1;

    // Sabotage
    Vec2  sabotagePos{ 0,0 };
    bool  sabotagePresent = false;
    bool  sabotageArmed = false;
    bool  sabotageDestroyed = false;
    float sabotageTimer = 0.0f;

    // Rescue
    Vec2 rescuePos{ 0,0 };
    bool rescuePresent = false;
    bool rescueFreed = false;

    // Common extraction
    Vec2 extractPos{ 0,0 };
    bool extractPresent = false;

    // Sweep
    int totalEnemiesAtStart = 0;
    int sweepRequiredKills = 0;

    // Stats
    int shotsFired = 0;
    int shotsHit = 0;
    int enemiesKilled = 0;

    // Reinforcements
    int respawnWavesRemaining = 0;

    float ambientSkirmishCooldownS = 0.0f; // time until next ambient skirmish is allowed
    float ambientSkirmishTimerS = 0.0f;    // internal timer for periodic checks

    bool summaryShown = false;
};

// -----------------------------------------------------------
// Foliage & trunks
// -----------------------------------------------------------

struct Trunk {
    int   c = 0;
    int   r = 0;
    Vec2  center;
    float dia = 10.0f;
};

struct Leaf {
    SDL_FRect rect{ 0,0,0,0 };
};

// -----------------------------------------------------------
// Painting ops (undo stack)
// -----------------------------------------------------------

struct PaintOp {
    int c = 0, r = 0;
    Tile before = Tile::Land;
    Tile after = Tile::Land;
};

// -----------------------------------------------------------
// Text cache for SDL_ttf
// -----------------------------------------------------------

struct TextKey {
    std::string s;
    uint8_t r, g, b, a;

    bool operator==(const TextKey& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a && s == o.s;
    }
};

struct TextKeyHash {
    std::size_t operator()(const TextKey& k) const noexcept {
        std::hash<std::string> hs;
        std::size_t h = hs(k.s);
        h ^= (k.r + 0x9e3779b9 + (h << 6) + (h >> 2));
        h ^= (k.g + 0x9e3779b9 + (h << 6) + (h >> 2));
        h ^= (k.b + 0x9e3779b9 + (h << 6) + (h >> 2));
        h ^= (k.a + 0x9e3779b9 + (h << 6) + (h >> 2));
        return h;
    }
};

struct TextTex {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};

// -----------------------------------------------------------
// Prefab for building footprints
// -----------------------------------------------------------

struct Prefab {
    int w = 0;
    int h = 0;
    std::vector<Tile> data;
};

// -----------------------------------------------------------
// Game class
// -----------------------------------------------------------

class Game {
public:
    Game() = default;
    ~Game() = default;

    bool init();
    void run();
    void cleanup();

private:
    // SDL
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;

    std::unordered_map<TextKey, TextTex, TextKeyHash> textCache;

    // World
    Map map;
    std::vector<Actor>  actors;
    Actor               player;
    bool                playerPresent = false;

    std::vector<Squad>  squads;
    std::vector<Vec2>   corpses;

    std::vector<Bullet> bullets;
    std::vector<SoundPing> sounds;
    std::vector<Bark>  barks;

    std::vector<Trunk> trunks;
    std::vector<Leaf>  leaves;
    std::vector<int>   trunkIndex; // per tile, index into trunks or -1

    MissionParams missionParams;
    MissionState  mission;

    std::vector<Prefab> prefabs;

    float gameTimeS = 0.0f;


    // Camera
    float camX = 0.0f;
    float camY = 0.0f;
    float zoom = cfg::ZoomSandbox;
    bool  playerUnderCanopy = false;

    // Mode & toggles
    bool running = true;
    bool paused = false;
    Mode mode = Mode::Player;

    bool labelsEnabled = true;
    bool hearingViz = false;
    bool visionViz = false;
    bool hudEnabled = true;
    bool barksEnabled = true;
    bool squadDebugViz = true;
    bool showAIIntentViz = true; // F4: AI intent lines + guard points



    bool showMissionBrief = false;
    bool showMissionDebrief = false;
    bool showMissionParams = false;
    bool sneakMode = false; // X toggled


    // Input
    bool kW = false, kA = false, kS = false, kD = false;
    bool kShift = false;
    int  mouseX = 0, mouseY = 0;
    int  worldMouseX = 0, worldMouseY = 0;
    bool mouseDownL = false, mouseDownR = false;

    // Paint
    int paintBrush = 1; // 0 player, 7 allies, 1 axis, 2 militia, 3 rebels, 4 erase, 5 wall, 6 tree
    bool paintSquad = false;
    std::stack<PaintOp> undo;

    // Timing
    Uint64 lastTicks = 0;
    double timeAccum = 0.0;

    // Internal helpers
    bool initSDL();
    bool initFont();
    void initWorld();
    void initPrefabs();

    void handleEvents();
    void update(float dt);
    void render();

    void updateCamera(float dt);

    // Map / tiles
    SDL_FRect tileRectWorld(int c, int r) const;
    bool inBoundsTile(int c, int r) const;
    bool isWalkableTile(int c, int r) const;
    bool isNavWalkable(int c, int r) const;
    bool isClearLand(int c, int r) const;

    SDL_FRect rectFrom(const Vec2& p, float w, float h) const;

    Map makeBlankMap() const;
    void applyTreesSparse(Map& m, float chancePerTile);
    void applyTreesClump(Map& m, int clumps, int radiusTiles);

    Vec2 randomWalkablePos(int marginTiles) const;
    Vec2 randomClearPos(int marginTiles, const std::vector<Vec2>& avoid, float minDistTiles) const;
    Vec2 randomOpenCellAround(const Vec2& center, int radiusTiles) const;

    // Foliage
    void rebuildFoliage();
    void drawTrunkOctagon(const Trunk& t);

    // Actors & squads
    Actor makeUnit(Faction f, const Vec2& pos);
    SDL_Color factionColor(Faction f) const;
    bool areEnemies(Faction a, Faction b) const;
    Faction randomEnemyFaction() const;

    void placeSquad(Faction f, int wx, int wy, int count);
    int  countLivingEnemies() const;



    // Pathfinding & movement
    bool collideSolid(const SDL_FRect& r) const;
    void moveWithCollide(Actor& a, const Vec2& vel, float maxSpeed, float dt);

    bool buildPath(const Vec2& from, const Vec2& to, Actor& a) const;
    void clearPath(Actor& a) const;

    Vec2 findNearestCoverToward(const Vec2& from, const Vec2& toward) const;

    void resolveActorCollisions(float dt);   // 👈 add this prototype here


    // Sensory
    bool losClear(const Vec2& a, const Vec2& b) const;
    bool sees(const Actor& a, const Vec2& targetPos, bool& outLOS) const;
    bool hears(const Actor& a, const SoundPing& s) const;

    bool acquireThreat(const Actor& self, Vec2& outPos, int& outIdx, bool& outSees) const;

    // AI
    void updateAI(Actor& a, float dt);
    void updateSquadBrain(int sid, float dt);
    void raiseAlarm(int level);
    void beginSquadScan(int sid, const Vec2& center, const Vec2& facing,
        float radius, float duration);

    void addSuppression(int squadId, float amount);

    bool playerKnownToAllies = false;
    float alliesCuriosityTimer = 0.0f;

    // Bark helper: world-position bark
    void pushBark(const Vec2& pos, const char* txt, float ttl = 2.0f);



    // Mission system
    void startMission(MissionKind kind);
    void updateMission(float dt);

    // Drawing
    void setDraw(SDL_Renderer* r, SDL_Color c) const;
    void drawText(const std::string& s, int x, int y, SDL_Color c);

    void drawWorld();
    void drawActors();
    void drawBullets();
    void drawBarks();
    void drawMissionHUD();
    void drawMissionParamsHUD();
    void drawHUD();
    void drawVisionOutlineAndRay(const Actor& a);
    void drawSquadDebug();

    bool inScreen(float x, float y, float margin = 0.0f) const;




    // Paint / control
    void handlePaintClick(int wx, int wy, bool leftClick);
    void handleControlClick(int wx, int wy, bool leftClick);
};

// -----------------------------------------------------------
// main
// -----------------------------------------------------------

int main(int, char**) {
    Game g;
    if (!g.init()) return 1;
    g.run();
    g.cleanup();
    return 0;
}

// -----------------------------------------------------------
// Game: init / cleanup / run
// -----------------------------------------------------------

bool Game::initSDL() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    if (TTF_Init() != 0) {
        std::printf("TTF_Init failed: %s\n", TTF_GetError());
        return false;
    }

    window = SDL_CreateWindow(
        "Pathfinders v6.8 Sandbox",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        cfg::ScreenW,
        cfg::ScreenH,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        std::printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return true;
}

bool Game::initFont() {
    font = TTF_OpenFont("C:\\Windows\\Fonts\\consola.ttf", 14);
    if (!font) {
        std::printf("TTF_OpenFont failed: %s\n", TTF_GetError());
        return false;
    }
    return true;
}

SDL_FRect Game::rectFrom(const Vec2& p, float w, float h) const {
    SDL_FRect r;
    r.x = p.x - w * 0.5f;
    r.y = p.y - h * 0.5f;
    r.w = w;
    r.h = h;
    return r;
}

SDL_FRect Game::tileRectWorld(int c, int r) const {
    SDL_FRect tr;
    tr.x = float(c * cfg::TileSize);
    tr.y = float(r * cfg::TileSize);
    tr.w = float(cfg::TileSize);
    tr.h = float(cfg::TileSize);
    return tr;
}

bool Game::inBoundsTile(int c, int r) const {
    return map.inBounds(c, r);
}

bool Game::isWalkableTile(int c, int r) const {
    Tile t = map.at(c, r);
    if (t == Tile::Land || t == Tile::Tree) return true;
    return false;
}

bool Game::isNavWalkable(int c, int r) const {
    Tile t = map.at(c, r);
    if (t == Tile::Land) return true; // trees blocked for nav
    return false;
}

bool Game::isClearLand(int c, int r) const {
    if (!inBoundsTile(c, r)) return false;
    if (map.at(c, r) != Tile::Land) return false;
    return true;
}

Map Game::makeBlankMap() const {
    Map m;
    m.init(cfg::MapCols, cfg::MapRows);

    for (int r = 0; r < m.rows; ++r) {
        for (int c = 0; c < m.cols; ++c) {
            if (r == 0 || c == 0 || r == m.rows - 1 || c == m.cols - 1) {
                m.set(c, r, Tile::Water);
            }
            else {
                m.set(c, r, Tile::Land);
            }
        }
    }
    return m;
}

void Game::applyTreesClump(Map& m, int clusters, int clusterRadiusTiles) {
    const int cols = m.cols;
    const int rows = m.rows;

    // Minimum spacing between trunks (in tiles)
    const float minTrunkSpacingTiles = 2.0f;
    const float minTrunkSpacingSq = minTrunkSpacingTiles * minTrunkSpacingTiles;

    std::vector<SDL_Point> treeTiles;

    for (int i = 0; i < clusters; ++i) {
        int cx = irand(4, cols - 5);
        int cy = irand(4, rows - 5);

        for (int y = -clusterRadiusTiles; y <= clusterRadiusTiles; ++y) {
            for (int x = -clusterRadiusTiles; x <= clusterRadiusTiles; ++x) {
                int tx = cx + x;
                int ty = cy + y;
                if (!m.inBounds(tx, ty)) continue;
                float d2 = float(x * x + y * y);
                if (d2 > float(clusterRadiusTiles * clusterRadiusTiles)) continue;

                // Only on land
                if (m.at(tx, ty) != Tile::Land) continue;

                // Enforce spacing vs already placed trees
                bool tooClose = false;
                for (auto& t : treeTiles) {
                    float dx = float(t.x - tx);
                    float dy = float(t.y - ty);
                    if (dx * dx + dy * dy < minTrunkSpacingSq) {
                        tooClose = true;
                        break;
                    }
                }
                if (tooClose) continue;

                m.set(tx, ty, Tile::Tree);
                treeTiles.push_back({ tx, ty });
            }
        }
    }
}

// More scattered “noise” trees across big open areas
void Game::applyTreesSparse(Map& m, float density) {
    const int cols = m.cols;
    const int rows = m.rows;

    const float minTrunkSpacingTiles = 2.0f;
    const float minTrunkSpacingSq = minTrunkSpacingTiles * minTrunkSpacingTiles;

    std::vector<SDL_Point> treeTiles;

    for (int y = 2; y < rows - 2; ++y) {
        for (int x = 2; x < cols - 2; ++x) {
            if (m.at(x, y) != Tile::Land) continue;
            if (frand(0.f, 1.f) > density) continue;

            bool tooClose = false;
            for (auto& t : treeTiles) {
                float dx = float(t.x - x);
                float dy = float(t.y - y);
                if (dx * dx + dy * dy < minTrunkSpacingSq) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;

            m.set(x, y, Tile::Tree);
            treeTiles.push_back({ x, y });
        }
    }

    // A few bonus isolated trees in big bare patches
    for (int i = 0; i < 32; ++i) {
        int x = irand(3, cols - 4);
        int y = irand(3, rows - 4);
        if (m.at(x, y) != Tile::Land) continue;

        int landCount = 0;
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx) {
                int xx = x + dx, yy = y + dy;
                if (!m.inBounds(xx, yy)) continue;
                if (m.at(xx, yy) == Tile::Land) landCount++;
            }

        if (landCount > 40) { // fairly open patch
            m.set(x, y, Tile::Tree);
            treeTiles.push_back({ x, y });
        }
    }
}


// Door-carving helper: carve 1–2 random gaps in the wall ring
static void carveDoors(Prefab& p, int minDoors = 1, int maxDoors = 2) {
    int numDoors = irand(minDoors, maxDoors);
    std::vector<std::pair<int, int>> wallCells;

    for (int y = 0; y < p.h; ++y) {
        for (int x = 0; x < p.w; ++x) {
            bool border = (y == 0 || x == 0 || y == p.h - 1 || x == p.w - 1);
            if (!border) continue;
            Tile t = p.data[y * p.w + x];
            if (t == Tile::Wall) {
                wallCells.emplace_back(x, y);
            }
        }
    }

    if (wallCells.empty()) return;
    for (int i = 0; i < numDoors; ++i) {
        auto [dx, dy] = wallCells[irand(0, (int)wallCells.size() - 1)];
        p.data[dy * p.w + dx] = Tile::Land;
    }
}

void Game::initPrefabs() {
    prefabs.clear();

    Prefab p1;
    p1.w = 16;
    p1.h = 12;
    p1.data.assign(p1.w * p1.h, Tile::Land);
    for (int y = 0; y < p1.h; ++y) {
        for (int x = 0; x < p1.w; ++x) {
            bool border = (y == 0 || x == 0 || y == p1.h - 1 || x == p1.w - 1);
            if (border) p1.data[y * p1.w + x] = Tile::Wall;
        }
    }
    carveDoors(p1);
    prefabs.push_back(p1);

    Prefab p2;
    p2.w = 20;
    p2.h = 16;
    p2.data.assign(p2.w * p2.h, Tile::Land);
    for (int y = 0; y < p2.h; ++y) {
        for (int x = 0; x < p2.w; ++x) {
            bool border = (y == 0 || x == 0 || y == p2.h - 1 || x == p2.w - 1);
            if (border) p2.data[y * p2.w + x] = Tile::Wall;
        }
    }
    carveDoors(p2, 2, 3);
    prefabs.push_back(p2);
}

void Game::rebuildFoliage() {
    trunks.clear();
    leaves.clear();
    trunkIndex.assign(map.cols * map.rows, -1);

    int idx = 0;
    for (int r = 0; r < map.rows; ++r) {
        for (int c = 0; c < map.cols; ++c) {
            if (map.at(c, r) == Tile::Tree) {
                Trunk t;
                t.c = c;
                t.r = r;
                SDL_FRect tr = tileRectWorld(c, r);
                t.center = Vec2{ tr.x + tr.w * 0.5f, tr.y + tr.h * 0.5f };
                t.dia = frand(cfg::TrunkMin, cfg::TrunkMax);
                trunks.push_back(t);
                trunkIndex[r * map.cols + c] = idx++;
            }
        }
    }

    for (const auto& t : trunks) {
        for (int i = 0; i < cfg::FoliagePerTrunk; ++i) {
            float ang = frand(0.f, 6.28318f);
            float rad = frand(0.2f * cfg::FoliageRadiusPx, cfg::FoliageRadiusPx);
            float w = frand(cfg::FoliageMinSize, cfg::FoliageMaxSize);
            float h = frand(cfg::FoliageMinSize, cfg::FoliageMaxSize);
            Leaf lf;
            lf.rect.x = t.center.x + std::cos(ang) * rad - w * 0.5f;
            lf.rect.y = t.center.y + std::sin(ang) * rad - h * 0.5f;
            lf.rect.w = w;
            lf.rect.h = h;
            leaves.push_back(lf);
        }
    }
}

void Game::drawTrunkOctagon(const Trunk& t) {
    SDL_SetRenderDrawColor(renderer,
        cfg::ColTrunk.r, cfg::ColTrunk.g, cfg::ColTrunk.b, cfg::ColTrunk.a);

    float r = t.dia * 0.5f;
    const int N = 8;
    float cx = t.center.x - camX;
    float cy = t.center.y - camY;
    SDL_FPoint pts[N];
    for (int i = 0; i < N; ++i) {
        float ang = (6.28318f * i) / N;
        pts[i].x = cx + std::cos(ang) * r;
        pts[i].y = cy + std::sin(ang) * r;
    }
    for (int i = 0; i < N; ++i) {
        int j = (i + 1) % N;
        SDL_RenderDrawLineF(renderer, pts[i].x, pts[i].y, pts[j].x, pts[j].y);
    }
}

void Game::initWorld() {
    map = makeBlankMap();
    applyTreesClump(map, 10, 5);
    applyTreesSparse(map, 0.02f);

    trunkIndex.assign(map.cols * map.rows, -1);
    rebuildFoliage();

    Vec2 spawn{
        map.cols * cfg::TileSize * 0.5f,
        map.rows * cfg::TileSize * 0.5f
    };
    player = Actor{};
    player.pos = spawn;
    player.team = Faction::Allies;
    player.hp = player.hpMax = 3;
    player.gun.magSize = 10;
    player.gun.inMag = 10;
    player.gun.fireCooldownS = 0.22f;
    player.gun.bulletDamage = 1;
    player.visionRange = 260.0f;
    player.visionFOVDeg = 100.0f;
    player.state = AIState::Idle;

    playerPresent = true;
    applyPlayerLoadout(player);

    camX = player.pos.x - (cfg::ScreenW / 2.0f);
    camY = player.pos.y - (cfg::ScreenH / 2.0f);
    zoom = cfg::ZoomSandbox;

    mission = MissionState{};
    missionParams = MissionParams{};
    squads.clear();
    actors.clear();
    corpses.clear();
    bullets.clear();
    sounds.clear();
    barks.clear();
    undo = std::stack<PaintOp>();

    initPrefabs();
}

bool Game::init() {
    if (!initSDL()) return false;
    if (!initFont()) return false;

    initWorld();

    lastTicks = SDL_GetPerformanceCounter();
    timeAccum = 0.0;
    running = true;
    paused = false;
    mode = Mode::Player;

    return true;
}

void Game::cleanup() {
    for (auto& kv : textCache) {
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    }
    textCache.clear();

    if (font) {
        TTF_CloseFont(font);
        font = nullptr;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    TTF_Quit();
    SDL_Quit();
}

void Game::run() {
    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        double freq = (double)SDL_GetPerformanceFrequency();
        float dt = float((now - lastTicks) / freq);
        if (dt > 0.1f) dt = 0.1f;
        lastTicks = now;

        handleEvents();
        if (!paused || mission.active) {
            update(dt);
        }
        render();
    }
}

// -----------------------------------------------------------
// Camera & simple helpers
// -----------------------------------------------------------

void Game::updateCamera(float dt) {
    if (!playerPresent) return;

    float visibleW = cfg::ScreenW / zoom;
    float visibleH = cfg::ScreenH / zoom;

    float targetX = player.pos.x - visibleW * 0.5f;
    float targetY = player.pos.y - visibleH * 0.5f;

    camX += (targetX - camX) * std::min(1.0f, dt * 6.0f);
    camY += (targetY - camY) * std::min(1.0f, dt * 6.0f);

    float mapW = map.cols * cfg::TileSize;
    float mapH = map.rows * cfg::TileSize;

    float maxCamX = std::max(0.0f, mapW - visibleW);
    float maxCamY = std::max(0.0f, mapH - visibleH);

    if (camX < 0) camX = 0;
    if (camY < 0) camY = 0;
    if (camX > maxCamX) camX = maxCamX;
    if (camY > maxCamY) camY = maxCamY;
}

// -----------------------------------------------------------
// Drawing helpers
// -----------------------------------------------------------

void Game::setDraw(SDL_Renderer* r, SDL_Color c) const {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

void Game::drawText(const std::string& s, int x, int y, SDL_Color c) {
    if (!font || s.empty()) return;

    TextKey key{ s, c.r, c.g, c.b, c.a };
    auto it = textCache.find(key);
    TextTex tex;

    if (it == textCache.end()) {
        SDL_Color white{ 255,255,255,255 };
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, s.c_str(), white);
        if (!surf) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, surf);
        tex.tex = t;
        tex.w = surf->w;
        tex.h = surf->h;
        SDL_FreeSurface(surf);
        textCache[key] = tex;
    }
    else {
        tex = it->second;
    }

    if (!tex.tex) return;
    SDL_Rect r{ x, y, tex.w, tex.h };
    SDL_SetTextureColorMod(tex.tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex.tex, c.a);
    SDL_RenderCopy(renderer, tex.tex, nullptr, &r);
}

// -----------------------------------------------------------
// Faction color
// -----------------------------------------------------------

SDL_Color Game::factionColor(Faction f) const {
    switch (f) {
    case Faction::Allies:  return SDL_Color{ 40, 190, 255, 255 };
    case Faction::Axis:    return SDL_Color{ 255, 80, 80, 255 };
    case Faction::Militia: return SDL_Color{ 240, 200, 80, 255 };
    case Faction::Rebels:  return SDL_Color{ 180, 120, 240, 255 };
    default:               return SDL_Color{ 255, 255, 255, 255 };
    }
}

inline bool sameSide(Faction a, Faction b)
{
    // Block A: Axis-side
    bool aAxis = (a == Faction::Axis || a == Faction::Militia);
    bool bAxis = (b == Faction::Axis || b == Faction::Militia);

    // Block B: Allied-side (includes Rebels + Player-as-Allies)
    bool aAllied = (a == Faction::Allies || a == Faction::Rebels);
    bool bAllied = (b == Faction::Allies || b == Faction::Rebels);

    if ((aAxis && bAxis) || (aAllied && bAllied))
        return true;

    return false;
}

bool Game::areEnemies(Faction a, Faction b) const
{
    if (a == b) return false;
    if (sameSide(a, b)) return false;
    return true;
}

// -----------------------------------------------------------
// Bark helpers (faction flavour + direction callouts)
// -----------------------------------------------------------

static std::string compass8(const Vec2& from, const Vec2& to) {
    Vec2 d = to - from;
    d.y = -d.y; // <-- flip so North means up on the Map
    if (lenSq(d) < 1.0f) return "here";

    // Screen coords: +y is DOWN. For compass words, treat +y as SOUTH.
    float a = std::atan2(-d.y, d.x); // -pi..pi, with "up" as +pi/2

    // Map to 8-way: E, NE, N, NW, W, SW, S, SE
    const char* dirs[8] = { "east", "north-east", "north", "north-west",
                            "west", "south-west", "south", "south-east" };

    float t = (a + 3.14159265f) / (2.0f * 3.14159265f); // 0..1
    int idx = (int)std::floor(t * 8.0f + 0.5f) & 7;
    return dirs[idx];
}

static const char* factionLabelFor(Faction speaker, Faction target) {
    if (speaker == Faction::Rebels) {
        switch (target) {
        case Faction::Axis:    return "fascists";
        case Faction::Militia: return "rats";
        case Faction::Allies:  return "rookies";
        default:               return "them";
        }
    }
    if (speaker == Faction::Militia) {
        switch (target) {
        case Faction::Axis:    return "comrade!";
        case Faction::Allies:  return "dogs";
        case Faction::Rebels:  return "scum";
        default:               return "them";
        }
    }
    if (speaker == Faction::Axis) {
        switch (target) {
        case Faction::Militia: return "boys";
        case Faction::Rebels:  return "scum";
        case Faction::Allies:  return "foreign dogs";
        default:               return "them";
        }
    }
    // Allies
    switch (target) {
    case Faction::Axis:    return "fascists";
    case Faction::Militia: return "trash";
    case Faction::Rebels:  return "resistance";
    default:               return "them";
    }
}

static std::string barkSpottedEnemy(Faction speaker, Faction target, const std::string& dirWord) {
    const char* label = factionLabelFor(speaker, target);
    if (speaker == Faction::Militia && target == Faction::Axis) {
        return std::string(label) + " " + dirWord + "!";
    }
    if (speaker == Faction::Axis && target == Faction::Militia) {
        return std::string("Militia ") + label + "! Look alive!";
    }
    return std::string(label) + " to the " + dirWord + "!";
}

static const char* planBark(Faction side, SquadIntent intent) {
    switch (side) {
    case Faction::Axis:
        switch (intent) {
        case SquadIntent::Hold:    return "Hold. Watch your sector.";
        case SquadIntent::Advance: return "Advance. Keep pressure.";
        case SquadIntent::Flank:   return "Flank them. Schnell!";
        case SquadIntent::Retreat: return "Fall back. Re-form!";
        case SquadIntent::Search:  return "Sweep. Find them.";
        }
        break;
    case Faction::Allies:
        switch (intent) {
        case SquadIntent::Hold:    return "Hold. Cover that lane.";
        case SquadIntent::Advance: return "Move up. Stay sharp.";
        case SquadIntent::Flank:   return "Flank. Go!";
        case SquadIntent::Retreat: return "Back! Find cover!";
        case SquadIntent::Search:  return "Sweep it. Eyes open.";
        }
        break;
    case Faction::Rebels:
        switch (intent) {
        case SquadIntent::Hold:    return "Hold here. Stay low.";
        case SquadIntent::Advance: return "Push! Push!";
        case SquadIntent::Flank:   return "Around the side! Move!";
        case SquadIntent::Retreat: return "Back! Back!";
        case SquadIntent::Search:  return "They're close. Check corners!";
        }
        break;
    case Faction::Militia:
        switch (intent) {
        case SquadIntent::Hold:    return "Stay put. Don't die.";
        case SquadIntent::Advance: return "Go on... go on!";
        case SquadIntent::Flank:   return "Try the side...!";
        case SquadIntent::Retreat: return "Run! Find cover!";
        case SquadIntent::Search:  return "Look around... carefully.";
        }
        break;
    }
    return "Move!";
}

static const char* calmBark(Faction side) {
    switch (side) {
    case Faction::Axis:    return "All clear. Stay alert.";
    case Faction::Allies:  return "Quiet... for now.";
    case Faction::Rebels:  return "Nothing. Keep moving.";
    case Faction::Militia: return "I don't like this...";
    default:               return "All clear.";
    }
}


Faction Game::randomEnemyFaction() const {
    std::vector<Faction> opts;
    if (missionParams.useAxis)    opts.push_back(Faction::Axis);
    if (missionParams.useMilitia) opts.push_back(Faction::Militia);
    if (missionParams.useRebels)  opts.push_back(Faction::Rebels);
    if (missionParams.useAllies)  opts.push_back(Faction::Allies);

    if (opts.empty()) {
        opts.push_back(Faction::Axis);
    }

    int idx = irand(0, (int)opts.size() - 1);
    return opts[idx];
}

Actor Game::makeUnit(Faction f, const Vec2& pos) {
    Actor a;
    a.pos = pos;
    a.team = f;

    // Square pawn for now – easier to match hitbox/rig later
    a.w = cfg::PawnSize;
    a.h = cfg::PawnSize;

    a.facing = Vec2(1, 0);
    a.hp = a.hpMax = (f == Faction::Axis ? 2 : 1);
    a.gun.magSize = 8;
    a.gun.inMag = 8;
    a.gun.fireCooldownS = (f == Faction::Axis ? 0.35f : 0.45f);
    a.gun.bulletDamage = 1;
    a.visionRange = (f == Faction::Axis ? 260.0f : 220.0f);
    a.visionFOVDeg = 95.0f;
    a.state = AIState::Patrol;
    // AI movement speeds (heavy/plodding)
    a.moveWalkSpeed = cfg::AIWalkSpeed;
    a.moveSprintSpeed = cfg::AISprintSpeed;

    buildDefaultHitRig(a);
    return a;
}

// -----------------------------------------------------------
// World rendering (map + foliage + mission icons)
// -----------------------------------------------------------

void Game::drawWorld() {
    // Tiles
    for (int r = 0; r < map.rows; ++r) {
        for (int c = 0; c < map.cols; ++c) {
            SDL_FRect tr = tileRectWorld(c, r);
            tr.x -= camX;
            tr.y -= camY;
            Tile t = map.at(c, r);

            if (t == Tile::Land)      setDraw(renderer, cfg::ColLand);
            else if (t == Tile::Water) setDraw(renderer, cfg::ColWater);
            else if (t == Tile::Wall)  setDraw(renderer, cfg::ColWall);
            else                       setDraw(renderer, cfg::ColLand);

            SDL_RenderFillRectF(renderer, &tr);
        }
    }

    // Primary mission icons before foliage (document, HVT, etc.)
    if ((mission.active || showMissionBrief || showMissionDebrief)) {
        if (mission.kind == MissionKind::Intel && mission.docPresent) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 200, 255);
            SDL_FRect ir{
                mission.docPos.x - 6.0f - camX,
                mission.docPos.y - 6.0f - camY,
                12.0f, 12.0f
            };
            SDL_RenderFillRectF(renderer, &ir);
        }
        if (mission.kind == MissionKind::HVT && mission.hvtPresent) {
            SDL_SetRenderDrawColor(renderer, 250, 150, 120, 255);
            SDL_FRect hr{
                mission.hvtPos.x - 7.0f - camX,
                mission.hvtPos.y - 7.0f - camY,
                14.0f, 14.0f
            };
            SDL_RenderFillRectF(renderer, &hr);
        }
        if (mission.kind == MissionKind::Sabotage && mission.sabotagePresent) {
            Uint8 a = mission.sabotageArmed ? 255 : 220;
            SDL_SetRenderDrawColor(renderer, 255, 190, 80, a);
            SDL_FRect sr{
                mission.sabotagePos.x - 8.0f - camX,
                mission.sabotagePos.y - 8.0f - camY,
                16.0f, 16.0f
            };
            SDL_RenderFillRectF(renderer, &sr);
        }
        if (mission.kind == MissionKind::Rescue && mission.rescuePresent) {
            SDL_SetRenderDrawColor(renderer, 180, 230, 255, 255);
            SDL_FRect rr{
                mission.rescuePos.x - 6.0f - camX,
                mission.rescuePos.y - 6.0f - camY,
                12.0f, 12.0f
            };
            SDL_RenderFillRectF(renderer, &rr);
        }
    }

    // Foliage + trunks (can obscure; extraction is redrawn later)
    playerUnderCanopy = false;

    for (const auto& leaf : leaves) {
        SDL_FRect lr = leaf.rect;
        lr.x -= camX;
        lr.y -= camY;

        bool underAny = false;

        if (playerPresent) {
            SDL_FRect pr = rectFrom(player.pos, player.w, player.h);
            pr.x -= camX;
            pr.y -= camY;
            if (SDL_HasIntersectionF(&lr, &pr)) {
                underAny = true;
                playerUnderCanopy = true;
            }
        }

        for (const auto& e : actors) {
            if (!e.alive()) continue;
            SDL_FRect er = rectFrom(e.pos, e.w, e.h);
            er.x -= camX;
            er.y -= camY;
            if (SDL_HasIntersectionF(&lr, &er)) {
                underAny = true;
                break;
            }
        }

        if (underAny) {
            SDL_SetRenderDrawColor(
                renderer,
                cfg::ColLeaf.r, cfg::ColLeaf.g, cfg::ColLeaf.b,
                (Uint8)(cfg::TreeFoliageAlpha * 255)
            );
        }
        else {
            setDraw(renderer, cfg::ColLeaf);
        }

        SDL_RenderFillRectF(renderer, &lr);
    }

    for (const auto& t : trunks)
        drawTrunkOctagon(t);

    // Extraction icon last (always visible above foliage)
    if ((mission.active || showMissionDebrief) && mission.extractPresent) {
        SDL_SetRenderDrawColor(renderer, 120, 220, 255, 255);
        SDL_FRect er{
            mission.extractPos.x - 8.0f - camX,
            mission.extractPos.y - 8.0f - camY,
            16.0f, 16.0f
        };
        SDL_RenderDrawRectF(renderer, &er);

        SDL_RenderDrawLineF(renderer,
            er.x, er.y + er.h * 0.5f,
            er.x + er.w, er.y + er.h * 0.5f);
        SDL_RenderDrawLineF(renderer,
            er.x + er.w * 0.5f, er.y,
            er.x + er.w * 0.5f, er.y + er.h);
    }
}

// -----------------------------------------------------------
// Actor / bullets / barks drawing
// (AI + senses + mission logic come in Part 2)
// -----------------------------------------------------------

bool Game::inScreen(float x, float y, float margin) const {
    return x >= -margin && y >= -margin &&
        x <= cfg::ScreenW + margin &&
        y <= cfg::ScreenH + margin;
}

void Game::drawBullets() {
    setDraw(renderer, cfg::ColBullet);
    for (const auto& bu : bullets) {
        SDL_FRect br{
            bu.pos.x - 2 - camX,
            bu.pos.y - 2 - camY,
            4,4
        };
        SDL_RenderFillRectF(renderer, &br);
    }
}

void Game::drawBarks() {
    if (!barksEnabled) return;
    const float cx = cfg::ScreenW * 0.5f;
    const float cy = cfg::ScreenH * 0.5f;
    const float margin = 24.0f;
    for (const auto& bk : barks) {
        float sx = (bk.pos.x - camX) * zoom;
        float sy = (bk.pos.y - camY) * zoom - 28.0f;
        if (sx >= 0 && sx <= cfg::ScreenW && sy >= 0 && sy <= cfg::ScreenH) {
            drawText(bk.text, (int)sx, (int)sy, cfg::ColUIAlt);
        }
        else {
            float vx = sx - cx;
            float vy = sy - cy;
            float L = std::sqrt(vx * vx + vy * vy);
            if (L < 1e-3f) continue;
            vx /= L; vy /= L;
            float tMin = 1e9f;
            float left = margin, right = cfg::ScreenW - margin;
            float top = margin, bottom = cfg::ScreenH - margin;

            if (vx > 0) {
                float t = (right - cx) / vx;
                float y = cy + vy * t;
                if (y >= top && y <= bottom && t > 0 && t < tMin) tMin = t;
            }
            if (vx < 0) {
                float t = (left - cx) / vx;
                float y = cy + vy * t;
                if (y >= top && y <= bottom && t > 0 && t < tMin) tMin = t;
            }
            if (vy > 0) {
                float t = (bottom - cy) / vy;
                float x = cx + vx * t;
                if (x >= left && x <= right && t > 0 && t < tMin) tMin = t;
            }
            if (vy < 0) {
                float t = (top - cy) / vy;
                float x = cx + vx * t;
                if (x >= left && x <= right && t > 0 && t < tMin) tMin = t;
            }
            if (tMin == 1e9f) continue;
            float ex = cx + vx * tMin;
            float ey = cy + vy * tMin;
            drawText(bk.text, (int)ex, (int)ey, cfg::ColUIAlt);
        }
    }
}

void Game::pushBark(const Vec2& pos, const char* txt, float ttl)
{
    if (!barksEnabled) return;

    barks.push_back({
        pos,
        std::string(txt),
        ttl
        });
}



void Game::drawVisionOutlineAndRay(const Actor& a) {
    if (!visionViz) return;
    float halfFov = deg2rad(a.visionFOVDeg * 0.5f);
    Vec2 dir = normalize(a.facing);
    float baseAng = std::atan2(dir.y, dir.x);
    float ang0 = baseAng - halfFov;
    float ang1 = baseAng + halfFov;
    float R = a.visionRange * (1.0f + 0.12f * (float)mission.alarmLevel);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 80);
    Vec2 p0 = a.pos + Vec2(std::cos(ang0), std::sin(ang0)) * R;
    Vec2 p1 = a.pos + Vec2(std::cos(ang1), std::sin(ang1)) * R;
    SDL_RenderDrawLineF(renderer,
        a.pos.x - camX, a.pos.y - camY,
        p0.x - camX, p0.y - camY
    );
    SDL_RenderDrawLineF(renderer,
        a.pos.x - camX, a.pos.y - camY,
        p1.x - camX, p1.y - camY
    );

    const int segs = 28;
    Vec2 prev = p0;
    for (int i = 1; i <= segs; i++) {
        float t = ang0 + (ang1 - ang0) * (float(i) / segs);
        Vec2 pt = a.pos + Vec2(std::cos(t), std::sin(t)) * R;
        SDL_RenderDrawLineF(renderer,
            prev.x - camX, prev.y - camY,
            pt.x - camX, pt.y - camY
        );
        prev = pt;
    }

    Vec2 tgtPos = a.pos;
    int  idx = -1;
    bool seesT = false;
    if (acquireThreat(a, tgtPos, idx, seesT) && seesT) {
        SDL_SetRenderDrawColor(renderer, 255, 200, 200, 140);
        SDL_RenderDrawLineF(renderer,
            a.pos.x - camX, a.pos.y - camY,
            tgtPos.x - camX, tgtPos.y - camY
        );
    }
}

void Game::drawActors() {
    // corpses
    for (const auto& cp : corpses) {
        setDraw(renderer, cfg::ColCorpse);
        SDL_FRect cr = rectFrom(cp, 20, 12);
        cr.x -= camX;
        cr.y -= camY;
        SDL_RenderFillRectF(renderer, &cr);
    }

    // player
    if (playerPresent) {
        setDraw(renderer, factionColor(player.team));
        SDL_FRect pr = rectFrom(player.pos, player.w, player.h);
        pr.x -= camX;
        pr.y -= camY;
        SDL_RenderFillRectF(renderer, &pr);

        setDraw(renderer, cfg::ColLine);
        Vec2 aP{ player.pos.x - camX, player.pos.y - camY };
        Vec2 bP{ aP.x + normalize(player.facing).x * 14.f,
                 aP.y + normalize(player.facing).y * 14.f };
        SDL_RenderDrawLineF(renderer, aP.x, aP.y, bP.x, bP.y);

        if (labelsEnabled) {
            std::string lab = "ALLY HP " + std::to_string(player.hp) +
                "  [" + std::string(weaponName(player.weapon.id)) + "]" +
                " M" + std::to_string(player.weapon.magAmmo) + "/R" + std::to_string(player.weapon.reserveAmmo);


            drawText(lab, (int)(pr.x - 10), (int)(pr.y + pr.h + 2), cfg::ColUI);
        }
        drawVisionOutlineAndRay(player);
    }

    // AI actors
    for (int idx = 0; idx < (int)actors.size(); ++idx) {
        const auto& e = actors[idx];
        if (!e.alive()) continue;

        setDraw(renderer, factionColor(e.team));
        SDL_FRect er = rectFrom(e.pos, e.w, e.h);
        er.x -= camX;
        er.y -= camY;
        SDL_RenderFillRectF(renderer, &er);

        bool isHVT =
            (mission.kind == MissionKind::HVT) &&
            (mission.active || showMissionDebrief) &&
            (idx == mission.hvtIndex);

        if (isHVT || e.isHVT) {
            SDL_SetRenderDrawColor(renderer, 255, 200, 140, 255);
            SDL_FRect hr{
                er.x - 4.0f,
                er.y - 4.0f,
                er.w + 8.0f,
                er.h + 8.0f
            };
            SDL_RenderDrawRectF(renderer, &hr);
        }

        if (e.selected && !mission.active) {
            SDL_SetRenderDrawColor(renderer,
                cfg::ColSelect.r, cfg::ColSelect.g,
                cfg::ColSelect.b, cfg::ColSelect.a);
            SDL_RenderDrawRectF(renderer, &er);
        }

        setDraw(renderer, cfg::ColLine);
        Vec2 a{ e.pos.x - camX, e.pos.y - camY };
        Vec2 b{ a.x + normalize(e.facing).x * 14.f,
                a.y + normalize(e.facing).y * 14.f };
        SDL_RenderDrawLineF(renderer, a.x, a.y, b.x, b.y);

        if (labelsEnabled) {
            const char* tn = (e.team == Faction::Axis ? "AXIS" :
                e.team == Faction::Militia ? "MIL" :
                e.team == Faction::Rebels ? "REB" : "ALLY");
            std::string lab = std::string(tn) + " HP " +
                std::to_string(e.hp) +
                " [" + std::string(weaponName(e.weapon.id)) + "]" +
                " M" + std::to_string(e.weapon.magAmmo) + "/R" + std::to_string(e.weapon.reserveAmmo) +
                (e.isLeader ? " *" : "") +
                (e.isHVT ? " [HVT] " : " ") +
                std::string("[") + stateName(e.state) + "]";

            drawText(lab, (int)(er.x - 10), (int)(er.y + er.h + 2), cfg::ColUI);
        }

        if (showAIIntentViz) {
            // Draw AI intent ray / vision outline
            drawVisionOutlineAndRay(e);

            // Draw guard/order point + line (if it has an order)
            if (e.hasOrder) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 140);
                SDL_RenderDrawLineF(renderer,
                    e.pos.x - camX, e.pos.y - camY,
                    e.orderPos.x - camX, e.orderPos.y - camY);

                SDL_FRect gp{
                    e.orderPos.x - 3.0f - camX,
                    e.orderPos.y - 3.0f - camY,
                    6.0f, 6.0f
                };
                SDL_RenderDrawRectF(renderer, &gp);
            }
        }

    }
}

void Game::drawSquadDebug() {
    // We draw in world space; assume renderer scale is already zoomed when called.
    for (const Squad& s : squads) {
        if (s.members.empty()) continue;

        // Compute center of alive members
        int aliveCount = 0;
        Vec2 center{ 0,0 };
        for (int idx : s.members) {
            if (idx < 0 || idx >= (int)actors.size()) continue;
            const Actor& a = actors[idx];
            if (!a.alive()) continue;
            center.x += a.pos.x;
            center.y += a.pos.y;
            ++aliveCount;
        }
        if (aliveCount == 0) continue;
        center.x /= aliveCount;
        center.y /= aliveCount;

        // Small circle at squad center
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 140);
        const float r = 6.0f;
        SDL_FRect cr{
            center.x - r - camX,
            center.y - r - camY,
            r * 2.0f,
            r * 2.0f
        };
        SDL_RenderDrawRectF(renderer, &cr);

        // Enemy contact point
        if (s.debugHasEnemy) {
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 180);
            SDL_FRect er{
                s.debugEnemyPos.x - 3.0f - camX,
                s.debugEnemyPos.y - 3.0f - camY,
                6.0f, 6.0f
            };
            SDL_RenderFillRectF(renderer, &er);
            SDL_RenderDrawLineF(renderer,
                center.x - camX, center.y - camY,
                s.debugEnemyPos.x - camX, s.debugEnemyPos.y - camY);
        }

        // Cover / retreat anchor
        if (s.debugHasCover) {
            SDL_SetRenderDrawColor(renderer, 0, 200, 255, 180);
            SDL_FRect rr{
                s.debugCoverPos.x - 4.0f - camX,
                s.debugCoverPos.y - 4.0f - camY,
                8.0f, 8.0f
            };
            SDL_RenderDrawRectF(renderer, &rr);
        }

        // Flank target
        if (s.debugHasFlank) {
            SDL_SetRenderDrawColor(renderer, 120, 255, 120, 200);
            SDL_FRect fr{
                s.debugFlankPos.x - 4.0f - camX,
                s.debugFlankPos.y - 4.0f - camY,
                8.0f, 8.0f
            };
            SDL_RenderDrawRectF(renderer, &fr);
            SDL_RenderDrawLineF(renderer,
                center.x - camX, center.y - camY,
                s.debugFlankPos.x - camX, s.debugFlankPos.y - camY);
        }
    }
}


// -----------------------------------------------------------
// Mission HUD + Mission Params HUD (buttons, toggles)
// -----------------------------------------------------------

void Game::drawMissionHUD() {
    if (!hudEnabled) return;
    int y = 8;

    std::string phaseStr;
    switch (mission.phase) {
    case MissionPhase::Ingress:  phaseStr = "INGRESS";  break;
    case MissionPhase::Exfil:    phaseStr = "EXFIL";    break;
    case MissionPhase::Complete: phaseStr = "COMPLETE"; break;
    case MissionPhase::Failed:   phaseStr = "FAILED";   break;
    default:                     phaseStr = "";         break;
    }

    std::string kindStr;
    switch (mission.kind) {
    case MissionKind::Intel:     kindStr = "INTEL";     break;
    case MissionKind::HVT:       kindStr = "HVT";       break;
    case MissionKind::Sabotage:  kindStr = "SABOTAGE";  break;
    case MissionKind::Rescue:    kindStr = "RESCUE";    break;
    case MissionKind::Sweep:     kindStr = "SWEEP";     break;
    }

    drawText("MISSION [" + kindStr + "]: " + phaseStr, 10, y, cfg::ColUI);
    y += 20;

    auto line = [&](const char* txt) {
        drawText(txt, 10, y, cfg::ColUI);
        y += 18;
        };

    if (mission.kind == MissionKind::Intel) {
        if (mission.phase == MissionPhase::Ingress) {
            line("TASK: Locate and retrieve the document.");
        }
        else if (mission.phase == MissionPhase::Exfil) {
            line("TASK: Reach extraction with the document.");
        }
        else if (mission.phase == MissionPhase::Complete) {
            line("TASK: Mission complete.");
        }
        else if (mission.phase == MissionPhase::Failed) {
            line("TASK: Mission failed.");
        }
    }
    else if (mission.kind == MissionKind::HVT) {
        if (mission.phase == MissionPhase::Ingress) {
            line("TASK: Locate and eliminate the officer.");
        }
        else if (mission.phase == MissionPhase::Exfil) {
            line("TASK: Reach extraction.");
        }
        else if (mission.phase == MissionPhase::Complete) {
            line("TASK: Mission complete.");
        }
        else if (mission.phase == MissionPhase::Failed) {
            line("TASK: Mission failed.");
        }
    }
    else if (mission.kind == MissionKind::Sabotage) {
        if (mission.phase == MissionPhase::Ingress) {
            line("TASK: Infiltrate and sabotage the target.");
        }
        else if (mission.phase == MissionPhase::Exfil) {
            line("TASK: Reach extraction.");
        }
        else if (mission.phase == MissionPhase::Complete) {
            line("TASK: Mission complete.");
        }
        else if (mission.phase == MissionPhase::Failed) {
            line("TASK: Mission failed.");
        }
    }
    else if (mission.kind == MissionKind::Rescue) {
        if (mission.phase == MissionPhase::Ingress) {
            line("TASK: Reach and secure the hostage.");
        }
        else if (mission.phase == MissionPhase::Exfil) {
            line("TASK: Escort the hostage to extraction.");
        }
        else if (mission.phase == MissionPhase::Complete) {
            line("TASK: Mission complete.");
        }
        else if (mission.phase == MissionPhase::Failed) {
            line("TASK: Mission failed.");
        }
    }
    else { // Sweep
        if (mission.phase == MissionPhase::Ingress) {
            line("TASK: Clear hostile forces in the area.");
        }
        else if (mission.phase == MissionPhase::Exfil) {
            line("TASK: Area secure. Move to extraction.");
        }
        else if (mission.phase == MissionPhase::Complete) {
            line("TASK: Mission complete.");
        }
        else if (mission.phase == MissionPhase::Failed) {
            line("TASK: Mission failed.");
        }

        if (mission.sweepRequiredKills > 0) {
            char sbuf[128];
            std::snprintf(sbuf, sizeof(sbuf),
                "Sweep: %d / %d enemies eliminated",
                mission.enemiesKilled,
                mission.sweepRequiredKills);
            drawText(sbuf, 10, y, cfg::ColUI);
            y += 18;
        }
    }

    if (playerPresent) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "HP: %d / %d   Mag: %d",
            player.hp, player.hpMax, player.gun.inMag);
        drawText(buf, 10, y, cfg::ColUI);
        y += 18;
    }

    char abuf[64];
    std::snprintf(abuf, sizeof(abuf), "ALARM: %d", mission.alarmLevel);
    drawText(abuf, 10, y, cfg::ColUI);
    y += 18;
}

void Game::drawMissionParamsHUD() {
    if (!showMissionParams || mission.active) return;

    int x = 10;
    int y = 140;

    drawText("--- MISSION PARAMS --- (F6 to hide)", x, y, cfg::ColUIAlt);
    y += 22;
    drawText("Click [-] or [+] buttons, or faction/mission boxes.", x, y, cfg::ColUI);
    y += 20;
    drawText("Applies when starting the NEXT mission (F11).", x, y, cfg::ColUI);
    y += 24;

    auto drawRow = [&](const std::string& label, const std::string& valueStr, int rowIndex) {
        int rowY = y + rowIndex * 22;
        drawText(label, x, rowY, cfg::ColUIAlt);

        int bx = x + 260;
        SDL_Rect minusR{ bx, rowY - 2, 18, 16 };
        SDL_Rect plusR{ bx + 60, rowY - 2, 18, 16 };

        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderFillRect(renderer, &minusR);
        SDL_RenderFillRect(renderer, &plusR);
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(renderer, &minusR);
        SDL_RenderDrawRect(renderer, &plusR);

        drawText("-", minusR.x + 5, minusR.y - 1, cfg::ColUI);
        drawText("+", plusR.x + 5, plusR.y - 1, cfg::ColUI);

        drawText(valueStr, minusR.x + 26, rowY, cfg::ColUI);
        };

    char buf[64];

    std::snprintf(buf, sizeof(buf), "%d", missionParams.enemySquadsBase);
    drawRow("Enemy squads (non-sweep)", buf, 0);

    std::snprintf(buf, sizeof(buf), "%d", missionParams.sweepSquadsBase);
    drawRow("Sweep squads", buf, 1);

    std::snprintf(buf, sizeof(buf), "%d", missionParams.respawnWaves);
    drawRow("Enemy respawn waves", buf, 2);

    std::snprintf(buf, sizeof(buf), "%.2f", missionParams.patrolRadiusScale);
    drawRow("Patrol radius scale", buf, 3);

    std::snprintf(buf, sizeof(buf), "%.2f", missionParams.sweepFraction);
    drawRow("Sweep fraction (kills required)", buf, 4);

    y += 22 * 6;

    // Faction toggles
    drawText("Active factions (enemies):", x, y, cfg::ColUIAlt);
    y += 20;

    auto drawToggle = [&](const char* label, bool on, int bx, int by) {
        SDL_Rect r{ bx, by, 70, 18 };
        if (on) SDL_SetRenderDrawColor(renderer, 60, 120, 60, 255);
        else    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(renderer, &r);
        drawText(label, r.x + 4, r.y + 2, cfg::ColUI);
        };

    int fx = x + 20;
    int fy = y;

    drawToggle("AXIS", missionParams.useAxis, fx, fy);
    drawToggle("MILITIA", missionParams.useMilitia, fx + 80, fy);
    drawToggle("REBELS", missionParams.useRebels, fx + 160, fy);
    drawToggle("ALLIES", missionParams.useAllies, fx + 240, fy); // NEW

    y += 26;

    // Mission type selection
    drawText("Preferred mission type (F11 uses current):", x, y, cfg::ColUIAlt);
    y += 20;

    auto drawKindBox = [&](MissionKind k, const char* label, int bx) {
        bool active = (mission.kind == k);
        SDL_Rect r{ bx, y, 80, 18 };
        if (active) SDL_SetRenderDrawColor(renderer, 90, 90, 140, 255);
        else        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(renderer, &r);
        drawText(label, r.x + 4, r.y + 2, cfg::ColUI);
        };

    int kx = x + 20;
    drawKindBox(MissionKind::Intel, "INTEL", kx);
    drawKindBox(MissionKind::HVT, "HVT", kx + 90);
    drawKindBox(MissionKind::Sabotage, "SAB", kx + 180);
    drawKindBox(MissionKind::Rescue, "RESCUE", kx + 270);
    drawKindBox(MissionKind::Sweep, "SWEEP", kx + 360);
}


// -----------------------------------------------------------
// HUD + overlays (non-mission)
// -----------------------------------------------------------

void Game::drawHUD() {
    if (mission.active) {
        drawMissionHUD();
        if (showMissionParams) drawMissionParamsHUD();
        return;
    }

    if (!hudEnabled) return;
    int y = 8;

    std::string modeStr = (mode == Mode::Player ? "PLAYER" :
        mode == Mode::Control ? "CONTROL" : "PAINT");
    drawText("[MODE] " + modeStr + "   (TAB)   " +
        std::string(paused ? "[PAUSED]" : "[RUNNING]") +
        "   F11: mission briefing   F6: mission params", 10, y, cfg::ColUI);
    y += 18;

    char zb[64];
    std::snprintf(zb, sizeof(zb), "Zoom: %.2fx (Wheel) | F1 labels  F2 barks  F3 hearing  F4 vision", zoom);
    drawText(zb, 10, y, cfg::ColUI);
    y += 18;

    drawText("PAINT MODE: 0 player  7 allies  1 axis  2 militia  3 rebels  4 erase  5 wall  6 tree", 10, y, cfg::ColUI);
    y += 18;

    drawText("CONTROL MODE: LMB select/drag. RMB move squad to clicked tile.", 10, y, cfg::ColUI);
    y += 18;

    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "CAM (%.0f, %.0f) | WORLD %dx%d",
        camX, camY,
        map.cols * cfg::TileSize,
        map.rows * cfg::TileSize);
    drawText(buf, 10, y, cfg::ColUI);
    y += 18;

    if (showMissionParams) {
        drawMissionParamsHUD();
    }
}

// -----------------------------------------------------------
// Random positions
// -----------------------------------------------------------

Vec2 Game::randomWalkablePos(int marginTiles) const {
    Vec2 fallback{
        map.cols * cfg::TileSize * 0.5f,
        map.rows * cfg::TileSize * 0.5f
    };

    for (int tries = 0; tries < 256; ++tries) {
        int c = irand(marginTiles, map.cols - 1 - marginTiles);
        int r = irand(marginTiles, map.rows - 1 - marginTiles);
        if (!isNavWalkable(c, r)) continue;
        SDL_FRect tr = tileRectWorld(c, r);
        return Vec2{ tr.x + tr.w * 0.5f, tr.y + tr.h * 0.5f };
    }
    return fallback;
}

Vec2 Game::randomClearPos(int marginTiles,
    const std::vector<Vec2>& avoid,
    float minDistTiles) const {
    float minDistPx = minDistTiles * cfg::TileSize;
    float minDistSq = minDistPx * minDistPx;

    Vec2 fallback = randomWalkablePos(marginTiles);

    // How far from tree trunks we want to be (px)
    const float trunkClear = cfg::PawnSize * 2.4f;
    const float trunkClearSq = trunkClear * trunkClear;

    for (int tries = 0; tries < 512; ++tries) {
        int c = irand(marginTiles, map.cols - 1 - marginTiles);
        int r = irand(marginTiles, map.rows - 1 - marginTiles);
        if (!isClearLand(c, r)) continue;

        SDL_FRect tr = tileRectWorld(c, r);
        Vec2 p{ tr.x + tr.w * 0.5f, tr.y + tr.h * 0.5f };

        bool bad = false;

        // Keep clear of manually requested avoids (player, objectives, etc.)
        for (auto& a : avoid) {
            if (lenSq(p - a) < minDistSq) {
                bad = true;
                break;
            }
        }
        if (bad) continue;

        // Also ensure immediate neighbors aren't walls/water
        bool neighborBlocked = false;
        for (int dr = -1; dr <= 1 && !neighborBlocked; ++dr) {
            for (int dc = -1; dc <= 1 && !neighborBlocked; ++dc) {
                int nc = c + dc, nr = r + dr;
                if (!inBoundsTile(nc, nr)) { neighborBlocked = true; break; }
                Tile t = map.at(nc, nr);
                if (t == Tile::Wall || t == Tile::Water) {
                    neighborBlocked = true;
                }
            }
        }
        if (neighborBlocked) continue;

        // NEW: avoid being too close to tree trunks (no spawning inside trunks)
        for (const auto& t : trunks) {
            if (lenSq(p - t.center) < trunkClearSq) {
                bad = true;
                break;
            }
        }
        if (bad) continue;

        return p;
    }
    return fallback;
}


Vec2 Game::randomOpenCellAround(const Vec2& center, int radiusTiles) const {
    int baseC = int(center.x / cfg::TileSize);
    int baseR = int(center.y / cfg::TileSize);
    for (int tries = 0; tries < 128; ++tries) {
        int c = baseC + irand(-radiusTiles, radiusTiles);
        int r = baseR + irand(-radiusTiles, radiusTiles);
        if (!isNavWalkable(c, r)) continue;
        SDL_FRect tr = tileRectWorld(c, r);
        return Vec2{ tr.x + tr.w * 0.5f, tr.y + tr.h * 0.5f };
    }
    return center;
}

// -----------------------------------------------------------
// Collision & pathfinding
// -----------------------------------------------------------

bool Game::collideSolid(const SDL_FRect& r) const {
    int c0 = int(std::floor(r.x / cfg::TileSize));
    int r0 = int(std::floor(r.y / cfg::TileSize));
    int c1 = int(std::floor((r.x + r.w) / cfg::TileSize));
    int r1 = int(std::floor((r.y + r.h) / cfg::TileSize));

    for (int rr = r0; rr <= r1; ++rr) {
        for (int cc = c0; cc <= c1; ++cc) {
            if (!map.inBounds(cc, rr)) return true;
            Tile t = map.at(cc, rr);
            if (t == Tile::Wall || t == Tile::Water) {
                return true;
            }
        }
    }

    // Narrow trunk collision: small square around trunk center
    for (const auto& t : trunks) {
        float half = t.dia * 0.5f;
        SDL_FRect tr{
            t.center.x - half,
            t.center.y - half,
            t.dia,
            t.dia
        };
        SDL_FRect rr2 = r;
        if (SDL_HasIntersectionF(&tr, &rr2)) {
            return true;
        }
    }

    return false;
}

void Game::moveWithCollide(Actor& a, const Vec2& desiredVel, float /*maxSpeed*/, float dt) {
    Vec2 vel = desiredVel;
    SDL_FRect r = rectFrom(a.pos, a.w, a.h);

    // X axis
    float dx = vel.x * dt;
    if (std::fabs(dx) > 0.0001f) {
        r.x += dx;
        if (collideSolid(r)) {
            r.x -= dx;
            vel.x = 0.0f;
        }
    }

    // Y axis
    float dy = vel.y * dt;
    if (std::fabs(dy) > 0.0001f) {
        r.y += dy;
        if (collideSolid(r)) {
            r.y -= dy;
            vel.y = 0.0f;
        }
    }

    a.pos.x = r.x + r.w * 0.5f;
    a.pos.y = r.y + r.h * 0.5f;
}

void Game::resolveActorCollisions(float /*dt*/) {
    const float minSep = cfg::PawnSize * 0.9f; // minimum separation distance
    const float minSepSq = minSep * minSep;

    // Simple pairwise separation: keep AI from overlapping, but don't shove into walls.
    for (size_t i = 0; i < actors.size(); ++i) {
        Actor& a = actors[i];
        if (!a.alive()) continue;

        for (size_t j = i + 1; j < actors.size(); ++j) {
            Actor& b = actors[j];
            if (!b.alive()) continue;

            Vec2 diff{ b.pos.x - a.pos.x, b.pos.y - a.pos.y };
            float d2 = lenSq(diff);
            if (d2 < 1e-4f || d2 > minSepSq) continue;

            float d = std::sqrt(d2);
            Vec2 n{ diff.x / d, diff.y / d };
            float overlap = (minSep - d);

            // Split displacement between both actors
            Vec2 deltaA{ -0.5f * overlap * n.x, -0.5f * overlap * n.y };
            Vec2 deltaB{ 0.5f * overlap * n.x,  0.5f * overlap * n.y };

            // Try moving A
            Vec2 oldA = a.pos;
            a.pos.x += deltaA.x;
            a.pos.y += deltaA.y;
            {
                SDL_FRect ra = rectFrom(a.pos, a.w, a.h);
                if (collideSolid(ra)) {
                    a.pos = oldA; // revert if we pushed into wall/water/trunk
                }
            }

            // Try moving B
            Vec2 oldB = b.pos;
            b.pos.x += deltaB.x;
            b.pos.y += deltaB.y;
            {
                SDL_FRect rb = rectFrom(b.pos, b.w, b.h);
                if (collideSolid(rb)) {
                    b.pos = oldB;
                }
            }
        }
    }
}


bool Game::buildPath(const Vec2& start, const Vec2& goal, Actor& a) const {
    const int cols = map.cols;
    const int rows = map.rows;
    const int count = cols * rows;

    auto toIndex = [&](int c, int r) {
        return r * cols + c;
        };

    int sc = int(start.x / cfg::TileSize);
    int sr = int(start.y / cfg::TileSize);
    int gc = int(goal.x / cfg::TileSize);
    int gr = int(goal.y / cfg::TileSize);

    if (!inBoundsTile(sc, sr) || !inBoundsTile(gc, gr)) {
        return false;
    }
    if (!isNavWalkable(sc, sr) || !isNavWalkable(gc, gr)) {
        return false;
    }

    std::vector<float> gscore(count, 1e9f);
    std::vector<float> fscore(count, 1e9f);
    std::vector<int>   parent(count, -1);
    std::vector<bool>  openFlag(count, false);
    std::vector<bool>  closed(count, false);

    auto hfun = [&](int c, int r) {
        float dx = float(c - gc);
        float dy = float(r - gr);
        return std::sqrt(dx * dx + dy * dy);
        };

    struct Node {
        int idx;
        float f;
    };

    std::vector<Node> open;
    int sIdx = toIndex(sc, sr);
    gscore[sIdx] = 0.0f;
    fscore[sIdx] = hfun(sc, sr);
    open.push_back({ sIdx, fscore[sIdx] });
    openFlag[sIdx] = true;

    auto popBest = [&]() -> int {
        int best = -1;
        float bestF = 1e9f;
        for (int i = 0; i < (int)open.size(); ++i) {
            if (open[i].f < bestF) {
                bestF = open[i].f;
                best = i;
            }
        }
        if (best == -1) return -1;
        int idx = open[best].idx;
        open[best] = open.back();
        open.pop_back();
        openFlag[idx] = false;
        return idx;
        };

    const int dC[4] = { 1,-1,0,0 };
    const int dR[4] = { 0,0,1,-1 };

    int gIdx = -1;
    for (;;) {
        int cur = popBest();
        if (cur == -1) break;
        if (cur == toIndex(gc, gr)) {
            gIdx = cur;
            break;
        }
        if (closed[cur]) continue;
        closed[cur] = true;

        int cc = cur % cols;
        int rr = cur / cols;
        for (int k = 0; k < 4; ++k) {
            int nc = cc + dC[k];
            int nr = rr + dR[k];
            if (!inBoundsTile(nc, nr)) continue;
            if (!isNavWalkable(nc, nr)) continue;

            int ni = toIndex(nc, nr);
            if (closed[ni]) continue;
            float cost = 1.0f;
            float tentativeG = gscore[cur] + cost;
            if (tentativeG < gscore[ni]) {
                gscore[ni] = tentativeG;
                fscore[ni] = tentativeG + hfun(nc, nr);
                parent[ni] = cur;
                if (!openFlag[ni]) {
                    open.push_back({ ni, fscore[ni] });
                    openFlag[ni] = true;
                }
            }
        }
    }

    if (gIdx == -1) {
        a.path.clear();
        a.pathIndex = -1;
        return false;
    }

    std::vector<Vec2> rev;
    int cur = gIdx;
    while (cur != -1) {
        int c = cur % cols;
        int r = cur / cols;
        SDL_FRect tr = tileRectWorld(c, r);
        rev.push_back(Vec2{ tr.x + tr.w * 0.5f, tr.y + tr.h * 0.5f });
        cur = parent[cur];
    }
    std::reverse(rev.begin(), rev.end());

    a.path = rev;
    a.pathIndex = rev.empty() ? -1 : 0;
    a.repathTimer = 0.0f;
    return true;
}

void Game::clearPath(Actor& a) const {
    a.path.clear();
    a.pathIndex = -1;
}

Vec2 Game::findNearestCoverToward(const Vec2& from, const Vec2& threat) const {
    Vec2 best = from;
    float bestDot = -1e9f;

    Vec2 toThreat = normalize(threat - from);
    int baseC = int(from.x / cfg::TileSize);
    int baseR = int(from.y / cfg::TileSize);

    int radiusTiles = 6;
    for (int dr = -radiusTiles; dr <= radiusTiles; ++dr) {
        for (int dc = -radiusTiles; dc <= radiusTiles; ++dc) {
            int c = baseC + dc;
            int r = baseR + dr;
            if (!inBoundsTile(c, r)) continue;
            Tile t = map.at(c, r);
            if (t != Tile::Wall) continue;

            SDL_FRect tr = tileRectWorld(c, r);
            Vec2 center{ tr.x + tr.w * 0.5f, tr.y + tr.h * 0.5f };
            Vec2 toCell = normalize(center - from);
            float d = length(center - from);
            if (d > cfg::TileSize * 8.0f) continue;

            float dot = toThreat.x * toCell.x + toThreat.y * toCell.y;
            if (dot > bestDot) {
                bestDot = dot;
                best = center;
            }
        }
    }
    return best;
}



// -----------------------------------------------------------
// LOS + hearing + threat acquisition
// -----------------------------------------------------------

bool Game::losClear(const Vec2& a, const Vec2& b) const {
    const int steps = 32;
    Vec2 d = b - a;
    for (int i = 1; i <= steps; ++i) {
        float t = float(i) / float(steps);
        Vec2 p = a + d * t;
        int c = int(p.x / cfg::TileSize);
        int r = int(p.y / cfg::TileSize);
        if (!inBoundsTile(c, r)) return false;
        Tile tile = map.at(c, r);
        if (tile == Tile::Wall || tile == Tile::Water) {
            return false;
        }
    }
    return true;
}

bool Game::sees(const Actor& a, const Vec2& targetPos, bool& outLOS) const {
    Vec2 to = targetPos - a.pos;
    float dist = length(to);
    if (dist < 1e-3f) { outLOS = false; return false; }

    float effRange = a.visionRange * (1.0f + 0.12f * (float)mission.alarmLevel);
    if (dist > effRange) { outLOS = false; return false; }

    Vec2 dir = normalize(to);
    float dot = a.facing.x * dir.x + a.facing.y * dir.y;
    float angDeg = std::acos(std::clamp(dot, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
    if (angDeg > a.visionFOVDeg * 0.5f) { outLOS = false; return false; }

    bool clear = losClear(a.pos, targetPos);
    outLOS = clear;
    return clear;
}

bool Game::hears(const Actor& a, const SoundPing& s) const {
    float d = length(s.pos - a.pos);
    return d <= s.radiusPx;
}

bool Game::acquireThreat(const Actor& a, Vec2& threatPos, int& threatIdx, bool& seesThreat) const {
    threatIdx = -1;
    seesThreat = false;
    bool found = false;
    float bestScore = -1.0f;

    auto considerVisual = [&](const Vec2& p, int idx) {
        bool los = false;
        if (!sees(a, p, los) || !los) return;

        float dist = length(p - a.pos);
        float effRange = a.visionRange * (1.0f + 0.12f * (float)mission.alarmLevel);
        float score = 2.0f + (effRange - dist) * 0.01f; // heavier than hearing
        if (score > bestScore) {
            bestScore = score;
            found = true;
            seesThreat = true;
            threatPos = p;
            threatIdx = idx;
        }
        };

    // Visual: player
    if (playerPresent && player.alive() && areEnemies(a.team, player.team)) {
        considerVisual(player.pos, -1);
    }

    // Visual: other actors
    for (int i = 0; i < (int)actors.size(); ++i) {
        const Actor& o = actors[i];
        if (!o.alive()) continue;
        if (!areEnemies(a.team, o.team)) continue;
        considerVisual(o.pos, i);
    }

    // Hearing: pings
    for (const auto& s : sounds) {
        if (!hears(a, s)) continue;
        float d = length(s.pos - a.pos);
        float score = 1.0f + (s.radiusPx - d) * 0.002f;
        if (score > bestScore) {
            bestScore = score;
            found = true;
            if (!seesThreat) {
                threatPos = s.pos;
                threatIdx = -1;
            }
        }
    }

    return found;
}

// -----------------------------------------------------------
// Squads
// -----------------------------------------------------------

void Game::placeSquad(Faction team, int worldX, int worldY, int count) {
    Squad s;
    s.id = (int)squads.size();
    s.side = team;
    s.home = Vec2{ (float)worldX, (float)worldY };
    s.patrolRadius = 220.0f * missionParams.patrolRadiusScale;
    s.role = MissionRole::AreaPatrol;
    s.roleAnchor = s.home;
    s.roleRadius = s.patrolRadius;
    s.leader = -1;
    s.mem.decayS = 8.0f;

    const float spacing = 24.0f;
    for (int i = 0; i < count; ++i) {
        float ang = (6.2831853f * i) / std::max(1, count);
        Vec2 offset{ std::cos(ang) * spacing, std::sin(ang) * spacing };
        Vec2 spawn = s.home + offset;

        // Small nudge if spawning in a wall
        int c = int(spawn.x / cfg::TileSize);
        int r = int(spawn.y / cfg::TileSize);
        if (!isNavWalkable(c, r)) {
            spawn = randomClearPos(4, { s.home }, 4.0f);
        }

        Actor a = makeUnit(team, spawn);
        a.squadId = s.id;
        if (i == 0) {
            a.isLeader = true;
            s.leader = (int)actors.size();
        }
        applyFactionLoadout(a);
        actors.push_back(a);
        s.members.push_back((int)actors.size() - 1);
    }


    
    // Squad bookkeeping
    s.initialCount = (int)s.members.size();
    s.suppression = 0.0f;
    s.confidence = 1.0f;

// --- NEW: build a few checkpoints around this squad's anchor ---

    Vec2 anchor = (s.role == MissionRole::None) ? s.home : s.roleAnchor;
    const float maxR = std::min(s.patrolRadius * 0.6f, 260.0f);
    int numCP = 2 + irand(0, 1); // 2–3 checkpoints

    for (int i = 0; i < numCP; ++i) {
        float ang = frand(0.0f, 6.28318f);
        float rad = frand(maxR * 0.3f, maxR);
        Vec2 cand = anchor + Vec2(std::cos(ang) * rad, std::sin(ang) * rad);

        int c = int(cand.x / cfg::TileSize);
        int r = int(cand.y / cfg::TileSize);
        if (!inBoundsTile(c, r) || !isNavWalkable(c, r))
            continue;

        SDL_FRect tr = tileRectWorld(c, r);
        Vec2 cp{ tr.x + tr.w * 0.5f, tr.y + tr.h * 0.5f };
        s.checkpoints.push_back(cp);
    }

    if (!s.checkpoints.empty()) {
        s.currentCheckpoint = 0;
        s.currentGoal = s.checkpoints[0];
        s.mode = SquadMode::Calm;
        s.modeTimer = frand(3.0f, 8.0f); // they’ll sit a bit before doing anything
    }

    squads.push_back(s);

}

void Game::addSuppression(int squadId, float amount)
{
    if (squadId < 0 || squadId >= (int)squads.size()) return;
    Squad& s = squads[squadId];
    s.suppression = std::clamp(s.suppression + amount, 0.0f, 100.0f);
}


int Game::countLivingEnemies() const {
    int cnt = 0;
    for (const auto& a : actors) {
        if (!a.alive()) continue;
        if (!areEnemies(a.team, player.team)) continue;
        cnt++;
    }
    return cnt;
}

void Game::updateSquadBrain(int sid, float dt) {
    if (sid < 0 || sid >= (int)squads.size()) return;
    Squad& s = squads[sid];

    // --- count alive members
    int alive = 0;
    for (int idx : s.members)
    {
        if (idx < 0 || idx >= (int)actors.size()) continue;
        if (actors[idx].alive()) alive++;
    }
    if (s.initialCount <= 0) s.initialCount = std::max(1, alive);

    float casualtyFrac = 1.0f - (alive / float(s.initialCount));

    // --- suppression decay
    const float suppressDecay = 12.0f; // units per second, tune to taste
    s.suppression = std::max(0.0f, s.suppression - suppressDecay * dt);
    float suppressNorm = std::clamp(s.suppression / 40.0f, 0.0f, 1.0f); // “full suppression” around 40

    // --- base confidence from casualties and suppression
    float conf = 1.0f;
    conf -= 0.6f * casualtyFrac;
    conf -= 0.4f * suppressNorm;

    bool highLoss = (casualtyFrac > 0.6f);
    bool midLoss = (casualtyFrac > 0.3f);
    bool lowConf = (conf < 0.4f);
    bool midConf = (conf >= 0.4f && conf < 0.7f);
    bool highConf = (conf >= 0.7f);

    // --- faction flavour (baseline confidence)
    switch (s.side) {
    case Faction::Axis:    conf += 0.10f; break;
    case Faction::Allies:  conf += 0.05f; break;
    case Faction::Rebels:  conf += 0.00f; break;
    case Faction::Militia: conf -= 0.10f; break;
    }

    s.confidence = std::clamp(conf, 0.0f, 1.0f);

    if (s.members.empty()) return;

    // --- Basic bookkeeping ---
    s.intentTimer = std::max(0.0f, s.intentTimer - dt);
    s.coordTimer = std::max(0.0f, s.coordTimer - dt);
    s.timeSinceContact += dt;
    // Cooldowns for idle behaviours
    s.calmBarkTimerS = std::max(0.0f, s.calmBarkTimerS - dt);
    s.idleScanCooldownS = std::max(0.0f, s.idleScanCooldownS - dt);

    s.underFire = false;
    s.debugHasEnemy = s.debugHasCover = s.debugHasFlank = false;

    // Compute alive members + center
    int aliveCount = 0;
    Vec2 center{ 0,0 };
    for (int idx : s.members) {
        if (idx < 0 || idx >= (int)actors.size()) continue;
        Actor& a = actors[idx];
        if (!a.alive()) continue;
        center.x += a.pos.x;
        center.y += a.pos.y;
        ++aliveCount;
    }
    if (aliveCount == 0) return;
    center.x /= aliveCount;
    center.y /= aliveCount;

    // Casualties since last frame
    if (s.lastAliveCount == 0) s.lastAliveCount = aliveCount;
    if (aliveCount < s.lastAliveCount) {
        s.recentCasualties += (s.lastAliveCount - aliveCount);
        s.lastAliveCount = aliveCount;
    }

    // --- Squad perception: aggregate threats ---
    bool   contactNow = false;
    bool   anyVisual = false;
    Vec2   enemyAccum{ 0,0 };
    int    enemySamples = 0;

    // auto pushBark = [&](const Vec2& p, const char* txt, float ttl) {
    //    if (!barksEnabled) return;
    //    barks.push_back({ p, std::string(txt), ttl });
    //    };

    for (int idx : s.members) {
        if (idx < 0 || idx >= (int)actors.size()) continue;
        Actor& a = actors[idx];
        if (!a.alive()) continue;

        // "Under fire" if recently hit
        if (a.recentlyHit) {
            s.underFire = true;
            s.timeSinceContact = 0.0f;
        }

        Vec2 threatPos;
        int  threatIdx = -1;
        bool seesThreat = false;
        if (acquireThreat(a, threatPos, threatIdx, seesThreat)) {
            contactNow = true;
            enemyAccum.x += threatPos.x;
            enemyAccum.y += threatPos.y;
            ++enemySamples;
            if (seesThreat) {
                anyVisual = true;
            }
            s.timeSinceContact = 0.0f;
        }
    }

    Vec2 enemyPos{ 0,0 };
    if (enemySamples > 0) {
        enemyPos.x = enemyAccum.x / enemySamples;
        enemyPos.y = enemyAccum.y / enemySamples;
        s.lastKnownEnemy = enemyPos;
        s.hasLastKnownEnemy = true;

        s.debugHasEnemy = true;
        s.debugEnemyPos = enemyPos;
    }

    
    // -------------------------------------------------------
    // Idle vs contact mode gating (prevents squads getting "stuck" in Seek)
    // -------------------------------------------------------
    if (contactNow) {
        s.mode = SquadMode::CombatContact;
        s.modeTimer = frand(3.0f, 6.0f); // keep moving / reacting
    } else {
        // If we recently had contact, drop back to calm after a short lull
        if (s.mode == SquadMode::CombatContact && s.timeSinceContact > 6.0f) {
            s.mode = SquadMode::Calm;
            s.modeTimer = frand(4.0f, 10.0f);
        }
    }

// --- Decide if we should re-plan a new intent ---
    bool canReplan = (s.intentTimer <= 0.0f);

    if (contactNow && canReplan) {
        // Simple scoring for intents
        float distToEnemy = length(enemyPos - center);

        // Base scores
        float scoreAdvance = 0.25f;
        float scoreFlank = 0.25f;
        float scoreHold = 0.25f;
        float scoreRetreat = 0.10f;
        float scoreSearch = 0.05f;

        // Confidence impact on scoring (local copy)
        float confLocal = std::clamp(s.confidence, 0.0f, 1.0f); // 0 = broken, 1 = fearless

        float confUp = 0.7f + confLocal * 0.6f;  // 0.7 .. 1.3 – boosts aggressive actions as confidence rises
        float confDown = 1.3f - confLocal * 0.6f;  // 1.3 .. 0.7 – boosts retreat more when confidence is low

        // Confidence pushes them toward/away from certain intents
        scoreAdvance *= confUp;                               // more likely to push forward if confident
        scoreFlank *= confUp;                               // same for flanks
        scoreSearch *= (0.8f + confLocal * 0.3f);            // mild influence
        // Hold stays relatively neutral
        scoreRetreat *= confDown;                             // retreat becomes more attractive when confidence drops

        // Basic situational boosts
        scoreHold += s.underFire ? 0.25f : 0.10f;
        scoreAdvance += anyVisual ? 0.30f : 0.10f;
        scoreFlank += anyVisual ? 0.40f : 0.10f;

        // If we only *hear* the enemy (no visual), "Search" becomes more attractive
        if (!anyVisual) {
            scoreSearch += 0.35f;
            scoreAdvance -= 0.05f; // slightly less eager to charge blind
        }

        // Distance-based adjustments
        if (distToEnemy < 140.0f) {
            // Very close – retreat and hold are attractive, flanks less so
            scoreRetreat += 0.50f;
            scoreHold -= 0.10f;
        }
        else if (distToEnemy < 260.0f) {
            // Mid-range – advancing and flanking are good options
            scoreAdvance += 0.20f;
            scoreFlank += 0.20f;
        }
        else {
            // Long range – maybe push slightly
            scoreAdvance += 0.10f;
        }

        // Casualties push toward retreat / caution
        if (s.recentCasualties >= 2) {
            scoreRetreat += 0.60f;
            scoreFlank -= 0.20f;
            scoreSearch += 0.10f;
        }

        // --- Faction-specific style on top of the above ---

        switch (s.side) {
        case Faction::Axis:
            // Professional, aggressive, likes flanks, not keen on falling back
            scoreAdvance *= 1.20f;
            scoreFlank *= 1.30f;
            scoreHold *= 1.00f;
            scoreRetreat *= 0.70f;
            scoreSearch *= 0.90f;

            if (highConf && anyVisual) {
                // High confidence Axis with eyes on target → even more flanky
                scoreFlank += 0.25f;
                scoreAdvance += 0.10f;
            }
            break;

        case Faction::Allies:
            // Balanced, doctrinal – solid holds and sensible advances, decent search
            scoreAdvance *= 1.10f;
            scoreFlank *= 1.05f;
            scoreHold *= 1.15f;
            scoreRetreat *= 0.95f;
            scoreSearch *= 1.15f;

            if (midLoss && !highLoss) {
                // Moderate losses – more likely to hold & search, less to rush
                scoreHold += 0.15f;
                scoreSearch += 0.15f;
                scoreAdvance -= 0.10f;
            }
            break;

        case Faction::Rebels:
            // Aggressive but brittle – love charging and flanks, not great at holding
            scoreAdvance *= 1.30f;
            scoreFlank *= 1.15f;
            scoreHold *= 0.85f;
            scoreRetreat *= 0.90f;
            scoreSearch *= 0.95f;

            if (highLoss || lowConf) {
                // When things go badly, they might snap and either rush or break
                scoreAdvance += 0.20f;
                scoreRetreat += 0.20f;
            }
            break;

        case Faction::Militia:
            // Skittish, poorly trained – likes holding behind cover & retreat/search
            scoreAdvance *= 0.80f;
            scoreFlank *= 0.80f;
            scoreHold *= 1.15f;
            scoreRetreat *= 1.40f;
            scoreSearch *= 1.20f;

            if (highLoss || lowConf) {
                // Under real pressure they will absolutely want to bug out
                scoreRetreat += 0.50f;
                scoreSearch += 0.20f;
            }
            break;
        }

        // Normalize-ish by clamping above 0
        
    // --- Suppression / flank tuning ---
    // When lightly suppressed and confident, squads are more willing to reposition/flank.
    if (suppressNorm < 0.25f) {
        scoreFlank += 0.18f * s.confidence;
        // If we only have sound contact (searching), allow a small chance to turn that into a flank.
        if (!anyVisual && s.confidence > 0.70f) scoreFlank += 0.10f;
    }
    // Under heavy suppression, reduce risky movement and bias toward hold/retreat.
    if (suppressNorm > 0.65f) {
        scoreFlank *= 0.55f;
        scoreAdvance *= 0.70f;
        scoreHold += 0.10f;
        scoreRetreat += 0.12f;
    }
auto clamp0 = [](float v) { return v < 0.f ? 0.f : v; };
        scoreHold = clamp0(scoreHold);
        scoreAdvance = clamp0(scoreAdvance);
        scoreFlank = clamp0(scoreFlank);
        scoreRetreat = clamp0(scoreRetreat);
        scoreSearch = clamp0(scoreSearch);

        // Choose intent
        SquadIntent bestIntent = SquadIntent::Hold;
        float       bestScore = scoreHold;

        auto consider = [&](SquadIntent intent, float sScore) {
            if (sScore > bestScore) {
                bestScore = sScore;
                bestIntent = intent;
            }
            };

        consider(SquadIntent::Advance, scoreAdvance);
        consider(SquadIntent::Flank, scoreFlank);
        consider(SquadIntent::Retreat, scoreRetreat);
        consider(SquadIntent::Search, scoreSearch);

        s.intent = bestIntent;
        s.intentExecuting = false;
        s.intentTimer = frand(4.0f, 7.0f);   // commit to this plan
        s.coordTimer = frand(0.6f, 1.2f);   // coordination delay
        s.hadFlankLOS = false;              // reset for flank feedback
        s.recentCasualties = 0;                 // "spent" in this decision

        // Bark the plan from leader (or squad center)
        Vec2 barkPos = center;
        if (s.leader >= 0 && s.leader < (int)actors.size() && actors[s.leader].alive()) {
            barkPos = actors[s.leader].pos;
        }
        pushBark(barkPos, planBark(s.side, s.intent), 2.0f);
}

    // If contact lost but we still have a last known position, allow Search plans later
    if (!contactNow && s.hasLastKnownEnemy && s.timeSinceContact < 8.0f) {
        // We treat this in "Search" when the timer frees up.
    }

    // --- Coordination phase: no new orders yet, just scanning / small drift ---
    if (!s.intentExecuting && s.coordTimer > 0.0f) {
        // Idle drift / scanning: let individual AI do light patrol / idle.
        // Militia-style uncertainty bark
        if (s.intent == SquadIntent::Flank || s.intent == SquadIntent::Advance) {
            // occasional grunt worry
            if (frand(0.f, 1.f) < 0.03f) {
                for (int idx : s.members) {
                    if (idx < 0 || idx >= (int)actors.size()) continue;
                    Actor& a = actors[idx];
                    if (!a.alive()) continue;
                    if (a.isLeader) continue;
                    pushBark(a.pos, "What do we do!?", 1.8f);
                    break;
                }
            }
        }
        return;
    }

    // --- Once coordination time runs out, issue orders if not already executing ---
    if (!s.intentExecuting && s.coordTimer <= 0.0f) {
        s.intentExecuting = true;

        // Simple feedback bark: "Go!"
        Vec2 barkPos = center;
        if (s.leader >= 0 && s.leader < (int)actors.size() && actors[s.leader].alive()) {
            barkPos = actors[s.leader].pos;
        }
        pushBark(barkPos, "Go, go!", 1.6f);

        // Precompute some shared points
        Vec2 contactPos = (enemySamples > 0) ? enemyPos : s.lastKnownEnemy;
        if (s.hasLastKnownEnemy) {
            s.debugHasEnemy = true;
            s.debugEnemyPos = contactPos;
        }

        // For flanks we’ll need the flank side
        Vec2 toEnemy = normalize(contactPos - center);
        Vec2 right{ -toEnemy.y, toEnemy.x };

        // Use mission role anchor as a "home" for hold / retreat behaviour
        Vec2 guardAnchor = (s.roleAnchor.x != 0.f || s.roleAnchor.y != 0.f)
            ? s.roleAnchor : s.home;
        if (length(guardAnchor - center) < 1.0f) {
            guardAnchor = center;
        }

        // Count for splitting into elements
        int n = 0;
        for (int idx : s.members) {
            if (idx < 0 || idx >= (int)actors.size()) continue;
            if (actors[idx].alive()) ++n;
        }
        int supportCount = std::max(1, n / 2);

        // ISSUE ORDERS PER INTENT
        int aliveIndex = 0;

        for (int idx : s.members) {
            if (idx < 0 || idx >= (int)actors.size()) continue;
            Actor& a = actors[idx];
            if (!a.alive()) continue;

            a.hasOrder = false; // reset, then set below if needed

            switch (s.intent) {
            case SquadIntent::Hold: {
                // Hold around guardAnchor in a loose formation
                float angle = (6.2831853f * (float)aliveIndex) / std::max(1, n);
                float radius = 32.0f + 8.0f * (aliveIndex % 3);
                Vec2 offset{ std::cos(angle) * radius, std::sin(angle) * radius };
                a.hasOrder = true;
                a.orderPos = guardAnchor + offset;
            } break;

            case SquadIntent::Advance: {
                // All move toward contact / last known position
                a.hasOrder = true;
                // Slight lateral offset to avoid straight line
                float side = (aliveIndex % 2 == 0) ? 1.0f : -1.0f;
                float sideAmt = 24.0f + 8.0f * (aliveIndex / 2);
                Vec2 advancePos = contactPos + right * side * sideAmt;
                a.orderPos = advancePos;
            } break;

            case SquadIntent::Flank: {
                // SUPPORT ELEMENT: partial cover between us and the enemy
                // FLANK ELEMENT: wide cover position on one side

                float flankSide = (frand(0.f, 1.f) < 0.5f) ? 1.0f : -1.0f;
                Vec2 flankTarget = contactPos
                    + right * flankSide * 220.0f
                    - toEnemy * 60.0f; // slightly behind / to the side

                // Cover-aware mid point for support
                Vec2 mid = center + normalize(contactPos - center) * 140.0f;
                Vec2 coverMid = findNearestCoverToward(mid, contactPos);
                if (length(coverMid - mid) > 24.0f) {
                    mid = coverMid;
                    s.debugHasCover = true;
                    s.debugCoverPos = coverMid;
                }

                // Cover-aware flank target
                Vec2 flankCover = findNearestCoverToward(flankTarget, contactPos);
                if (length(flankCover - flankTarget) > 24.0f) {
                    flankTarget = flankCover;
                }
                s.debugHasFlank = true;
                s.debugFlankPos = flankTarget;

                if (aliveIndex < supportCount) {
                    // SUPPORT
                    a.hasOrder = true;
                    a.orderPos = mid;
                }
                else {
                    // FLANKERS
                    a.hasOrder = true;
                    a.orderPos = flankTarget;
                }
            } break;

            case SquadIntent::Retreat: {
                // Retreat into cover if possible, else away from enemy toward guardAnchor
                a.hasOrder = true;

                Vec2 cover = findNearestCoverToward(a.pos, contactPos);
                if (length(cover - a.pos) > 24.0f) {
                    a.orderPos = cover;
                    s.debugHasCover = true;
                    s.debugCoverPos = cover;
                }
                else {
                    Vec2 away = guardAnchor;
                    if (length(a.pos - contactPos) < length(a.pos - guardAnchor)) {
                        Vec2 dir = normalize(a.pos - contactPos);
                        away = a.pos + dir * 260.0f;
                    }
                    a.orderPos = away;
                }
            } break;

            case SquadIntent::Search: {
                // Move cautiously toward last known enemy position
                if (s.hasLastKnownEnemy) {
                    a.hasOrder = true;
                    Vec2 searchPos = s.lastKnownEnemy;
                    // Small spread around that point
                    float angle = (6.2831853f * (float)aliveIndex) / std::max(1, n);
                    Vec2 off{ std::cos(angle) * 40.0f, std::sin(angle) * 40.0f };
                    a.orderPos = searchPos + off;
                }
            } break;
            }

            ++aliveIndex;
        }
    }

    // --- No contact / long time since contact: drift back to guardAnchor ---
    
    // -----------------------------
    // Calm patrol (anti-stuck idle)
    // -----------------------------
    if (!contactNow) {
        // Timers
        s.modeTimer = std::max(0.0f, s.modeTimer - dt);
        s.calmBarkTimerS = std::max(0.0f, s.calmBarkTimerS - dt);

        // Pick a stable "anchor" for this squad
        Vec2 anchor = (s.role == MissionRole::None) ? s.home : s.roleAnchor;

        // State machine:
        // mode 0 = idle/scan near anchor
        // mode 1 = 1-2 units "go look" at a nearby point
        // mode 2 = return to anchor

    // If we're calm, clear any leftover orders so individuals can run Patrol/Idle logic.
    if (s.mode == SquadMode::Calm && !contactNow) {
        for (int mi : s.members) {
            if (mi < 0 || mi >= (int)actors.size()) continue;
            Actor& a = actors[mi];
            if (!a.alive()) continue;
            a.hasOrder = false;
        }
    }
        if (s.modeTimer <= 0.0f) {
            if (s.mode == SquadMode::Calm) {
            // Occasional short scan while calm (Stalker/Zero Sievert vibe)
            if (!s.scanning && s.idleScanCooldownS <= 0.0f && frand(0.f, 1.f) < 0.35f) {
                Vec2 scanFrom = anchor;
                Vec2 scanDir = Vec2(1, 0);
                if (s.leader >= 0 && s.leader < (int)actors.size() && actors[s.leader].alive()) {
                    scanFrom = actors[s.leader].pos;
                    scanDir = actors[s.leader].facing;
                }
                beginSquadScan(sid, scanFrom + normalize(scanDir) * 70.0f, scanDir, 140.0f, frand(4.5f, 8.0f));
                s.idleScanCooldownS = frand(10.0f, 18.0f);
            }

                s.mode = SquadMode::PatrolSweep;
                s.modeTimer = frand(4.0f, 8.0f);

                // Choose goal: cycle checkpoints when available, else random nearby
                if (!s.checkpoints.empty()) {
                    s.currentCheckpoint = (s.currentCheckpoint + 1) % (int)s.checkpoints.size();
                    s.currentGoal = s.checkpoints[s.currentCheckpoint];
                } else {
                    s.currentGoal = anchor + Vec2{ frand(-140.f, 140.f), frand(-140.f, 140.f) };
                }

                // Choose 1-2 members to walk out (prefer non-leader)
                int walkers = std::min(2, std::max(1, (int)s.members.size() / 3));
                for (int w = 0; w < walkers; ++w) {
                    int pick = -1;
                    for (int guard = 0; guard < 8; ++guard) {
                        int mi = s.members[irand(0, (int)s.members.size() - 1)];
                        if (mi == s.leader) continue;
                        if (mi >= 0 && mi < (int)actors.size() && actors[mi].alive()) { pick = mi; break; }
                    }
                    if (pick >= 0) {
                        actors[pick].hasOrder = true;
                        actors[pick].orderPos = s.currentGoal + Vec2{ frand(-40.f, 40.f), frand(-40.f, 40.f) };
                    }
                }

            } else if (s.mode == SquadMode::PatrolSweep) {
                s.mode = SquadMode::ReturnToAnchor;
                s.modeTimer = frand(3.0f, 6.0f);

                // Return walkers to anchor
                for (int mi : s.members) {
                    if (mi < 0 || mi >= (int)actors.size()) continue;
                    Actor& a = actors[mi];
                    if (!a.alive()) continue;
                    if (!a.hasOrder) continue;
                    a.orderPos = anchor + Vec2{ frand(-48.f, 48.f), frand(-48.f, 48.f) };
                }

            } else { // mode 2 -> back to idle
                s.mode = SquadMode::Calm;
                s.modeTimer = frand(6.0f, 14.0f);

                // Occasional "uneventful" bark from leader
                if (barksEnabled && s.calmBarkTimerS <= 0.0f) {
                    Vec2 barkPos = anchor;
                    if (s.leader >= 0 && s.leader < (int)actors.size() && actors[s.leader].alive()) barkPos = actors[s.leader].pos;
                    if (frand(0.f, 1.f) < 0.35f) {
                        pushBark(barkPos, calmBark(s.side), 2.0f);
                        s.calmBarkTimerS = frand(10.0f, 18.0f);
                    } else {
                        s.calmBarkTimerS = frand(6.0f, 12.0f);
                    }
                }
            }
        }
    }
}



void Game::beginSquadScan(int sid, const Vec2& center, const Vec2& facing,
    float radius, float duration)
{
    if (sid < 0 || sid >= (int)squads.size()) return;
    Squad& s = squads[sid];

    s.scanning = true;
    s.scanCenter = center;
    s.scanRadius = radius;
    s.scanTimer = duration;

    Vec2 dir = normalize(facing);
    if (length(dir) < 0.001f) dir = Vec2(1, 0);
    Vec2 right{ -dir.y, dir.x };

    for (int i = 0; i < (int)s.members.size(); ++i) {
        int aiIndex = s.members[i];
        if (aiIndex < 0 || aiIndex >= (int)actors.size()) continue;
        Actor& a = actors[aiIndex];

        a.inScan = true;

        if (a.isLeader) {
            // Leader hangs back behind the scan centre
            a.scanPos = center - dir * (radius * 0.4f);
        }
        else {
            // Others fan out ahead and sideways in front of the leader
            float ringR = frand(radius * 0.3f, radius);
            float side = (i % 2 == 0) ? -1.0f : 1.0f;
            float sideScale = frand(0.1f, 0.8f);
            Vec2 offset =
                dir * ringR * frand(0.4f, 1.0f) +
                right * side * sideScale * ringR * 0.6f;

            a.scanPos = center + offset;
        }

        // Clamp scanPos to walkable land as a safety net
        int c = (int)(a.scanPos.x / cfg::TileSize);
        int r = (int)(a.scanPos.y / cfg::TileSize);
        if (!inBoundsTile(c, r) || !isWalkableTile(c, r)) {
            a.scanPos = center + Vec2(
                frand(-radius * 0.3f, radius * 0.3f),
                frand(-radius * 0.3f, radius * 0.3f));
        }
    }
}



void Game::raiseAlarm(int level) {
    mission.alarmLevel = std::clamp(mission.alarmLevel + level, 0, 5);
}




// -----------------------------------------------------------
// AI update
// -----------------------------------------------------------

void Game::updateAI(Actor& a, float dt) {
    if (!a.alive()) return;

    a.nextThink    -= dt;
    a.idleTimer    -= dt;
    a.repathTimer  -= dt;
    a.barkCooldown  = std::max(0.f, a.barkCooldown - dt);

    a.recentlyHitTimer -= dt;
    if (a.recentlyHitTimer <= 0.f) {
        a.recentlyHit = false;
    }

    weaponUpdate(a, dt);

    // 1) Normal threat acquisition first
    Vec2 threatPos{};
    int  threatIdx   = -1;
    bool seesThreat  = false;
    bool hasThreat   = acquireThreat(a, threatPos, threatIdx, seesThreat);

    if (!hasThreat && a.hasOrder) {
        float d = length(a.orderPos - a.pos);
        if (d < 13.0f) {
            a.hasOrder = false;
            clearPath(a);
            if (a.state == AIState::Seek) a.state = AIState::Patrol;
        }
    }


    // Flavour bark when visually spotting an enemy (faction-to-faction callouts)
    if (hasThreat && seesThreat && barksEnabled && a.barkCooldown <= 0.f) {
        Faction tgt = (threatIdx == -1) ? player.team : actors[threatIdx].team;
        if (frand(0.f, 1.f) < 0.20f) { // occasional, not spam
            std::string dirWord = compass8(a.pos, threatPos);
            std::string line = barkSpottedEnemy(a.team, tgt, dirWord);
            pushBark(a.pos, line.c_str(), 1.9f);
            a.barkCooldown = 2.6f;
        }
    }

    // 2) Ally suspicion of player (secret operative)
    bool observingUnknownFriend = false;

    if (a.team == Faction::Allies &&
        playerPresent && player.alive() &&
        &a != &player &&                     // don't run this on the player
        !hasThreat)                          // only when not busy with enemies
    {
        Vec2 toP   = player.pos - a.pos;
        float dist = length(toP);

        if (dist < a.visionRange * 0.8f && losClear(a.pos, player.pos))
        {
            // Allies see the player but not yet "known" as friendly
            if (!playerKnownToAllies)
            {
                observingUnknownFriend = true;
                alliesCuriosityTimer += dt;

                // Face the player
                if (dist > 1.f)
                    a.facing = normalize(toP);

                // Mild follow if idle / not engaged
                Vec2 desired{0,0};
                if (dist > 140.f && dist < 320.f) {
                    desired = normalize(toP) * (a.moveWalkSpeed * 0.5f);
                }
                moveWithCollide(a, desired, a.moveWalkSpeed, dt);

                // One-time “who is that?” bark early in curiosity window
                if (a.barkCooldown <= 0.f && alliesCuriosityTimer < 2.0f)
                {
                    pushBark(a.pos, "Who is that?", 1.8f);
                    a.barkCooldown = 3.0f;
                }

                // After watching a bit, accept the player as friendly
                if (alliesCuriosityTimer > 5.0f && !playerKnownToAllies)
                {
                    playerKnownToAllies = true;
                    if (a.barkCooldown <= 0.f)
                    {
                        pushBark(a.pos, "Must be with another unit.", 2.5f);
                        a.barkCooldown = 4.0f;
                    }
                }

                // IMPORTANT: early-out from combat AI while just observing
                if (observingUnknownFriend)
                    return;
            }
        }
    }

    
    // Rebel intel: friendly rebels will occasionally give the player a directional callout
    // when the player is in their FOV and they're not currently engaged.
    if (a.team == Faction::Rebels &&
        playerPresent && player.alive() &&
        sameSide(a.team, player.team) &&
        !hasThreat)
    {
        bool los = false;
        if (sees(a, player.pos, los) && los) {
            Vec2 bestEnemyPos{};
            bool haveEnemy = false;
            float bestD2 = 1e30f;

            // Prefer squad memory
            if (a.squadId >= 0 && a.squadId < (int)squads.size()) {
                const Squad& sq = squads[a.squadId];
                if (sq.hasLastKnownEnemy) {
                    bestEnemyPos = sq.lastKnownEnemy;
                    haveEnemy = true;
                }
            }

            // Else, nearest enemy to the player (cheap scan)
            if (!haveEnemy) {
                for (const auto& o : actors) {
                    if (!o.alive()) continue;
                    if (!areEnemies(player.team, o.team)) continue;
                    float d2 = lenSq(o.pos - player.pos);
                    if (d2 < bestD2) { bestD2 = d2; bestEnemyPos = o.pos; haveEnemy = true; }
                }
            }

            if (barksEnabled && a.barkCooldown <= 0.f) {
                if (haveEnemy) {
                    std::string dirWord = compass8(player.pos, bestEnemyPos);
                    std::string line = std::string("Psst. Enemy activity to the ") + dirWord + ".";
                    pushBark(a.pos, line.c_str(), 2.3f);
                    a.barkCooldown = 6.0f;
                } else if (frand(0.f, 1.f) < 0.06f) {
                    pushBark(a.pos, "Stay low. Roads aren't safe.", 2.3f);
                    a.barkCooldown = 6.0f;
                }
            }
        }
    }

// 3) From here on, use hasThreat / states as usual...
    //    recentlyHit cover, AIState selection, etc.
    //    (your existing code continues here)



    // If just hit and no clear target, bias toward cover from that direction
    if (a.recentlyHit && !hasThreat) {
        Vec2 anchor = (length(a.lastShotOrigin - a.pos) > 1.0f)
            ? a.lastShotOrigin : a.pos;
        Vec2 cover = findNearestCoverToward(a.pos, anchor);
        if (length(cover - a.pos) > 8.f) {
            if (a.path.empty() || a.repathTimer <= 0.f) {
                buildPath(a.pos, cover, a);
                a.repathTimer = 1.0f;
            }
            a.state = AIState::Hunker;
        }
    }

    // --- High-level state selection / thinking ---
    if (a.nextThink <= 0.f) {
        a.nextThink = frand(0.35f, 0.6f);

        if (hasThreat) {
            float dst = length(threatPos - a.pos);
            if (seesThreat) {
                if (dst > cfg::StandoffRange * 1.4f) {
                    a.state = AIState::Seek;
                }
                else if (dst < cfg::StandoffRange * 0.6f) {
                    a.state = AIState::HoldCover;
                }
                else {
                    a.state = AIState::Attack;
                }
            }
            else {
                a.state = AIState::Search;
                a.investigatePos = threatPos;
            }
        }
        else {
            if (a.recentlyHit) {
                a.state = AIState::Hunker;
            }
            else if (a.hasOrder) {
                a.state = AIState::Seek;
            }
            else {
                if (a.state != AIState::Patrol && a.state != AIState::Idle)
                    a.state = AIState::Patrol;
            }
        }
    }

    Vec2 desiredVel{ 0,0 };
    float maxSpeed = a.moveWalkSpeed;
    maxSpeed *= 0.78f; // global inertia

    switch (a.state) {
    case AIState::Idle:
        desiredVel = Vec2{ 0,0 };
        break;

    case AIState::Patrol: {
        const Squad* sq =
            (a.squadId >= 0 && a.squadId < (int)squads.size())
            ? &squads[a.squadId] : nullptr;

        Vec2  anchor = a.pos;
        float maxSquadR = 200.0f;

        if (sq) {
            if (sq->role == MissionRole::None)
                anchor = sq->home;
            else
                anchor = sq->roleAnchor;
            maxSquadR = sq->patrolRadius;
        }

        bool needTarget = false;

        if (a.patrolTarget.x == 0.0f && a.patrolTarget.y == 0.0f) {
            needTarget = true;
        }
        else {
            float distToAnchor = length(a.patrolTarget - anchor);
            if (distToAnchor > 0.7f * maxSquadR) {
                needTarget = true; // don't wander too far
            }
            float distToTarget = length(a.patrolTarget - a.pos);
            if (distToTarget < 6.0f) {
                // At post: mostly hold, occasionally adjust
                desiredVel = Vec2{ 0,0 };

                if (frand(0.0f, 1.0f) < 0.25f * dt) {
                    float yaw = std::atan2(a.facing.y, a.facing.x);
                    yaw += frand(-0.5f, 0.5f);
                    a.facing = normalize(Vec2(std::cos(yaw), std::sin(yaw)));
                }

                if (frand(0.0f, 1.0f) < 0.10f * dt) {
                    needTarget = true; // small “shuffle” within area
                }
            }
        }

        if (needTarget) {
            if (sq && a.isLeader) {
                // Leader picks a waypoint within squad area,
                // like a checkpoint for the whole squad to drift around.
                float baseR = std::min(maxSquadR * 0.4f, 140.0f);
                if (sq->role == MissionRole::Reserve)
                    baseR = std::min(maxSquadR * 0.7f, 220.0f);

                float ang = frand(0.0f, 6.28318f);
                float rad = frand(10.0f, baseR);
                a.patrolTarget = anchor + Vec2(std::cos(ang) * rad, std::sin(ang) * rad);
            }
            else if (sq && sq->leader >= 0 && sq->leader < (int)actors.size()) {
                // Members hang near leader
                const Actor& lead = actors[sq->leader];
                float maxR = 26.0f;
                float ang = frand(0.0f, 6.28318f);
                float rad = frand(3.0f, maxR);
                a.patrolTarget = lead.pos + Vec2(std::cos(ang) * rad, std::sin(ang) * rad);
            }
            else {
                float maxR = 22.0f;
                float ang = frand(0.0f, 6.28318f);
                float rad = frand(2.0f, maxR);
                a.patrolTarget = a.pos + Vec2(std::cos(ang) * rad, std::sin(ang) * rad);
            }
        }

        if (a.patrolTarget.x != 0.0f || a.patrolTarget.y != 0.0f) {
            Vec2  to = a.patrolTarget - a.pos;
            float d = length(to);
            if (d > 6.0f) {
                Vec2 dir = normalize(to);
                float walkSpeed = a.moveWalkSpeed * 0.7f; // heavy & plodding
                desiredVel = dir * walkSpeed;
                a.facing = dir;
            }
            else {
                desiredVel = Vec2{ 0,0 };
            }
        }
    } break;

    case AIState::Search: {
        const Squad* sq =
            (a.squadId >= 0 && a.squadId < (int)squads.size())
            ? &squads[a.squadId] : nullptr;

        // If squad already scanning and this actor is inScan,
        // just hand over to Investigate state.
        if (sq && sq->scanning && a.inScan) {
            a.state = AIState::Investigate;
            break;
        }

        Vec2  to = a.investigatePos - a.pos;
        float d = length(to);

        if (d > 32.0f) {
            // Still travelling to the suspicious spot
            if (a.path.empty() || a.repathTimer <= 0.0f) {
                buildPath(a.pos, a.investigatePos, a);
                a.repathTimer = 0.8f;
            }
            maxSpeed = a.moveSprintSpeed;
        }
        else {
            // Close enough – leader sets up a squad scan in front of him
            if (sq && a.isLeader && !sq->scanning) {
                Vec2 dir = normalize(to);
                if (length(dir) < 0.001f) dir = a.facing;
                float radius = cfg::StandoffRange * 0.7f;
                Vec2 scanCenter = a.pos + dir * (radius * 0.5f);

                beginSquadScan(a.squadId, scanCenter, dir,
                    radius, frand(8.0f, 14.0f));
            }
            a.state = AIState::Investigate;
        }
    } break;

    case AIState::Seek: {
        Vec2 dest = a.hasOrder ? a.orderPos : threatPos;

        // If the destination is extremely close, don't bother pathing.
        float d = length(dest - a.pos);

        if (d > 12.f) {
            if (a.path.empty() || a.repathTimer <= 0.f) {
                buildPath(a.pos, dest, a);
                a.repathTimer = 0.7f;
            }
        }

        maxSpeed = a.moveSprintSpeed * 0.75f;

        // Fallback: if pathing fails (empty path), just steer directly.
        if (a.path.empty() && d > 6.f) {
            Vec2 dir = normalize(dest - a.pos);
            desiredVel = dir * (a.moveWalkSpeed * 0.65f);
            a.facing = dir;
        }

    } break;

    case AIState::Hunker:
    case AIState::HoldCover:
        desiredVel = Vec2{ 0,0 };
        break;

    case AIState::Attack: {
        if (hasThreat && seesThreat) {
            Vec2 to = threatPos - a.pos;
            if (length(to) > 1.f) a.facing = normalize(to);
            desiredVel = Vec2{ 0,0 };

            if (weaponCanFire(a)) {
                const WeaponDef& wd = weaponDef(a.weapon.id);
                Vec2 dir0 = normalize(to);
                float spreadRad = (wd.spreadDeg * 3.14159265f / 180.0f);
                if (a.armWoundS > 0.0f) spreadRad *= 1.6f;


                for (int p = 0; p < std::max(1, wd.pellets); ++p) {
                    float ang = std::atan2(dir0.y, dir0.x) + frand(-spreadRad, spreadRad);
                    Vec2 dir{ std::cos(ang), std::sin(ang) };

                    Bullet b;
                    b.pos = a.pos;
                    b.dir = dir;
                    b.traveled = 0.f;
                    b.speed = wd.projectileSpeed;
                    b.wid = a.weapon.id;
                    b.maxRange = wd.maxRange;
                    b.dmg = (int)std::round(wd.baseDamage);
                    b.src = a.team;
                    bullets.push_back(b);
                }

                a.weapon.magAmmo--;
                a.weapon.fireTimer = wd.fireCooldownS;

                // keep legacy mag aligned
                a.gun.inMag = a.weapon.magAmmo;


                sounds.push_back({
                    a.pos,
                    cfg::GunshotHearTiles * cfg::TileSize,
                    cfg::HearDecayS
                    });

                raiseAlarm(1);
            }
        }
        else {
            a.state = AIState::Search;
        }
    } break;

    case AIState::Investigate: {
        const Squad* sq =
            (a.squadId >= 0 && a.squadId < (int)squads.size())
            ? &squads[a.squadId] : nullptr;

        if (!(sq && sq->scanning && a.inScan)) {
            // No active scan for this actor – go back to patrol formation
            a.state = AIState::Patrol;
            a.inScan = false;
            break;
        }

        // Move slowly to assigned scan position, then stand and sweep visually.
        Vec2  to = a.scanPos - a.pos;
        float d = length(to);

        if (d > 8.0f) {
            Vec2 dir = normalize(to);
            float speed = a.moveWalkSpeed * 0.6f; // careful advancement
            desiredVel = dir * speed;
            a.facing = dir;
        }
        else {
            desiredVel = Vec2{ 0,0 };

            // Look around within arc – "searching" motion
            if (frand(0.0f, 1.0f) < 0.45f * dt) {
                float yaw = std::atan2(a.facing.y, a.facing.x);
                yaw += frand(-0.8f, 0.8f);
                a.facing = normalize(Vec2(std::cos(yaw), std::sin(yaw)));
            }
        }
    } break;

    case AIState::Flank:
    case AIState::Flee:
        maxSpeed = a.moveSprintSpeed;
        break;
    }

    // Path following override
    if (!a.path.empty() && a.pathIndex >= 0 && a.pathIndex < (int)a.path.size()) {
        Vec2  waypoint = a.path[a.pathIndex];
        Vec2  to = waypoint - a.pos;
        float d = length(to);
        if (d < 8.f) {
            a.pathIndex++;
            if (a.pathIndex >= (int)a.path.size()) {
                clearPath(a);
                if (a.hasOrder && length(a.orderPos - a.pos) < 12.f) {
                    a.hasOrder = false;
                }
            }
        }
        else {
            Vec2 dir = normalize(to);
            desiredVel = dir * maxSpeed;
            a.facing = dir;
        }
    }

    moveWithCollide(a, desiredVel, maxSpeed, dt);

    // Auto-reload when empty & not in immediate danger
    if (a.weapon.magAmmo <= 0 && !a.weapon.reloading) {
        weaponStartReload(a);
    }
}


// -----------------------------------------------------------
// Mission start / setup
// -----------------------------------------------------------

void Game::startMission(MissionKind kind) {
    mission = MissionState{};
    mission.kind = kind;
    mission.active = true;
    mission.phase = MissionPhase::Ingress;
    mission.alarmLevel = 0;
    mission.respawnWavesRemaining = missionParams.respawnWaves;

    paused = false;
    mode = Mode::Player;
    zoom = cfg::ZoomPlayer;

    labelsEnabled = true;
    hudEnabled = true;

    bullets.clear();
    sounds.clear();
    actors.clear();
    squads.clear();
    corpses.clear();
    barks.clear();
    undo = std::stack<PaintOp>();

    // Map layout variant
    int variant = irand(0, 2);
    if (variant == 0) {
        map = makeBlankMap();
        applyTreesClump(map, 12, 5);
        applyTreesSparse(map, 0.03f);
    }
    else if (variant == 1) {
        map = makeBlankMap();
        auto p = prefabs[0];
        int c0 = (map.cols - p.w) / 2, r0 = (map.rows - p.h) / 2;
        for (int y = 0; y < p.h; ++y)
            for (int x = 0; x < p.w; ++x) {
                auto t = p.data[y * p.w + x];
                if (t != Tile::Land) map.set(c0 + x, r0 + y, t);
            }
        applyTreesSparse(map, 0.05f);
    }
    else {
        map = makeBlankMap();
        auto p = prefabs[1];
        int c0 = (map.cols - p.w) / 2, r0 = (map.rows - p.h) / 2;
        for (int y = 0; y < p.h; ++y)
            for (int x = 0; x < p.w; ++x) {
                auto t = p.data[y * p.w + x];
                if (t != Tile::Land) map.set(c0 + x, r0 + y, t);
            }
        applyTreesClump(map, 8, 4);
        applyTreesSparse(map, 0.025f);
    }

    rebuildFoliage();

    // Player spawn in one of four corners, on clear land
    {
        int marginTiles = 3;
        int corner = irand(0, 3); // 0 NW, 1 NE, 2 SW, 3 SE
        Vec2 spawn = { map.cols * cfg::TileSize * 0.5f,
                       map.rows * cfg::TileSize * 0.5f };

        for (int tries = 0; tries < 128; ++tries) {
            int cMin = marginTiles;
            int cMax = map.cols - 1 - marginTiles;
            int rMin = marginTiles;
            int rMax = map.rows - 1 - marginTiles;

            int c, r;
            if (corner == 0) { // NW
                c = irand(cMin, cMin + 6);
                r = irand(rMin, rMin + 6);
            }
            else if (corner == 1) { // NE
                c = irand(cMax - 6, cMax);
                r = irand(rMin, rMin + 6);
            }
            else if (corner == 2) { // SW
                c = irand(cMin, cMin + 6);
                r = irand(rMax - 6, rMax);
            }
            else { // SE
                c = irand(cMax - 6, cMax);
                r = irand(rMax - 6, rMax);
            }

            if (!isClearLand(c, r)) continue;
            spawn = Vec2{
                c * cfg::TileSize + cfg::TileSize * 0.5f,
                r * cfg::TileSize + cfg::TileSize * 0.5f
            };
            break;
        }

        player = makeUnit(Faction::Allies, spawn);
        player.hp = player.hpMax = 3;
        player.team = Faction::Allies;
        playerPresent = true;

        // Ensure mission-start player is armed (missions recreate the player here)
        applyPlayerLoadout(player);
    }

    const float T = (float)cfg::TileSize;
    std::vector<Vec2> avoid;
    avoid.push_back(player.pos);

    // Mission-specific objective & initial squads
    if (mission.kind == MissionKind::Intel) {
        mission.docPos = randomClearPos(6, avoid, 12.0f);
        avoid.push_back(mission.docPos);

        mission.extractPos = randomClearPos(6, avoid, 14.0f);
        avoid.push_back(mission.extractPos);

        mission.docPresent = true;
        mission.docTaken = false;
        mission.extractPresent = true;

        // Guard squad at intel (Axis, tighter formation)
        placeSquad(Faction::Axis, (int)mission.docPos.x, (int)mission.docPos.y, 4);
        if (!squads.empty()) {
            Squad& g = squads.back();
            g.role = MissionRole::ObjectiveGuard;
            g.roleAnchor = mission.docPos;
            g.roleRadius = 140.f * missionParams.patrolRadiusScale;
        }
    }
    else if (mission.kind == MissionKind::HVT) {
        mission.hvtPos = randomClearPos(6, avoid, 12.0f);
        avoid.push_back(mission.hvtPos);

        Actor hvt = makeUnit(Faction::Axis, mission.hvtPos);
        hvt.hp = hvt.hpMax = 3;
        hvt.isHVT = true;
        hvt.squadId = -1;
        applyFactionLoadout(hvt);
        actors.push_back(hvt);
        mission.hvtIndex = (int)actors.size() - 1;
        mission.hvtPresent = true;
        mission.hvtKilled = false;

        mission.extractPos = randomClearPos(6, avoid, 14.0f);
        avoid.push_back(mission.extractPos);
        mission.extractPresent = true;

        // Guard squad at HVT
        placeSquad(Faction::Axis, (int)mission.hvtPos.x, (int)mission.hvtPos.y, 5);
        if (!squads.empty()) {
            Squad& g = squads.back();
            g.role = MissionRole::ObjectiveGuard;
            g.roleAnchor = mission.hvtPos;
            g.roleRadius = 150.f * missionParams.patrolRadiusScale;
        }
    }
    else if (mission.kind == MissionKind::Sabotage) {
        mission.sabotagePos = randomClearPos(6, avoid, 12.0f);
        avoid.push_back(mission.sabotagePos);

        mission.sabotagePresent = true;
        mission.sabotageArmed = false;
        mission.sabotageDestroyed = false;
        mission.sabotageTimer = 0.0f;

        mission.extractPos = randomClearPos(6, avoid, 14.0f);
        avoid.push_back(mission.extractPos);
        mission.extractPresent = true;

        placeSquad(Faction::Axis, (int)mission.sabotagePos.x, (int)mission.sabotagePos.y, 4);
        if (!squads.empty()) {
            Squad& g = squads.back();
            g.role = MissionRole::ObjectiveGuard;
            g.roleAnchor = mission.sabotagePos;
            g.roleRadius = 140.f * missionParams.patrolRadiusScale;
        }
    }
    else if (mission.kind == MissionKind::Rescue) {
        mission.rescuePos = randomClearPos(6, avoid, 12.0f);
        avoid.push_back(mission.rescuePos);

        mission.rescuePresent = true;
        mission.rescueFreed = false;

        mission.extractPos = randomClearPos(6, avoid, 14.0f);
        avoid.push_back(mission.extractPos);
        mission.extractPresent = true;

        placeSquad(Faction::Axis, (int)mission.rescuePos.x, (int)mission.rescuePos.y, 4);
        if (!squads.empty()) {
            Squad& g = squads.back();
            g.role = MissionRole::ObjectiveGuard;
            g.roleAnchor = mission.rescuePos;
            g.roleRadius = 140.f * missionParams.patrolRadiusScale;
        }
    }
    else { // Sweep
        mission.extractPos = randomClearPos(6, avoid, 16.0f);
        avoid.push_back(mission.extractPos);
        mission.extractPresent = true;

        int sweepSquads = std::max(1, missionParams.sweepSquadsBase);
        for (int i = 0; i < sweepSquads; ++i) {
            Vec2 mid = randomClearPos(6, avoid, 10.0f);
            avoid.push_back(mid);
            Faction f = randomEnemyFaction();
            placeSquad(f, (int)mid.x, (int)mid.y, 4);
            if (!squads.empty()) {
                Squad& p = squads.back();
                p.role = MissionRole::AreaPatrol;
                p.roleAnchor = mid;
                p.roleRadius = 260.f * missionParams.patrolRadiusScale;
            }
        }
    }

    // Common roaming + reserve for non-sweep
    if (mission.kind != MissionKind::Sweep) {
        Vec2 anchor =
            (mission.kind == MissionKind::Intel) ? mission.docPos :
            (mission.kind == MissionKind::HVT) ? mission.hvtPos :
            (mission.kind == MissionKind::Sabotage) ? mission.sabotagePos :
            mission.rescuePos;

        Vec2 mid = anchor + (mission.extractPos - anchor) * 0.5f;

        if (length(mid - player.pos) < 10 * T) {
            mid = randomClearPos(6, avoid, 10.0f);
        }

        int extraSquads = std::max(0, missionParams.enemySquadsBase - 1);

        // Patrol squad: ambient roaming, random faction
        {
            Faction f = randomEnemyFaction();
            placeSquad(f, (int)mid.x, (int)mid.y, 4);
            if (!squads.empty()) {
                Squad& p = squads.back();
                p.role = MissionRole::AreaPatrol;
                p.roleAnchor = mid;
                p.roleRadius = 220.f * missionParams.patrolRadiusScale;
            }
        }

        // Reserve squad: random faction further away
        Vec2 reserveAnchor = randomClearPos(6, avoid, 10.0f);
        {
            Faction f = randomEnemyFaction();
            placeSquad(f, (int)reserveAnchor.x, (int)reserveAnchor.y, 5);
            if (!squads.empty()) {
                Squad& r = squads.back();
                r.role = MissionRole::Reserve;
                r.roleAnchor = reserveAnchor;
                r.roleRadius = 260.f * missionParams.patrolRadiusScale;
            }
        }

        // Extra roaming squads
        for (int i = 0; i < extraSquads; ++i) {
            Vec2 roamAnchor = randomClearPos(6, avoid, 8.0f);
            Faction f = randomEnemyFaction();
            placeSquad(f, (int)roamAnchor.x, (int)roamAnchor.y, 3 + (i % 2));
            if (!squads.empty()) {
                Squad& rr = squads.back();
                rr.role = MissionRole::AreaPatrol;
                rr.roleAnchor = roamAnchor;
                rr.roleRadius = 260.f * missionParams.patrolRadiusScale;
            }
            avoid.push_back(roamAnchor);
        }
    }

    // Count enemies for sweep
    mission.totalEnemiesAtStart = countLivingEnemies();
    if (mission.kind == MissionKind::Sweep) {
        float frac = std::clamp(missionParams.sweepFraction, 0.1f, 1.0f);
        mission.sweepRequiredKills =
            std::max(1, (int)std::ceil(mission.totalEnemiesAtStart * frac));
    }

    if (barksEnabled) {
        if (mission.kind == MissionKind::Intel)
            barks.push_back({ player.pos, "Mission start. Find the document.", 2.5f });
        else if (mission.kind == MissionKind::HVT)
            barks.push_back({ player.pos, "Mission start. Find the officer.", 2.5f });
        else if (mission.kind == MissionKind::Sabotage)
            barks.push_back({ player.pos, "Mission start. Sabotage the target.", 2.5f });
        else if (mission.kind == MissionKind::Rescue)
            barks.push_back({ player.pos, "Mission start. Locate the hostage.", 2.5f });
        else
            barks.push_back({ player.pos, "Mission start. Sweep the area.", 2.5f });
    }
}

// -----------------------------------------------------------
// Mission update (including respawn waves)
// -----------------------------------------------------------

void Game::updateMission(float dt) {
    if (!mission.active) return;

    if (!playerPresent || !player.alive()) {
        mission.phase = MissionPhase::Failed;
        mission.active = false;
        paused = true;
        showMissionDebrief = true;
        if (barksEnabled) {
            barks.push_back({ player.pos, "Mission failed.", 3.0f });
        }
        std::printf("[MISSION] FAILED\n");
        return;
    }

    // Intel
    if (mission.kind == MissionKind::Intel) {
        if (mission.docPresent && playerPresent) {
            float d = length(player.pos - mission.docPos);
            if (d < cfg::TileSize * 0.9f) {
                mission.docPresent = false;
                mission.docTaken = true;
                mission.phase = MissionPhase::Exfil;
                if (barksEnabled) {
                    barks.push_back({ player.pos, "Got the intel. Head to extraction.", 3.0f });
                }
                std::printf("[MISSION] Intel secured.\n");
            }
        }

        if (mission.docTaken && mission.extractPresent && playerPresent) {
            float d = length(player.pos - mission.extractPos);
            if (d < cfg::TileSize * 1.1f) {
                mission.phase = MissionPhase::Complete;
                mission.active = false;
                paused = true;
                showMissionDebrief = true;
                if (barksEnabled) {
                    barks.push_back({ player.pos, "We're out. Good work.", 3.0f });
                }

                if (!mission.summaryShown) {
                    mission.summaryShown = true;
                    std::printf("=== MISSION COMPLETE (INTEL) ===\n");
                    std::printf("Shots fired:   %d\n", mission.shotsFired);
                    std::printf("Shots hit:     %d\n", mission.shotsHit);
                    std::printf("Enemies killed:%d\n", mission.enemiesKilled);
                    std::printf("Alarm level:   %d\n", mission.alarmLevel);
                    std::printf("================================\n");
                }
            }
        }
    }
    // HVT
    else if (mission.kind == MissionKind::HVT) {
        if (mission.hvtPresent && mission.hvtIndex >= 0 &&
            mission.hvtIndex < (int)actors.size() &&
            !actors[mission.hvtIndex].alive()) {
            mission.hvtPresent = false;
            mission.hvtKilled = true;
            mission.hvtPos = actors[mission.hvtIndex].pos;
        }

        if (mission.hvtKilled && mission.phase == MissionPhase::Ingress) {
            mission.phase = MissionPhase::Exfil;
            raiseAlarm(3);
            if (barksEnabled) {
                barks.push_back({ player.pos, "Target down. Move to extraction.", 3.0f });
            }
            std::printf("[MISSION] HVT down.\n");
        }

        if (mission.hvtKilled && mission.extractPresent && playerPresent) {
            float d = length(player.pos - mission.extractPos);
            if (d < cfg::TileSize * 1.1f) {
                mission.phase = MissionPhase::Complete;
                mission.active = false;
                paused = true;
                showMissionDebrief = true;
                if (barksEnabled) {
                    barks.push_back({ player.pos, "Objective achieved. Exfil complete.", 3.0f });
                }

                if (!mission.summaryShown) {
                    mission.summaryShown = true;
                    std::printf("=== MISSION COMPLETE (HVT) ===\n");
                    std::printf("Shots fired:   %d\n", mission.shotsFired);
                    std::printf("Shots hit:     %d\n", mission.shotsHit);
                    std::printf("Enemies killed:%d\n", mission.enemiesKilled);
                    std::printf("Alarm level:   %d\n", mission.alarmLevel);
                    std::printf("================================\n");
                }
            }
        }
    }
    // Sabotage
    else if (mission.kind == MissionKind::Sabotage) {
        if (mission.sabotagePresent && playerPresent && !mission.sabotageArmed) {
            float d = length(player.pos - mission.sabotagePos);
            if (d < cfg::TileSize * 0.9f) {
                mission.sabotageArmed = true;
                mission.sabotageTimer = 2.5f;
                raiseAlarm(2);
                if (barksEnabled) {
                    barks.push_back({ player.pos, "Charges planted, stand clear!", 3.0f });
                }
                std::printf("[MISSION] Sabotage charges planted.\n");
            }
        }

        if (mission.sabotageArmed && !mission.sabotageDestroyed) {
            mission.sabotageTimer -= dt;
            if (mission.sabotageTimer <= 0.f) {
                mission.sabotageDestroyed = true;
                mission.sabotagePresent = false;
                mission.phase = MissionPhase::Exfil;
                raiseAlarm(3);
                if (barksEnabled) {
                    barks.push_back({ player.pos, "Target destroyed. Move to extraction.", 3.0f });
                }
                std::printf("[MISSION] Sabotage complete.\n");
            }
        }

        if (mission.sabotageDestroyed && mission.extractPresent && playerPresent) {
            float d = length(player.pos - mission.extractPos);
            if (d < cfg::TileSize * 1.1f) {
                mission.phase = MissionPhase::Complete;
                mission.active = false;
                paused = true;
                showMissionDebrief = true;
                if (barksEnabled) {
                    barks.push_back({ player.pos, "Objective achieved. Exfil complete.", 3.0f });
                }

                if (!mission.summaryShown) {
                    mission.summaryShown = true;
                    std::printf("=== MISSION COMPLETE (SABOTAGE) ===\n");
                    std::printf("Shots fired:   %d\n", mission.shotsFired);
                    std::printf("Shots hit:     %d\n", mission.shotsHit);
                    std::printf("Enemies killed:%d\n", mission.enemiesKilled);
                    std::printf("Alarm level:   %d\n", mission.alarmLevel);
                    std::printf("===================================\n");
                }
            }
        }
    }
    // Rescue
    else if (mission.kind == MissionKind::Rescue) {
        if (mission.rescuePresent && playerPresent) {
            float d = length(player.pos - mission.rescuePos);
            if (d < cfg::TileSize * 0.9f) {
                mission.rescuePresent = false;
                mission.rescueFreed = true;
                mission.phase = MissionPhase::Exfil;
                raiseAlarm(2);
                if (barksEnabled) {
                    barks.push_back({ player.pos, "Hostage secured. Head to extraction.", 3.0f });
                }
                std::printf("[MISSION] Hostage secured.\n");
            }
        }

        if (mission.rescueFreed && mission.extractPresent && playerPresent) {
            float d = length(player.pos - mission.extractPos);
            if (d < cfg::TileSize * 1.1f) {
                mission.phase = MissionPhase::Complete;
                mission.active = false;
                paused = true;
                showMissionDebrief = true;
                if (barksEnabled) {
                    barks.push_back({ player.pos, "Rescue extracted. Good work.", 3.0f });
                }

                if (!mission.summaryShown) {
                    mission.summaryShown = true;
                    std::printf("=== MISSION COMPLETE (RESCUE) ===\n");
                    std::printf("Shots fired:   %d\n", mission.shotsFired);
                    std::printf("Shots hit:     %d\n", mission.shotsHit);
                    std::printf("Enemies killed:%d\n", mission.enemiesKilled);
                    std::printf("Alarm level:   %d\n", mission.alarmLevel);
                    std::printf("=================================\n");
                }
            }
        }
    }
    // Sweep
    else {
        if (mission.phase == MissionPhase::Ingress &&
            mission.sweepRequiredKills > 0 &&
            mission.enemiesKilled >= mission.sweepRequiredKills) {
            mission.phase = MissionPhase::Exfil;
            raiseAlarm(3);
            if (barksEnabled) {
                barks.push_back({ player.pos, "Area secure. Move to extraction.", 3.0f });
            }
            std::printf("[MISSION] Sweep threshold reached.\n");
        }

        if (mission.phase == MissionPhase::Exfil &&
            mission.extractPresent && playerPresent) {
            float d = length(player.pos - mission.extractPos);
            if (d < cfg::TileSize * 1.1f) {
                mission.phase = MissionPhase::Complete;
                mission.active = false;
                paused = true;
                showMissionDebrief = true;
                if (barksEnabled) {
                    barks.push_back({ player.pos, "Sweep complete. Exfil successful.", 3.0f });
                }

                if (!mission.summaryShown) {
                    mission.summaryShown = true;
                    std::printf("=== MISSION COMPLETE (SWEEP) ===\n");
                    std::printf("Shots fired:   %d\n", mission.shotsFired);
                    std::printf("Shots hit:     %d\n", mission.shotsHit);
                    std::printf("Enemies killed:%d\n", mission.enemiesKilled);
                    std::printf("Sweep target:  %d\n", mission.sweepRequiredKills);
                    std::printf("Alarm level:   %d\n", mission.alarmLevel);
                    std::printf("================================\n");
                }
            }
        }
    }

    // 
    // -------------------------------------------------------
    // Ambient skirmishes: spawn small enemy-vs-enemy fights in clear areas
    // -------------------------------------------------------
    mission.ambientSkirmishCooldownS = std::max(0.0f, mission.ambientSkirmishCooldownS - dt);
    mission.ambientSkirmishTimerS += dt;

    // Try a skirmish every ~10 seconds, but only if the mission isn't already extremely "hot"
    if (mission.ambientSkirmishCooldownS <= 0.0f && mission.ambientSkirmishTimerS > 10.0f) {
        mission.ambientSkirmishTimerS = 0.0f;

        float p = 0.22f - 0.03f * (float)mission.alarmLevel;
        if (mission.phase == MissionPhase::Exfil) p *= 0.6f;

        if (frand(0.0f, 1.0f) < std::max(0.0f, p)) {
            std::vector<Vec2> avoid{ player.pos };

            // Keep it away from the player so it's "ambient" rather than immediate spawn-in combat
            Vec2 center = randomClearPos(8, avoid, 22.0f);

            // Pick two hostile factions (avoid Allies to reduce "friendly spawn behind you" feel).
            Faction pool[3] = { Faction::Axis, Faction::Militia, Faction::Rebels };
            Faction aSide = pool[irand(0, 2)];
            Faction bSide = pool[irand(0, 2)];
            int guard = 0;
            while ((!areEnemies(aSide, bSide) || aSide == bSide) && guard++ < 12) {
                aSide = pool[irand(0, 2)];
                bSide = pool[irand(0, 2)];
            }

            Vec2 offA{ frand(-60.f, -28.f), frand(-60.f, -28.f) };
            Vec2 offB{ frand( 28.f,  60.f), frand( 28.f,  60.f) };

            placeSquad(aSide, (int)(center.x + offA.x), (int)(center.y + offA.y), 3 + irand(0, 1));
            placeSquad(bSide, (int)(center.x + offB.x), (int)(center.y + offB.y), 3 + irand(0, 1));

            // Fake a distant gunfire ping to wake up nearby squads
            sounds.push_back({ center, cfg::GunshotHearTiles * cfg::TileSize * 0.85f, cfg::HearDecayS * 0.9f });

            if (barksEnabled) {
                barks.push_back({ center, "Distant gunfire...", 2.2f });
            }

            mission.ambientSkirmishCooldownS = frand(22.0f, 40.0f);
        }
    }

// Simple reinforcement logic: when enemy count dips, spawn new roaming squad
    if (mission.respawnWavesRemaining > 0) {
        int living = countLivingEnemies();
        int threshold = std::max(1, mission.totalEnemiesAtStart / 3);
        if (living < threshold) {
            std::vector<Vec2> avoid{ player.pos };
            Vec2 anchor = randomClearPos(4, avoid, 10.0f);
            Faction f = randomEnemyFaction();
            placeSquad(f, (int)anchor.x, (int)anchor.y, 4);
            mission.totalEnemiesAtStart += 4;
            mission.respawnWavesRemaining--;
            if (barksEnabled) {
                barks.push_back({ anchor, "Enemy reinforcements!", 2.5f });
            }
            std::printf("[MISSION] Reinforcement wave spawned. Remaining: %d\n",
                mission.respawnWavesRemaining);
        }
    }

    (void)dt;
}

// -----------------------------------------------------------
// Paint / control click helpers (sandbox)
// -----------------------------------------------------------

void Game::handlePaintClick(int wx, int wy, bool leftClick) {
    if (!leftClick) return;

    int c = wx / cfg::TileSize;
    int r = wy / cfg::TileSize;
    if (!inBoundsTile(c, r)) return;

    if (paintBrush == 0) {
        // move player spawn
        SDL_FRect tr = tileRectWorld(c, r);
        player.pos = Vec2{ tr.x + tr.w * 0.5f, tr.y + tr.h * 0.5f };
        playerPresent = true;
        return;
    }

    if (paintBrush == 1 || paintBrush == 2 || paintBrush == 3 || paintBrush == 7) {
        // Spawn unit
        Faction f = Faction::Axis;
        if (paintBrush == 2) f = Faction::Militia;
        else if (paintBrush == 3) f = Faction::Rebels;
        else if (paintBrush == 7) f = Faction::Allies;

        SDL_FRect tr = tileRectWorld(c, r);
        Vec2 pos{ tr.x + tr.w * 0.5f, tr.y + tr.h * 0.5f };
        Actor a = makeUnit(f, pos);
        actors.push_back(a);
        return;
    }

    // Tile painting
    Tile before = map.at(c, r);
    Tile after = before;

    if (paintBrush == 4) { // erase to land
        after = Tile::Land;
    }
    else if (paintBrush == 5) { // wall
        after = Tile::Wall;
    }
    else if (paintBrush == 6) { // tree
        after = Tile::Tree;
    }

    if (before != after) {
        PaintOp op;
        op.c = c; op.r = r;
        op.before = before;
        op.after = after;
        undo.push(op);
        map.set(c, r, after);
        rebuildFoliage();
    }
}

void Game::handleControlClick(int wx, int wy, bool leftClick) {
    if (leftClick) {
        // select nearest unit
        Vec2 p{ (float)wx, (float)wy };
        int best = -1;
        float bestD2 = 32.0f * 32.0f;
        for (int i = 0; i < (int)actors.size(); ++i) {
            Actor& a = actors[i];
            if (!a.alive()) continue;
            float d2 = lenSq(a.pos - p);
            if (d2 < bestD2) {
                bestD2 = d2;
                best = i;
            }
        }
        for (auto& a : actors) a.selected = false;
        if (best >= 0) actors[best].selected = true;
    }
    else {
        // move selected as a loose squad
        Vec2 dst{ (float)wx, (float)wy };
        std::vector<int> selectedIdx;
        for (int i = 0; i < (int)actors.size(); ++i) {
            if (actors[i].selected && actors[i].alive())
                selectedIdx.push_back(i);
        }
        if (selectedIdx.empty()) return;

        float spacing = 20.0f;
        for (int k = 0; k < (int)selectedIdx.size(); ++k) {
            Actor& a = actors[selectedIdx[k]];
            Vec2 offset{
                std::cos(6.28318f * k / std::max(1, (int)selectedIdx.size())) * spacing,
                std::sin(6.28318f * k / std::max(1, (int)selectedIdx.size())) * spacing
            };
            Vec2 goal = dst + offset;
            a.orderPos = goal;
            a.hasOrder = true;
            a.state = AIState::Seek;
            buildPath(a.pos, goal, a);
        }
    }
}

// -----------------------------------------------------------
// Mission params click handling (buttons & toggles)
// -----------------------------------------------------------

void Game::handleEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            running = false;
            break;

        case SDL_MOUSEMOTION: {
            mouseX = e.motion.x;
            mouseY = e.motion.y;
            worldMouseX = int(camX + mouseX / zoom);
            worldMouseY = int(camY + mouseY / zoom);
        } break;

        case SDL_MOUSEBUTTONDOWN: {
            if (e.button.button == SDL_BUTTON_LEFT) {
                mouseDownL = true;

                // Params panel: handle buttons & toggles
                if (showMissionParams && !mission.active) {
                    int mx = e.button.x;
                    int my = e.button.y;

                    // Rows 0..4
                    const int baseY = 206; // as laid out in drawMissionParamsHUD
                    const int rowH = 22;
                    int row = (my - baseY) / rowH;
                    if (row >= 0 && row <= 4) {
                        int minusX = 10 + 260;
                        int plusX = minusX + 60;
                        int rowY = baseY + row * rowH;
                        int minusY = rowY - 2;
                        int plusY = rowY - 2;

                        bool inMinus = (mx >= minusX && mx < minusX + 18 &&
                            my >= minusY && my < minusY + 16);
                        bool inPlus = (mx >= plusX && mx < plusX + 18 &&
                            my >= plusY && my < plusY + 16);

                        if (inMinus || inPlus) {
                            bool plus = inPlus;

                            auto adjInt = [&](int& v, int minV, int maxV) {
                                v += plus ? 1 : -1;
                                v = std::clamp(v, minV, maxV);
                                };
                            auto adjFloat = [&](float& v, float delta, float minV, float maxV) {
                                v += plus ? delta : -delta;
                                v = std::clamp(v, minV, maxV);
                                };

                            switch (row) {
                            case 0: // enemySquadsBase
                                adjInt(missionParams.enemySquadsBase, 0, 8);
                                break;
                            case 1: // sweepSquadsBase
                                adjInt(missionParams.sweepSquadsBase, 1, 10);
                                break;
                            case 2: // respawn waves
                                adjInt(missionParams.respawnWaves, 0, 6);
                                break;
                            case 3: // patrol radius
                                adjFloat(missionParams.patrolRadiusScale, 0.1f, 0.4f, 2.5f);
                                break;
                            case 4: // sweep fraction
                                adjFloat(missionParams.sweepFraction, 0.05f, 0.1f, 1.0f);
                                break;
                            }
                            break;
                        }
                    }

                    // Faction toggles
                    int fx = 10 + 20;
                    int fy = 358;
                    SDL_Rect axisR{ fx,         fy, 70, 18 };
                    SDL_Rect milR{ fx + 80,   fy, 70, 18 };
                    SDL_Rect rebR{ fx + 160,   fy, 70, 18 };
                    SDL_Rect allR{ fx + 240,   fy, 70, 18 }; // NEW: Allies

                    if (mx >= axisR.x && mx < axisR.x + axisR.w &&
                        my >= axisR.y && my < axisR.y + axisR.h) {
                        missionParams.useAxis = !missionParams.useAxis;
                    }
                    else if (mx >= milR.x && mx < milR.x + milR.w &&
                        my >= milR.y && my < milR.y + milR.h) {
                        missionParams.useMilitia = !missionParams.useMilitia;
                    }
                    else if (mx >= rebR.x && mx < rebR.x + rebR.w &&
                        my >= rebR.y && my < rebR.y + rebR.h) {
                        missionParams.useRebels = !missionParams.useRebels;
                    }
                    else if (mx >= allR.x && mx < allR.x + allR.w &&
                        my >= allR.y && my < allR.y + allR.h) {
                        missionParams.useAllies = !missionParams.useAllies;
                    }

                    // Ensure at least one faction stays active
                    if (!missionParams.useAxis &&
                        !missionParams.useMilitia &&
                        !missionParams.useRebels &&
                        !missionParams.useAllies)
                    {
                        missionParams.useAxis = true; // fall back to Axis as default
                    }



                    // Mission kind boxes
                    int kx = 10 + 20;
                    int ky = 404;
                    SDL_Rect intelR{ kx,       ky, 80, 18 };
                    SDL_Rect hvtR{ kx + 90,  ky, 80, 18 };
                    SDL_Rect sabR{ kx + 180, ky, 80, 18 };
                    SDL_Rect resR{ kx + 270, ky, 80, 18 };
                    SDL_Rect swpR{ kx + 360, ky, 80, 18 };

                    if (mx >= intelR.x && mx < intelR.x + intelR.w &&
                        my >= intelR.y && my < intelR.y + intelR.h) {
                        mission.kind = MissionKind::Intel;
                    }
                    else if (mx >= hvtR.x && mx < hvtR.x + hvtR.w &&
                        my >= hvtR.y && my < hvtR.y + hvtR.h) {
                        mission.kind = MissionKind::HVT;
                    }
                    else if (mx >= sabR.x && mx < sabR.x + sabR.w &&
                        my >= sabR.y && my < sabR.y + sabR.h) {
                        mission.kind = MissionKind::Sabotage;
                    }
                    else if (mx >= resR.x && mx < resR.x + resR.w &&
                        my >= resR.y && my < resR.y + resR.h) {
                        mission.kind = MissionKind::Rescue;
                    }
                    else if (mx >= swpR.x && mx < swpR.x + swpR.w &&
                        my >= swpR.y && my < swpR.y + swpR.h) {
                        mission.kind = MissionKind::Sweep;
                    }

                    // After handling param clicks, don't propagate as paint/control
                    break;
                }

                // Sandbox clicks
                if (!mission.active) {
                    int wx = int(camX + e.button.x / zoom);
                    int wy = int(camY + e.button.y / zoom);
                    if (mode == Mode::Paint) {
                        handlePaintClick(wx, wy, true);
                    }
                    else if (mode == Mode::Control) {
                        handleControlClick(wx, wy, true);
                    }
                }
            }
            else if (e.button.button == SDL_BUTTON_RIGHT) {
                mouseDownR = true;

                if (!mission.active) {
                    int wx = int(camX + e.button.x / zoom);
                    int wy = int(camY + e.button.y / zoom);
                    if (mode == Mode::Paint) {
                        // nothing
                    }
                    else if (mode == Mode::Control) {
                        handleControlClick(wx, wy, false);
                    }
                }
            }
        } break;

        case SDL_MOUSEBUTTONUP: {
            if (e.button.button == SDL_BUTTON_LEFT) {
                mouseDownL = false;
            }
            else if (e.button.button == SDL_BUTTON_RIGHT) {
                mouseDownR = false;
            }
        } break;

        case SDL_MOUSEWHEEL: {
            float factor = (e.wheel.y > 0) ? 1.1f : 0.9f;
            zoom *= factor;
            zoom = std::clamp(zoom, 0.5f, 2.5f);
        } break;

        case SDL_KEYDOWN: {
            SDL_Keycode k = e.key.keysym.sym;

            if (showMissionBrief) {
                if (k == SDLK_RETURN) {
                    showMissionBrief = false;
                    startMission(mission.kind);
                }
                else if (k == SDLK_ESCAPE) {
                    showMissionBrief = false;
                    mission = MissionState{};
                    mission.active = false;
                }
                break;
            }

            if (showMissionDebrief) {
                if (k == SDLK_r) {
                    showMissionDebrief = false;
                    startMission(mission.kind);
                }
                else if (k == SDLK_d) {
                    showMissionDebrief = false;
                    mission = MissionState{};
                    mission.active = false;
                    initWorld();
                }
                else if (k == SDLK_ESCAPE) {
                    running = false;
                }
                break;
            }

            switch (k) {
            case SDLK_ESCAPE:
                running = false;
                break;
            case SDLK_TAB:
                if (!mission.active) {
                    if (mode == Mode::Player)      mode = Mode::Control;
                    else if (mode == Mode::Control) mode = Mode::Paint;
                    else                             mode = Mode::Player;
                }
                break;
            case SDLK_w: kW = true; break;
            case SDLK_s: kS = true; break;
            case SDLK_a: kA = true; break;
            case SDLK_d: kD = true; break;
            case SDLK_LSHIFT:
            case SDLK_RSHIFT: kShift = true; break;
            case SDLK_r:
                if (playerPresent && player.alive() && !showMissionDebrief && !showMissionBrief) {
                    weaponStartReload(player);
                }
                break;

            case SDLK_x:
                // Toggle sneak mode
                sneakMode = !sneakMode;
                break;

            case SDLK_0: paintBrush = 0; break;
            case SDLK_1: paintBrush = 1; break;
            case SDLK_2: paintBrush = 2; break;
            case SDLK_3: paintBrush = 3; break;
            case SDLK_4: paintBrush = 4; break;
            case SDLK_5: paintBrush = 5; break;
            case SDLK_6: paintBrush = 6; break;
            case SDLK_7: paintBrush = 7; break;

            case SDLK_F1: labelsEnabled = !labelsEnabled;  break;
            case SDLK_F2: barksEnabled = !barksEnabled;   break;
            case SDLK_F3: hearingViz = !hearingViz;     break;
            case SDLK_F4: showAIIntentViz = !showAIIntentViz; break;
            case SDLK_F6: showMissionParams = !showMissionParams; break;
            case SDLK_F7: squadDebugViz = !squadDebugViz;  break;
            case SDLK_F8: hudEnabled = !hudEnabled;     break;
            
            case SDLK_F11:
                if (!mission.active && !showMissionBrief && !showMissionDebrief) {
                    showMissionBrief = true;
                }
                break;

            default:
                break;
            }
        } break;

        case SDL_KEYUP: {
            SDL_Keycode k = e.key.keysym.sym;
            switch (k) {
            case SDLK_w: kW = false; break;
            case SDLK_s: kS = false; break;
            case SDLK_a: kA = false; break;
            case SDLK_d: kD = false; break;
            case SDLK_LSHIFT:
            case SDLK_RSHIFT: kShift = false; break;
            default: break;
            }
        } break;
        }
    }
}

static inline const char* calloutPlayerHit(HitZone z) {
    switch (z) {
    case HitZone::Head:  return "Direct hit! Head!";
    case HitZone::Torso: return "Good hit! Center mass!";
    case HitZone::Legs:  return "Tagged the legs!";
    case HitZone::ArmL:  return "Snagged left arm!";
    case HitZone::ArmR:  return "Snagged right arm!";
    default: return "Hit!";
    }
}
static inline const char* calloutPlayerHurt(HitZone z) {
    switch (z) {
    case HitZone::Head:  return "I'm hit! Head!";
    case HitZone::Torso: return "I'm hit! Torso!";
    case HitZone::Legs:  return "I'm hit! Leg!";
    case HitZone::ArmL:  return "I'm hit! Arm!";
    case HitZone::ArmR:  return "I'm hit! Arm!";
    default: return "I'm hit!";
    }
}


// -----------------------------------------------------------
// Update
// -----------------------------------------------------------

void Game::update(float dt) {
    // Update sound pings
    for (auto& s : sounds) {
        s.ttl -= dt;
        s.radiusPx = std::max(0.f, s.radiusPx - (cfg::TileSize * dt * 3.f));
    }
    sounds.erase(
        std::remove_if(sounds.begin(), sounds.end(),
            [](const SoundPing& s) { return s.ttl <= 0.f || s.radiusPx <= 0.f; }),
        sounds.end());

    gameTimeS += dt;


    // Update barks
    for (auto& b : barks) {
        b.ttl -= dt;
    }
    barks.erase(
        std::remove_if(barks.begin(), barks.end(),
            [](const Bark& b) { return b.ttl <= 0.f; }),
        barks.end());

  
    // Player movement + facing + footsteps
    if (playerPresent && player.alive()) {
        Vec2 move{ 0,0 };
        if (kW) move.y -= 1.f;
        if (kS) move.y += 1.f;
        if (kA) move.x -= 1.f;
        if (kD) move.x += 1.f;

        float speed;
        if (sneakMode) {
            speed = cfg::PlayerSneakSpeed;
        }
        else if (kShift) {
            speed = cfg::PlayerSprintSpeed;
        }
        else {
            speed = cfg::PlayerWalkSpeed;
        }

        if (lenSq(move) > 0.01f) {
            move = normalize(move) * speed;
        }

        if (player.legWoundS > 0.0f) speed *= 0.65f;
        


        moveWithCollide(player, move, speed, dt);

        player.legWoundS = std::max(0.0f, player.legWoundS - dt);
        player.armWoundS = std::max(0.0f, player.armWoundS - dt);

        // Facing toward mouse
        Vec2 m{ (float)worldMouseX, (float)worldMouseY };
        Vec2 toM = m - player.pos;
        if (length(toM) > 4.f) {
            player.facing = normalize(toM);
        }

        weaponUpdate(player, dt);


        // 🔫 Player fire with LMB (only in Player mode)
        if (mouseDownL && mode == Mode::Player && weaponCanFire(player)) {
            Vec2 dir0 = normalize(Vec2{ (float)worldMouseX, (float)worldMouseY } - player.pos);

            const WeaponDef& wd = weaponDef(player.weapon.id);
            float spreadRad = (wd.spreadDeg * 3.14159265f / 180.0f);
            if (player.armWoundS > 0.0f) spreadRad *= 1.6f;


            // Spawn pellets (shotguns) or single projectile
            for (int p = 0; p < std::max(1, wd.pellets); ++p) {
                float ang = std::atan2(dir0.y, dir0.x) + frand(-spreadRad, spreadRad);
                Vec2 dir{ std::cos(ang), std::sin(ang) };

                Bullet b;
                b.pos = player.pos;
                b.dir = dir;
                b.traveled = 0.f;
                b.speed = wd.projectileSpeed;
                b.maxRange = wd.maxRange;
                b.wid = player.weapon.id;
                b.dmg = (int)std::round(wd.baseDamage);
                b.src = player.team;

                bullets.push_back(b);
            }

            player.weapon.magAmmo--;
            player.weapon.fireTimer = wd.fireCooldownS;



            // Keep legacy mag aligned for any remaining old UI
            player.gun.inMag = player.weapon.magAmmo;

            mission.shotsFired++;
          


            // Gunshot ping stays loud-ish, regardless of sneak
            sounds.push_back({
                player.pos,
                cfg::GunshotHearTiles * cfg::TileSize,
                cfg::HearDecayS * 0.7f
                });
        }

        // Footstep noise – **none** when sneaking
        if (!sneakMode && lenSq(move) > 1.f) {
            float hearTiles = kShift ? cfg::FootstepHearSprintTiles : cfg::FootstepHearWalkTiles;
            sounds.push_back({
                player.pos,
                hearTiles * cfg::TileSize,
                cfg::HearDecayS * 0.7f
                });
        }
    }


    // Squad brains
    for (int i = 0; i < (int)squads.size(); ++i) {
        updateSquadBrain(i, dt);
    }

    // Bullets move (per-weapon speed/range)
    for (auto& b : bullets) {
        float step = b.speed * dt;
        b.pos = b.pos + b.dir * step;
        b.traveled += step;
    }


    auto bulletHitsSolid = [&](const Bullet& b) {
        SDL_FRect r{ b.pos.x - 2.f, b.pos.y - 2.f, 4.f, 4.f };
        return collideSolid(r);
        };

    std::vector<bool> bulletDead(bullets.size(), false);

    // After moving bullets, before full collision, optional near-miss suppression:
    for (const auto& b : bullets)
    {
        for (int i = 0; i < (int)actors.size(); ++i)
        {
            Actor& a = actors[i];
            if (!a.alive()) continue;
            if (!areEnemies(b.src, a.team)) continue;

            float d = length(a.pos - b.pos);
            if (d < 70.0f && d >(a.w * 0.5f + 4.0f))  // close but not hit
            {
                if (a.squadId >= 0)
                    addSuppression(a.squadId, 3.0f);
            }
        }

        // Optional: player near-miss, in case you want their behaviour later
        // float dp = length(player.pos - b.pos);
        // ...
    }


    // Bullet collision vs map + player + actors
    for (int bi = 0; bi < (int)bullets.size(); ++bi) {
        Bullet& b = bullets[bi];
        if (b.traveled > b.maxRange || bulletHitsSolid(b)) {
            bulletDead[bi] = true;
            continue;
        }

        // Hit player
        if (playerPresent && player.alive() && areEnemies(b.src, player.team)) {
            float d = length(player.pos - b.pos);
            if (d < (player.w * 0.5f + 2.f)) {

                HitZone z = HitZone::Torso;
                resolveHitZone(player, b.pos, z);

                float mult = zoneMultiplier(z) * weaponZoneBias(b.wid, z);
                int dealt = (int)std::round((float)b.dmg * mult * gDamageScale);

                player.hp -= dealt;

                player.lastHitZone = z;
                player.lastHitTime = gameTimeS;
                player.lastShotOrigin = b.pos - b.dir * 40.0f;

                // Wounds
                if (z == HitZone::Legs) player.legWoundS = std::max(player.legWoundS, 3.0f);
                if (z == HitZone::ArmL || z == HitZone::ArmR) player.armWoundS = std::max(player.armWoundS, 3.0f);

                // Bark (throttled)
                if (barksEnabled && gameTimeS >= player.nextCalloutS) {
                    barks.push_back({ player.pos, calloutPlayerHurt(z), 1.2f });
                    player.nextCalloutS = gameTimeS + 0.55f;
                }

                bulletDead[bi] = true;
                mission.shotsHit++;
                if (player.hp <= 0) {
                    corpses.push_back(player.pos);
                }
                continue;
            }
        }


        // Hit actors
        for (int i = 0; i < (int)actors.size(); ++i) {
            Actor& a = actors[i];
            if (!a.alive()) continue;
            if (!areEnemies(b.src, a.team)) continue;

            float d = length(a.pos - b.pos);
            if (d < (a.w * 0.5f + 2.f)) {
                {
                    HitZone z = HitZone::Torso;
                    resolveHitZone(a, b.pos, z);

                    float mult = zoneMultiplier(z) * weaponZoneBias(b.wid, z);
                    int dealt = (int)std::round((float)b.dmg * mult * gDamageScale);


                    a.hp -= dealt;
                    a.lastHitZone = z;
                    a.lastHitTime = gameTimeS;
                    a.lastShotOrigin = b.pos - b.dir * 40.0f;

                    // Wounds
                    if (z == HitZone::Legs) a.legWoundS = std::max(a.legWoundS, 3.0f);
                    if (z == HitZone::ArmL || z == HitZone::ArmR) a.armWoundS = std::max(a.armWoundS, 3.0f);

                    // Player callout when player is the shooter (Allies are basically just player right now)
                    if (barksEnabled && b.src == player.team && gameTimeS >= player.nextCalloutS) {
                        barks.push_back({ player.pos, calloutPlayerHit(z), 1.0f });
                        player.nextCalloutS = gameTimeS + 0.45f;
                    }

                }

                a.recentlyHit = true;
                a.recentlyHitTimer = 3.0f;
                a.lastShotOrigin = b.pos;

                // NEW: suppression spike on hit
                if (a.squadId >= 0)
                    addSuppression(a.squadId, 12.0f);

                bulletDead[bi] = true;

                mission.shotsHit++;
                if (a.hp <= 0) {
                    corpses.push_back(a.pos);
                    mission.enemiesKilled++;
                }

                raiseAlarm(1);
                break;
            }
        }
    }

    // Remove dead bullets
    std::vector<Bullet> aliveBullets;
    aliveBullets.reserve(bullets.size());
    for (int i = 0; i < (int)bullets.size(); ++i) {
        if (!bulletDead[i]) aliveBullets.push_back(bullets[i]);
    }
    bullets.swap(aliveBullets);



    // AI update
    for (auto& a : actors) {
        if (!a.alive()) continue;
        updateAI(a, dt);

        a.legWoundS = std::max(0.0f, a.legWoundS - dt);
        a.armWoundS = std::max(0.0f, a.armWoundS - dt);
    }




    // NEW: resolve AI crowding, but don't push into walls
    resolveActorCollisions(dt);


    // Mission logic
    if (mission.active) {
        updateMission(dt);
    }

    // Camera follow
    updateCamera(dt);
}

// -----------------------------------------------------------
// Render
// -----------------------------------------------------------

void Game::render() {
    setDraw(renderer, cfg::ColBg);
    SDL_RenderClear(renderer);

    // World
    SDL_RenderSetScale(renderer, zoom, zoom);
    drawWorld();
    drawActors();
    drawBullets();

    if (squadDebugViz) {
        drawSquadDebug();
    }

    // Hearing viz
    if (hearingViz) {
        SDL_SetRenderDrawColor(renderer, cfg::ColPing.r, cfg::ColPing.g, cfg::ColPing.b, cfg::ColPing.a);
        for (const auto& s : sounds) {
            float sx = (s.pos.x - camX);
            float sy = (s.pos.y - camY);
            float r = s.radiusPx;

            const int segs = 24;
            SDL_FPoint prev{};
            for (int i = 0; i <= segs; ++i) {
                float t = (6.28318f * i) / segs;
                SDL_FPoint p{
                    sx + std::cos(t) * r,
                    sy + std::sin(t) * r
                };
                if (i > 0) {
                    SDL_RenderDrawLineF(renderer, prev.x, prev.y, p.x, p.y);
                }
                prev = p;
            }
        }
    }

    SDL_RenderSetScale(renderer, 1.f, 1.f);

    // HUD & barks
    drawHUD();
    drawBarks();

    // Simple mission brief/debrief overlays
    if (showMissionBrief) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        SDL_Rect r{ 80, 80, cfg::ScreenW - 160, cfg::ScreenH - 160 };
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
        SDL_RenderDrawRect(renderer, &r);

        drawText("MISSION BRIEFING", 100, 100, cfg::ColUIAlt);
        drawMissionHUD(); // reuse text
        drawText("ENTER: Deploy  |  ESC: Cancel", 100, cfg::ScreenH - 120, cfg::ColUIAlt);
    }

    if (showMissionDebrief) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_Rect r{ 80, 80, cfg::ScreenW - 160, cfg::ScreenH - 160 };
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
        SDL_RenderDrawRect(renderer, &r);

        drawText("MISSION DEBRIEF", 100, 100, cfg::ColUIAlt);
        drawMissionHUD();
        drawText("R: Replay mission  |  D: Return to sandbox  |  ESC: Quit", 100,
            cfg::ScreenH - 120, cfg::ColUIAlt);
    }

    SDL_RenderPresent(renderer);
}
