#ifndef WSW_7f020248_88b8_40f2_ae59_1e97c51e2f82_H
#define WSW_7f020248_88b8_40f2_ae59_1e97c51e2f82_H

#include "../gameshared/q_shared.h"
#include "../gameshared/q_comref.h"
#include "../gameshared/q_math.h"
#include "../gameshared/gs_public.h"

#include "../qcommon/wswstringview.h"

struct sfx_s;
struct model_s;
struct shader_s;

constexpr const unsigned kNumCrosshairs = 10;
constexpr const char *kCrosshairsFormat = "gfx/hud/crosshair%d";
constexpr const unsigned kNumStrongCrosshairs = 3;
constexpr const char *kStrongCrosshairsFormat = "gfx/hud/strong_crosshair%d";

class MediaCache {
	// Currently all entries are precached.
	// Separate types that check a handle status on every access should be introduced if lazy loading is needed.
public:
	template <typename T>
	class CachedHandle {
		friend class MediaCache;

		// TODO: We can compress links to offsets from parent
		CachedHandle<T> *m_next { nullptr };
		T *m_handle { nullptr };
		const wsw::StringView m_name;

		explicit CachedHandle( const wsw::StringView &name ) : m_name( name ) {}
	public:
		[[nodiscard]]
		operator T*() { return m_handle; }
		[[nodiscard]]
		operator const T*() const { return m_handle; }
	};

	class CachedSound : public CachedHandle<sfx_s> {
		friend class MediaCache;
		CachedSound( MediaCache *parent, const wsw::StringView &name );
	};
	class CachedModel : public CachedHandle<model_s> {
		friend class MediaCache;
		CachedModel( MediaCache *parent, const wsw::StringView &name );
	};
	class CachedMaterial : public CachedHandle<shader_s> {
		friend class MediaCache;
		CachedMaterial( MediaCache *parent, const wsw::StringView &name );
	};

	template <typename T>
	class ArbitraryLengthHandlesArray {
		friend class MediaCache;
	protected:
		ArbitraryLengthHandlesArray<T> *m_next { nullptr };
		const char *const m_format;
		T **m_handles { nullptr };
		const unsigned m_numHandles;
		const unsigned m_indexShift;

		ArbitraryLengthHandlesArray( const char *format, unsigned numHandles, unsigned indexShift = 1u );
	public:
		[[nodiscard]]
		auto length() const -> unsigned { return m_numHandles; }

		[[nodiscard]]
		auto operator[]( unsigned index ) -> T * {
			assert( index < m_numHandles );
			return m_handles[index];
		}
		[[nodiscard]]
		auto operator[]( unsigned index ) const -> const T * {
			assert( index < m_numHandles );
			return m_handles[index];
		}
	};

	template <typename T, unsigned Length>
	class CachedHandlesArray : public ArbitraryLengthHandlesArray<T> {
		friend class MediaCache;
		T *m_handlesStorage[Length];
	protected:
		explicit CachedHandlesArray( const char *format, unsigned indexShift = 1u );
	};

	template <unsigned Length>
	class CachedSoundsArray : public CachedHandlesArray<sfx_s, Length> {
		friend class MediaCache;
		CachedSoundsArray( MediaCache *parent, const char *format, unsigned indexShift = 1u );
	};

	template <unsigned Length>
	class CachedMaterialsArray : public CachedHandlesArray<shader_s, Length> {
		friend class MediaCache;
		CachedMaterialsArray( MediaCache *parent, const char *format, unsigned indexShift = 1u );
	};

	void registerSounds();
	void registerModels();
	void registerMaterials();

	using LinkedSoundsArray = ArbitraryLengthHandlesArray<sfx_s>;
	using LinkedMaterialsArray = ArbitraryLengthHandlesArray<shader_s>;
private:
	// TODO: Should we just keep precached and non-precached linked handles in different lists?
	CachedSound *m_sounds { nullptr };
	CachedModel *m_models { nullptr };
	CachedMaterial *m_materials { nullptr };

	LinkedSoundsArray *m_soundsArrays { nullptr };
	LinkedMaterialsArray *m_materialsArrays { nullptr };

	template <typename T>
	void link( T *item, T **head ) {
		item->m_next = *head;
		*head = item;
	}

	void registerSound( CachedSound *sound );
	void registerModel( CachedModel *model );
	void registerMaterial( CachedMaterial *material );
	void registerSoundsArray( LinkedSoundsArray *sound );
	void registerMaterialsArray( LinkedMaterialsArray *material );

