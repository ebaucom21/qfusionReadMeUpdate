#include "snd_effect_sampler.h"
#include "snd_leaf_props_cache.h"
#include "snd_effects_allocator.h"
#include "snd_propagation.h"

#include "../gameshared/q_collision.h"

#include <limits>
#include <random>

static UnderwaterFlangerEffectSampler underwaterFlangerEffectSampler;

static ReverbEffectSampler reverbEffectSampler;

Effect *EffectSamplers::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) {
	Effect *effect;
	if( ( effect = ::underwaterFlangerEffectSampler.TryApply( listenerProps, src, tryReusePropsSrc ) ) ) {
		return effect;
	}
	if( ( effect = ::reverbEffectSampler.TryApply( listenerProps, src, tryReusePropsSrc ) ) ) {
		return effect;
	}

	Com_Error( ERR_FATAL, "EffectSamplers::TryApply(): Can't find an applicable effect sampler\n" );
}

// We want sampling results to be reproducible especially for leaf sampling and thus use this local implementation
static std::minstd_rand0 samplingRandom;

float EffectSamplers::SamplingRandom() {
	typedef decltype( samplingRandom ) R;
	return ( samplingRandom() - R::min() ) / (float)( R::max() - R::min() );
}

Effect *UnderwaterFlangerEffectSampler::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t * ) {
	if( !listenerProps.isInLiquid && !src->envUpdateState.isInLiquid ) {
		return nullptr;
	}

	float directObstruction = 0.9f;
	if( src->envUpdateState.isInLiquid && listenerProps.isInLiquid ) {
		directObstruction = ComputeDirectObstruction( listenerProps, src );
	}

	auto *effect = EffectsAllocator::Instance()->NewFlangerEffect( src );
	effect->directObstruction = directObstruction;
	effect->hasMediumTransition = src->envUpdateState.isInLiquid ^ listenerProps.isInLiquid;
	return effect;
}

static bool ENV_TryReuseSourceReverbProps( src_t *src, const src_t *tryReusePropsSrc, EaxReverbEffect *newEffect ) {
	if( !tryReusePropsSrc ) {
		return false;
	}

	auto *reuseEffect = Effect::Cast<const EaxReverbEffect *>( tryReusePropsSrc->envUpdateState.effect );
	if( !reuseEffect ) {
		return false;
	}

	// We are already sure that both sources are in the same contents kind (non-liquid).
	// Check distance between sources.
	const float squareDistance = DistanceSquared( tryReusePropsSrc->origin, src->origin );
	// If they are way too far for reusing
	if( squareDistance > 96 * 96 ) {
		return false;
	}

	// If they are very close, feel free to just copy props
	if( squareDistance < 4.0f * 4.0f ) {
		newEffect->CopyReverbProps( reuseEffect );
		return true;
	}

	// Do a coarse raycast test between these two sources
	vec3_t start, end, dir;
	VectorSubtract( tryReusePropsSrc->origin, src->origin, dir );
	const float invDistance = 1.0f / sqrtf( squareDistance );
	VectorScale( dir, invDistance, dir );
	// Offset start and end by a dir unit.
	// Ensure start and end are in "air" and not on a brush plane
	VectorAdd( src->origin, dir, start );
	VectorSubtract( tryReusePropsSrc->origin, dir, end );

	trace_t trace;
	S_Trace( &trace, start, end, vec3_origin, vec3_origin, MASK_SOLID );
	if( trace.fraction != 1.0f ) {
		return false;
	}

	newEffect->CopyReverbProps( reuseEffect );
	return true;
}

void ObstructedEffectSampler::SetupDirectObstructionSamplingProps( src_t *src, unsigned minSamples, unsigned maxSamples ) {
	float quality = s_environment_sampling_quality->value;
	samplingProps_t *props = &src->envUpdateState.directObstructionSamplingProps;

	// If the quality is valid and has not been modified since the pattern has been set
	if( props->quality == quality ) {
		return;
	}

	unsigned numSamples = GetNumSamplesForCurrentQuality( minSamples, maxSamples );

	props->quality = quality;
	props->numSamples = numSamples;
	props->valueIndex = (uint16_t)( EffectSamplers::SamplingRandom() * std::numeric_limits<uint16_t>::max() );
}

struct DirectObstructionOffsetsHolder {
	enum { NUM_VALUES = 256 };
	vec3_t offsets[NUM_VALUES];
	enum { MAX_OFFSET = 20 };

