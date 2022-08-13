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

#include "transienteffectssystem.h"
#include "../cgame/cg_local.h"
#include "../client/client.h"
#include "../qcommon/links.h"

#include <cstdlib>
#include <cstring>

TransientEffectsSystem::~TransientEffectsSystem() {
	for( EntityEffect *effect = m_entityEffectsHead, *next = nullptr; effect; effect = next ) { next = effect->next;
		unlinkAndFreeEntityEffect( effect );
	}
	for( LightEffect *effect = m_lightEffectsHead, *next = nullptr; effect; effect = next ) { next = effect->next;
		unlinkAndFreeLightEffect( effect );
	}
	for( DelayedEffect *effect = m_delayedEffectsHead, *next = nullptr; effect; effect = next ) { next = effect->next;
		unlinkAndFreeDelayedEffect( effect );
	}
}

#define asByteColor( r, g, b, a ) {  \
		(uint8_t)( ( r ) * 255.0f ), \
		(uint8_t)( ( g ) * 255.0f ), \
		(uint8_t)( ( b ) * 255.0f ), \
		(uint8_t)( ( a ) * 255.0f )  \
	}

static const byte_vec4_t kFireCoreReplacementPalette[] {
	asByteColor( 1.00f, 0.67f, 0.56f, 0.56f ),
	asByteColor( 1.00f, 0.67f, 0.50f, 0.67f ),
	asByteColor( 1.00f, 0.56f, 0.42f, 0.50f ),
	asByteColor( 1.00f, 0.56f, 0.42f, 0.28f ),
};

static const byte_vec4_t kFireReplacementPalette[] {
	asByteColor( 1.00f, 0.42f, 0.00f, 0.15f ),
	asByteColor( 1.00f, 0.28f, 0.00f, 0.15f ),
	asByteColor( 1.00f, 0.67f, 0.00f, 0.15f ),
	asByteColor( 1.00f, 0.50f, 0.00f, 0.15f ),
	asByteColor( 1.00f, 0.56f, 0.00f, 0.15f ),
};

static const byte_vec4_t kDarkFireDecayPalette[] {
	asByteColor( 0.3f, 0.2f, 0.2f, 0.3f ),
	asByteColor( 0.2f, 0.2f, 0.2f, 0.4f ),
	asByteColor( 0.3f, 0.1f, 0.1f, 0.3f ),
	asByteColor( 0.2f, 0.1f, 0.1f, 0.3f ),
	asByteColor( 0.3f, 0.2f, 0.2f, 0.3f ),
	asByteColor( 0.0f, 0.0f, 0.0f, 0.3f ),
};

static const byte_vec4_t kLightFireDecayPalette[] {
	asByteColor( 0.7f, 0.7f, 0.7f, 0.1f ),
	asByteColor( 0.3f, 0.3f, 0.3f, 0.2f ),
	asByteColor( 0.4f, 0.4f, 0.4f, 0.2f ),
	asByteColor( 0.5f, 0.5f, 0.5f, 0.3f ),
	asByteColor( 0.6f, 0.6f, 0.6f, 0.2f ),
	asByteColor( 0.7f, 0.7f, 0.7f, 0.1f ),
};

struct FireHullLayerParamsHolder {
	SimulatedHullsSystem::ColorChangeTimelineNode darkColorChangeTimeline[5][3] {
		{
			{ /* Layer 0 */ },
			{
				.activateAtLifetimeFraction = 0.35f, .replacementPalette = kFireCoreReplacementPalette,
				.sumOfDropChanceForThisSegment = 0.0f, .sumOfReplacementChanceForThisSegment = 0.1f,
			},
			{
				.activateAtLifetimeFraction = 0.6f, .replacementPalette = kFireReplacementPalette,
				.sumOfDropChanceForThisSegment = 3.5f, .sumOfReplacementChanceForThisSegment = 0.5f,
			}
		},
		{
			{ /* Layer 1 */ },
			{
				.activateAtLifetimeFraction = 0.35f, .replacementPalette = kFireReplacementPalette,
				.sumOfDropChanceForThisSegment = 0.0f, .sumOfReplacementChanceForThisSegment = 0.2f,
			},
			{
				.activateAtLifetimeFraction = 0.6f, .replacementPalette = kFireReplacementPalette,
				.sumOfDropChanceForThisSegment = 2.5f, .sumOfReplacementChanceForThisSegment = 3.5f,
			}
		},
		{
			{ /* Layer 2 */ },
			{
				.activateAtLifetimeFraction = 0.3f, .replacementPalette = kFireReplacementPalette,
				.sumOfDropChanceForThisSegment = 0.0f, .sumOfReplacementChanceForThisSegment = 0.3f,
			},
			{
				.activateAtLifetimeFraction = 0.6f, .replacementPalette = kFireReplacementPalette,
				.sumOfDropChanceForThisSegment = 2.0f, .sumOfReplacementChanceForThisSegment = 4.0f,
				.allowIncreasingOpacity = true
			}
		},
		{
			{ /* Layer 3 */ },
			{
				.activateAtLifetimeFraction = 0.3f, .replacementPalette = kFireReplacementPalette,
				.sumOfDropChanceForThisSegment = 0.0f, .sumOfReplacementChanceForThisSegment = 0.5f,
			},
			{
				.activateAtLifetimeFraction = 0.55f,
				.sumOfDropChanceForThisSegment = 2.0f, .sumOfReplacementChanceForThisSegment = 4.5f,
				.allowIncreasingOpacity = true
			}
		},
		{
			{ /* Layer 4 */ },
			{
				.activateAtLifetimeFraction = 0.1f, .replacementPalette = kFireReplacementPalette,
				.sumOfDropChanceForThisSegment = 0.0f, .sumOfReplacementChanceForThisSegment = 0.5f,
			},
			{
				.activateAtLifetimeFraction = 0.5f,
				.sumOfDropChanceForThisSegment = 2.0f, .sumOfReplacementChanceForThisSegment = 4.5f,
				.allowIncreasingOpacity = true
			}
		}
	};

