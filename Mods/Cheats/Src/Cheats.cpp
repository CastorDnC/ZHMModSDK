#include "Cheats.h"

#include <Hooks.h>
#include <Logging.h>
#include <Globals.h>
#include <Functions.h>
#include <Events.h>

#include <Glacier/ZActor.h>
#include <Glacier/ZSpatialEntity.h>
#include <Glacier/ZCameraEntity.h>
#include <Glacier/ZHitman5.h>
#include <Glacier/ZModule.h>
#include <Glacier/ZGameLoopManager.h>
#include <Glacier/SGameUpdateEvent.h>
#include <Glacier/ZItem.h>
#include <Glacier/CompileReflection.h>
#include <Glacier/EUpdateMode.h>

#include <IRenderer.h>

#include <IconsMaterialDesign.h>
#include <imgui.h>

#include <directxmath.h>

#include <array>
#include <string>
#include <algorithm>
#include <string_view>
#include <cmath>

// ─── Wallbang constants (same targets as TitaniumBullets) ───────────────────

namespace {

static constexpr const char kPenetrationAmmoConfigIdStr[] = "87ae0524-2f22-4fe0-82e1-84a050b43cf0";
static const ZRepositoryID kPenetrationAmmoConfigId = ZRepositoryID(kPenetrationAmmoConfigIdStr);

static const std::array<ZRepositoryID, 20> kWallbangTargets = {
    ZRepositoryID("06f7302c-60f5-41d6-9c5f-6f0659efeea4"),
    ZRepositoryID("1064bfbe-61d0-40e6-ac05-4a2c210a6e13"),
    ZRepositoryID("1d43a4aa-1bdb-4318-b97f-ebb2427d63cf"),
    ZRepositoryID("270de950-858a-46cb-9c84-4007a5f914fc"),
    ZRepositoryID("298cc6ab-41fc-475c-a9a7-afdb79d69017"),
    ZRepositoryID("2d9d14aa-ca9d-4393-a793-8b0412eb0176"),
    ZRepositoryID("357e6077-6ed0-4f5a-955a-bdf1331c3ecf"),
    ZRepositoryID("38b41bbb-2bb3-4e83-9ccd-86a806f8cc4c"),
    ZRepositoryID("57564829-af0a-4369-af40-f5943d3c1b6a"),
    ZRepositoryID("a25320e6-fd52-4c74-92f6-61f9fc157fc9"),
    ZRepositoryID("a29c35c2-526f-447e-b6ae-b15ad64bba67"),
    ZRepositoryID("c86f2315-ac67-4df1-bdc2-d18e45e0506e"),
    ZRepositoryID("d6ad5fd8-e673-4852-8062-3d790fb2b1d8"),
    ZRepositoryID("de4a2ec0-8fdf-488e-a564-2610383cf2c3"),
    ZRepositoryID("e1f41c79-bbd6-4fd1-b2df-30628d39c767"),
    ZRepositoryID("e5e70578-d49b-446c-900b-5ccbf3c1985f"),
    ZRepositoryID("e6dfcbaa-10bc-454f-a793-4a72e7db4241"),
    ZRepositoryID("eb374e6a-5ebc-4452-8b07-65516139f3e8"),
    ZRepositoryID("f9886fb6-8b04-4e23-b7e9-0eb16aa34057"),
    ZRepositoryID("fb1ed921-817d-4ced-9e0d-85743fb23aaa"),
};

static const ZString kIdKey   = ZString(std::string_view("ID_"));
static const ZString kAmmoKey = ZString(std::string_view("AmmoConfig"));

SDynamicObjectKeyValuePair* FindPair(TArray<SDynamicObjectKeyValuePair>* p_Entries, const ZString& p_Key) {
    if (!p_Entries) return nullptr;
    for (auto& entry : *p_Entries) {
        if (entry.sKey == p_Key) return &entry;
    }
    return nullptr;
}

ZRepositoryID RepoIdFromDynamic(const ZDynamicObject& p_Val) {
    if (const auto* s_Repo = p_Val.As<ZRepositoryID>()) return *s_Repo;
    if (const auto* s_Str  = p_Val.As<ZString>())       return ZRepositoryID(*s_Str);
    return ZRepositoryID("00000000-0000-0000-0000-000000000000");
}

bool IsWallbangTarget(const ZRepositoryID& p_Id) {
    return std::find(kWallbangTargets.begin(), kWallbangTargets.end(), p_Id) != kWallbangTargets.end();
}

// ─── Color helpers ───────────────────────────────────────────────────────────

SVector4 ColorAlive()    { return {0.20f, 0.90f, 0.20f, 1.f}; }
SVector4 ColorGuard()    { return {0.90f, 0.40f, 0.10f, 1.f}; }
SVector4 ColorTarget()   { return {0.95f, 0.85f, 0.10f, 1.f}; }
SVector4 ColorDead()     { return {0.50f, 0.50f, 0.50f, 0.6f}; }
SVector4 ColorText()     { return {1.f,   1.f,   1.f,   1.f}; }
SVector4 ColorTextDead() { return {0.55f, 0.55f, 0.55f, 1.f}; }
SVector4 ColorLine()     { return {1.f,   0.20f, 0.20f, 0.7f}; }

ImVec4 ToImVec4(const SVector4& c) { return {c.x, c.y, c.z, c.w}; }

} // namespace