	template <typename T>
	[[nodiscard]]
	auto formatName( char *buffer, ArbitraryLengthHandlesArray<T> *array, unsigned index ) -> const char *;
public:
	MediaCache();

	CachedSound sfxChat { this, wsw::StringView( S_CHAT ) };
	CachedSoundsArray<2> sfxRic { this, "sounds/weapons/ric%d" };
	CachedSoundsArray<4> sfxWeaponHit { this, S_WEAPON_HITS, 0 };
	CachedSoundsArray<4> sfxWeaponHit2 { this, "sounds/misc/hit_plus_%d", 0 };

	CachedSound sfxWeaponKill { this, wsw::StringView( S_WEAPON_KILL ) };
	CachedSound sfxWeaponHitTeam { this, wsw::StringView( S_WEAPON_HIT_TEAM ) };
	CachedSound sfxWeaponUp { this, wsw::StringView( S_WEAPON_SWITCH ) };
	CachedSound sfxWeaponUpNoAmmo { this, wsw::StringView( S_WEAPON_NOAMMO ) };

	CachedSound sfxWalljumpFailed { this, wsw::StringView( "sounds/world/ft_walljump_failed" ) };

	CachedSound sfxItemRespawn { this, wsw::StringView( S_ITEM_RESPAWN ) };
	CachedSound sfxPlayerRespawn { this, wsw::StringView( S_PLAYER_RESPAWN ) };
	CachedSound sfxTeleportIn { this, wsw::StringView( S_TELEPORT_IN ) };
	CachedSound sfxTeleportOut { this, wsw::StringView( S_TELEPORT_OUT ) };
	CachedSound sfxShellHit { this, wsw::StringView( S_SHELL_HIT ) };

	CachedSoundsArray<3> sfxGunbladeWeakShot { this, S_WEAPON_GUNBLADE_W_SHOT_1_to_3 };
	CachedSoundsArray<3> sfxBladeFleshHit { this, S_WEAPON_GUNBLADE_W_HIT_FLESH_1_to_3 };
	CachedSoundsArray<2> sfxBladeWallHit { this, S_WEAPON_GUNBLADE_W_HIT_WALL_1_to_2 };

	CachedSound sfxGunbladeStrongShot { this, wsw::StringView( S_WEAPON_GUNBLADE_S_SHOT ) };

	CachedSoundsArray<2> sfxGunbladeStrongHit { this, S_WEAPON_GUNBLADE_S_HIT_1_to_2 };

	CachedSound sfxRiotgunWeakHit { this, wsw::StringView( S_WEAPON_RIOTGUN_W_HIT ) };
	CachedSound sfxRiotgunStrongHit { this, wsw::StringView( S_WEAPON_RIOTGUN_S_HIT ) };

	CachedSoundsArray<2> sfxGrenadeWeakBounce { this, S_WEAPON_GRENADE_W_BOUNCE_1_to_2 };
	CachedSoundsArray<2> sfxGrenadeStrongBounce { this, S_WEAPON_GRENADE_S_BOUNCE_1_to_2, 1 };

	CachedSound sfxGrenadeWeakExplosion { this, wsw::StringView( S_WEAPON_GRENADE_W_HIT ) };
	CachedSound sfxGrenadeStrongExplosion { this, wsw::StringView( S_WEAPON_GRENADE_S_HIT ) };

	CachedSound sfxBombletBounce { this, wsw::StringView( S_BOMBLET_BOUNCE ) };
	CachedSound sfxBombletExplosion { this, wsw::StringView( S_BOMBLET_EXPLOSION ) };

	CachedSound sfxRocketLauncherWeakHit { this, wsw::StringView( S_WEAPON_ROCKET_W_HIT ) };
	CachedSound sfxRocketLauncherStrongHit { this, wsw::StringView( S_WEAPON_ROCKET_S_HIT ) };

	CachedSound sfxPlasmaWeakHit { this, wsw::StringView( S_WEAPON_PLASMAGUN_W_HIT ) };
	CachedSound sfxPlasmaStrongHit { this, wsw::StringView( S_WEAPON_PLASMAGUN_S_HIT ) };

