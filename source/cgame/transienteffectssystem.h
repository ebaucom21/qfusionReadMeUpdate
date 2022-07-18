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

#ifndef WSW_c583803c_d204_40e4_8883_36cdfe1ccf20_H
#define WSW_c583803c_d204_40e4_8883_36cdfe1ccf20_H

#include "../qcommon/freelistallocator.h"
#include "../qcommon/randomgenerator.h"
#include "../ref/ref.h"

#include "simulatedhullssystem.h"

#include <variant>

struct CMShapeList;
class DrawSceneRequest;

struct ConicalFlockParams;
struct EllipsoidalFlockParams;

/// Manages "fire-and-forget" effects that usually get spawned upon events.
class TransientEffectsSystem {
public:
	~TransientEffectsSystem();

	void spawnExplosion( const float *origin, float radius = 72.0f );
	void spawnCartoonHitEffect( const float *origin, const float *dir, int damage );
	void spawnBleedingVolumeEffect( const float *origin, const float *dir, int damage,
									const float *bloodColor, unsigned duration );
	void spawnElectroboltHitEffect( const float *origin, const float *dir, const float *decalColor,
									const float *energyColor, bool spawnDecal );
	void spawnInstagunHitEffect( const float *origin, const float *dir, const float *decalColor,
								 const float *energyColor, bool spawnDecal );
	void spawnPlasmaImpactEffect( const float *origin, const float *dir );

	void spawnGunbladeBlastImpactEffect( const float *origin, const float *dir );
	void spawnGunbladeBladeImpactEffect( const float *origin, const float *dir );

	void spawnBulletLikeImpactEffect( const float *origin, const float *dir );

	void spawnDustImpactEffect( const float *origin, const float *dir, float radius );

	void spawnDashEffect( const float *origin, const float *dir );

	void simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request );
private:
	struct EntityEffect {
		EntityEffect *prev { nullptr }, *next { nullptr };
		int64_t spawnTime { 0 };
		unsigned duration { 0 };
		float rcpDuration { 0.0f };
		unsigned fadeInDuration { 0 };
		float rcpFadeInDuration { 0.0f };
		float rcpFadeOutDuration { 0.0f };
		float velocity[3] { 0.0f, 0.0f, 0.0f };
		// The entity scale once it fades in
		float fadedInScale { 1.0f };
		// The entity scale once it fades out (no shrinking by default)
		float fadedOutScale { 1.0f };
		// The entity alpha upon spawn
		float initialAlpha { 1.0f };
		// The entity alpha once it fades out (alpha fade out is on by default)
		float fadedOutAlpha { 0.0f };
		entity_t entity;
	};

	struct LightEffect {
		LightEffect *prev { nullptr }, *next { nullptr };
		int64_t spawnTime { 0 };
		unsigned duration { 0 };
		unsigned fadeInDuration { 0 };
		unsigned fadeOutOffset { 0 };

		float origin[3] { 0.0f, 0.0f, 0.0f };
		float color[3] { 0.0f, 0.0f, 0.0f };
		float radius { 0.0f };
	};

	struct RegularHullSpawnRecord {
		using Hull        = SimulatedHullsSystem::BaseRegularSimulatedHull;
		using AllocMethod = Hull * (SimulatedHullsSystem::*)( int64_t, unsigned );

		float scale { 1.0f };
		float speed { 250.0f };
		float speedSpread { 50.0f };
		float color[4] { 1.0f, 1.0f, 1.0f, 1.0f };
		unsigned timeout { 250 };
		AllocMethod allocMethod { nullptr };
	};

	struct ConcentricHullSpawnRecord {
		using LayerParams = std::span<const SimulatedHullsSystem::HullLayerParams>;
		using Hull        = SimulatedHullsSystem::BaseConcentricSimulatedHull;
		using AllocMethod = Hull * (SimulatedHullsSystem::*)( int64_t, unsigned );

		LayerParams layerParams;
		float scale { 1.0f };
		unsigned timeout { 250 };
		AllocMethod allocMethod { nullptr };
	};

	struct ParticleFlockSpawnRecord {
		// These pointers refer to objects with a greater, often &'static lifetime
		const Particle::AppearanceRules *appearanceRules { nullptr };
		const ConicalFlockParams *conicalFlockParams { nullptr };
		const EllipsoidalFlockParams *ellipsoidalFlockParams { nullptr };
		// TODO: Get rid of separate bins in ParticleSystem public interface
		enum Bin { Small, Medium, Large } bin { Small };
	};

	struct DelayedEffect {
		DelayedEffect *prev { nullptr }, *next { nullptr };
		int64_t spawnTime { 0 };

		unsigned spawnDelay { 250 };

		float origin[3] { 0.0f, 0.0f, 0.0f };
		float velocity[3] { 0.0f, 0.0f, 0.0f };
		float angularVelocity[3] { 0.0f, 0.0f, 0.0f };
		float angles[3] { 0.0f, 0.0f, 0.0f };
		float restitution { 1.0f };
		float gravity { 0.0f };

		// Hulls and particle flocks are not mutually exclusive
		std::optional<std::variant<RegularHullSpawnRecord, ConcentricHullSpawnRecord>> maybeSomeKindOfHull;
		std::optional<ParticleFlockSpawnRecord> maybeParticleFlock;
	};

	void unlinkAndFreeEntityEffect( EntityEffect *effect );
	void unlinkAndFreeLightEffect( LightEffect *effect );
	void unlinkAndFreeDelayedEffect( DelayedEffect *effect );

	[[nodiscard]]
	auto addModelEffect( model_s *model, const float *origin, const float *dir, unsigned duration ) -> EntityEffect *;

	[[nodiscard]]
	auto addSpriteEffect( shader_s *material, const float *origin, float radius, unsigned duration ) -> EntityEffect *;

	[[nodiscard]]
	auto allocEntityEffect( int64_t currTime, unsigned duration ) -> EntityEffect *;

	[[nodiscard]]
	auto allocLightEffect( int64_t currTime, unsigned duration, unsigned fadeInDuration,
						   unsigned fadeOutOffset ) -> LightEffect *;

	[[nodiscard]]
	auto allocDelayedEffect( int64_t currTime, const float *origin, const float *velocity,
							 unsigned delay ) -> DelayedEffect *;

	void spawnElectroboltLikeHitEffect( const float *origin, const float *dir, const float *decalColor,
										const float *energyColor, model_s *model, bool spawnDecal );

	void simulateEntityEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds, DrawSceneRequest *request );
	void simulateLightEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds, DrawSceneRequest *request );
	void simulateDelayedEffects( int64_t currTime, float timeDeltaSeconds );

	wsw::HeapBasedFreelistAllocator m_entityEffectsAllocator { sizeof( EntityEffect ), 256 };
	wsw::HeapBasedFreelistAllocator m_lightEffectsAllocator { sizeof( LightEffect ), 72 };
	wsw::HeapBasedFreelistAllocator m_delayedEffectsAllocator { sizeof( DelayedEffect ), 32 };

	EntityEffect *m_entityEffectsHead { nullptr };
	LightEffect *m_lightEffectsHead { nullptr };
	DelayedEffect *m_delayedEffectsHead { nullptr };

	wsw::RandomGenerator m_rng;
	int64_t m_lastTime { 0 };
};

#endif