// ─── Init / lifecycle ────────────────────────────────────────────────────────

Cheats::Cheats() {}

Cheats::~Cheats() {
    if (Globals::GameLoopManager) {
        const ZMemberDelegate<Cheats, void(const SGameUpdateEvent&)> s_Del(this, &Cheats::OnFrameUpdate);
        Globals::GameLoopManager->UnregisterFrameUpdate(s_Del, 1, EUpdateMode::eUpdateAlways);
    }

    if (m_WallbangPatched) {
        RestoreWallbangPatch();
    }
}

void Cheats::Init() {
    Hooks::ZEntitySceneContext_ClearScene->AddDetour(this, &Cheats::OnClearScene);
    Hooks::ZHM5ItemWeapon_SetBulletsInMagazine->AddDetour(this, &Cheats::ZHM5ItemWeapon_SetBulletsInMagazine);
    Hooks::ZActor_YouGotHit->AddDetour(this, &Cheats::ZActor_YouGotHit);
    Hooks::ZHM5WeaponRecoilController_RecoilWeapon->AddDetour(this, &Cheats::ZHM5WeaponRecoilController_RecoilWeapon);
    Hooks::ZHitmanMorphemePostProcessor_UpdateWeaponRecoil->AddDetour(this, &Cheats::ZHitmanMorphemePostProcessor_UpdateWeaponRecoil);
    Hooks::ZHM5ItemWeapon_FireProjectiles->AddDetour(this, &Cheats::ZHM5ItemWeapon_FireProjectiles);
}

void Cheats::OnEngineInitialized() {
    const ZMemberDelegate<Cheats, void(const SGameUpdateEvent&)> s_Del(this, &Cheats::OnFrameUpdate);
    Globals::GameLoopManager->RegisterFrameUpdate(s_Del, 1, EUpdateMode::eUpdateAlways);
}

// ─── Menu / UI ───────────────────────────────────────────────────────────────

void Cheats::OnDrawMenu() {
    if (ImGui::Button(ICON_MD_SHIELD " CHEATS")) {
        m_MenuActive = !m_MenuActive;
    }
}