	DirectObstructionOffsetsHolder() {
		for( auto *v: offsets ) {
			for( int i = 0; i < 3; ++i ) {
				v[i] = -MAX_OFFSET + 2 * MAX_OFFSET * EffectSamplers::SamplingRandom();
			}
		}
	}
};

static DirectObstructionOffsetsHolder directObstructionOffsetsHolder;

float ObstructedEffectSampler::ComputeDirectObstruction( const ListenerProps &listenerProps, src_t *src ) {
	trace_t trace;
	envUpdateState_t *updateState;
	float *originOffset;
	vec3_t testedListenerOrigin;
	vec3_t testedSourceOrigin;
	float squareDistance;
	unsigned numTestedRays, numPassedRays;
	unsigned valueIndex;

	updateState = &src->envUpdateState;

	VectorCopy( listenerProps.origin, testedListenerOrigin );
	// TODO: We assume standard view height
	testedListenerOrigin[2] += 18.0f;

	squareDistance = DistanceSquared( testedListenerOrigin, src->origin );
	// Shortcut for sounds relative to the player
	if( squareDistance < 32.0f * 32.0f ) {
		return 0.0f;
	}

	if( !S_LeafsInPVS( listenerProps.GetLeafNum(), S_PointLeafNum( src->origin ) ) ) {
		return 1.0f;
	}

	vec3_t hintBounds[2];
	ClearBounds( hintBounds[0], hintBounds[1] );
	AddPointToBounds( testedListenerOrigin, hintBounds[0], hintBounds[1] );
	AddPointToBounds( src->origin, hintBounds[0], hintBounds[1] );
	// Account for obstruction sampling offsets
	// as we are going to compute the top node hint once
	for( int i = 0; i < 3; ++i ) {
		hintBounds[0][i] -= DirectObstructionOffsetsHolder::MAX_OFFSET;
		hintBounds[1][i] += DirectObstructionOffsetsHolder::MAX_OFFSET;
	}

	const int topNodeHint = S_FindTopNodeForBox( hintBounds[0], hintBounds[1] );
	S_Trace( &trace, testedListenerOrigin, src->origin, vec3_origin, vec3_origin, MASK_SOLID, topNodeHint );
	if( trace.fraction == 1.0f && !trace.startsolid ) {
		// Consider zero obstruction in this case
		return 0.0f;
	}

	SetupDirectObstructionSamplingProps( src, 3, MAX_DIRECT_OBSTRUCTION_SAMPLES );

	numPassedRays = 0;
	numTestedRays = updateState->directObstructionSamplingProps.numSamples;
	valueIndex = updateState->directObstructionSamplingProps.valueIndex;
	for( unsigned i = 0; i < numTestedRays; i++ ) {
		valueIndex = ( valueIndex + 1 ) % DirectObstructionOffsetsHolder::NUM_VALUES;
		originOffset = directObstructionOffsetsHolder.offsets[ valueIndex ];

		VectorAdd( src->origin, originOffset, testedSourceOrigin );
		S_Trace( &trace, testedListenerOrigin, testedSourceOrigin, vec3_origin, vec3_origin, MASK_SOLID, topNodeHint );
		if( trace.fraction == 1.0f && !trace.startsolid ) {
			numPassedRays++;
		}
	}

	return 1.0f - 0.9f * ( numPassedRays / (float)numTestedRays );
}

Effect *ReverbEffectSampler::TryApply( const ListenerProps &listenerProps, src_t *src, const src_t *tryReusePropsSrc ) {
	EaxReverbEffect *effect = EffectsAllocator::Instance()->NewReverbEffect( src );
	effect->directObstruction = ComputeDirectObstruction( listenerProps, src );
	// We try reuse props only for reverberation effects
	// since reverberation effects sampling is extremely expensive.
	// Moreover, direct obstruction reuse is just not valid,
	// since even a small origin difference completely changes it.
	if( ENV_TryReuseSourceReverbProps( src, tryReusePropsSrc, effect ) ) {
		src->envUpdateState.needsInterpolation = false;
	} else {
		ComputeReverberation( listenerProps, src, effect );
	}
	return effect;
}