	SimulatedHullsSystem::HullLayerParams darkHullLayerParams[5] {
		{
			.speed = 22.5f, .finalOffset = 8.0f,
			.speedSpikeChance = 0.05f, .minSpeedSpike = 10.0f, .maxSpeedSpike = 15.0f,
			.biasAlongChosenDir = 30.0f,
			.baseInitialColor = { 1.0f, 0.9f, 0.9f, 1.0f },
			.bulgeInitialColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		},
		{
			.speed = 35.0f, .finalOffset = 6.0f,
			.speedSpikeChance = 0.05f, .minSpeedSpike = 7.5f, .maxSpeedSpike = 15.0f,
			.biasAlongChosenDir = 25.0f,
			.baseInitialColor = { 1.0f, 0.9f, 0.9f, 0.7f },
			.bulgeInitialColor = { 1.0f, 1.0f, 1.0f, 0.9f },
		},
		{
			.speed = 45.0f, .finalOffset = 4.0f,
			.speedSpikeChance = 0.05f, .minSpeedSpike = 7.5f, .maxSpeedSpike = 15.0f,
			.biasAlongChosenDir = 20.0f,
			.baseInitialColor = { 1.0f, 0.9f, 0.4f, 0.7f },
			.bulgeInitialColor = { 1.0f, 0.9f, 0.7f, 0.9f },
		},
		{
			.speed = 52.5f, .finalOffset = 2.0f,
			.speedSpikeChance = 0.05f, .minSpeedSpike = 7.5f, .maxSpeedSpike = 15.0f,
			.biasAlongChosenDir = 20.0f,
			.baseInitialColor = { 1.0f, 0.6f, 0.3f, 0.7f },
			.bulgeInitialColor = { 1.0f, 0.9f, 0.7f, 0.7f },
		},
		{
			.speed = 60.0f, .finalOffset = 0.0f,
			.speedSpikeChance = 0.05f, .minSpeedSpike = 7.5f, .maxSpeedSpike = 15.0f,
			.biasAlongChosenDir = 10.0f,
			.baseInitialColor = { 1.0f, 0.5f, 0.2f, 0.7f },
			.bulgeInitialColor = { 1.0f, 0.7f, 0.4f, 0.7f },
		},
	};

	SimulatedHullsSystem::HullLayerParams lightHullLayerParams[5];
	SimulatedHullsSystem::ColorChangeTimelineNode lightColorChangeTimeline[5][3];

	SimulatedHullsSystem::HullLayerParams darkClusterHullLayerParams[2];
	SimulatedHullsSystem::HullLayerParams lightClusterHullLayerParams[2];

	FireHullLayerParamsHolder() noexcept {
		for( size_t layerNum = 0; layerNum < std::size( darkHullLayerParams ); ++layerNum ) {
			// Set the timeline which is not set inline to reduce boilerplate
			darkHullLayerParams[layerNum].colorChangeTimeline = darkColorChangeTimeline[layerNum];

			std::memcpy( lightColorChangeTimeline[layerNum], darkColorChangeTimeline[layerNum],
						 sizeof( darkColorChangeTimeline[layerNum] ) );

			const size_t lastNodeIndex = std::size( lightColorChangeTimeline[layerNum] ) - 1;

			// Raise it for light hulls so they morph to smoke more aggressively
			lightColorChangeTimeline[layerNum][lastNodeIndex].sumOfReplacementChanceForThisSegment *= 1.5f;

			// Set replacement palettes that differ for these layers
			if( layerNum > 2 ) {
				assert( darkColorChangeTimeline[layerNum][lastNodeIndex].replacementPalette.empty() );
				darkColorChangeTimeline[layerNum][lastNodeIndex].replacementPalette  = kDarkFireDecayPalette;
				assert( lightColorChangeTimeline[layerNum][lastNodeIndex].replacementPalette.empty() );
				lightColorChangeTimeline[layerNum][lastNodeIndex].replacementPalette = kLightFireDecayPalette;
			}

			lightHullLayerParams[layerNum] = darkHullLayerParams[layerNum];
			lightHullLayerParams[layerNum].colorChangeTimeline = lightColorChangeTimeline[layerNum];
		}

		assert( std::size( darkClusterHullLayerParams ) == 2 && std::size( lightClusterHullLayerParams ) == 2 );
		darkClusterHullLayerParams[0]  = std::begin( darkHullLayerParams )[0];
		darkClusterHullLayerParams[1]  = std::end( darkHullLayerParams )[-1];
		lightClusterHullLayerParams[0] = std::begin( lightHullLayerParams )[0];
		lightClusterHullLayerParams[1] = std::end( lightHullLayerParams )[-1];

		for( SimulatedHullsSystem::HullLayerParams *layerParam: { &darkClusterHullLayerParams[0],
																  &darkClusterHullLayerParams[1],
																  &lightClusterHullLayerParams[0],
																  &lightClusterHullLayerParams[1] } ) {
			// Account for small lifetime relatively to the primary hull
			layerParam->minSpeedSpike *= 3.0f;
			layerParam->maxSpeedSpike *= 3.0f;
			layerParam->speed         *= 3.0f;
			// Make it more irregular as well
			layerParam->biasAlongChosenDir *= 1.5f;
		}
	}
};

static const FireHullLayerParamsHolder kFireHullParams;

static const byte_vec4_t kSmokeSoftLayerFadeInPalette[] {
	asByteColor( 0.65f, 0.65f, 0.65f, 0.12f ),
	asByteColor( 0.70f, 0.70f, 0.70f, 0.12f ),
	asByteColor( 0.75f, 0.75f, 0.75f, 0.12f ),
	asByteColor( 0.55f, 0.55f, 0.55f, 0.12f ),
	asByteColor( 0.60f, 0.60f, 0.60f, 0.12f ),
};

static const byte_vec4_t kSmokeHardLayerFadeInPalette[] {
	asByteColor( 0.65f, 0.65f, 0.65f, 0.05f ),
	asByteColor( 0.70f, 0.70f, 0.70f, 0.05f ),
	asByteColor( 0.75f, 0.75f, 0.75f, 0.05f ),
	asByteColor( 0.55f, 0.55f, 0.55f, 0.05f ),
	asByteColor( 0.60f, 0.60f, 0.60f, 0.05f ),
};

static const SimulatedHullsSystem::ColorChangeTimelineNode kSmokeHullSoftLayerColorChangeTimeline[4] {
	{
	},
	{
		.activateAtLifetimeFraction = 0.21f, .replacementPalette = kSmokeSoftLayerFadeInPalette,
		.sumOfReplacementChanceForThisSegment = 3.5f,
		.allowIncreasingOpacity = true,
	},
	{
		.activateAtLifetimeFraction = 0.40f,
	},
	{
		.activateAtLifetimeFraction = 0.85f,
		.sumOfDropChanceForThisSegment = 3.0f
	}
};

static const SimulatedHullsSystem::ColorChangeTimelineNode kSmokeHullHardLayerColorChangeTimeline[4] {
	{
	},
	{
		.activateAtLifetimeFraction = 0.23f, .replacementPalette = kSmokeHardLayerFadeInPalette,
		.sumOfReplacementChanceForThisSegment = 3.5f,
		.allowIncreasingOpacity = true,
	},
	{
		.activateAtLifetimeFraction = 0.40f,
	},
	{
		.activateAtLifetimeFraction = 0.70f,
		.sumOfDropChanceForThisSegment = 2.5f
	}
};

static const uint8_t kSmokeHullNoColorChangeVertexColor[4] { 127, 127, 127, 0 };
static const uint16_t kSmokeHullNoColorChangeIndices[] { 28, 100, 101, 103, 104, 106, 157, 158 };