void Cheats::OnDrawUI(bool p_HasFocus) {
    if (!p_HasFocus || !m_MenuActive) return;

    ImGui::PushFont(SDK()->GetImGuiBlackFont());
    const bool s_Showing = ImGui::Begin("CHEATS", &m_MenuActive);
    ImGui::PushFont(SDK()->GetImGuiRegularFont());

    if (s_Showing) {
        // ── ESP ──────────────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader(ICON_MD_VISIBILITY " ESP", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable ESP", &m_ESPEnabled);

            if (m_ESPEnabled) {
                ImGui::Indent();
                ImGui::Checkbox("Show Boxes",    &m_ESPShowBoxes);
                ImGui::Checkbox("Show Lines",    &m_ESPShowLines);
                ImGui::Checkbox("Show Names",    &m_ESPShowNames);
                ImGui::Checkbox("Show Health",   &m_ESPShowHealth);
                ImGui::Checkbox("Show Distance", &m_ESPShowDistance);
                ImGui::Checkbox("Show Dead",     &m_ESPShowDead);
                ImGui::Unindent();

                ImGui::Spacing();
                ImGui::TextDisabled("Green = Civilian  Orange = Guard  Yellow = Target  Gray = Dead");
            }
        }

        ImGui::Separator();

        // ── Aimbot ───────────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader(ICON_MD_GPS_FIXED " AIMBOT", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Aimbot", &m_AimbotEnabled);

            if (m_AimbotEnabled) {
                ImGui::Indent();
                ImGui::SliderFloat("FOV (degrees)", &m_AimbotFOV, 1.0f, 90.0f);
                ImGui::TextDisabled("Snaps camera to nearest NPC within FOV");
                ImGui::Unindent();
            }
        }

        ImGui::Separator();

        // ── Wallbang ─────────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader(ICON_MD_BLUR_ON " WALLBANG", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Wallbang (Bullet Penetration)", &m_WallbangEnabled)) {
                if (m_WallbangEnabled) {
                    ApplyWallbangPatch();
                } else {
                    RestoreWallbangPatch();
                }
            }

            if (m_WallbangPatched) {
                ImGui::TextColored({0.2f, 0.9f, 0.2f, 1.f}, "Patched (%u entries)", m_WallbangPatchedCount);
            } else if (m_WallbangEnabled) {
                ImGui::TextColored({1.f, 0.8f, 0.1f, 1.f}, "Waiting for repository...");
            }
        }

        ImGui::Separator();

        // ── Weapon Cheats ────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader(ICON_MD_SPORTS_SCORE " WEAPON CHEATS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Infinite Ammo",   &m_InfiniteAmmoEnabled);
            ImGui::Checkbox("No Recoil",       &m_NoRecoilEnabled);
            ImGui::Checkbox("Super Accuracy",  &m_SuperAccuracyEnabled);
            ImGui::Checkbox("Rapid Fire",      &m_RapidFireEnabled);
            ImGui::Checkbox("One Hit Kill",    &m_OneHitKillEnabled);
        }
    }

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopFont();
}

// ─── ESP ─────────────────────────────────────────────────────────────────────