float ReverbEffectSampler::GetEmissionRadius() const {
	// Do not even bother casting rays 999999 units ahead for very attenuated sources.
	// However, clamp/normalize the hit distance using the same defined threshold
	float attenuation = src->attenuation;

	if( attenuation <= 1.0f ) {
		return 999999.9f;
	}

	clamp_high( attenuation, 10.0f );
	float distance = 4.0f * REVERB_ENV_DISTANCE_THRESHOLD;
	distance -= 3.5f * Q_Sqrt( attenuation / 10.0f ) * REVERB_ENV_DISTANCE_THRESHOLD;
	return distance;
}

void ReverbEffectSampler::ResetMutableState( const ListenerProps &listenerProps_, src_t *src_, EaxReverbEffect *effect_ ) {
	this->listenerProps = &listenerProps_;
	this->src = src_;
	this->effect = effect_;

	GenericRaycastSampler::ResetMutableState( primaryRayDirs, reflectionPoints, primaryHitDistances, src->origin );

	VectorCopy( listenerProps_.origin, testedListenerOrigin );
	testedListenerOrigin[2] += 18.0f;
}

void ReverbEffectSampler::ComputeReverberation( const ListenerProps &listenerProps_,
												src_t *src_,
												EaxReverbEffect *effect_ ) {
	ResetMutableState( listenerProps_, src_, effect_ );

	numPrimaryRays = GetNumSamplesForCurrentQuality( 16, MAX_REVERB_PRIMARY_RAY_SAMPLES );

	SetupPrimaryRayDirs();

	EmitPrimaryRays();

	if( !numPrimaryHits ) {
		SetMinimalReverbProps();
		return;
	}

	ProcessPrimaryEmissionResults();
	EmitSecondaryRays();
}

void ReverbEffectSampler::SetupPrimaryRayDirs() {
	assert( numPrimaryRays );

	SetupSamplingRayDirs( primaryRayDirs, numPrimaryRays );
}

void ReverbEffectSampler::ProcessPrimaryEmissionResults() {
	// Instead of trying to compute these factors every sampling call,
	// reuse pre-computed properties of CM map leafs that briefly resemble rooms/convex volumes.
	assert( src->envUpdateState.leafNum >= 0 );

	const auto *const leafPropsCache = LeafPropsCache::Instance();
	const LeafProps &leafProps = leafPropsCache->GetPropsForLeaf( src->envUpdateState.leafNum );

	const float roomSizeFactor = leafProps.getRoomSizeFactor();
	const float metallnessFactor = leafProps.getMetallnessFactor();
	const float skyFactor = leafProps.getSkyFactor();

	// The density must be within [0.0, 1.0] range.
	// Lower the density is, more tinny and metallic a sound appear.
	effect->density = 1.0f - metallnessFactor;

	// The diffusion must be within [0.0, 1.0] range.
	// Low values feel like a modulation and like a quickly panning echo.
	effect->diffusion = 1.0f;
	// Apply a non-standard diffusion only for a huge outdoor environment.
	effect->diffusion -= roomSizeFactor * Q_Sqrt( skyFactor );

	// The decay time should be within [0.1, 20.0] range.
	// A reverberation starts being really heard from values greater than 0.5.
	constexpr auto maxDecay = EaxReverbEffect::MAX_REVERB_DECAY;
	// This is a minimal decay chosen for tiny rooms
	constexpr auto minDecay = 0.6f;
	// This is an additional decay time that is added on an outdoor environment
	constexpr auto skyExtraDecay = 1.0f;
	static_assert( maxDecay > minDecay + skyExtraDecay, "" );

	effect->decayTime = minDecay + ( maxDecay - minDecay - skyExtraDecay ) * roomSizeFactor + skyExtraDecay * skyFactor;
	assert( effect->decayTime <= maxDecay );

	// The late reverberation gain affects effect strength a lot.
	// It must be within [0.0, 10.0] range.
	// We should really limit us to values below 1.0, preferably even closer to 0.1..0.2 for a generic environment.
	// Higher values can feel "right" for a "cinematic" scene, but are really annoying for an actual in-game experience.

	// This is a base value for huge spaces
	const float distantGain = 0.1f;
	// Let's try doing "energy preservation": an increased decay should lead to decreased gain.
	// These formulae do not have any theoretical foundations but feels good.
	// The `decayFrac` is close to 0 for tiny rooms/short decay and is close to 1 for huge rooms/long decay
	const float decayFrac = ( effect->decayTime - minDecay ) / ( maxDecay - minDecay );
	// This gain factor should be close to 1 for tiny rooms and quickly fall down to almost 0
	const float gainFactorForRoomSize = std::pow( 1.0f - decayFrac, 5.0f );
	effect->lateReverbGain = distantGain + 0.6f * gainFactorForRoomSize;

	const int listenerLeafNum = listenerProps->GetLeafNum();
	const auto *const table = PropagationTable::Instance();
	if( table->HasDirectPath( src->envUpdateState.leafNum, listenerLeafNum ) ) {
		effect->indirectAttenuation = 0.0f;
	} else {
		[[maybe_unused]] vec3_t tableDir;
		float tableDistance = 1.0f;
		if( table->GetIndirectPathProps( src->envUpdateState.leafNum, listenerLeafNum, tableDir, &tableDistance ) ) {
			// The table stores a distance up to 2^16 and clamps everything above.
			// Putting a reference limit at 2^16 does not feel good so we clamp to a value 4x lower.
			constexpr auto maxDistance = (float)( 1u << 14u );
			constexpr auto invMaxDistance = 1.0f / maxDistance;
			const float frac = std::min( maxDistance, tableDistance ) * invMaxDistance;
			assert( frac >= 0.0f && frac <= 1.0f );
			effect->indirectAttenuation = Q_Sqrt( frac );
		} else {
			effect->indirectAttenuation = 1.0f;
		}
	}

	// Consider the indirect path distance having the same effect as the room size.

	// Must be within [0.0 ... 0.1] range
	effect->lateReverbDelay = 0.011f + 0.088f * std::max( effect->indirectAttenuation, roomSizeFactor );
	// Must be within [0.0, 0.3] range.
	effect->reflectionsDelay = 0.007f + 0.29f *
		std::max( effect->indirectAttenuation, ( 0.5f + 0.5f * skyFactor ) * roomSizeFactor );

	// 0.5 is the value of a neutral surface
	const float smoothness = leafProps.getSmoothnessFactor();
	if( smoothness <= 0.5f ) {
		// [1000, 2500]
		effect->hfReference = 1000.0f + ( 2.0f * smoothness ) * 1500.0f;
	} else {
		// [2500, 5000]
		effect->hfReference = 2500.0f + ( 2.0f * ( smoothness - 0.5f ) ) * 2500.0f;
	}

	effect->gainHf = ( 0.4f + 0.4f * metallnessFactor );
}

