#include "efxpresetsregistry.h"
#include "snd_local.h"

// TODO: Switch to using OpenAL SOFT headers across the entire codebase?
#include "../../third-party/openal-soft/include/AL/efx-presets.h"

EfxPresetsRegistry EfxPresetsRegistry::s_instance;

// This is a common supertype for all presets that are declared globally
// and link themselves to the global presets cache
// (so we do not have to maintain an array of presets and its length manually)
struct EfxPresetEntry {
	const wsw::HashedStringView name;
	EfxPresetEntry *nextInHashBin { nullptr };
	EfxReverbProps props { EfxReverbProps::NoInit };

	explicit EfxPresetEntry( const char *presetMacroName, EFXEAXREVERBPROPERTIES &&eaxProps ) noexcept
		: name( presetMacroName ) {
		const auto binIndex = name.getHash() % EfxPresetsRegistry::kNumHashBins;
		this->nextInHashBin = EfxPresetsRegistry::s_instance.m_hashBins[binIndex];
		EfxPresetsRegistry::s_instance.m_hashBins[binIndex] = this;

		this->props.density             = eaxProps.flDensity;
		this->props.diffusion           = eaxProps.flDiffusion;
		this->props.gain                = eaxProps.flGain;
		this->props.gainHf              = eaxProps.flGainHF;
		this->props.gainLf              = eaxProps.flGainLF;
		this->props.decayTime           = eaxProps.flDecayTime;
		this->props.decayHfRatio        = eaxProps.flDecayHFRatio;
		this->props.decayLfRatio        = eaxProps.flDecayLFRatio;
		this->props.reflectionsGain     = eaxProps.flReflectionsGain;
		this->props.reflectionsDelay    = eaxProps.flReflectionsDelay;
		this->props.lateReverbGain      = eaxProps.flLateReverbGain;
		this->props.lateReverbDelay     = eaxProps.flLateReverbDelay;
		this->props.echoTime            = eaxProps.flEchoTime;
		this->props.echoDepth           = eaxProps.flEchoDepth;
		this->props.modulationTime      = eaxProps.flModulationTime;
		this->props.modulationDepth     = eaxProps.flModulationDepth;
		this->props.airAbsorptionGainHf = eaxProps.flAirAbsorptionGainHF;
		this->props.hfReference         = eaxProps.flHFReference;
		this->props.lfReference         = eaxProps.flLFReference;
	}
};

auto EfxPresetsRegistry::findByName( const wsw::StringView &name ) const -> const EfxReverbProps * {
	return findByName( wsw::HashedStringView( name ) );
}

auto EfxPresetsRegistry::findByName( const wsw::HashedStringView &name ) const -> const EfxReverbProps * {
	const auto binIndex = name.getHash() % kNumHashBins;
	for( EfxPresetEntry *entry = m_hashBins[binIndex]; entry; entry = entry->nextInHashBin ) {
		if( entry->name.equalsIgnoreCase( name ) ) {
			return &entry->props;
		}
	}
	return nullptr;
}

#define DEFINE_PRESET( presetMacroName ) \
	static EfxPresetEntry presetMacroName##_presetEntry( #presetMacroName, EFX_REVERB_PRESET_##presetMacroName );

//$cat efx-presets.h | grep EFX_R | sed -e 's/EFX_REVERB_PRESET_//g' | awk '{print(sprintf("DEFINE_PRESET( %s );", $2));}'