void Cheats::OnDraw3D(IRenderer* p_Renderer) {
    if (!m_ESPEnabled) return;
    if (!Globals::ActorManager) return;

    const auto s_LocalHitman = SDK()->GetLocalPlayer();
    SVector3 s_PlayerPos = {};

    if (s_LocalHitman) {
        if (const auto* s_Spatial = s_LocalHitman.m_entityRef.QueryInterface<ZSpatialEntity>()) {
            const auto s_Mat = s_Spatial->GetObjectToWorldMatrix();
            s_PlayerPos = {s_Mat.Trans.x, s_Mat.Trans.y, s_Mat.Trans.z};
        }
    }

    for (int i = 0; i < Globals::ActorManager->m_activatedActors.size(); ++i) {
        const auto& s_ActorRef = Globals::ActorManager->m_activatedActors[i];
        ZActor* s_Actor = s_ActorRef.m_pInterfaceRef;

        if (!s_Actor) continue;

        const bool s_Alive = s_Actor->m_bAlive;
        if (!s_Alive && !m_ESPShowDead) continue;

        const ZSpatialEntity* s_Spatial = s_ActorRef.m_entityRef.QueryInterface<ZSpatialEntity>();
        if (!s_Spatial) continue;

        const SMatrix s_WorldMat = s_Spatial->GetObjectToWorldMatrix();
        const SVector3 s_Feet = {s_WorldMat.Trans.x, s_WorldMat.Trans.y, s_WorldMat.Trans.z};
        const SVector3 s_Head = {s_Feet.x, s_Feet.y, s_Feet.z + 1.75f};

        // Choose color by priority: target > guard > alive civilian > dead
        SVector4 s_Color;
        if (!s_Alive) {
            s_Color = ColorDead();
        } else if (s_Actor->m_bContractTarget || s_Actor->m_bContractTargetLive) {
            s_Color = ColorTarget();
        } else if (s_Actor->m_bActiveEnforcer || s_Actor->m_bActiveSentry || s_Actor->m_bIsPotentialEnforcer) {
            s_Color = ColorGuard();
        } else {
            s_Color = ColorAlive();
        }

        // Box
        if (m_ESPShowBoxes) {
            const SVector3 s_Min = {s_Feet.x - 0.35f, s_Feet.y - 0.35f, s_Feet.z};
            const SVector3 s_Max = {s_Feet.x + 0.35f, s_Feet.y + 0.35f, s_Head.z};
            p_Renderer->DrawBox3D(s_Min, s_Max, s_Color);
        }

        // Line from screen bottom-center to NPC feet
        if (m_ESPShowLines && s_Alive) {
            SVector2 s_ScreenFeet;
            if (p_Renderer->WorldToScreen(s_Feet, s_ScreenFeet)) {
                p_Renderer->DrawLine3D(s_PlayerPos, s_Feet, ColorLine(), ColorLine());
            }
        }

        // Text label above head
        const bool s_HasText = m_ESPShowNames || m_ESPShowHealth || m_ESPShowDistance;
        if (s_HasText) {
            SVector2 s_ScreenHead;
            if (!p_Renderer->WorldToScreen(s_Head, s_ScreenHead)) continue;

            const SVector4& s_TextColor = s_Alive ? ColorText() : ColorTextDead();

            float s_TextY = s_ScreenHead.y - 4.f;
            const float s_LineHeight = 12.f;

            if (m_ESPShowNames) {
                const std::string s_Name(s_Actor->m_sActorName.c_str(), s_Actor->m_sActorName.size());
                p_Renderer->DrawText2D(
                    ZString(s_Name.c_str()),
                    {s_ScreenHead.x, s_TextY},
                    s_TextColor, 0.f, 1.f,
                    TextAlignment::Center, TextAlignment::Bottom
                );
                s_TextY -= s_LineHeight;
            }

            if (m_ESPShowHealth && s_Alive) {
                const std::string s_HP = std::to_string(static_cast<int>(s_Actor->m_fCurrentHitPoints)) + " HP";
                p_Renderer->DrawText2D(
                    ZString(s_HP.c_str()),
                    {s_ScreenHead.x, s_TextY},
                    {0.3f, 1.f, 0.3f, 1.f}, 0.f, 0.9f,
                    TextAlignment::Center, TextAlignment::Bottom
                );
                s_TextY -= s_LineHeight;
            }

            if (m_ESPShowDistance) {
                const float s_Dx = s_Feet.x - s_PlayerPos.x;
                const float s_Dy = s_Feet.y - s_PlayerPos.y;
                const float s_Dz = s_Feet.z - s_PlayerPos.z;
                const float s_Dist = std::sqrtf(s_Dx * s_Dx + s_Dy * s_Dy + s_Dz * s_Dz);
                const std::string s_DistStr = std::to_string(static_cast<int>(s_Dist)) + "m";
                p_Renderer->DrawText2D(
                    ZString(s_DistStr.c_str()),
                    {s_ScreenHead.x, s_TextY},
                    {0.8f, 0.8f, 0.8f, 1.f}, 0.f, 0.9f,
                    TextAlignment::Center, TextAlignment::Bottom
                );
            }
        }
    }
}

// ─── Aimbot ──────────────────────────────────────────────────────────────────

