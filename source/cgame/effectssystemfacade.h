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
#include "../qcommon/wswstaticdeque.h"

class DrawSceneRequest;
struct sfx_s;
struct trace_s;

struct SolidImpact {
	float origin[3] { 0.0f, 0.0f, 0.0f };
	float normal[3] { 0.0f, 0.0f, +1.0f };
	float incidentDir[3] { 0.0f, 0.0f, -1.0f };
	int surfFlags { 0 };
};

struct LiquidImpact {
	float origin[3] { 0.0f, 0.0f, 0.0f };
	float burstDir[3] { 0.0f, 0.0f, +1.0f };
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

	void spawnBulletImpactEffect( const SolidImpact &impact );

	void spawnUnderwaterBulletLikeImpactEffect( const float *origin, const float *normal );

	void spawnMultiplePelletImpactEffects( std::span<const SolidImpact> impacts );

	void spawnBulletLiquidImpactEffect( const LiquidImpact &impact );

	void spawnMultipleLiquidImpactEffects( std::span<const LiquidImpact> impacts, float percentageScale,
										   std::pair<float, float> randomRotationAngleCosineRange,
										   std::pair<unsigned, unsigned> delayRange = { 0, 0 } );

	void spawnBulletTracer( int owner, const float *from, const float *to );

	void spawnPelletTracers( int owner, const float *from, std::span<const vec3_t> to );

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

	void spawnBulletGenericImpactRosette( const FlockOrientation &orientation, float minPercentage, float maxPercentage );

	void spawnBulletMetalImpactRosette( const FlockOrientation &orientation );

	// Normally `delay` would have been a last default argument,
	// but there are already fine tune parameters,
	// and packing parameters in a struct adds way too much clutter.

	void spawnBulletMetalRicochetParticles( unsigned delay, const FlockOrientation &orientation, float upShiftScale,
											unsigned materialParam, float minPercentage, float maxPercentage );

	void spawnBulletMetalDebrisParticles( unsigned delay, const FlockOrientation &orientation, float upShiftScale,
										  unsigned materialParam, float minPercentage, float maxPercentage );

	void spawnStoneDustParticles( unsigned delay, const FlockOrientation &orientation, float upShiftScale,
								  unsigned materialParam, float dustPercentageScale = 1.0f );

	void spawnStuccoDustParticles( unsigned delay, const FlockOrientation &orientation,
								   float upShiftScale, unsigned materialParam );

	void spawnWoodBulletImpactParticles( unsigned delay, const FlockOrientation &orientation,
										 float upShiftScale, unsigned materialParam,
										 float debrisPercentageScale = 1.0f );

	void spawnDirtImpactParticles( unsigned delay, const FlockOrientation &orientation,
								   float upShiftScale, unsigned materialParam );

	void spawnSandImpactParticles( unsigned delay, const FlockOrientation &orientation,
								   float upShiftScale, unsigned materialParam, float dustPercentageScale = 1.0f );

	void spawnGlassImpactParticles( unsigned delay, const FlockOrientation &orientation,
									float upShiftScale, unsigned materialParam );

	void spawnBulletImpactParticleEffectForMaterial( const FlockOrientation &flockOrientation,
													 SurfImpactMaterial impactMaterial, unsigned materialParam );

	void spawnPelletImpactParticleEffectForMaterial( const FlockOrientation &flockOrientation,
													 SurfImpactMaterial impactMaterial, unsigned materialParam,
													 unsigned index, unsigned total );

	// TODO: Hide bins from ParticleSystem public interface
	template <typename FlockParams>
	void spawnOrPostponeImpactParticleEffect( unsigned delay, const FlockParams &flockParams,
											  const Particle::AppearanceRules &appearanceRules,
											  TransientEffectsSystem::ParticleFlockBin bin =
												  TransientEffectsSystem::ParticleFlockBin::Small );

	void spawnExplosionImpactParticleEffectForMaterial( const FlockOrientation &flockOrientation,
														SurfImpactMaterial impactMaterial, unsigned materialParam );

	[[nodiscard]]
	static auto getImpactSfxGroupForMaterial( SurfImpactMaterial impactMaterial ) -> unsigned;
	[[nodiscard]]
	static auto getImpactSfxGroupForSurfFlags( int surfFlags ) -> unsigned;
	[[nodiscard]]
	auto getSfxForImpactGroup( unsigned group ) -> sfx_s *;

	void spawnMultipleExplosionImpactEffects( std::span<const SolidImpact> impacts );

	struct ImpactSoundLimiterParams {
		float dropChanceAtZeroDistance { 1.0f };
		float startDroppingAtDistance { 96.0f };
		float dropChanceAtZeroTimeDiff { 1.0f };
		unsigned startDroppingAtTimeDiff { 100 };
	};

	static const ImpactSoundLimiterParams kLiquidImpactSoundLimiterParams;

	void startSoundForImpactUsingLimiter( sfx_s *sfx, uintptr_t groupTag, const SolidImpact &impact,
										  const ImpactSoundLimiterParams &params );
	void startSoundForImpactUsingLimiter( sfx_s *sfx, uintptr_t groupTag, const LiquidImpact &impact,
										  const ImpactSoundLimiterParams &params );

	void startSoundForImpactUsingLimiter( sfx_s *sfx, uintptr_t groupTag, const float *origin,
										  const ImpactSoundLimiterParams &params );

	void spawnLiquidImpactParticleEffect( unsigned delay, const LiquidImpact &impact, float percentageScale,
										  std::pair<float, float> randomRotationAngleCosineRange );

	TrackedEffectsSystem m_trackedEffectsSystem;
	TransientEffectsSystem m_transientEffectsSystem;
	wsw::RandomGenerator m_rng;

	shader_s *m_bloodMaterials[3];
	wsw::StaticVector<std::pair<sfx_s **, unsigned>, 5> m_impactSfxForGroups;

	struct ImpactSoundLimiterEntry {
		int64_t timestamp;
		uintptr_t groupTag;
		vec3_t origin;
	};

	wsw::StaticDeque<ImpactSoundLimiterEntry, 48> m_impactSoundLimiterEntries;
};

#endif