DEFINE_PRESET( GENERIC );
DEFINE_PRESET( PADDEDCELL );
DEFINE_PRESET( ROOM );
DEFINE_PRESET( BATHROOM );
DEFINE_PRESET( LIVINGROOM );
DEFINE_PRESET( STONEROOM );
DEFINE_PRESET( AUDITORIUM );
DEFINE_PRESET( CONCERTHALL );
DEFINE_PRESET( CAVE );
DEFINE_PRESET( ARENA );
DEFINE_PRESET( HANGAR );
DEFINE_PRESET( CARPETEDHALLWAY );
DEFINE_PRESET( HALLWAY );
DEFINE_PRESET( STONECORRIDOR );
DEFINE_PRESET( ALLEY );
DEFINE_PRESET( FOREST );
DEFINE_PRESET( CITY );
DEFINE_PRESET( MOUNTAINS );
DEFINE_PRESET( QUARRY );
DEFINE_PRESET( PLAIN );
DEFINE_PRESET( PARKINGLOT );
DEFINE_PRESET( SEWERPIPE );
DEFINE_PRESET( UNDERWATER );
DEFINE_PRESET( DRUGGED );
DEFINE_PRESET( DIZZY );
DEFINE_PRESET( PSYCHOTIC );
DEFINE_PRESET( CASTLE_SMALLROOM );
DEFINE_PRESET( CASTLE_SHORTPASSAGE );
DEFINE_PRESET( CASTLE_MEDIUMROOM );
DEFINE_PRESET( CASTLE_LARGEROOM );
DEFINE_PRESET( CASTLE_LONGPASSAGE );
DEFINE_PRESET( CASTLE_HALL );
DEFINE_PRESET( CASTLE_CUPBOARD );
DEFINE_PRESET( CASTLE_COURTYARD );
DEFINE_PRESET( CASTLE_ALCOVE );
DEFINE_PRESET( FACTORY_SMALLROOM );
DEFINE_PRESET( FACTORY_SHORTPASSAGE );
DEFINE_PRESET( FACTORY_MEDIUMROOM );
DEFINE_PRESET( FACTORY_LARGEROOM );
DEFINE_PRESET( FACTORY_LONGPASSAGE );
DEFINE_PRESET( FACTORY_HALL );
DEFINE_PRESET( FACTORY_CUPBOARD );
DEFINE_PRESET( FACTORY_COURTYARD );
DEFINE_PRESET( FACTORY_ALCOVE );
DEFINE_PRESET( ICEPALACE_SMALLROOM );
DEFINE_PRESET( ICEPALACE_SHORTPASSAGE );
DEFINE_PRESET( ICEPALACE_MEDIUMROOM );
DEFINE_PRESET( ICEPALACE_LARGEROOM );
DEFINE_PRESET( ICEPALACE_LONGPASSAGE );
DEFINE_PRESET( ICEPALACE_HALL );
DEFINE_PRESET( ICEPALACE_CUPBOARD );
DEFINE_PRESET( ICEPALACE_COURTYARD );
DEFINE_PRESET( ICEPALACE_ALCOVE );
DEFINE_PRESET( SPACESTATION_SMALLROOM );
DEFINE_PRESET( SPACESTATION_SHORTPASSAGE );
DEFINE_PRESET( SPACESTATION_MEDIUMROOM );
DEFINE_PRESET( SPACESTATION_LARGEROOM );
DEFINE_PRESET( SPACESTATION_LONGPASSAGE );
DEFINE_PRESET( SPACESTATION_HALL );
DEFINE_PRESET( SPACESTATION_CUPBOARD );
DEFINE_PRESET( SPACESTATION_ALCOVE );
DEFINE_PRESET( WOODEN_SMALLROOM );
DEFINE_PRESET( WOODEN_SHORTPASSAGE );
DEFINE_PRESET( WOODEN_MEDIUMROOM );
DEFINE_PRESET( WOODEN_LARGEROOM );
DEFINE_PRESET( WOODEN_LONGPASSAGE );
DEFINE_PRESET( WOODEN_HALL );
DEFINE_PRESET( WOODEN_CUPBOARD );
DEFINE_PRESET( WOODEN_COURTYARD );
DEFINE_PRESET( WOODEN_ALCOVE );
DEFINE_PRESET( SPORT_EMPTYSTADIUM );
DEFINE_PRESET( SPORT_SQUASHCOURT );
DEFINE_PRESET( SPORT_SMALLSWIMMINGPOOL );
DEFINE_PRESET( SPORT_LARGESWIMMINGPOOL );
DEFINE_PRESET( SPORT_GYMNASIUM );
DEFINE_PRESET( SPORT_FULLSTADIUM );
DEFINE_PRESET( SPORT_STADIUMTANNOY );
DEFINE_PRESET( PREFAB_WORKSHOP );
DEFINE_PRESET( PREFAB_SCHOOLROOM );
DEFINE_PRESET( PREFAB_PRACTISEROOM );
DEFINE_PRESET( PREFAB_OUTHOUSE );
DEFINE_PRESET( PREFAB_CARAVAN );
DEFINE_PRESET( DOME_TOMB );
DEFINE_PRESET( PIPE_SMALL );
DEFINE_PRESET( DOME_SAINTPAULS );
DEFINE_PRESET( PIPE_LONGTHIN );
DEFINE_PRESET( PIPE_LARGE );
DEFINE_PRESET( PIPE_RESONANT );
DEFINE_PRESET( OUTDOORS_BACKYARD );
DEFINE_PRESET( OUTDOORS_ROLLINGPLAINS );
DEFINE_PRESET( OUTDOORS_DEEPCANYON );
DEFINE_PRESET( OUTDOORS_CREEK );
DEFINE_PRESET( OUTDOORS_VALLEY );
DEFINE_PRESET( MOOD_HEAVEN );
DEFINE_PRESET( MOOD_HELL );
DEFINE_PRESET( MOOD_MEMORY );
DEFINE_PRESET( DRIVING_COMMENTATOR );
DEFINE_PRESET( DRIVING_PITGARAGE );
DEFINE_PRESET( DRIVING_INCAR_RACER );
DEFINE_PRESET( DRIVING_INCAR_SPORTS );
DEFINE_PRESET( DRIVING_INCAR_LUXURY );
DEFINE_PRESET( DRIVING_FULLGRANDSTAND );
DEFINE_PRESET( DRIVING_EMPTYGRANDSTAND );
DEFINE_PRESET( DRIVING_TUNNEL );
DEFINE_PRESET( CITY_STREETS );
DEFINE_PRESET( CITY_SUBWAY );
DEFINE_PRESET( CITY_MUSEUM );
DEFINE_PRESET( CITY_LIBRARY );
DEFINE_PRESET( CITY_UNDERPASS );
DEFINE_PRESET( CITY_ABANDONED );
DEFINE_PRESET( DUSTYROOM );
DEFINE_PRESET( CHAPEL );
DEFINE_PRESET( SMALLWATERROOM );

