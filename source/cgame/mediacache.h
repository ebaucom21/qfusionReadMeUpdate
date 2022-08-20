#ifndef WSW_7f020248_88b8_40f2_ae59_1e97c51e2f82_H
#define WSW_7f020248_88b8_40f2_ae59_1e97c51e2f82_H

#include "../gameshared/q_shared.h"
#include "../gameshared/q_comref.h"
#include "../gameshared/q_math.h"
#include "../gameshared/gs_public.h"

#include "../qcommon/wswstringview.h"
#include "../qcommon/wswvector.h"

struct sfx_s;
struct model_s;
struct shader_s;

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
		[[nodiscard]]
		auto getAddressOfHandle() -> T ** { return &m_handle; }
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
	class CachedHandlesArray {
		friend class MediaCache;
	protected:
		// TODO: Compress these references
		CachedHandlesArray<T> *m_next { nullptr };
		MediaCache *m_parent;
		// TODO: Specify an offset in some shared buffer with strings?
		const char *const m_format;
		uint16_t m_numHandles { 0 };
		uint16_t m_handlesOffset { 0 };
		const uint16_t m_indexShift;

		CachedHandlesArray( MediaCache *parent, const char *format, unsigned indexShift )
			: m_parent( parent ), m_format( format ), m_indexShift( indexShift ) {
			assert( std::strlen( format ) < MAX_QPATH );
		}
	public:
		[[nodiscard]]
		auto length() const -> unsigned { return m_numHandles; }

		[[nodiscard]]
		auto operator[]( unsigned index ) -> T * {
			assert( index < m_numHandles );
			return (T *)m_parent->m_handlesArraysDataStorage[index + m_handlesOffset];
		}
		[[nodiscard]]
		auto operator[]( unsigned index ) const -> const T * {
			assert( index < m_numHandles );
			return (const T *)m_parent->m_handlesArraysDataStorage[index + m_handlesOffset];
		}
		[[nodiscard]]
		auto getAddressOfHandles() -> T ** { return (T **)&m_parent->m_handlesArraysDataStorage[m_handlesOffset]; }
	};

	class CachedSoundsArray: public CachedHandlesArray<sfx_s> {
		friend class MediaCache;
		CachedSoundsArray( MediaCache *parent, const char *format, unsigned indexShift = 1u );
	};

	void registerSounds();
	void registerModels();
	void registerMaterials();
private:
	// TODO: Should we just keep precached and non-precached linked handles in different lists?
	CachedSound *m_sounds { nullptr };
	CachedModel *m_models { nullptr };
	CachedMaterial *m_materials { nullptr };

	CachedSoundsArray *m_soundsArrays { nullptr };

	// TODO: Use a custom lightweight type
	wsw::Vector<void *> m_handlesArraysDataStorage;

	template <typename T>
	void link( T *item, T **head ) {
		item->m_next = *head;
		*head = item;
	}

	void registerSound( CachedSound *sound );
	void registerModel( CachedModel *model );
	void registerMaterial( CachedMaterial *material );
	void registerSoundsArray( CachedSoundsArray *sounds );