	// Lasergun sounds
	CachedSound sfxLasergunWeakHum { this, wsw::StringView( S_WEAPON_LASERGUN_W_HUM ) };
	CachedSound sfxLasergunWeakQuadHum { this, wsw::StringView( S_WEAPON_LASERGUN_W_QUAD_HUM ) };
	CachedSound sfxLasergunWeakStop { this, wsw::StringView( S_WEAPON_LASERGUN_W_STOP ) };
	CachedSound sfxLasergunStrongHum { this, wsw::StringView( S_WEAPON_LASERGUN_S_HUM ) };
	CachedSound sfxLasergunStrongQuadHum { this, wsw::StringView( S_WEAPON_LASERGUN_S_QUAD_HUM ) };
	CachedSound sfxLasergunStrongStop { this, wsw::StringView( S_WEAPON_LASERGUN_S_STOP ) };
	CachedSound sfxLasergunHit[3] {
		{ this, wsw::StringView( S_WEAPON_LASERGUN_HIT_0 ) }, 
		{ this, wsw::StringView( S_WEAPON_LASERGUN_HIT_1 ) },
		{ this, wsw::StringView( S_WEAPON_LASERGUN_HIT_2 ) }
	};

	CachedSound sfxWaveWeakHit { this, wsw::StringView( S_WEAPON_SHOCKWAVE_W_HIT ) };
	CachedSound sfxWaveStrongHit { this, wsw::StringView( S_WEAPON_SHOCKWAVE_S_HIT ) };

	CachedSound sfxElectroboltHit { this, wsw::StringView( S_WEAPON_ELECTROBOLT_HIT ) };

	CachedSound sfxExplosionLfe { this, wsw::StringView( S_EXPLOSION_LFE ) };
	CachedSound sfxQuadFireSound { this, wsw::StringView( S_QUAD_FIRE ) };

	CachedModel modRocketExplosion { this, wsw::StringView( PATH_ROCKET_EXPLOSION_MODEL ) };
	CachedModel modPlasmaExplosion { this, wsw::StringView( PATH_PLASMA_EXPLOSION_MODEL ) };
	CachedModel modWaveExplosion { this, wsw::StringView( PATH_SHOCKWAVE_EXPLOSION_MODEL ) };

	CachedModel modDash { this, wsw::StringView( "models/effects/dash_burst.md3" ) };
	CachedModel modHeadStun { this, wsw::StringView( "models/effects/head_stun.md3" ) };

	CachedModel modBulletExplode { this, wsw::StringView( PATH_BULLET_EXPLOSION_MODEL ) };
	CachedModel modBladeWallHit { this, wsw::StringView( PATH_GUNBLADEBLAST_IMPACT_MODEL ) };
	CachedModel modBladeWallExplo { this, wsw::StringView( PATH_GUNBLADEBLAST_EXPLOSION_MODEL ) };
	CachedModel modElectroBoltWallHit { this, wsw::StringView( PATH_ELECTROBLAST_IMPACT_MODEL ) };
	CachedModel modInstagunWallHit { this, wsw::StringView( PATH_INSTABLAST_IMPACT_MODEL ) };
	CachedModel modLasergunWallExplo { this, wsw::StringView( PATH_LASERGUN_IMPACT_MODEL ) };

	CachedMaterialsArray<kNumCrosshairs> shaderCrosshair { this, kCrosshairsFormat };
	CachedMaterialsArray<kNumStrongCrosshairs> shaderStrongCrosshair { this, kStrongCrosshairsFormat };

	CachedMaterial shaderParticle { this, wsw::StringView( "particle" ) };

	CachedMaterial shaderNet { this, wsw::StringView( "gfx/hud/net" ) };
	CachedMaterial shaderSelect { this, wsw::StringView( "gfx/hud/select" ) };
	CachedMaterial shaderChatBalloon { this, wsw::StringView( PATH_BALLONCHAT_ICON ) };
	CachedMaterial shaderDownArrow { this, wsw::StringView( "gfx/2d/arrow_down" ) };

	CachedMaterial shaderPlayerShadow { this, wsw::StringView( "gfx/decals/shadow" ) };

	CachedMaterial shaderWaterBubble { this, wsw::StringView( "gfx/misc/waterBubble" ) };
	CachedMaterial shaderSmokePuff { this, wsw::StringView( "gfx/misc/smokepuff" ) };

	CachedMaterial shaderSmokePuff1 { this, wsw::StringView( "gfx/misc/smokepuff1" ) };
	CachedMaterial shaderSmokePuff2 { this, wsw::StringView( "gfx/misc/smokepuff2" ) };
	CachedMaterial shaderSmokePuff3 { this, wsw::StringView( "gfx/misc/smokepuff3" ) };