static_assert( sizeof( EfxReverbProps ) % sizeof( float ) == 0 );
// Don't compute it by expression, count manually so the check is more robust wrt changes.
static constexpr unsigned kNumFloatFields = 19;
// All fields are floats, with the single exception that is handled separately.
static_assert( sizeof( EfxReverbProps ) / sizeof( float ) == kNumFloatFields + 1 );

void lerpReverbProps( const EfxReverbProps *from, float frac, const EfxReverbProps *to, EfxReverbProps *dest ) {
	assert( frac >= 0.0f && frac <= 1.0f );

	auto *const destFieldData       = (float *)dest;
	const auto *const fromFieldData = (const float *)from;
	const auto *const toFieldData   = (const float *)to;

	const float complementFrac = 1.0f - frac;

	unsigned fieldIndex = 0;
	do {
		destFieldData[fieldIndex] = complementFrac * fromFieldData[fieldIndex] + frac * toFieldData[fieldIndex];
	} while( ++fieldIndex < kNumFloatFields );

	// Choose AL_FALSE over AL_TRUE for decayHfLimit
	static_assert( AL_FALSE < AL_TRUE );
	dest->decayHfLimit = wsw::min( from->decayHfLimit, to->decayHfLimit );
}

void mixReverbProps( const EfxReverbProps **begin, const EfxReverbProps **end, EfxReverbProps *dest ) {
	assert( begin < end );
	const float normalizer = Q_Rcp( (float)( end - begin ) );

	std::memset( (void *)dest, 0, sizeof( *dest ) );

	dest->decayHfLimit = ( *begin )->decayHfLimit;
	const EfxReverbProps **ppPreset = begin;
	do {
		assert( *ppPreset != dest );
		auto *const destFieldData      = (float *)dest;
		const auto *const srcFieldData = (const float *)*ppPreset;

		unsigned fieldIndex = 0;
		do {
			destFieldData[fieldIndex] += srcFieldData[fieldIndex];
		} while( ++fieldIndex < kNumFloatFields );

		static_assert( AL_FALSE < AL_TRUE );
		dest->decayHfLimit = wsw::min( dest->decayHfLimit, ( *ppPreset )->decayHfLimit );
	} while( ++ppPreset < end );

	unsigned fieldIndex = 0;
	do {
		( (float *)dest )[fieldIndex] *= normalizer;
	} while( ++fieldIndex < kNumFloatFields );
}