void ReverbEffectSampler::SetMinimalReverbProps() {
	effect->density = 1.0f;
	effect->diffusion = 1.0f;
	effect->decayTime = 0.60f;
	effect->reflectionsDelay = 0.007f;
	effect->lateReverbGain = 0.15f;
	effect->lateReverbDelay = 0.011f;
	effect->gainHf = 0.0f;
	effect->hfReference = 5000.0f;
}

void ReverbEffectSampler::EmitSecondaryRays() {
	int listenerLeafNum = listenerProps->GetLeafNum();

	auto *const panningUpdateState = &src->panningUpdateState;
	panningUpdateState->numPrimaryRays = numPrimaryRays;

	trace_t trace;

	unsigned numPassedSecondaryRays = 0;
	panningUpdateState->numPassedSecondaryRays = 0;
	for( unsigned i = 0; i < numPrimaryHits; i++ ) {
		// Cut off by PVS system early, we are not interested in actual ray hit points contrary to the primary emission.
		if( !S_LeafsInPVS( listenerLeafNum, S_PointLeafNum( reflectionPoints[i] ) ) ) {
			continue;
		}

		S_Trace( &trace, reflectionPoints[i], testedListenerOrigin, vec3_origin, vec3_origin, MASK_SOLID );
		if( trace.fraction == 1.0f && !trace.startsolid ) {
			numPassedSecondaryRays++;
			float *savedPoint = panningUpdateState->reflectionPoints[panningUpdateState->numPassedSecondaryRays++];
			VectorCopy( reflectionPoints[i], savedPoint );
		}
	}

	if( numPrimaryHits ) {
		float frac = numPassedSecondaryRays / (float)numPrimaryHits;
		// The secondary rays obstruction is complement to the `frac`
		effect->secondaryRaysObstruction = 1.0f - frac;
	} else {
		// Set minimal feasible values
		effect->secondaryRaysObstruction = 1.0f;
	}
}