	CachedMaterial shaderStrongRocketFireTrailPuff { this, wsw::StringView( "gfx/misc/strong_rocket_fire" ) };
	CachedMaterial shaderWeakRocketFireTrailPuff { this, wsw::StringView( "gfx/misc/strong_rocket_fire" ) };
	CachedMaterial shaderTeleporterSmokePuff { this, wsw::StringView( "TeleporterSmokePuff" ) };
	CachedMaterial shaderGrenadeTrailSmokePuff { this, wsw::StringView( "gfx/grenadetrail_smoke_puf" ) };
	CachedMaterial shaderRocketTrailSmokePuff { this, wsw::StringView( "gfx/misc/rocketsmokepuff" ) };
	CachedMaterial shaderBloodTrailPuff { this, wsw::StringView( "gfx/misc/bloodtrail_puff" ) };
	CachedMaterial shaderBloodTrailLiquidPuff { this, wsw::StringView( "gfx/misc/bloodtrailliquid_puff" ) };
	CachedMaterial shaderBloodImpactPuff { this, wsw::StringView( "gfx/misc/bloodimpact_puff" ) };
	CachedMaterial shaderCartoonHit { this, wsw::StringView( "gfx/misc/cartoonhit" ) };
	CachedMaterial shaderCartoonHit2 { this, wsw::StringView( "gfx/misc/cartoonhit2" ) };
	CachedMaterial shaderCartoonHit3 { this, wsw::StringView( "gfx/misc/cartoonhit3" ) };
	CachedMaterial shaderTeamMateIndicator { this, wsw::StringView( "gfx/indicators/teammate_indicator" ) };
	CachedMaterial shaderTeamCarrierIndicator { this, wsw::StringView( "gfx/indicators/teamcarrier_indicator" ) };
	CachedMaterial shaderTeleportShellGfx { this, wsw::StringView( "gfx/misc/teleportshell" ) };

	CachedMaterial shaderAdditiveParticleShine { this, wsw::StringView( "additiveParticleShine" ) };

	CachedMaterial shaderBladeMark { this, wsw::StringView( "gfx/decals/d_blade_hit" ) };
	CachedMaterial shaderBulletMark { this, wsw::StringView( "gfx/decals/d_bullet_hit" ) };
	CachedMaterial shaderExplosionMark { this, wsw::StringView( "gfx/decals/d_explode_hit" ) };
	CachedMaterial shaderPlasmaMark { this, wsw::StringView( "gfx/decals/d_plasma_hit" ) };
	CachedMaterial shaderElectroboltMark { this, wsw::StringView( "gfx/decals/d_electrobolt_hit" ) };
	CachedMaterial shaderInstagunMark { this, wsw::StringView( "gfx/decals/d_instagun_hit" ) };

	CachedMaterial shaderElectroBeamOld { this, wsw::StringView( "gfx/misc/electro" ) };
	CachedMaterial shaderElectroBeamOldAlpha { this, wsw::StringView( "gfx/misc/electro_alpha" ) };
	CachedMaterial shaderElectroBeamOldBeta { this, wsw::StringView( "gfx/misc/electro_beta" ) };
	CachedMaterial shaderElectroBeamA { this, wsw::StringView( "gfx/misc/electro2a" ) };
	CachedMaterial shaderElectroBeamAAlpha { this, wsw::StringView( "gfx/misc/electro2a_alpha" ) };
	CachedMaterial shaderElectroBeamABeta { this, wsw::StringView( "gfx/misc/electro2a_beta" ) };
	CachedMaterial shaderElectroBeamB { this, wsw::StringView( "gfx/misc/electro2b" ) };
	CachedMaterial shaderElectroBeamBAlpha { this, wsw::StringView( "gfx/misc/electro2b_alpha" ) };
	CachedMaterial shaderElectroBeamBBeta { this, wsw::StringView( "gfx/misc/electro2b_beta" ) };
	CachedMaterial shaderElectroBeamRing { this, wsw::StringView( "gfx/misc/beamring.tga" ) };
	CachedMaterial shaderWaveCorona { this, wsw::StringView( "gfx/misc/shockwave_corona" ) };
	CachedMaterial shaderWaveSparks { this, wsw::StringView( "gfx/misc/shockwave_sparks" ) };
	CachedMaterial shaderInstaBeam { this, wsw::StringView( "gfx/misc/instagun" ) };
	CachedMaterial shaderLaserGunBeam { this, wsw::StringView( "gfx/misc/laserbeam" ) };
	CachedMaterial shaderRocketExplosion { this, wsw::StringView( PATH_ROCKET_EXPLOSION_SPRITE ) };
	CachedMaterial shaderRocketExplosionRing { this, wsw::StringView( PATH_ROCKET_EXPLOSION_RING_SPRITE ) };
	CachedMaterial shaderWaveExplosionRing { this, wsw::StringView( PATH_WAVE_EXPLOSION_RING_SPRITE ) };

