#include "snd_env_effects.h"
#include "snd_effect_sampler.h"
#include "snd_propagation.h"

#include <algorithm>
#include <limits>
#include <cmath>

void Effect::CheckCurrentlyBoundEffect( src_t *src ) {
	ALint effectType;

	// We limit every source to have only a single effect.
	// This is required to comply with the runtime effects count restriction.
	// If the effect type has been changed, we have to delete an existing effect.
	alGetEffecti( src->effect, AL_EFFECT_TYPE, &effectType );
	if( this->type == effectType ) {
		return;
	}

	// Detach the slot from the source
	alSource3i( src->source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	// Detach the effect from the slot
	alAuxiliaryEffectSloti( src->effectSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );

	// TODO: Can we reuse the effect?
	alDeleteEffects( 1, &src->effect );

	IntiallySetupEffect( src );
}

void Effect::IntiallySetupEffect( src_t *src ) {
	alGenEffects( 1, &src->effect );
	alEffecti( src->effect, AL_EFFECT_TYPE, this->type );
}

float Effect::GetSourceGain( src_s *src ) const {
	return src->fvol * src->volumeVar->value;
}

void Effect::AdjustGain( src_t *src ) const {
	alSourcef( src->source, AL_GAIN, GetSourceGain( src ) );
}

void Effect::AttachEffect( src_t *src ) {
	// Set gain in any case (useful if the "attenuate on obstruction" flag has been turned off).
	AdjustGain( src );

	// Attach the filter to the source
	alSourcei( src->source, AL_DIRECT_FILTER, src->directFilter );
	// Attach the effect to the slot
	alAuxiliaryEffectSloti( src->effectSlot, AL_EFFECTSLOT_EFFECT, src->effect );
	// Feed the slot from the source
	alSource3i( src->source, AL_AUXILIARY_SEND_FILTER, src->effectSlot, 0, AL_FILTER_NULL );
}

void UnderwaterFlangerEffect::IntiallySetupEffect( src_t *src ) {
	Effect::IntiallySetupEffect( src );
	// This is the only place where the flanger gets tweaked
	alEffectf( src->effect, AL_FLANGER_DEPTH, 0.5f );
	alEffectf( src->effect, AL_FLANGER_FEEDBACK, -0.4f );
}

float UnderwaterFlangerEffect::GetSourceGain( src_t *src ) const {
	float gain = src->fvol * src->volumeVar->value;
	// Lower gain significantly if there is a medium transition
	// (if the listener is not in liquid and the source is, and vice versa)
	if( hasMediumTransition ) {
		gain *= 0.25f;
	}

	// Modify the gain by the direct obstruction factor
	// Lowering the gain by 1/3 on full obstruction is fairly sufficient (its not linearly perceived)
	gain *= 1.0f - 0.33f * directObstruction;
	assert( gain >= 0.0f && gain <= 1.0f );
	return gain;
}

void UnderwaterFlangerEffect::BindOrUpdate( src_t *src ) {
	CheckCurrentlyBoundEffect( src );

	alFilterf( src->directFilter, AL_LOWPASS_GAINHF, 0.0f );

	AttachEffect( src );
}

float EaxReverbEffect::GetAttnFracBasedOnSampledEnvironment() const {
	if( directObstruction == 0.0f ) {
		return 0.0f;
	}

	assert( directObstruction >= 0.0f && directObstruction <= 1.0f );
	assert( secondaryRaysObstruction >= 0.0f && secondaryRaysObstruction <= 1.0f );
	assert( indirectAttenuation >= 0.0f && indirectAttenuation <= 1.0f );

	// Both partial obstruction factors are within [0, 1] range, so we can get an average
	const float obstructionFrac =  0.5f * ( this->directObstruction + this->secondaryRaysObstruction );
	assert( obstructionFrac >= 0.0f && obstructionFrac <= 1.0f );

	const float attenuation = std::max( indirectAttenuation, 0.7f * obstructionFrac );
	assert( attenuation >= 0.0f && attenuation <= 1.0f );
	return attenuation;
}

float EaxReverbEffect::GetSourceGain( src_t *src ) const {
	const float attenuation = GetAttnFracBasedOnSampledEnvironment();
	const float sourceGainFrac = ( 1.0f - attenuation );
	assert( sourceGainFrac >= 0.0f && sourceGainFrac <= 1.0f );
	const float result = ( src->fvol * src->volumeVar->value ) * sourceGainFrac;
	assert( result >= 0.0f && result <= 1.0f );
	return result;
}

void EaxReverbEffect::BindOrUpdate( src_t *src ) {
	CheckCurrentlyBoundEffect( src );

	const float attenuation = GetAttnFracBasedOnSampledEnvironment();
	const float filterHfGainFrac = 0.1f + 0.9f * ( 1.0f - attenuation );
	assert( filterHfGainFrac >= 0.1f && filterHfGainFrac <= 1.0f );
	const float reverbHfGainFrac = 0.5f + 0.5f * ( 1.0f - attenuation );
	assert( reverbHfGainFrac >= 0.5f && reverbHfGainFrac <= 1.0f );

	alEffectf( src->effect, AL_EAXREVERB_DENSITY, this->density );
	alEffectf( src->effect, AL_EAXREVERB_DIFFUSION, this->diffusion );
	alEffectf( src->effect, AL_EAXREVERB_GAINHF, this->gainHf * reverbHfGainFrac );
	alEffectf( src->effect, AL_EAXREVERB_DECAY_TIME, this->decayTime );
	alEffectf( src->effect, AL_EAXREVERB_REFLECTIONS_DELAY, this->reflectionsDelay );
	alEffectf( src->effect, AL_EAXREVERB_LATE_REVERB_GAIN, this->lateReverbGain );
	alEffectf( src->effect, AL_EAXREVERB_LATE_REVERB_DELAY, this->lateReverbDelay );

	alEffectf( src->effect, AL_EAXREVERB_HFREFERENCE, this->hfReference );

	alFilterf( src->directFilter, AL_LOWPASS_GAINHF, filterHfGainFrac );

	AttachEffect( src );
}

void UnderwaterFlangerEffect::InterpolateProps( const Effect *oldOne, int timeDelta ) {
	const auto *that = Cast<UnderwaterFlangerEffect *>( oldOne );
	if( !that ) {
		return;
	}

	const Interpolator interpolator( timeDelta );
	directObstruction = interpolator( directObstruction, that->directObstruction, 0.0f, 1.0f );
}

bool EaxReverbEffect::ShouldKeepLingering( float sourceQualityHint, int64_t millisNow ) const {
	if( sourceQualityHint <= 0 ) {
		return false;
	}
	if( millisNow - lastUpdateAt > 200 ) {
		return false;
	}
	clamp_high( sourceQualityHint, 1.0f );
	float factor = 0.5f * sourceQualityHint;
	factor += 0.25f * ( ( 1.0f - directObstruction ) + ( 1.0f - secondaryRaysObstruction ) );
	assert( factor >= 0.0f && factor <= 1.0f );
	return distanceAtLastUpdate < 192.0f + 768.0f * factor;
}

void EaxReverbEffect::InterpolateProps( const Effect *oldOne, int timeDelta ) {
	const auto *that = Cast<EaxReverbEffect *>( oldOne );
	if( !that ) {
		return;
	}

	Interpolator interpolator( timeDelta );
	directObstruction = interpolator( directObstruction, that->directObstruction, 0.0f, 1.0f );
	density = interpolator( density, that->density, 0.0f, 1.0f );
	diffusion = interpolator( diffusion, that->diffusion, 0.0f, 1.0f );
	gainHf = interpolator( gainHf, that->gainHf, 0.0f, 1.0f );
	decayTime = interpolator( decayTime, that->decayTime, 0.1f, 20.0f );
	reflectionsDelay = interpolator( reflectionsDelay, that->reflectionsDelay, 0.0f, 0.3f );
	lateReverbGain = interpolator( lateReverbGain, that->lateReverbGain, 0.0f, 10.0f );
	lateReverbDelay = interpolator( lateReverbDelay, that->lateReverbDelay, 0.0f, 0.1f );
	secondaryRaysObstruction = interpolator( secondaryRaysObstruction, that->secondaryRaysObstruction, 0.0f, 1.0f );
	hfReference = interpolator( hfReference, that->hfReference, 1000.0f, 20000.0f );
	indirectAttenuation = interpolator( indirectAttenuation, that->indirectAttenuation, 0.0f, 1.0f );
}

void EaxReverbEffect::CopyReverbProps( const EaxReverbEffect *that ) {
	// Avoid doing memcpy... This is not a POD type
	density = that->density;
	diffusion = that->diffusion;
	gainHf = that->gainHf;
	decayTime = that->decayTime;
	reflectionsDelay = that->reflectionsDelay;
	lateReverbGain = that->lateReverbGain;
	lateReverbDelay = that->lateReverbDelay;
	secondaryRaysObstruction = that->secondaryRaysObstruction;
	hfReference = that->hfReference;
	indirectAttenuation = that->indirectAttenuation;
}

void EaxReverbEffect::UpdatePanning( src_s *src, const vec3_t listenerOrigin, const mat3_t listenerAxes ) {
	const auto *updateState = &src->panningUpdateState;

	// Unfortunately we have to recompute directions every panning update
	// as the source might have moved and we update panning much more frequently than emission points
	// (TODO: use cached results for non-moving sources?)
	vec3_t reflectionDirs[PanningUpdateState::MAX_POINTS];
	unsigned numReflectionDirs = 0;

	// A weighted sum of directions. Will be used for reflections panning.
	vec3_t reverbPanDir = { 0, 0, 0 };
	for( unsigned i = 0; i < updateState->numPassedSecondaryRays; ++i ) {
		float *dir = &reflectionDirs[numReflectionDirs][0];
		VectorSubtract( listenerOrigin, src->panningUpdateState.reflectionPoints[i], dir );
		float squareDistance = VectorLengthSquared( dir );
		// Do not even take into account directions that have very short segments
		if( squareDistance < 72.0f * 72.0f ) {
			continue;
		}

		numReflectionDirs++;
		const float distance = std::sqrt( squareDistance );
		VectorScale( dir, 1.0f / distance, dir );
		// Store the distance as the 4-th vector component

		// Note: let's apply a factor giving far reflection points direction greater priority.
		// Otherwise the reverb is often panned to a nearest wall and that's totally wrong.
		float factor = 0.3f + 0.7f * ( distance / REVERB_ENV_DISTANCE_THRESHOLD );
		VectorMA( reverbPanDir, factor, dir, reverbPanDir );
	}

	if( numReflectionDirs ) {
		VectorNormalize( reverbPanDir );
	}

	// "If there is an active EaxReverbEffect, setting source origin/velocity is delegated to it".
	UpdateDelegatedSpatialization( src, listenerOrigin );

	vec3_t basePan;
	// Convert to "speakers" coordinate system
	basePan[0] = -DotProduct( reverbPanDir, &listenerAxes[AXIS_RIGHT] );
	// Not sure about "minus" sign in this case...
	// We need something like 9.1 sound system (that has channels distinction in height) to test that
	basePan[1] = -DotProduct( reverbPanDir, &listenerAxes[AXIS_UP] );
	basePan[2] = -DotProduct( reverbPanDir, &listenerAxes[AXIS_FORWARD] );

	float reflectionsPanScale;
	float lateReverbPanScale;
	// Let late reverb be more focused for huge/vast environments
	// Do not pan early reverb for own sounds.
	// Pan early reverb much less for own sounds.
	const float decayFrac = std::min( 1.0f, decayTime / MAX_REVERB_DECAY );
	// If the sound is relative to the listener, lower the panning
	if( src->attenuation == 1.0f ) {
		lateReverbPanScale = 0.25f * decayFrac;
		reflectionsPanScale = 0.0f;
	} else {
		lateReverbPanScale = 0.5f * decayFrac;
		reflectionsPanScale = 0.5f * lateReverbPanScale;
	}

	vec3_t reflectionsPan, lateReverbPan;
	VectorCopy( basePan, reflectionsPan );
	VectorScale( reflectionsPan, reflectionsPanScale, reflectionsPan );
	VectorCopy( basePan, lateReverbPan );
	VectorScale( lateReverbPan, lateReverbPanScale, lateReverbPan );

	alEffectfv( src->effect, AL_EAXREVERB_REFLECTIONS_PAN, reflectionsPan );
	alEffectfv( src->effect, AL_EAXREVERB_LATE_REVERB_PAN, lateReverbPan );
}

void EaxReverbEffect::UpdateDelegatedSpatialization( struct src_s *src, const vec3_t listenerOrigin ) {
	if( src->attenuation == ATTN_NONE ) {
		// It MUST already be a relative sound
#ifndef PUBLIC_BUILD
		ALint value;
		alGetSourcei( src->source, AL_SOURCE_RELATIVE, &value );
		assert( value == AL_TRUE );
#endif
		return;
	}

	alSourcei( src->source, AL_SOURCE_RELATIVE, AL_FALSE );

	const float *sourceOrigin = src->origin;
	// Setting effect panning vectors is not sufficient for "realistic" obstruction,
	// as the dry path is still propagates like if there were no obstacles and walls.
	// We try modifying the source origin as well to simulate sound propagation.
	// These conditions must be met:
	// 1) the direct path is fully obstructed
	// 2) there is a definite propagation path
	if( directObstruction == 1.0f ) {
		// Provide a fake origin for the source that is at the same distance
		// as the real origin and is aligned to the sound propagation "window"
		// TODO: Precache at least the listener leaf for this sound backend update frame
		if( const int listenerLeaf = S_PointLeafNum( listenerOrigin ) ) {
			if( const int srcLeaf = S_PointLeafNum( src->origin ) ) {
				vec3_t dir;
				float distance;
				if( PropagationTable::Instance()->GetIndirectPathProps( srcLeaf, listenerLeaf, dir, &distance ) ) {
					// The table stores distance using this granularity, so it might be zero
					// for very close leaves. Adding an extra distance won't harm
					// (even if the indirect path length is already larger than the straight euclidean distance).
					distance += 256.0f;
					// Feels better with this multiplier
					distance *= 1.15f;
					// Negate the vector scale multiplier as the dir is an sound influx dir to the listener
					// and we want to shift the origin along the line of the dir but from the listener
					VectorScale( dir, -distance, tmpSourceOrigin );
					// Shift the listener origin in `dir` direction for `distance` units
					VectorAdd( listenerOrigin, tmpSourceOrigin, tmpSourceOrigin );
					// Use the shifted origin as a fake position in the world-space for the source
					sourceOrigin = tmpSourceOrigin;
				}
			}
		}
	}

	alSourcefv( src->source, AL_POSITION, sourceOrigin );
	// The velocity is kept untouched for now.
	alSourcefv( src->source, AL_VELOCITY, src->velocity );
}