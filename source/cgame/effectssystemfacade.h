/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2022 Chasseur de bots

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef WSW_17bd35f8_e886_4cc0_a896_33e286002b87_H
#define WSW_17bd35f8_e886_4cc0_a896_33e286002b87_H

#include "trackedeffectssystem.h"
#include "transienteffectssystem.h"
#include "../qcommon/randomgenerator.h"

class DrawSceneRequest;
struct sfx_s;
struct trace_s;

struct Impact {
	float origin[3] { 0.0f, 0.0f, 0.0f };
	float normal[3] { 0.0f, 0.0f, -1.0f };
	float dir[3] { 0.0f, 0.0f, -1.0f };
	int surfFlags { 0 };
	int contents { 0 };
};

enum class SurfImpactMaterial : unsigned;

struct FlockOrientation;

class EffectsSystemFacade {
public:
	void spawnRocketExplosionEffect( const float *origin, const float *impactNormal, int mode );
	void spawnGrenadeExplosionEffect( const float *origin, const float *impactNormal, int mode );
	void spawnShockwaveExplosionEffect( const float *origin, const float *impactNormal, int mode );
	void spawnPlasmaExplosionEffect( const float *origin, const float *impactNormal, int mode );
	void spawnGenericExplosionEffect( const float *origin, int mode, float radius );

	void spawnGrenadeBounceEffect( int entNum, int mode );

	void spawnPlayerHitEffect( const float *origin, const float *dir, int damage );

	void spawnElectroboltHitEffect( const float *origin, const float *impactNormal, const float *impactDir,
									bool spawnDecal, int ownerNum );
	void spawnInstagunHitEffect( const float *origin, const float *impactNormal, const float *impactDir,
								 bool spawnDecal, int ownerNum );

	void spawnGunbladeBladeHitEffect( const float *origin, const float *dir );
	void spawnGunbladeBlastHitEffect( const float *origin, const float *dir );

	void spawnBulletImpactEffect( const Impact &impact );

	void spawnUnderwaterBulletLikeImpactEffect( const Impact &impact );

	void spawnMultiplePelletImpactEffects( std::span<const Impact> impacts );

	void spawnBulletLiquidImpactEffect( const Impact &impact );

	void spawnMultipleLiquidImpactEffects( std::span<const Impact> impacts, float percentageScale,
										   std::pair<float, float> randomRotationAngleCosineRange );

	void spawnLandingDustImpactEffect( const float *origin, const float *dir ) {
		spawnDustImpactEffect( origin, dir, 50.0f );
	}

	void spawnWalljumpDustImpactEffect( const float *origin, const float *dir ) {
		spawnDustImpactEffect( origin, dir, 64.0f );
	}

	void spawnDashEffect( const float *oldOrigin, const float *newOrigin );

	void touchRocketTrail( int entNum, const float *origin, int64_t currTime ) {
		m_trackedEffectsSystem.touchRocketTrail( entNum, origin, currTime );
	}
	void touchGrenadeTrail( int entNum, const float *origin, int64_t currTime ) {
		m_trackedEffectsSystem.touchGrenadeTrail( entNum, origin, currTime );
	}
	void touchBlastTrail( int entNum, const float *origin, int64_t currTime ) {
		m_trackedEffectsSystem.touchBlastTrail( entNum, origin, currTime );
	}
	void touchElectroTrail( int entNum, int ownerNum, const float *origin, int64_t currTime ) {
		m_trackedEffectsSystem.touchElectroTrail( entNum, ownerNum, origin, currTime );
	}

	void updateStraightLaserBeam( int entNum, const float *from, const float *to, int64_t currTime ) {
		m_trackedEffectsSystem.updateStraightLaserBeam( entNum, from, to, currTime );
	}

	void updateCurvedLaserBeam( int entNum, std::span<const vec3_t> points, int64_t currTime ) {
		m_trackedEffectsSystem.updateCurvedLaserBeam( entNum, points, currTime );
	}

	void spawnElectroboltBeam( const float *from, const float *to, int team );
	void spawnInstagunBeam( const float *from, const float *to, int team );

	void spawnWorldLaserBeam( const float *from, const float *to, float width );
	void spawnGameDebugBeam( const float *from, const float *to, const float *color, int parm );

	void spawnPlayerTeleInEffect( int entNum, const float *origin, model_s *model ) {
		m_trackedEffectsSystem.spawnPlayerTeleInEffect( entNum, origin, model );
	}
	void spawnPlayerTeleOutEffect( int entNum, const float *origin, model_s *model ) {
		m_trackedEffectsSystem.spawnPlayerTeleOutEffect( entNum, origin, model );
	}

	void resetEntityEffects( int entNum ) { m_trackedEffectsSystem.resetEntityEffects( entNum ); }

	void simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *drawSceneRequest );
private:
	void startSound( sfx_s *sfx, const float *origin, float attenuation = 1.0f );
	void startRelativeSound( sfx_s *sfx, int entNum, float attenuation = 1.0f );

	void spawnExplosionEffect( const float *origin, const float *dir, sfx_s *sfx, float radius, bool addSoundLfe );

	void spawnDustImpactEffect( const float *origin, const float *dir, float radius );

	void spawnBulletImpactParticleEffectForMaterial( const FlockOrientation &flockOrientation,
													 SurfImpactMaterial impactMaterial, unsigned materialParam );
	void spawnPelletImpactParticleEffectForMaterial( const FlockOrientation &flockOrientation,
													 SurfImpactMaterial impactMaterial, unsigned materialParam,
													 unsigned index, unsigned total );

	[[nodiscard]]
	static auto getImpactSfxGroupForMaterial( SurfImpactMaterial impactMaterial ) -> unsigned;
	[[nodiscard]]
	static auto getImpactSfxGroupForSurfFlags( int surfFlags ) -> unsigned;
	[[nodiscard]]
	auto getSfxForImpactGroup( unsigned group ) -> sfx_s *;

	void spawnMultipleExplosionImpactEffects( std::span<const Impact> impacts );

	// std::span<> won't work for arrays of pointers
	void spawnImpactSoundsWhenNeededUsingTheseSounds( std::span<const Impact> impacts, sfx_s **begin, sfx_s **end );
	void spawnImpactSoundsWhenNeededCheckingMaterials( std::span<const Impact> impacts );

	void startSoundForImpact( sfx_s *sfx, const Impact &impact );

	void spawnLiquidImpactParticleEffect( const Impact &impact, float percentageScale,
										  std::pair<float, float> randomRotationAngleCosineRange );

	TrackedEffectsSystem m_trackedEffectsSystem;
	TransientEffectsSystem m_transientEffectsSystem;
	wsw::RandomGenerator m_rng;

	shader_s *m_bloodMaterials[3];
	wsw::StaticVector<std::pair<sfx_s **, unsigned>, 5> m_impactSfxForGroups;
};

#endif