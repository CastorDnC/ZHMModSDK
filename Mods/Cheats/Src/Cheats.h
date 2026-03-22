#pragma once

#include "IPluginInterface.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZMath.h>
#include <Glacier/ZResource.h>
#include <Glacier/ZObject.h>
#include <Glacier/THashMap.h>

#include <vector>

class ZHM5ItemWeapon;
class IFirearm;
class IBaseCharacter;
struct SHitInfo;
class ZHitmanMorphemePostProcessor;
class ZHM5WeaponRecoilController;
class ZEntitySceneContext;

class Cheats : public IPluginInterface {
public:
    Cheats();
    ~Cheats() override;

    void Init() override;
    void OnEngineInitialized() override;
    void OnDrawMenu() override;
    void OnDrawUI(bool p_HasFocus) override;
    void OnDraw3D(IRenderer* p_Renderer) override;

private:
    void OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent);

    bool EnsureRepositoryLoaded();
    void ApplyWallbangPatch();
    void RestoreWallbangPatch();

    DECLARE_PLUGIN_DETOUR(Cheats, void, OnClearScene, ZEntitySceneContext* th, bool p_FullyUnloadScene);

    DECLARE_PLUGIN_DETOUR(Cheats, void, ZHM5ItemWeapon_SetBulletsInMagazine, IFirearm* th, int32_t nBullets);

    DECLARE_PLUGIN_DETOUR(Cheats, bool, ZActor_YouGotHit, IBaseCharacter* th, const SHitInfo& hitInfo);

    DECLARE_PLUGIN_DETOUR(
        Cheats, void, ZHM5WeaponRecoilController_RecoilWeapon,
        ZHM5WeaponRecoilController* th, const TEntityRef<ZHM5ItemWeapon>& rWeapon
    );

    DECLARE_PLUGIN_DETOUR(
        Cheats, void, ZHitmanMorphemePostProcessor_UpdateWeaponRecoil,
        ZHitmanMorphemePostProcessor* th, float fDeltaTime,
        const THashMap<int32_t, int32_t, TDefaultHashMapPolicy<int32_t>>& charboneMap,
        TArrayRef<int32_t> hierarchy
    );

    DECLARE_PLUGIN_DETOUR(Cheats, bool, ZHM5ItemWeapon_FireProjectiles, ZHM5ItemWeapon* th, bool bMayStartSound);

private:
    bool m_MenuActive = false;

    // ESP
    bool m_ESPEnabled = false;
    bool m_ESPShowDead = false;
    bool m_ESPShowNames = true;
    bool m_ESPShowHealth = true;
    bool m_ESPShowDistance = true;
    bool m_ESPShowBoxes = true;
    bool m_ESPShowLines = false;

    // Aimbot
    bool m_AimbotEnabled = false;
    float m_AimbotFOV = 15.0f;

    // Wallbang
    bool m_WallbangEnabled = false;
    bool m_WallbangPatched = false;
    bool m_WallbangAutoApplyDisabled = false;
    bool m_WallbangLoggedNotReady = false;
    uint32_t m_WallbangPatchedCount = 0;
    ZResourcePtr m_RepositoryResource;
    std::vector<std::pair<ZRepositoryID, ZDynamicObject>> m_OriginalAmmoConfigs;

    // Weapon cheats
    bool m_InfiniteAmmoEnabled = false;
    bool m_NoRecoilEnabled = false;
    bool m_OneHitKillEnabled = false;
    bool m_SuperAccuracyEnabled = false;
    bool m_RapidFireEnabled = false;
};

DECLARE_ZHM_PLUGIN(Cheats)
