#ifndef QFUSION_SND_ENV_EFFECTS_H
#define QFUSION_SND_ENV_EFFECTS_H

#include "snd_local.h"

struct src_s;

static constexpr float REVERB_ENV_DISTANCE_THRESHOLD = 3072.0f;

class Effect {
public:
	virtual void BindOrUpdate( src_s *src ) = 0;

	virtual void InterpolateProps( const Effect *oldOne, int timeDelta ) {};

	virtual unsigned GetLingeringTimeout() const {
		return 500;
	};

	virtual bool ShouldKeepLingering( float sourceQualityHint, int64_t millisNow ) const {
		return sourceQualityHint > 0;
	};

	virtual void UpdatePanning( src_s *src, const vec3_t listenerOrigin, const mat3_t listenerAxes ) {}

	virtual ~Effect() = default;

	template <typename T> static const T Cast( const Effect *effect ) {
		// We might think of optimizing it in future, that's why the method is favourable over the direct cast
		return dynamic_cast<T>( const_cast<Effect *>( effect ) );
	}

	void AdjustGain( src_s *src ) const;

	// A timestamp of last props update
	int64_t lastUpdateAt { 0 };
	// An distance between emitter and listener at last props update
	float distanceAtLastUpdate { 0.0f };
protected:
	ALint type;

	explicit Effect( ALint type_ ): type( type_ ) {}

	class Interpolator {
		float newWeight, oldWeight;
	public:
		explicit Interpolator( int timeDelta ) {
			assert( timeDelta >= 0 );
			float frac = timeDelta / 175.0f;
			if( frac <= 1.0f ) {
				newWeight = 0.5f + 0.3f * frac;
				oldWeight = 0.5f - 0.3f * frac;
			} else {
				newWeight = 1.0f;
				oldWeight = 0.0f;
			}
		}

		float operator()( float rawNewValue, float oldValue, float mins, float maxs ) const {
			float result = newWeight * ( rawNewValue ) + oldWeight * ( oldValue );
			Q_clamp( result, mins, maxs );
			return result;
		}
	};

	void CheckCurrentlyBoundEffect( src_s *src );
	virtual void IntiallySetupEffect( src_s *src );

	virtual float GetSourceGain( src_s *src ) const;

	void AttachEffect( src_s *src );
};

class UnderwaterFlangerEffect final: public Effect {
	void IntiallySetupEffect( src_s *src ) override;
	float GetSourceGain( src_s *src ) const override;
public:
	UnderwaterFlangerEffect(): Effect( AL_EFFECT_FLANGER ) {}

	float directObstruction;
	bool hasMediumTransition;

	void BindOrUpdate( src_s *src ) override;
	void InterpolateProps( const Effect *oldOne, int timeDelta ) override;

	unsigned GetLingeringTimeout() const override {
		return 500;
	}

	bool ShouldKeepLingering( float sourceQualityHint, int64_t millisNow ) const override {
		return sourceQualityHint > 0;
	}
};

struct EfxPresetEntry;

class EaxReverbEffect final: public Effect {
	friend class ReverbEffectSampler;

	void UpdateDelegatedSpatialization( struct src_s *src, const vec3_t listenerOrigin );

	vec3_t tmpSourceOrigin { 0, 0, 0 };

	float GetAttnFracBasedOnSampledEnvironment() const;

	float GetSourceGain( src_s *src ) const override;
public:
	EaxReverbEffect(): Effect( AL_EFFECT_EAXREVERB ) {}

	float density             = AL_EAXREVERB_DEFAULT_DENSITY;
	float diffusion           = AL_EAXREVERB_DEFAULT_DIFFUSION;
	float gain                = 0.0f;
	float gainLf              = AL_EAXREVERB_DEFAULT_GAINLF;
	float gainHf              = AL_EAXREVERB_DEFAULT_GAINHF;
	float decayTime           = AL_EAXREVERB_DEFAULT_DECAY_TIME;
	float decayHfRatio        = AL_EAXREVERB_DEFAULT_DECAY_HFRATIO;
	float decayLfRatio        = AL_EAXREVERB_DEFAULT_DECAY_LFRATIO;
	float reflectionsGain     = AL_EAXREVERB_DEFAULT_REFLECTIONS_GAIN;
	float reflectionsDelay    = AL_EAXREVERB_DEFAULT_REFLECTIONS_DELAY;
	float lateReverbGain      = AL_EAXREVERB_DEFAULT_LATE_REVERB_GAIN;
	float lateReverbDelay     = AL_EAXREVERB_DEFAULT_LATE_REVERB_DELAY;
	float echoTime            = AL_EAXREVERB_DEFAULT_ECHO_TIME;
	float echoDepth           = AL_EAXREVERB_DEFAULT_ECHO_DEPTH;
	float airAbsorptionGainHf = AL_EAXREVERB_DEFAULT_AIR_ABSORPTION_GAINHF;
	float lfReference         = AL_EAXREVERB_DEFAULT_LFREFERENCE;
	float hfReference         = AL_EAXREVERB_DEFAULT_HFREFERENCE;

	float directObstruction { 0.0f };
	// An intermediate of the reverb sampling algorithm, useful for gain adjustment
	float secondaryRaysObstruction { 0.0f };

	// A custom parameter, [0.0 ... 1.0], 0.0 for directly reachable sources,
	// 1.0 for very distant indirect-reachable or completely unreachable sources.
	float indirectAttenuation { 0.0f };

	unsigned GetLingeringTimeout() const override {
		return (unsigned)( decayTime * 1000 + 50 );
	}

	bool ShouldKeepLingering( float sourceQualityHint, int64_t millisNow ) const override;

	void BindOrUpdate( struct src_s *src ) override;
	void InterpolateProps( const Effect *oldOne, int timeDelta ) override;

	void CopyReverbProps( const EaxReverbEffect *that );

	void UpdatePanning( src_s *src, const vec3_t listenerOrigin, const mat3_t listenerAxes ) override;
};

#endif