void Cheats::OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent) {
    // Wallbang: retry patch every frame until applied
    if (m_WallbangEnabled && !m_WallbangPatched && !m_WallbangAutoApplyDisabled) {
        ApplyWallbangPatch();
    }

    if (!m_AimbotEnabled) return;
    if (!Globals::ActorManager) return;

    ZCameraEntity* s_Cam = Functions::GetCurrentCamera->Call();
    if (!s_Cam) return;

    const SMatrix s_CamMat = s_Cam->GetObjectToWorldMatrix();
    const SVector3 s_CamPos = {s_CamMat.Trans.x, s_CamMat.Trans.y, s_CamMat.Trans.z};

    // Half-FOV in radians for comparison against dot product
    const float s_HalfFovRad = m_AimbotFOV * 0.5f * (3.14159265f / 180.f);
    const float s_CosHalfFov = std::cosf(s_HalfFovRad);

    // Camera forward = -ZAxis (CameraBackward)
    const SVector3 s_CamFwd = {-s_CamMat.ZAxis.x, -s_CamMat.ZAxis.y, -s_CamMat.ZAxis.z};

    ZActor* s_BestActor = nullptr;
    SVector3 s_BestHead = {};
    float s_BestDot = s_CosHalfFov; // only snap to NPCs within our FOV cone

    for (int i = 0; i < Globals::ActorManager->m_activatedActors.size(); ++i) {
        const auto& s_Ref = Globals::ActorManager->m_activatedActors[i];
        ZActor* s_Actor = s_Ref.m_pInterfaceRef;

        if (!s_Actor || !s_Actor->m_bAlive) continue;

        const ZSpatialEntity* s_Spatial = s_Ref.m_entityRef.QueryInterface<ZSpatialEntity>();
        if (!s_Spatial) continue;

        const SMatrix s_Mat = s_Spatial->GetObjectToWorldMatrix();
        const SVector3 s_Head = {s_Mat.Trans.x, s_Mat.Trans.y, s_Mat.Trans.z + 1.6f};

        // Direction from camera to NPC head
        const float s_Dx = s_Head.x - s_CamPos.x;
        const float s_Dy = s_Head.y - s_CamPos.y;
        const float s_Dz = s_Head.z - s_CamPos.z;
        const float s_Len = std::sqrtf(s_Dx * s_Dx + s_Dy * s_Dy + s_Dz * s_Dz);
        if (s_Len < 0.001f) continue;

        // Dot product with camera forward = cos(angle between camera and NPC)
        const float s_Dot = (s_CamFwd.x * s_Dx + s_CamFwd.y * s_Dy + s_CamFwd.z * s_Dz) / s_Len;

        if (s_Dot > s_BestDot) {
            s_BestDot = s_Dot;
            s_BestActor = s_Actor;
            s_BestHead = s_Head;
        }
    }

    if (!s_BestActor) return;

    // Build look-at world matrix for the camera
    using namespace DirectX;

    const XMVECTOR s_Eye    = XMVectorSet(s_CamPos.x,    s_CamPos.y,    s_CamPos.z,    1.f);
    const XMVECTOR s_Target = XMVectorSet(s_BestHead.x,  s_BestHead.y,  s_BestHead.z,  1.f);
    const XMVECTOR s_WorldUp = XMVectorSet(0.f, 0.f, 1.f, 0.f);

    // Build view matrix (camera-from-world), then invert to get world-from-camera
    const XMMATRIX s_ViewLH  = XMMatrixLookAtLH(s_Eye, s_Target, s_WorldUp);
    const XMMATRIX s_WorldLH = XMMatrixInverse(nullptr, s_ViewLH);

    SMatrix s_NewMat(s_WorldLH);
    s_NewMat.Trans = s_CamMat.Trans; // keep camera position

    s_Cam->SetObjectToWorldMatrixFromEditor(s_NewMat);
}

// ─── Wallbang (repository patch) ─────────────────────────────────────────────

bool Cheats::EnsureRepositoryLoaded() {
    if (!Globals::ResourceManager) return false;

    if (m_RepositoryResource.m_nResourceIndex.val == -1) {
        const auto s_Id = ResId<"[assembly:/repository/pro.repo].pc_repo">;
        Globals::ResourceManager->GetResourcePtr(m_RepositoryResource, s_Id, 0);
    }

    return m_RepositoryResource && m_RepositoryResource.GetResourceInfo().status == RESOURCE_STATUS_VALID;
}

