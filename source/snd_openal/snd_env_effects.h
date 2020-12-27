#ifndef QFUSION_SND_ENV_EFFECTS_H
#define QFUSION_SND_ENV_EFFECTS_H

#include "snd_local.h"

struct src_s;

static constexpr float REVERB_ENV_DISTANCE_THRESHOLD = 2048.0f + 256.0f;

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

	virtual float GetMasterGain( src_s *src ) const;

	void AttachEffect( src_s *src );
};

class UnderwaterFlangerEffect final: public Effect {
	void IntiallySetupEffect( src_s *src ) override;
	float GetMasterGain( src_s *src ) const override;
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

class ReverbEffect: public Effect {
	float GetMasterGain( struct src_s *src ) const final;
public:
	static constexpr float MAX_REVERB_DECAY = 5.6f + 0.001f;
protected:
	void InterpolateCommonReverbProps( const Interpolator &interpolator, const ReverbEffect *that );
public:
	explicit ReverbEffect( ALint type_ ): Effect( type_ ) {}

	// A regular direct obstruction (same as for the flanger effect)
	float directObstruction;

	float density;              // [0.0 ... 1.0]    default 1.0
	float diffusion;            // [0.0 ... 1.0]    default 1.0
	float gain;                 // [0.0 ... 1.0]    default 0.32
	float gainHf;               // [0.0 ... 1.0]    default 0.89
	float decayTime;            // [0.1 ... 20.0]   default 1.49
	float reflectionsGain;      // [0.0 ... 3.16]   default 0.05
	float reflectionsDelay;     // [0.0 ... 0.3]    default 0.007
	float lateReverbGain;       // [0.0 ... 10.0]   default 1.26
	float lateReverbDelay;      // [0.0 ... 0.1]    default 0.011

	// An intermediate of the reverb sampling algorithm, useful for gain adjustment
	float secondaryRaysObstruction;

	virtual void ReusePreset( const EfxPresetEntry *presetHandle );

	virtual void CopyReverbProps( const ReverbEffect *that );

	unsigned GetLingeringTimeout() const override {
		return (unsigned)( decayTime * 1000 + 50 );
	}

	bool ShouldKeepLingering( float sourceQualityHint, int64_t millisNow ) const override;
};

class StandardReverbEffect final: public ReverbEffect {
	friend class ReverbEffectSampler;
public:
	StandardReverbEffect(): ReverbEffect( AL_EFFECT_REVERB ) {}

	void BindOrUpdate( struct src_s *src ) override;
	void InterpolateProps( const Effect *oldOne, int timeDelta ) override;
};

class EaxReverbEffect final: public ReverbEffect {
	friend class EfxPresetsHolder;
	friend class ReverbEffectSampler;

	void UpdateDelegatedSpatialization( struct src_s *src, const vec3_t listenerOrigin );

	vec3_t tmpSourceOrigin { 0, 0, 0 };
public:
	EaxReverbEffect(): ReverbEffect( AL_EFFECT_EAXREVERB ) {}

	float hfReference; // [1000 ... 20000]  default 5000

	float echoTime;   // [0.075 ... 0.25]  default 0.25
	float echoDepth;  // [0.0   ... 1.0]   default 0.0

	void ReusePreset( const EfxPresetEntry *presetHandle ) override;

	void BindOrUpdate( struct src_s *src ) override;
	void InterpolateProps( const Effect *oldOne, int timeDelta ) override;

	void CopyReverbProps( const ReverbEffect *that ) override;

	void UpdatePanning( src_s *src, const vec3_t listenerOrigin, const mat3_t listenerAxes ) override;
};

#endif