	CachedMaterial shaderLaser { this, wsw::StringView( "gfx/misc/laser" ) };

	CachedMaterial shaderFlagFlare { this, wsw::StringView( PATH_FLAG_FLARE_SHADER ) };

	CachedMaterial shaderRaceGhostEffect { this, wsw::StringView( "gfx/raceghost" ) };

	CachedMaterial shaderWeaponIcon[WEAP_TOTAL - 1] {
		{ this, wsw::StringView( PATH_GUNBLADE_ICON ) },
		{ this, wsw::StringView( PATH_MACHINEGUN_ICON ) },
		{ this, wsw::StringView( PATH_RIOTGUN_ICON ) },
		{ this, wsw::StringView( PATH_GRENADELAUNCHER_ICON ) },
		{ this, wsw::StringView( PATH_ROCKETLAUNCHER_ICON ) },
		{ this, wsw::StringView( PATH_PLASMAGUN_ICON ) },
		{ this, wsw::StringView( PATH_LASERGUN_ICON ) },
		{ this, wsw::StringView( PATH_ELECTROBOLT_ICON ) },
		{ this, wsw::StringView( PATH_SHOCKWAVE_ICON ) },
		{ this, wsw::StringView( PATH_INSTAGUN_ICON ) },
	};

	CachedMaterial shaderNoGunWeaponIcon[WEAP_TOTAL - 1] {
		{ this, wsw::StringView( PATH_NG_GUNBLADE_ICON ) },
		{ this, wsw::StringView( PATH_NG_MACHINEGUN_ICON ) },
		{ this, wsw::StringView( PATH_NG_RIOTGUN_ICON ) },
		{ this, wsw::StringView( PATH_NG_GRENADELAUNCHER_ICON ) },
		{ this, wsw::StringView( PATH_NG_ROCKETLAUNCHER_ICON ) },
		{ this, wsw::StringView( PATH_NG_PLASMAGUN_ICON ) },
		{ this, wsw::StringView( PATH_NG_LASERGUN_ICON ) },
		{ this, wsw::StringView( PATH_NG_ELECTROBOLT_ICON ) },
		{ this, wsw::StringView( PATH_NG_SHOCKWAVE_ICON ) },
		{ this, wsw::StringView( PATH_NG_INSTAGUN_ICON ) },
	};

	CachedMaterial shaderGunbladeBlastIcon { this, wsw::StringView( PATH_GUNBLADE_BLAST_ICON ) };

	CachedMaterialsArray<3> shaderInstagunChargeIcon { this, "gfx/hud/icons/weapon/instagun_%d", 0 };

	CachedMaterial shaderKeyIcon[KEYICON_TOTAL] {
		{ this, wsw::StringView( PATH_KEYICON_FORWARD ) },
		{ this, wsw::StringView( PATH_KEYICON_BACKWARD ) },
		{ this, wsw::StringView( PATH_KEYICON_LEFT ) },
		{ this, wsw::StringView( PATH_KEYICON_RIGHT ) },
		{ this, wsw::StringView( PATH_KEYICON_FIRE ) },
		{ this, wsw::StringView( PATH_KEYICON_JUMP ) },
		{ this, wsw::StringView( PATH_KEYICON_CROUCH ) },
		{ this, wsw::StringView( PATH_KEYICON_SPECIAL ) }
	};

	CachedMaterial shaderSbNums { this, wsw::StringView( "gfx/hud/sbnums" ) };

	static constexpr unsigned kCrosshairTag = 1;
	static constexpr unsigned kStrongCrosshairTag = 2;

	[[nodiscard]]
	auto findMaterialsArrayByTag( unsigned tag ) -> LinkedMaterialsArray *;
};

#endif