void Cheats::ApplyWallbangPatch() {
    if (m_WallbangPatched) return;

    if (!EnsureRepositoryLoaded()) {
        if (!m_WallbangLoggedNotReady) {
            Logger::Debug("[Cheats] pro.repo not ready yet; retrying...");
            m_WallbangLoggedNotReady = true;
        }
        return;
    }

    m_WallbangLoggedNotReady = false;

    const auto s_RepoData = static_cast<THashMap<
        ZRepositoryID, ZDynamicObject, TDefaultHashMapPolicy<ZRepositoryID>>*>(
        m_RepositoryResource.GetResourceData()
    );

    if (!s_RepoData) {
        Logger::Warn("[Cheats] pro.repo data is null");
        m_WallbangAutoApplyDisabled = true;
        return;
    }

    m_OriginalAmmoConfigs.clear();
    m_WallbangPatchedCount = 0;

    // Fast path: lookup by map key
    for (const auto& s_TargetId : kWallbangTargets) {
        auto s_It = s_RepoData->find(s_TargetId);
        if (s_It == s_RepoData->end()) continue;

        auto* s_Entries = s_It->second.As<TArray<SDynamicObjectKeyValuePair>>();
        auto* s_Pair    = FindPair(s_Entries, kAmmoKey);
        if (!s_Pair) continue;

        if (s_Pair->value.As<ZRepositoryID>()) {
            m_OriginalAmmoConfigs.emplace_back(s_TargetId, s_Pair->value);
            s_Pair->value = kPenetrationAmmoConfigId;
            ++m_WallbangPatchedCount;
        } else if (s_Pair->value.As<ZString>()) {
            m_OriginalAmmoConfigs.emplace_back(s_TargetId, s_Pair->value);
            s_Pair->value = ZString(std::string_view(kPenetrationAmmoConfigIdStr));
            ++m_WallbangPatchedCount;
        }
    }

    // Fallback: scan all entries by ID_ field
    if (m_WallbangPatchedCount == 0) {
        for (auto& [s_Key, s_Obj] : *s_RepoData) {
            auto* s_Entries = s_Obj.As<TArray<SDynamicObjectKeyValuePair>>();
            if (!s_Entries) continue;

            auto* s_IdPair = FindPair(s_Entries, kIdKey);
            if (!s_IdPair) continue;

            if (!IsWallbangTarget(RepoIdFromDynamic(s_IdPair->value))) continue;

            auto* s_Pair = FindPair(s_Entries, kAmmoKey);
            if (!s_Pair) continue;

            if (s_Pair->value.As<ZRepositoryID>()) {
                m_OriginalAmmoConfigs.emplace_back(s_Key, s_Pair->value);
                s_Pair->value = kPenetrationAmmoConfigId;
                ++m_WallbangPatchedCount;
            } else if (s_Pair->value.As<ZString>()) {
                m_OriginalAmmoConfigs.emplace_back(s_Key, s_Pair->value);
                s_Pair->value = ZString(std::string_view(kPenetrationAmmoConfigIdStr));
                ++m_WallbangPatchedCount;
            }
        }
    }

    if (m_WallbangPatchedCount == 0) {
        Logger::Warn("[Cheats] Wallbang: no matching repository entries found.");
        m_WallbangAutoApplyDisabled = true;
        return;
    }

    m_WallbangPatched = true;
    m_WallbangAutoApplyDisabled = false;
    Logger::Info("[Cheats] Wallbang: patched {} entries.", m_WallbangPatchedCount);
}

void Cheats::RestoreWallbangPatch() {
    if (!m_WallbangPatched) return;

    if (!EnsureRepositoryLoaded()) {
        m_WallbangPatched = false;
        m_OriginalAmmoConfigs.clear();
        return;
    }

    const auto s_RepoData = static_cast<THashMap<
        ZRepositoryID, ZDynamicObject, TDefaultHashMapPolicy<ZRepositoryID>>*>(
        m_RepositoryResource.GetResourceData()
    );

    if (!s_RepoData) {
        m_WallbangPatched = false;
        m_OriginalAmmoConfigs.clear();
        return;
    }

    for (const auto& [s_Key, s_Original] : m_OriginalAmmoConfigs) {
        auto s_It = s_RepoData->find(s_Key);
        if (s_It == s_RepoData->end()) continue;

        auto* s_Entries = s_It->second.As<TArray<SDynamicObjectKeyValuePair>>();
        auto* s_Pair    = FindPair(s_Entries, kAmmoKey);
        if (!s_Pair) continue;

        s_Pair->value = s_Original;
    }

    m_WallbangPatched = false;
    m_WallbangPatchedCount = 0;
    m_OriginalAmmoConfigs.clear();
    Logger::Info("[Cheats] Wallbang: patch restored.");
}