public:
	MediaCache();

	CachedSound sfxChat { this, wsw::StringView( S_CHAT ) };
	CachedSoundsArray sfxWeaponHit { this, S_WEAPON_HITS, 0 };
	CachedSoundsArray sfxWeaponHit2 { this, "sounds/misc/hit_plus_%d", 0 };

	CachedSoundsArray sfxImpactMetal { this, "sounds/weapons/impact_metal_%d" };
	CachedSoundsArray sfxImpactGlass { this, "sounds/weapons/impact_glass_%d" };
	CachedSoundsArray sfxImpactWood { this, "sounds/weapons/impact_wood_%d" };
	CachedSoundsArray sfxImpactSoft { this, "sounds/weapons/impact_soft_%d" };
	CachedSoundsArray sfxImpactSolid { this, "sounds/weapons/impact_solid_%d" };
	CachedSoundsArray sfxImpactWater { this, "sounds/weapons/impact_water_%d" };

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

	CachedSoundsArray sfxGunbladeWeakShot { this, S_WEAPON_GUNBLADE_W_SHOT_1_to_3 };
	CachedSoundsArray sfxBladeFleshHit { this, S_WEAPON_GUNBLADE_W_HIT_FLESH_1_to_3 };
	CachedSoundsArray sfxBladeWallHit { this, S_WEAPON_GUNBLADE_W_HIT_WALL_1_to_2 };

	CachedSound sfxGunbladeStrongShot { this, wsw::StringView( S_WEAPON_GUNBLADE_S_SHOT ) };

	CachedSoundsArray sfxGunbladeStrongHit { this, S_WEAPON_GUNBLADE_S_HIT_1_to_2 };

	CachedSoundsArray sfxGrenadeWeakBounce { this, S_WEAPON_GRENADE_W_BOUNCE_1_to_2 };
	CachedSoundsArray sfxGrenadeStrongBounce { this, S_WEAPON_GRENADE_S_BOUNCE_1_to_2, 1 };

	CachedSound sfxGrenadeWeakExplosion { this, wsw::StringView( S_WEAPON_GRENADE_W_HIT ) };
	CachedSound sfxGrenadeStrongExplosion { this, wsw::StringView( S_WEAPON_GRENADE_S_HIT ) };

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

	CachedMaterial shaderSparkParticle { this, wsw::StringView( "gfx/misc/particle_spark" ) };
	CachedMaterial shaderBlastParticle { this, wsw::StringView( "gfx/misc/particle_blast" ) };
	CachedMaterial shaderDebrisParticle { this, wsw::StringView( "gfx/misc/particle_debris" ) };
	CachedMaterial shaderBloodParticle { this, wsw::StringView( "gfx/misc/particle_blood" ) };
	CachedMaterial shaderFlareParticle { this, wsw::StringView( "gfx/misc/particle_flare" ) };
	CachedMaterial shaderElectroParticle { this, wsw::StringView( "gfx/misc/particle_electro" ) };

	CachedMaterial shaderNet { this, wsw::StringView( "gfx/hud/net" ) };
	CachedMaterial shaderSelect { this, wsw::StringView( "gfx/hud/select" ) };
	CachedMaterial shaderChatBalloon { this, wsw::StringView( PATH_BALLONCHAT_ICON ) };
	CachedMaterial shaderDownArrow { this, wsw::StringView( "gfx/2d/arrow_down" ) };

	CachedMaterial shaderWaterBubble { this, wsw::StringView( "gfx/misc/waterBubble" ) };
	CachedMaterial shaderSmokePuff { this, wsw::StringView( "gfx/misc/smokepuff" ) };

	CachedMaterial shaderSmokePuff1 { this, wsw::StringView( "gfx/misc/smokepuff1" ) };
	CachedMaterial shaderSmokePuff2 { this, wsw::StringView( "gfx/misc/smokepuff2" ) };
	CachedMaterial shaderSmokePuff3 { this, wsw::StringView( "gfx/misc/smokepuff3" ) };

	CachedMaterial shaderCartoonHit { this, wsw::StringView( "gfx/misc/cartoonhit" ) };
	CachedMaterial shaderCartoonHit2 { this, wsw::StringView( "gfx/misc/cartoonhit2" ) };
	CachedMaterial shaderCartoonHit3 { this, wsw::StringView( "gfx/misc/cartoonhit3" ) };
	CachedMaterial shaderTeamMateIndicator { this, wsw::StringView( "gfx/indicators/teammate_indicator" ) };
	CachedMaterial shaderTeamCarrierIndicator { this, wsw::StringView( "gfx/indicators/teamcarrier_indicator" ) };
	CachedMaterial shaderTeleportShellGfx { this, wsw::StringView( "gfx/misc/teleportshell" ) };

	CachedMaterial shaderElectroBeam { this, wsw::StringView( "gfx/misc/electro" ) };
	CachedMaterial shaderElectroBeamAlpha { this, wsw::StringView( "gfx/misc/electro_alpha" ) };
	CachedMaterial shaderElectroBeamBeta { this, wsw::StringView( "gfx/misc/electro_beta" ) };
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
};

#endif
