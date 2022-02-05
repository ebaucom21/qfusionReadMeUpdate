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

#ifndef WSW_4d42a1db_cb72_4adf_97c9_3317a0fae4b9_H
#define WSW_4d42a1db_cb72_4adf_97c9_3317a0fae4b9_H

#include "../qcommon/freelistallocator.h"
#include "../qcommon/randomgenerator.h"
#include "../gameshared/q_shared.h"
#include "particlesystem.h"

class DrawSceneRequest;
struct model_s;

class TrackedEffectsSystem {
public:
	TrackedEffectsSystem() = default;
	~TrackedEffectsSystem();

	void touchRocketTrail( int entNum, const float *origin, int64_t currTime ) {
		touchRocketOrGrenadeTrail( entNum, origin, &m_rocketParticlesFlockFiller, currTime );
	}
	void touchGrenadeTrail( int entNum, const float *origin, int64_t currTime ) {
		touchRocketOrGrenadeTrail( entNum, origin, &m_grenadeParticlesFlockFiller, currTime );
	}

	void touchPlasmaTrail( int entNum, const float *origin, int64_t currTime );
	void touchBlastTrail( int entNum, const float *origin, int64_t currTime );
	void touchElectroTrail( int entNum, const float *origin, int64_t currTime );

	void spawnPlayerTeleInEffect( int entNum, const float *origin, model_s *model ) {
		spawnPlayerTeleEffect( entNum, origin, model, 0 );
	}

	void spawnPlayerTeleOutEffect( int entNum, const float *origin, model_s *model ) {
		spawnPlayerTeleEffect( entNum, origin, model, 1 );
	}

	void resetEntityEffects( int entNum );

	void simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *drawSceneRequest );
private:
	struct FireTrail {
		FireTrail *prev { nullptr }, *next { nullptr };
		int64_t touchedAt { 0 };
		int entNum { std::numeric_limits<int>::max() };
	};

	struct ParticleTrail {
		ParticleTrail *prev { nullptr }, *next { nullptr };
		ParticleFlock *particleFlock { nullptr };
		float lastDropOrigin[3];
		int64_t touchedAt { 0 };
		int64_t lastParticleAt { 0 };
		float dropDistance { 16.0f };
		unsigned maxParticlesPerDrop { 1 };
		unsigned maxParticlesInFlock { ~0u };
		int entNum { std::numeric_limits<int>::max() };
	};

	struct TeleEffect {
		TeleEffect *prev { nullptr }, *next { nullptr };
		int64_t spawnTime { 0 };
		model_s *model;
		unsigned lifetime { 0 };
		float origin[3];
		float color[3];
		int clientNum { std::numeric_limits<int>::max() };
		int inOutIndex { std::numeric_limits<int>::max() };
	};

	struct AttachedClientEffects {
		TeleEffect *teleEffects[2] { nullptr, nullptr };
	};

	struct AttachedEntityEffects {
		FireTrail *fireTrail { nullptr };
		ParticleTrail *particleTrail { nullptr };
	};

	void unlinkAndFree( FireTrail *fireTrail );
	void unlinkAndFree( ParticleTrail *particleTrail );
	void unlinkAndFree( TeleEffect *teleEffect );

	[[nodiscard]]
	auto allocParticleTrail( int entNum, const float *origin, unsigned particleSystemBin,
							 Particle::AppearanceRules &&appearanceRules  ) -> ParticleTrail *;

	void updateParticleTrail( ParticleTrail *trail, const float *origin, ConeFlockFiller *filler, int64_t currTime );

	void spawnPlayerTeleEffect( int clientNum, const float *origin, model_s *model, int inOrOutIndex );

	void touchRocketOrGrenadeTrail( int entNum, const float *origin, ConeFlockFiller *filler, int64_t currTime );

	static constexpr unsigned kClippedTrailsBin = ParticleSystem::kClippedTrailFlocksBin;
	static constexpr unsigned kNonClippedTrailsBin = ParticleSystem::kNonClippedTrailFlocksBin;

	FireTrail *m_fireTrailsHead { nullptr };
	ParticleTrail *m_particleTrailsHead { nullptr };
	TeleEffect *m_teleEffectsHead { nullptr };

	wsw::HeapBasedFreelistAllocator m_fireTrailsAllocator { sizeof( FireTrail ), 64 };
	wsw::HeapBasedFreelistAllocator m_particleTrailsAllocator { sizeof( ParticleTrail ), 64 };
	wsw::HeapBasedFreelistAllocator m_teleEffectsAllocator { sizeof( TeleEffect ), 2 * MAX_CLIENTS };

	ConeFlockFiller m_rocketParticlesFlockFiller {
		.gravity     = -200,
		.angle       = 15,
		.bounceCount = 0,
		.minSpeed    = 75,
		.maxSpeed    = 150,
		.minTimeout  = 150,
		.maxTimeout  = 300
	};

	ConeFlockFiller m_grenadeParticlesFlockFiller {
		.gravity     = +200,
		.angle       = 30,
		.bounceCount = 0,
		.minSpeed    = 75,
		.maxSpeed    = 100,
		.minTimeout  = 150,
		.maxTimeout  = 300
	};

	ConeFlockFiller m_blastParticlesFlockFiller {
		.gravity     = -300,
		.angle       = 30,
		.bounceCount = 0,
		.minSpeed    = 50,
		.maxSpeed    = 50,
		.minTimeout  = 300,
		.maxTimeout  = 450
	};

	ConeFlockFiller m_plasmaParticlesFlockFiller {
		.gravity     = 0,
		.angle       = 15,
		.bounceCount = 0,
		.minSpeed    = 100,
		.maxSpeed    = 150,
		.minTimeout  = 100,
		.maxTimeout  = 150
	};

	ConeFlockFiller m_electroParticlesFlockFiller {
		.gravity     = 0,
		.angle       = 60,
		.bounceCount = 0,
		.minSpeed    = 50,
		.maxSpeed    = 50,
		.minTimeout  = 100,
		.maxTimeout  = 150
	};

	AttachedEntityEffects m_attachedEntityEffects[MAX_EDICTS];
	AttachedClientEffects m_attachedClientEffects[MAX_CLIENTS];

	wsw::RandomGenerator m_rng;

	int64_t m_lastTime { 0 };
};

#endif