// ─── Scene clear ─────────────────────────────────────────────────────────────

DEFINE_PLUGIN_DETOUR(Cheats, void, OnClearScene, ZEntitySceneContext* th, bool p_FullyUnloadScene) {
    m_RepositoryResource        = {};
    m_OriginalAmmoConfigs.clear();
    m_WallbangPatched            = false;
    m_WallbangPatchedCount       = 0;
    m_WallbangAutoApplyDisabled  = false;
    m_WallbangLoggedNotReady     = false;

    return HookResult<void>(HookAction::Continue());
}

// ─── Weapon hooks ─────────────────────────────────────────────────────────────

DEFINE_PLUGIN_DETOUR(Cheats, void, ZHM5ItemWeapon_SetBulletsInMagazine, IFirearm* th, int32_t nBullets) {
    if (!m_InfiniteAmmoEnabled || nBullets != 0) {
        return HookResult<void>(HookAction::Continue());
    }

    const auto s_LocalHitman = SDK()->GetLocalPlayer();
    if (!s_LocalHitman) return HookResult<void>(HookAction::Continue());

    ZHM5ItemWeapon* s_Weapon = static_cast<ZHM5ItemWeapon*>(th);
    if (s_Weapon->m_pOwner != s_LocalHitman.m_entityRef) {
        return HookResult<void>(HookAction::Continue());
    }

    // Refill to capacity
    p_Hook->CallOriginal(th, s_Weapon->GetMagazineCapacity());
    return HookResult<void>(HookAction::Return());
}

DEFINE_PLUGIN_DETOUR(Cheats, bool, ZActor_YouGotHit, IBaseCharacter* th, const SHitInfo& hitInfo) {
    if (m_OneHitKillEnabled) {
        ZActor* s_Actor = static_cast<ZActor*>(th);
        s_Actor->m_fCurrentHitPoints = 0.f;
    }
    return HookResult<bool>(HookAction::Continue());
}

DEFINE_PLUGIN_DETOUR(
    Cheats, void, ZHitmanMorphemePostProcessor_UpdateWeaponRecoil,
    ZHitmanMorphemePostProcessor* th, float fDeltaTime,
    const THashMap<int32_t, int32_t, TDefaultHashMapPolicy<int32_t>>& charboneMap,
    TArrayRef<int32_t> hierarchy
) {
    if (m_NoRecoilEnabled) {
        return HookResult<void>(HookAction::Return());
    }
    return HookResult<void>(HookAction::Continue());
}

DEFINE_PLUGIN_DETOUR(
    Cheats, void, ZHM5WeaponRecoilController_RecoilWeapon,
    ZHM5WeaponRecoilController* th, const TEntityRef<ZHM5ItemWeapon>& rWeapon
) {
    if (!m_NoRecoilEnabled) {
        return HookResult<void>(HookAction::Continue());
    }

    p_Hook->CallOriginal(th, rWeapon);
    th->m_vRecoil = SVector2(0.f, 0.f);
    return HookResult<void>(HookAction::Return());
}

DEFINE_PLUGIN_DETOUR(Cheats, bool, ZHM5ItemWeapon_FireProjectiles, ZHM5ItemWeapon* th, bool bMayStartSound) {
    if (!m_SuperAccuracyEnabled && !m_RapidFireEnabled) {
        return HookResult<bool>(HookAction::Continue());
    }

    const auto s_LocalHitman = SDK()->GetLocalPlayer();
    if (!s_LocalHitman) return HookResult<bool>(HookAction::Continue());

    if (th->m_pOwner != s_LocalHitman.m_entityRef) {
        return HookResult<bool>(HookAction::Continue());
    }

    if (m_RapidFireEnabled) {
        th->m_tLastShootTime = ZGameTime{};
    }

    bool s_Result = p_Hook->CallOriginal(th, bMayStartSound);

    if (m_SuperAccuracyEnabled) {
        th->m_fPrecisionFactor = 0.f;
    }

    return HookResult<bool>(HookAction::Return(), s_Result);
}

DEFINE_ZHM_PLUGIN(Cheats)