static const ColorLifespan kExplosionSmokeColors[3] {
	{
		.initialColor  = { 0.5f, 0.5f, 0.5f, 0.0f },
		.fadedInColor  = { 0.25f, 0.25f, 0.25f, 0.23f },
		.fadedOutColor = { 0.2f, 0.2f, 0.2f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.33f, .startFadingOutAtLifetimeFrac = 0.67f,
	},
	{
		.initialColor  = { 0.5f, 0.5f, 0.5f, 0.0f },
		.fadedInColor  = { 0.50f, 0.50f, 0.50f, 0.23f },
		.fadedOutColor = { 0.4f, 0.4f, 0.4f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.33f, .startFadingOutAtLifetimeFrac = 0.67f,
	},
	{
		.initialColor  = { 0.5f, 0.5f, 0.5f, 0.0f },
		.fadedInColor  = { 0.75f, 0.75f, 0.75f, 0.23f },
		.fadedOutColor = { 0.6f, 0.6f, 0.6f, 0.0f },
		.finishFadingInAtLifetimeFrac = 0.33f, .startFadingOutAtLifetimeFrac = 0.67f,
	},
};

Particle::AppearanceRules TransientEffectsSystem::s_explosionSmokeAppearanceRules {
	.colors       = kExplosionSmokeColors,
	.kind         = Particle::Sprite,
	.radius       = 9.0f,
	.radiusSpread = 5.0f,
};

const EllipsoidalFlockParams TransientEffectsSystem::s_explosionSmokeFlockParams {
	.stretchScale  = 1.25f,
	.gravity       = -65.0f,
	.restitution   = 0.33f,
	.minSpeed      = 35.0f,
	.maxSpeed      = 65.0f,
	.minShiftSpeed = 55.0f,
	.maxShiftSpeed = 70.0f,
	.minPercentage = 0.7f,
	.maxPercentage = 0.9f,
	.minTimeout    = 1200,
	.maxTimeout    = 1750,
};

void TransientEffectsSystem::spawnExplosion( const float *fireOrigin, const float *smokeOrigin, float radius ) {
	// 250 for radius of 64
	// TODO: Make radius affect hulls
	constexpr float lightRadiusScale = 1.0f / 64.0f;
	allocLightEffect( m_lastTime, fireOrigin, vec3_origin, 0.0f, 400u, LightLifespan {
		.initialColor  = { 1.0f, 0.9f, 0.7f },
		.fadedInColor  = { 1.0f, 0.8f, 0.5f },
		.fadedOutColor = { 1.0f, 0.5f, 0.0f },
		.fadedInRadius = 250.0f * radius * lightRadiusScale,
		.finishRadiusFadingInAtLifetimeFrac = 0.15f,
		.startRadiusFadingOutAtLifetimeFrac = 0.75f,
	});

	SimulatedHullsSystem *const hullsSystem = &cg.simulatedHullsSystem;

	float fireHullScale;
	unsigned fireHullTimeout;
	std::span<const SimulatedHullsSystem::HullLayerParams> fireHullLayerParams;
	if( cg_explosionsSmoke->integer ) {
		fireHullScale       = 1.40f;
		fireHullTimeout     = 550;
		fireHullLayerParams = kFireHullParams.lightHullLayerParams;
	} else {
		fireHullScale       = 1.55f;
		fireHullTimeout     = 500;
		fireHullLayerParams = kFireHullParams.darkHullLayerParams;
	}

	if( auto *const hull = hullsSystem->allocFireHull( m_lastTime, fireHullTimeout ) ) {
		hullsSystem->setupHullVertices( hull, fireOrigin, fireHullScale, fireHullLayerParams );
		assert( !hull->layers[0].useDrawOnTopHack );
		hull->vertexViewDotFade          = ExternalMesh::FadeOutContour;
		hull->layers[0].useDrawOnTopHack = true;
		hull->layers[0].overrideHullFade = ExternalMesh::NoFade;
		hull->layers[1].overrideHullFade = ExternalMesh::NoFade;
	}

	if( cg_explosionsWave->integer ) {
		const vec4_t waveColor { 1.0f, 1.0f, 1.0f, 0.05f };
		if( auto *const hull = hullsSystem->allocWaveHull( m_lastTime, 250 ) ) {
			hullsSystem->setupHullVertices( hull, fireOrigin, waveColor, 500.0f, 50.0f );
		}
	}

	if( smokeOrigin ) {
		// TODO: It would look better if smoke hulls are coupled together/allocated at once
		spawnSmokeHull( m_lastTime, smokeOrigin, 75.0f, 10.0f, { +160.0f, +45.0f }, { +75.0f, -25.0f },
						ExternalMesh::FadeOutCenter, kSmokeHullHardLayerColorChangeTimeline );
		spawnSmokeHull( m_lastTime, smokeOrigin, 90.0f, 10.0f, { +170.0f, +40.0f }, { +85.0f, -20.0f },
						ExternalMesh::FadeOutContour, kSmokeHullSoftLayerColorChangeTimeline );
		spawnSmokeHull( m_lastTime, smokeOrigin, 99.0f, 10.0f, { +180.0f, +35.0f }, { +95.0f, -15.0f },
						ExternalMesh::FadeOutContour, kSmokeHullSoftLayerColorChangeTimeline );

		// ColorLifespan is quite limited to efficiently suppress drawing the flock at start.
		// Let's spawn smoke particles as a delayed effect.

		s_explosionSmokeAppearanceRules.materials = cgs.media.shaderFlareParticle.getAddressOfHandle();

		allocDelayedEffect( m_lastTime, smokeOrigin, vec3_origin, 300, ParticleFlockSpawnRecord {
			.appearanceRules        = &s_explosionSmokeAppearanceRules,
			.ellipsoidalFlockParams = &s_explosionSmokeFlockParams,
			.bin                    = ParticleFlockSpawnRecord::Large
		});
	}

	if( cg_explosionsClusters->integer ) {
		std::span<const SimulatedHullsSystem::HullLayerParams> clusterHullLayerParams;
		float minSpawnerSpeed, maxSpawnerSpeed;
		unsigned maxSpawnedClusters;
		if( cg_explosionsSmoke->integer ) {
			clusterHullLayerParams = ::kFireHullParams.lightClusterHullLayerParams;
			minSpawnerSpeed = 105.0f, maxSpawnerSpeed = 125.0f;
			maxSpawnedClusters = 7;
		} else {
			clusterHullLayerParams = ::kFireHullParams.darkClusterHullLayerParams;
			minSpawnerSpeed = 115.0f, maxSpawnerSpeed = 135.0f;
			maxSpawnedClusters = 10;
		}

		unsigned numSpawnedClusters = 0;
		unsigned oldDirNum          = 0;

		while( numSpawnedClusters < maxSpawnedClusters ) {
			const unsigned dirNum  = m_rng.nextBoundedFast( std::size( kPredefinedDirs ) );
			const float *randomDir = kPredefinedDirs[dirNum];
			// Just check against the last directory so this converges faster
			if( DotProduct( randomDir, kPredefinedDirs[oldDirNum] ) > 0.7f ) {
				continue;
			}

			oldDirNum = dirNum;
			numSpawnedClusters++;

			vec3_t randomVelocity;
			const float randomSpeed = m_rng.nextFloat( minSpawnerSpeed, maxSpawnerSpeed );
			VectorScale( randomDir, randomSpeed, randomVelocity );

			const auto spawnDelay = ( fireHullTimeout / 4 ) + m_rng.nextBoundedFast( fireHullTimeout / 4 );
			allocDelayedEffect( m_lastTime, fireOrigin, randomVelocity, spawnDelay, ConcentricHullSpawnRecord {
				.layerParams = clusterHullLayerParams,
				.scale       = m_rng.nextFloat( 0.11f, 0.37f ) * fireHullScale,
				.timeout     = fireHullTimeout / 3,
				.allocMethod = (ConcentricHullSpawnRecord::AllocMethod)&SimulatedHullsSystem::allocFireClusterHull,
				.vertexViewDotFade         = ExternalMesh::FadeOutContour,
				.useLayer0DrawOnTopHack    = true,
				.overrideLayer0ViewDotFade = ExternalMesh::NoFade,
			});
		}
	}
}

void TransientEffectsSystem::spawnSmokeHull( int64_t currTime, const float *origin, float speed, float speedSpread,
											 std::pair<float, float> archimedesAccel, std::pair<float, float> xyAccel,
											 ExternalMesh::ViewDotFade viewDotFade, ColorChangeTimeline colorTimeline ) {
	if( auto *const hull = cg.simulatedHullsSystem.allocSmokeHull( currTime, 2000 ) ) {
		hull->archimedesTopAccel      = archimedesAccel.first;
		hull->archimedesBottomAccel   = archimedesAccel.second;
		hull->xyExpansionTopAccel     = xyAccel.first;
		hull->xyExpansionBottomAccel  = xyAccel.second;

		hull->colorChangeTimeline      = colorTimeline;
		hull->noColorChangeIndices     = kSmokeHullNoColorChangeIndices;
		hull->noColorChangeVertexColor = kSmokeHullNoColorChangeVertexColor;

		hull->expansionStartAt = m_lastTime + 125;

		hull->lodCurrLevelTangentRatio = 0.10f;
		hull->tesselateClosestLod      = true;
		hull->leprNextLevelColors      = true;
		hull->applyVertexDynLight      = true;
		hull->vertexViewDotFade        = viewDotFade;

		const vec4_t initialSmokeColor { 0.0f, 0.0f, 0.0f, 0.03f };
		cg.simulatedHullsSystem.setupHullVertices( hull, origin, initialSmokeColor, speed, speedSpread );
	}
}

void TransientEffectsSystem::spawnCartoonHitEffect( const float *origin, const float *dir, int damage ) {
	if( cg_cartoonHitEffect->integer ) {
		float radius = 0.0f;
		shader_s *material = nullptr;
		if( damage > 64 ) {
			// OUCH!
			std::tie( material, radius ) = std::make_pair( cgs.media.shaderCartoonHit3, 24.0f );
		} else if( damage > 50 ) {
			// POW!
			std::tie( material, radius ) = std::make_pair( cgs.media.shaderCartoonHit, 19.0f );
		} else if( damage > 38 ) {
			// SPLITZOW!
			std::tie( material, radius ) = std::make_pair( cgs.media.shaderCartoonHit2, 15.0f );
		}

		if( material ) {
			// TODO:
			vec3_t localDir;
			if( !VectorLength( dir ) ) {
				VectorNegate( &cg.view.axis[AXIS_FORWARD], localDir );
			} else {
				VectorNormalize2( dir, localDir );
			}

			vec3_t spriteOrigin;
			// Move effect a bit up from player
			VectorCopy( origin, spriteOrigin );
			spriteOrigin[2] += ( playerbox_stand_maxs[2] - playerbox_stand_mins[2] ) + 1.0f;

			EntityEffect *effect = addSpriteEffect( material, spriteOrigin, radius, 700u );
			effect->entity.rotation = 0.0f;
			// TODO: Add a correct sampling of random sphere points as a random generator method
			for( unsigned i = 0; i < 3; ++i ) {
				effect->velocity[i] = m_rng.nextFloat( -10.0f, +10.0f );
			}
		}
	}
}

void TransientEffectsSystem::spawnBleedingVolumeEffect( const float *origin, const float *dir, int damage,
														const float *bloodColor, unsigned duration ) {
	if( auto *hull = cg.simulatedHullsSystem.allocWaveHull( m_lastTime, duration ) ) {
		const vec4_t hullColor { bloodColor[0], bloodColor[1], bloodColor[2], 0.1f };
		const vec3_t hullOrigin { origin[0] + dir[0], origin[1] + dir[1], origin[2] + dir[2] };
		float speed = 100.0f;
		if( damage < 25 ) {
			speed = 50.0f;
		} else if( damage < 50 ) {
			speed = 75.0f;
		}
		cg.simulatedHullsSystem.setupHullVertices( hull, hullOrigin, hullColor, speed, 0.1f * speed );
	}
}

void TransientEffectsSystem::spawnElectroboltHitEffect( const float *origin, const float *dir, const float *decalColor,
														const float *energyColor, bool spawnDecal ) {
	spawnElectroboltLikeHitEffect( origin, dir, decalColor, energyColor, cgs.media.modElectroBoltWallHit, spawnDecal );
}

void TransientEffectsSystem::spawnInstagunHitEffect( const float *origin, const float *dir, const float *decalColor,
													 const float *energyColor, bool spawnDecal ) {
	spawnElectroboltLikeHitEffect( origin, dir, decalColor, energyColor, cgs.media.modInstagunWallHit, spawnDecal );
}

void TransientEffectsSystem::spawnElectroboltLikeHitEffect( const float *origin, const float *dir, 
															const float *decalColor, const float *energyColor, 
															model_s *model, bool spawnDecal ) {
	if( spawnDecal ) {
		EntityEffect *entityEffect = addModelEffect( model, origin, dir, 600u );
		VectorScale( decalColor, 255.0f, entityEffect->entity.shaderRGBA );
	}

	allocLightEffect( m_lastTime, origin, dir, 4.0f, 250u, LightLifespan {
		.initialColor  = { 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { energyColor[0], energyColor[1], energyColor[2] },
		.fadedOutColor = { energyColor[0], energyColor[1], energyColor[2] },
		.finishColorFadingInAtLifetimeFrac  = 0.10f,
		.fadedInRadius                      = 144.0f,
		.finishRadiusFadingInAtLifetimeFrac = 0.10f
	});

	if( cg_explosionsWave->integer ) {
		if( auto *hull = cg.simulatedHullsSystem.allocWaveHull( m_lastTime, 200 ) ) {
			const vec4_t hullColor { energyColor[0], energyColor[1], energyColor[2], 0.075f };
			cg.simulatedHullsSystem.setupHullVertices( hull, origin, hullColor, 750.0f, 100.0f );
		}
		if( auto *hull = cg.simulatedHullsSystem.allocWaveHull( m_lastTime, 200 ) ) {
			const vec4_t hullColor { energyColor[0], energyColor[1], energyColor[2], 0.1f };
			cg.simulatedHullsSystem.setupHullVertices( hull, origin, hullColor, 125.0f, 50.0f );
		}
	}
}

void TransientEffectsSystem::spawnPlasmaImpactEffect( const float *origin, const float *dir ) {
	EntityEffect *const entityEffect = addModelEffect( cgs.media.modPlasmaExplosion, origin, dir, 300u );
	entityEffect->fadedInScale = entityEffect->fadedOutScale = 2.5f;

	allocLightEffect( m_lastTime, origin, dir, 4.0f, 200, LightLifespan {
		.initialColor  = { 1.0f, 1.0f, 1.0f },
		.fadedInColor  = { 0.0f, 1.0f, 0.3f },
		.fadedOutColor = { 0.0f, 0.7f, 0.0f },
		.fadedInRadius = 96.0f,
	});

	if( cg_explosionsWave->integer ) {
		if( auto *hull = cg.simulatedHullsSystem.allocWaveHull( m_lastTime, 175 ) ) {
			const vec4_t hullColor { colorGreen[0], colorGreen[1], colorGreen[2], 0.05f };
			cg.simulatedHullsSystem.setupHullVertices( hull, origin, hullColor, 300.0f, 75.0f );
		}
	}
}

static const byte_vec4_t kBlastHullLayer0ReplacementPalette[] {
	asByteColor( 0.7f, 0.7f, 0.5f, 0.1f ),
	asByteColor( 0.7f, 0.7f, 0.4f, 0.1f ),
	asByteColor( 0.7f, 0.7f, 0.4f, 0.1f ),
};

static const byte_vec4_t kBlastHullLayer1ReplacementPalette[] {
	asByteColor( 0.7f, 0.7f, 0.5f, 0.1f ),
	asByteColor( 0.7f, 0.7f, 0.4f, 0.1f ),
	asByteColor( 0.7f, 0.6f, 0.4f, 0.1f ),
};

static const byte_vec4_t kBlastHullLayer2ReplacementPalette[] {
	asByteColor( 0.7f, 0.7f, 0.3f, 0.1f ),
	asByteColor( 0.7f, 0.7f, 0.4f, 0.1f ),
	asByteColor( 0.7f, 0.5f, 0.4f, 0.1f ),
};

static const byte_vec4_t kBlastHullDecayPalette[] {
	asByteColor( 0.7f, 0.7f, 0.4f, 0.05f ),
	asByteColor( 0.7f, 0.3f, 0.2f, 0.05f ),
	asByteColor( 0.7f, 0.4f, 0.1f, 0.05f )
};

static const SimulatedHullsSystem::ColorChangeTimelineNode kBlastHullLayer0ColorChangeTimeline[3] {
	{
		.sumOfDropChanceForThisSegment = 0.1f,
	},
	{
		.activateAtLifetimeFraction = 0.33f, .replacementPalette = kBlastHullLayer0ReplacementPalette,
		.sumOfDropChanceForThisSegment = 0.1f, .sumOfReplacementChanceForThisSegment = 0.3f,
	},
	{
		.activateAtLifetimeFraction = 0.67f, .replacementPalette = kBlastHullDecayPalette,
		.sumOfDropChanceForThisSegment = 1.5f, .sumOfReplacementChanceForThisSegment = 1.0f,
	}
};

static const SimulatedHullsSystem::ColorChangeTimelineNode kBlastHullLayer1ColorChangeTimeline[3] {
	{
	},
	{
		.activateAtLifetimeFraction = 0.33f, .replacementPalette = kBlastHullLayer1ReplacementPalette,
		.sumOfDropChanceForThisSegment = 0.1f, .sumOfReplacementChanceForThisSegment = 0.5f,
	},
	{
		.activateAtLifetimeFraction = 0.67f, .replacementPalette = kBlastHullDecayPalette,
		.sumOfDropChanceForThisSegment = 1.5f, .sumOfReplacementChanceForThisSegment = 1.0f,
	}
};

static const SimulatedHullsSystem::ColorChangeTimelineNode kBlastHullLayer2ColorChangeTimeline[3] {
	{
	},
	{
		.activateAtLifetimeFraction = 0.33f, .replacementPalette = kBlastHullLayer2ReplacementPalette,
		.sumOfDropChanceForThisSegment = 0.1f, .sumOfReplacementChanceForThisSegment = 0.7f,
	},
	{
		.activateAtLifetimeFraction = 0.67f, .replacementPalette = kBlastHullDecayPalette,
		.sumOfDropChanceForThisSegment = 1.5f, .sumOfReplacementChanceForThisSegment = 1.0f,
	}
};

static const SimulatedHullsSystem::HullLayerParams kBlastHullLayerParams[3] {
	{
		.speed = 30.0f, .finalOffset = 5.0f,
		.speedSpikeChance = 0.10f, .minSpeedSpike = 5.0f, .maxSpeedSpike = 20.0f,
		.biasAlongChosenDir = 15.0f,
		.baseInitialColor = { 0.9f, 1.0f, 0.6f, 1.0f },
		.bulgeInitialColor = { 0.9f, 1.0f, 1.0f, 1.0f },
		.colorChangeTimeline = kBlastHullLayer0ColorChangeTimeline
	},
	{
		.speed = 40.0f, .finalOffset = 2.5f,
		.speedSpikeChance = 0.08f, .minSpeedSpike = 5.0f, .maxSpeedSpike = 20.0f,
		.biasAlongChosenDir = 15.0f,
		.baseInitialColor = { 1.0f, 0.7f, 0.4f, 1.0f },
		.bulgeInitialColor = { 1.0f, 0.8f, 0.5f, 1.0f },
		.colorChangeTimeline = kBlastHullLayer1ColorChangeTimeline
	},
	{
		.speed = 50.0f, .finalOffset = 0.0f,
		.speedSpikeChance = 0.08f, .minSpeedSpike = 5.0f, .maxSpeedSpike = 20.0f,
		.biasAlongChosenDir = 15.0f,
		.baseInitialColor = { 0.7, 0.6f, 0.4f, 0.7f },
		.bulgeInitialColor = { 1.0f, 0.7f, 0.4f, 0.7f },
		.colorChangeTimeline = kBlastHullLayer2ColorChangeTimeline
	},
};

void TransientEffectsSystem::spawnGunbladeBlastImpactEffect( const float *origin, const float *dir ) {
	allocLightEffect( m_lastTime, origin, dir, 8.0f, 350u, LightLifespan {
		.initialColor  = { 1.0f, 1.0f, 0.5f },
		.fadedInColor  = { 1.0f, 0.8f, 0.3f },
		.fadedOutColor = { 0.5f, 0.7f, 0.3f },
		.fadedInRadius = 144.0f,
	});

	const vec3_t hullOrigin { origin[0] + 8.0f * dir[0], origin[1] + 8.0f * dir[1], origin[2] + 8.0f * dir[2] };

	if( auto *hull = cg.simulatedHullsSystem.allocBlastHull( m_lastTime, 450 ) ) {
		cg.simulatedHullsSystem.setupHullVertices( hull, hullOrigin, 1.25f, kBlastHullLayerParams );
		assert( !hull->layers[0].useDrawOnTopHack );
		hull->vertexViewDotFade          = ExternalMesh::FadeOutContour;
		hull->layers[0].useDrawOnTopHack = true;
		hull->layers[0].overrideHullFade = ExternalMesh::NoFade;
	}

	if( cg_explosionsWave->integer ) {
		if( auto *hull = cg.simulatedHullsSystem.allocWaveHull( m_lastTime, 200 ) ) {
			const vec4_t waveHullColor { 1.0f, 0.9f, 0.6f, 0.05f };
			cg.simulatedHullsSystem.setupHullVertices( hull, hullOrigin, waveHullColor, 300.0f, 30.0f );
		}
	}
}

void TransientEffectsSystem::spawnGunbladeBladeImpactEffect( const float *origin, const float *dir ) {
	(void)addModelEffect( cgs.media.modBladeWallHit, origin, dir, 300u );
	// TODO: Add light when hitting metallic surfaces?
}

void TransientEffectsSystem::spawnBulletLikeImpactEffect( const float *origin, const float *dir ) {
	EntityEffect *effect = addModelEffect( cgs.media.modBladeWallExplo, origin, dir, 100u );
	effect->fadedInScale  = 0.3f;
	effect->fadedOutScale = 0.0f;
	// TODO: Add light when hitting metallic surfaces?
}

void TransientEffectsSystem::spawnDustImpactEffect( const float *origin, const float *dir, float radius ) {
	vec3_t axis1, axis2;
	PerpendicularVector( axis2, dir );
	CrossProduct( dir, axis2, axis1 );

	VectorNormalize( axis1 ), VectorNormalize( axis2 );

	float angle = 0.0f;
	constexpr const int count = 12;
	const float speed = 0.67f * radius;
	const float angleStep = (float)M_TWOPI * Q_Rcp( (float)count );
	for( int i = 0; i < count; ++i ) {
		const float scale1 = std::sin( angle ), scale2 = std::cos( angle );
		angle += angleStep;

		vec3_t velocity { 0.0f, 0.0f, 0.0f };
		VectorMA( velocity, speed * scale1, axis1, velocity );
		VectorMA( velocity, speed * scale2, axis2, velocity );

		EntityEffect *effect = addSpriteEffect( cgs.media.shaderSmokePuff2, origin, 10.0f, 700u );
		effect->fadedInScale = 0.33f;
		effect->fadedOutScale = 0.0f;
		effect->initialAlpha = 0.25f;
		effect->fadedOutAlpha = 0.0f;
		VectorCopy( velocity, effect->velocity );
	}
}

void TransientEffectsSystem::spawnDashEffect( const float *origin, const float *dir ) {
	// Model orientation/streching hacks
	vec3_t angles;
	VecToAngles( dir, angles );
	angles[1] += 270.0f;
	EntityEffect *effect = addModelEffect( cgs.media.modDash, origin, dir, 700u );
	AnglesToAxis( angles, effect->entity.axis );
	// Scale Z
	effect->entity.axis[2 * 3 + 2] *= 2.0f;
	// Size hacks
	effect->fadedInScale = effect->fadedOutScale = 0.15f;
}

auto TransientEffectsSystem::addModelEffect( model_s *model, const float *origin, const float *dir, unsigned duration ) -> EntityEffect * {
	EntityEffect *const effect = allocEntityEffect( m_lastTime, duration );

	std::memset( &effect->entity, 0, sizeof( entity_s ) );
	effect->entity.rtype = RT_MODEL;
	effect->entity.renderfx = RF_NOSHADOW;
	effect->entity.model = model;
	effect->entity.customShader = nullptr;
	effect->entity.shaderTime = m_lastTime;
	effect->entity.scale = 0.0f;
	effect->entity.rotation = (float)m_rng.nextBounded( 360 );

	VectorSet( effect->entity.shaderRGBA, 255, 255, 255 );

	NormalVectorToAxis( dir, &effect->entity.axis[0] );
	VectorCopy( origin, effect->entity.origin );

	return effect;
}

auto TransientEffectsSystem::addSpriteEffect( shader_s *material, const float *origin, float radius, unsigned duration ) -> EntityEffect * {
	EntityEffect *effect = allocEntityEffect( m_lastTime, duration );

	std::memset( &effect->entity, 0, sizeof( entity_s ) );
	effect->entity.rtype = RT_SPRITE;
	effect->entity.renderfx = RF_NOSHADOW;
	effect->entity.radius = radius;
	effect->entity.customShader = material;
	effect->entity.shaderTime = m_lastTime;
	effect->entity.scale = 0.0f;
	effect->entity.rotation = (float)m_rng.nextBounded( 360 );

	VectorSet( effect->entity.shaderRGBA, 255, 255, 255 );

	Matrix3_Identity( effect->entity.axis );
	VectorCopy( origin, effect->entity.origin );

	return effect;
}

auto TransientEffectsSystem::allocEntityEffect( int64_t currTime, unsigned duration ) -> EntityEffect * {
	void *mem = m_entityEffectsAllocator.allocOrNull();
	if( !mem ) [[unlikely]] {
		// TODO: Prioritize effects so unimportant ones get evicted first
		EntityEffect *oldestEffect = nullptr;
		// TODO: Choose by nearest timeout/lifetime fraction?
		int64_t oldestSpawnTime = std::numeric_limits<int64_t>::max();
		for( EntityEffect *effect = m_entityEffectsHead; effect; effect = effect->next ) {
			if( oldestSpawnTime > effect->spawnTime ) {
				oldestSpawnTime = effect->spawnTime;
				oldestEffect = effect;
			}
		}
		assert( oldestEffect );
		wsw::unlink( oldestEffect, &m_entityEffectsHead );
		oldestEffect->~EntityEffect();
		mem = oldestEffect;
	}

	auto *effect = new( mem )EntityEffect;

	assert( duration <= std::numeric_limits<uint16_t>::max() );
	// Try forcing 16-bit division if a compiler fails to optimize division by constant
	unsigned fadeInDuration = (uint16_t)duration / (uint16_t)10;
	if( fadeInDuration > 33 ) [[likely]] {
		fadeInDuration = 33;
	} else if( fadeInDuration < 1 ) [[unlikely]] {
		fadeInDuration = 1;
	}

	unsigned fadeOutDuration;
	if( duration > fadeInDuration ) [[likely]] {
		fadeOutDuration = duration - fadeInDuration;
	} else {
		fadeOutDuration = fadeInDuration;
		duration = fadeInDuration + 1;
	}

	effect->duration = duration;
	effect->rcpDuration = Q_Rcp( (float)duration );
	effect->fadeInDuration = fadeInDuration;
	effect->rcpFadeInDuration = Q_Rcp( (float)fadeInDuration );
	effect->rcpFadeOutDuration = Q_Rcp( (float)fadeOutDuration );
	effect->spawnTime = currTime;

	wsw::link( effect, &m_entityEffectsHead );
	return effect;
}

auto TransientEffectsSystem::allocLightEffect( int64_t currTime, const float *origin, const float *offset,
											   float offsetScale, unsigned duration,
											   LightLifespan &&lightLifespan ) -> LightEffect * {
	void *mem = m_lightEffectsAllocator.allocOrNull();
	// TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Generalize
	if( !mem ) [[unlikely]] {
		// TODO: Prioritize effects so unimportant ones get evicted first
		LightEffect *oldestEffect = nullptr;
		// TODO: Choose by nearest timeout/lifetime fraction?
		int64_t oldestSpawnTime = std::numeric_limits<int64_t>::max();
		for( LightEffect *effect = m_lightEffectsHead; effect; effect = effect->next ) {
			if( oldestSpawnTime > effect->spawnTime ) {
				oldestSpawnTime = effect->spawnTime;
				oldestEffect = effect;
			}
		}
		assert( oldestEffect );
		wsw::unlink( oldestEffect, &m_lightEffectsHead );
		oldestEffect->~LightEffect();
		mem = oldestEffect;
	}

	auto *effect          = new( mem )LightEffect;
	effect->duration      = duration;
	effect->spawnTime     = currTime;
	effect->lightLifespan = std::forward<LightLifespan>( lightLifespan );
	VectorMA( origin, offsetScale, offset, effect->origin );

	wsw::link( effect, &m_lightEffectsHead );
	return effect;
}

auto TransientEffectsSystem::allocDelayedEffect( int64_t currTime, const float *origin, const float *velocity,
												 unsigned delay, DelayedEffect::SpawnRecord &&spawnRecord )
												 -> DelayedEffect * {
	void *mem = m_delayedEffectsAllocator.allocOrNull();
	// TODO!!!!!!!!!!!!
	if( !mem ) {
		DelayedEffect *oldestEffect = nullptr;
		// TODO: Choose by nearest timeout/lifetime fraction?
		int64_t oldestSpawnTime = std::numeric_limits<int64_t>::max();
		for( DelayedEffect *effect = m_delayedEffectsHead; effect; effect = effect->next ) {
			if( oldestSpawnTime > effect->spawnTime ) {
				oldestSpawnTime = effect->spawnTime;
				oldestEffect = effect;
			}
		}
		assert( oldestEffect );
		wsw::unlink( oldestEffect, &m_delayedEffectsHead );
		oldestEffect->~DelayedEffect();
		mem = oldestEffect;
	}

	auto *effect = new( mem )DelayedEffect { .spawnRecord = std::move_if_noexcept( spawnRecord ) };
	effect->spawnTime  = currTime;
	effect->spawnDelay = delay;
	VectorCopy( origin, effect->origin );
	VectorCopy( velocity, effect->velocity );

	wsw::link( effect, &m_delayedEffectsHead );
	return effect;
}

void TransientEffectsSystem::unlinkAndFreeEntityEffect( EntityEffect *effect ) {
	wsw::unlink( effect, &m_entityEffectsHead );
	effect->~EntityEffect();
	m_entityEffectsAllocator.free( effect );
}

void TransientEffectsSystem::unlinkAndFreeLightEffect( LightEffect *effect ) {
	wsw::unlink( effect, &m_lightEffectsHead );
	effect->~LightEffect();
	m_lightEffectsAllocator.free( effect );
}

void TransientEffectsSystem::unlinkAndFreeDelayedEffect( DelayedEffect *effect ) {
	wsw::unlink( effect, &m_delayedEffectsHead );
	effect->~DelayedEffect();
	m_delayedEffectsAllocator.free( effect );
}

void TransientEffectsSystem::simulateFrameAndSubmit( int64_t currTime, DrawSceneRequest *request ) {
	// Limit the time step
	const float timeDeltaSeconds = 1e-3f * (float)wsw::min<int64_t>( 33, currTime - m_lastTime );

	simulateDelayedEffects( currTime, timeDeltaSeconds );
	simulateEntityEffectsAndSubmit( currTime, timeDeltaSeconds, request );
	simulateLightEffectsAndSubmit( currTime, timeDeltaSeconds, request );

	m_lastTime = currTime;
}

void TransientEffectsSystem::simulateEntityEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds,
															 DrawSceneRequest *request ) {
	const model_s *const dashModel = cgs.media.modDash;
	const float backlerp = 1.0f - cg.lerpfrac;

	EntityEffect *nextEffect = nullptr;
	for( EntityEffect *__restrict effect = m_entityEffectsHead; effect; effect = nextEffect ) {
		nextEffect = effect->next;

		if( effect->spawnTime + effect->duration <= currTime ) [[unlikely]] {
			unlinkAndFreeEntityEffect( effect );
			continue;
		}

		const auto lifetimeMillis = (unsigned)( currTime - effect->spawnTime );
		assert( lifetimeMillis < effect->duration );

		if( lifetimeMillis >= effect->fadeInDuration ) [[likely]] {
			assert( effect->duration > effect->fadeInDuration );
			const float fadeOutFrac = (float)( lifetimeMillis - effect->fadeInDuration ) * effect->rcpFadeOutDuration;
			effect->entity.scale = std::lerp( effect->fadedInScale, effect->fadedOutScale, fadeOutFrac );
		} else {
			const float fadeInFrac = (float)lifetimeMillis * effect->rcpFadeInDuration;
			effect->entity.scale = effect->fadedInScale * fadeInFrac;
		}

		// Dash model hacks
		if( effect->entity.model == dashModel ) [[unlikely]] {
			float *const zScale = effect->entity.axis + ( 2 * 3 ) + 2;
			*zScale -= 4.0f * timeDeltaSeconds;
			if( *zScale < 0.01f ) {
				unlinkAndFreeEntityEffect( effect );
				continue;
			}
		}

		vec3_t moveVec;
		VectorScale( effect->velocity, timeDeltaSeconds, moveVec );
		VectorAdd( effect->entity.origin, moveVec, effect->entity.origin );

		const float lifetimeFrac = (float)lifetimeMillis * effect->rcpDuration;

		effect->entity.backlerp = backlerp;
		const float alpha = std::lerp( effect->initialAlpha, effect->fadedOutAlpha, lifetimeFrac );
		effect->entity.shaderRGBA[3] = (uint8_t)( 255 * alpha );

		request->addEntity( &effect->entity );
	}
}

void TransientEffectsSystem::simulateLightEffectsAndSubmit( int64_t currTime, float timeDeltaSeconds,
															DrawSceneRequest *request ) {
	LightEffect *nextEffect = nullptr;
	for( LightEffect *__restrict effect = m_lightEffectsHead; effect; effect = nextEffect ) {
		nextEffect = effect->next;

		if( effect->spawnTime + effect->duration <= currTime ) [[unlikely]] {
			unlinkAndFreeLightEffect( effect );
			continue;
		}

		const float lifetimeFrac = (float)( currTime - effect->spawnTime ) * Q_Rcp( (float)effect->duration );

		float radius, color[3];
		effect->lightLifespan.getRadiusAndColorForLifetimeFrac( lifetimeFrac, &radius, color );

		if( radius >= 1.0f ) [[likely]] {
			request->addLight( effect->origin, radius, 0.0f, color );
		}
	}

	// TODO: Add and use a bulk submission of lights
}

void TransientEffectsSystem::simulateDelayedEffects( int64_t currTime, float timeDeltaSeconds ) {
	DelayedEffect *nextEffect = nullptr;
	for( DelayedEffect *effect = m_delayedEffectsHead; effect; effect = nextEffect ) { nextEffect = effect->next;
		// TODO: Normalize angles each step?
		VectorMA( effect->angles, timeDeltaSeconds, effect->angularVelocity, effect->angles );

		vec3_t idealOrigin;
		VectorMA( effect->origin, timeDeltaSeconds, effect->velocity, idealOrigin );
		idealOrigin[2] += 0.5f * effect->gravity * timeDeltaSeconds * timeDeltaSeconds;

		effect->velocity[2] += effect->gravity * timeDeltaSeconds;

		trace_t trace;
		CM_TransformedBoxTrace( cl.cms, &trace, effect->origin, idealOrigin, vec3_origin, vec3_origin,
								nullptr, MASK_SOLID | MASK_WATER, nullptr, nullptr, 0 );

		if( trace.fraction == 1.0f ) {
			VectorCopy( idealOrigin, effect->origin );
		} else if( !( trace.startsolid | trace.allsolid ) ) {
			vec3_t velocityDir;
			VectorCopy( effect->velocity, velocityDir );
			if( const float squareSpeed = VectorLengthSquared( velocityDir ); squareSpeed > 1.0f ) {
				const float rcpSpeed = Q_RSqrt( squareSpeed );
				VectorScale( velocityDir, rcpSpeed, velocityDir );
				vec3_t reflectedDir;
				VectorReflect( velocityDir, trace.plane.normal, 0.0f, reflectedDir );
				addRandomRotationToDir( reflectedDir, &m_rng, 0.9f );
				const float newSpeed = effect->restitution * squareSpeed * rcpSpeed;
				VectorScale( reflectedDir, newSpeed, effect->velocity );
				// This is not correct but is sufficient
				VectorAdd( trace.endpos, trace.plane.normal, effect->origin );
			}
		}

		const int64_t triggerAt = effect->spawnTime + effect->spawnDelay;
		if( triggerAt <= currTime ) {
			// Don't spawn in solid or while contacting solid
			if( trace.fraction == 1.0f && !trace.startsolid ) {
				if( const auto *hullRecord = std::get_if<RegularHullSpawnRecord>( &effect->spawnRecord ) ) {
					auto method = hullRecord->allocMethod;
					if( auto *hull = ( cg.simulatedHullsSystem.*method )( m_lastTime, hullRecord->timeout ) ) {
						cg.simulatedHullsSystem.setupHullVertices( hull, effect->origin, hullRecord->color,
																   hullRecord->speed, hullRecord->speedSpread );
						hull->colorChangeTimeline      = hullRecord->colorChangeTimeline;

						hull->lodCurrLevelTangentRatio = hullRecord->lodCurrLevelTangentRatio;
						hull->tesselateClosestLod      = hullRecord->tesselateClosestLod;
						hull->leprNextLevelColors      = hullRecord->lerpNextLevelColors;
						hull->applyVertexDynLight      = hullRecord->applyVertexDynLight;
						hull->vertexViewDotFade        = hullRecord->vertexViewDotFade;
					}
				}
				if( const auto *hullRecord = std::get_if<ConcentricHullSpawnRecord>( &effect->spawnRecord ) ) {
					auto method = hullRecord->allocMethod;
					if( auto *hull = ( cg.simulatedHullsSystem.*method )( m_lastTime, hullRecord->timeout ) ) {
						cg.simulatedHullsSystem.setupHullVertices( hull, effect->origin, hullRecord->scale,
																   hullRecord->layerParams );
						assert( !hull->layers[0].useDrawOnTopHack );
						hull->vertexViewDotFade          = hullRecord->vertexViewDotFade;
						hull->layers[0].useDrawOnTopHack = hullRecord->useLayer0DrawOnTopHack;
						hull->layers[0].overrideHullFade = hullRecord->overrideLayer0ViewDotFade;
					}
				}
				if( const auto *flockRecord = std::get_if<ParticleFlockSpawnRecord>( &effect->spawnRecord ) ) {
					const Particle::AppearanceRules &arules = *flockRecord->appearanceRules;
					// These branches use different types
					if( flockRecord->conicalFlockParams ) {
						ConicalFlockParams flockParams( *flockRecord->conicalFlockParams );
						VectorCopy( effect->origin, flockParams.origin );
						VectorClear( flockParams.offset );
						AngleVectors( effect->angles, flockParams.dir, nullptr, nullptr );
						// TODO: "using enum"
						using Pfsr = ParticleFlockSpawnRecord;
						switch( flockRecord->bin ) {
							case Pfsr::Small: cg.particleSystem.addSmallParticleFlock( arules, flockParams ); break;
							case Pfsr::Medium: cg.particleSystem.addMediumParticleFlock( arules, flockParams ); break;
							case Pfsr::Large: cg.particleSystem.addLargeParticleFlock( arules, flockParams ); break;
						}
					} else {
						EllipsoidalFlockParams flockParams( *flockRecord->ellipsoidalFlockParams );
						VectorCopy( effect->origin, flockParams.origin );
						VectorClear( flockParams.offset );
						if( flockParams.stretchScale != 1.0f ) {
							AngleVectors( effect->angles, flockParams.stretchDir, nullptr, nullptr );
						}
						// TODO: "using enum"
						using Pfsr = ParticleFlockSpawnRecord;
						switch( flockRecord->bin ) {
							case Pfsr::Small: cg.particleSystem.addSmallParticleFlock( arules, flockParams ); break;
							case Pfsr::Medium: cg.particleSystem.addMediumParticleFlock( arules, flockParams ); break;
							case Pfsr::Large: cg.particleSystem.addLargeParticleFlock( arules, flockParams ); break;
						}
					}
				}
				unlinkAndFreeDelayedEffect( effect );
			} else if( triggerAt + 25 < currTime ) {
				// If the "grace" period for getting out of solid has expired
				unlinkAndFreeDelayedEffect( effect );
			}
		}
	}
}