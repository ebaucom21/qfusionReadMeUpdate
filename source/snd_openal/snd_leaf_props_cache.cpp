#include "snd_leaf_props_cache.h"
#include "snd_effect_sampler.h"
#include "snd_computation_host.h"
#include "snd_presets_registry.h"
#include "../qcommon/glob.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/wswstringview.h"
#include "../qcommon/wswstringsplitter.h"
#include "../qcommon/wswstaticstring.h"
#include "../qcommon/wswstdtypes.h"
#include "../qcommon/wswfs.h"

#include <algorithm>
#include <new>
#include <limits>
#include <cstdlib>

using wsw::operator""_asView;

class LeafPropsIOHelper {
protected:
	int leafCounter { 0 };
public:
	virtual ~LeafPropsIOHelper() = default;
};

struct EfxPresetEntry;

class LeafPropsReader final: public CachedComputationReader, public LeafPropsIOHelper {
public:
	using PresetHandle = const EfxPresetEntry *;
private:
	bool ParseLine( char *line, unsigned lineLength, LeafProps *props, PresetHandle *presetHandle );
public:
	explicit LeafPropsReader( const LeafPropsCache *parent_, int fileFlags )
		: CachedComputationReader( parent_, fileFlags, true ) {}

	enum Status {
		OK,
		DONE,
		ERROR
	};

	Status ReadNextProps( LeafProps *props, PresetHandle *presetRef );
};

class LeafPropsWriter final: public CachedComputationWriter, public LeafPropsIOHelper {
public:
	explicit LeafPropsWriter( const LeafPropsCache *parent_ )
		: CachedComputationWriter( parent_ ) {}

	bool WriteProps( const LeafProps &props );
};

LeafPropsReader::Status LeafPropsReader::ReadNextProps( LeafProps *props, PresetHandle *presetHandle ) {
	if( fsResult < 0 ) {
		return ERROR;
	}

	SkipWhiteSpace();
	if( !*dataPtr ) {
		return DONE;
	}

	char *nextLine = strchr( dataPtr, '\n' );
	if( nextLine ) {
		*nextLine = '\0';
	}

	size_t lineLength = nextLine - dataPtr;
	if( lineLength > std::numeric_limits<uint32_t>::max() ) {
		dataPtr = nullptr;
		return ERROR;
	}

	if( !ParseLine( dataPtr, (uint32_t)lineLength, props, presetHandle ) ) {
		dataPtr = nullptr;
		return ERROR;
	}

	if( nextLine ) {
		dataPtr = nextLine + 1;
	} else {
		fileData[0] = '\0';
		dataPtr = fileData;
	}

	leafCounter++;

	return OK;
}

bool LeafPropsReader::ParseLine( char *line, unsigned lineLength, LeafProps *props, PresetHandle *presetHandle ) {
	char *linePtr = line;
	char *endPtr = nullptr;

	// Parsing errors are caught implicitly by the counter comparison below
	const auto num = (int)std::strtol( linePtr, &endPtr, 10 );
	if( !*endPtr || !isspace( *endPtr ) ) {
		return false;
	}

	if( num != leafCounter ) {
		return false;
	}

	// Trim whitespace at start and end. This is mandatory for FindPresetByName() call.

	linePtr = endPtr;
	while( ( linePtr - line < lineLength ) && ::isspace( *linePtr ) ) {
		linePtr++;
	}

	endPtr = line + lineLength;
	while( endPtr > linePtr && ( !*endPtr || ::isspace( *endPtr ) ) ) {
		endPtr--;
	}

	if( endPtr <= linePtr ) {
		return false;
	}

	// Terminate after the character we have stopped at
	*++endPtr = '\0';

	// Save for recovery in case of leaf props scanning failure
	const char *maybePresetName = linePtr;
	int lastPartNum = 0;
	float parts[4];
	for(; lastPartNum < 4; ++lastPartNum ) {
		double v = ::strtod( linePtr, &endPtr );
		if( endPtr == linePtr ) {
			break;
		}
		// The value is numeric but is out of range.
		// It is not a preset for sure, return with failure immediately.
		// The range is [0, 1] for first 4 parts and [1000, 20000] for last 2 parts.
		if( lastPartNum <= 3 ) {
			if( v < 0.0 || v > 1.0 ) {
				return false;
			}
		} else if( v < 1000.0f || v > 20000.0f ) {
			return false;
		}
		if( lastPartNum != 3 ) {
			if( !isspace( *endPtr ) ) {
				return false;
			}
		} else if( *endPtr ) {
			return false;
		}
		parts[lastPartNum] = (float)v;
		linePtr = endPtr + 1;
	}

	if( !lastPartNum ) {
		if( ( *presetHandle = EfxPresetsRegistry::Instance()->FindByName( maybePresetName ) ) ) {
			return true;
		}
		// A verbose reporting of an illegal preset name is important as it is typed by a designer manually
		Com_Printf( S_COLOR_YELLOW "The token `%s` for leaf %d is not a valid preset name\n", maybePresetName, num );
		return false;
	}

	// There should be 4 parts
	if( lastPartNum != 4 ) {
		return false;
	}

	// The HF reference range must be valid
	props->setRoomSizeFactor( parts[0] );
	props->setSkyFactor( parts[1] );
	props->setSmoothnessFactor( parts[2] );
	props->setMetallnessFactor( parts[3] );
	return true;
}

bool LeafPropsWriter::WriteProps( const LeafProps &props ) {
	if( fsResult < 0 ) {
		return false;
	}

	char buffer[MAX_STRING_CHARS];
	int charsPrinted = Q_snprintfz( buffer, sizeof( buffer ),
									"%d %.2f %.2f %.2f %.2f\r\n",
									leafCounter,
									props.getRoomSizeFactor(),
									props.getSkyFactor(),
									props.getSmoothnessFactor(),
									props.getMetallnessFactor());

	int charsWritten = FS_Write( buffer, (size_t)charsPrinted, fd );
	if( charsWritten != charsPrinted ) {
		fsResult = -1;
		return false;
	}

	leafCounter++;
	return true;
}

static SingletonHolder<LeafPropsCache> instanceHolder;

LeafPropsCache *LeafPropsCache::Instance() {
	return instanceHolder.Instance();
}

void LeafPropsCache::Init() {
	instanceHolder.Init();
}

void LeafPropsCache::Shutdown() {
	instanceHolder.Shutdown();
}

void LeafPropsCache::ResetExistingState() {
	if( leafProps ) {
		Q_free( leafProps );
	}
	if( leafPresets ) {
		Q_free( leafPresets );
	}

	leafProps = (LeafProps *)Q_malloc( sizeof( LeafProps ) * NumLeafs() );
	leafPresets = (PresetHandle *)Q_malloc( sizeof( PresetHandle ) * NumLeafs() );
}

bool LeafPropsCache::TryReadFromFile( int fsFlags ) {
	LeafPropsReader reader( this, fsFlags );
	return TryReadFromFile( &reader );
}

bool LeafPropsCache::TryReadFromFile( LeafPropsReader *reader ) {
	int numReadProps = 0;
	for(;; ) {
		LeafProps props;
		PresetHandle presetHandle = nullptr;
		switch( reader->ReadNextProps( &props, &presetHandle ) ) {
			case LeafPropsReader::OK:
				if( numReadProps + 1 > NumLeafs() ) {
					return false;
				}
				this->leafProps[numReadProps] = props;
				this->leafPresets[numReadProps] = presetHandle;
				numReadProps++;
				break;
			case LeafPropsReader::DONE:
				return numReadProps == NumLeafs();
			default:
				return false;
		}
	}
}

bool LeafPropsCache::SaveToCache() {
	LeafPropsWriter writer( this );
	for( int i = 0, end = NumLeafs(); i < end; ++i ) {
		if( !writer.WriteProps( this->leafProps[i] ) ) {
			return false;
		}
	}

	return true;
}

struct LeafPropsBuilder {
	float roomSizeFactor { 0.0f };
	float skyFactor { 0.0f };
	float smoothnessFactor { 0.0f };
	float metalnessFactor { 0.0f };
	int numProps { 0 };

	void operator+=( const LeafProps &propsToAdd ) {
		roomSizeFactor += propsToAdd.getRoomSizeFactor();
		skyFactor += propsToAdd.getSkyFactor();
		smoothnessFactor += propsToAdd.getSmoothnessFactor();
		metalnessFactor += propsToAdd.getMetallnessFactor();
		++numProps;
	}

	LeafProps Result() {
		LeafProps result;
		if( numProps ) {
			float scale = 1.0f / numProps;
			roomSizeFactor *= scale;
			skyFactor *= scale;
			smoothnessFactor *= scale;
			metalnessFactor *= scale;
		}
		result.setRoomSizeFactor( roomSizeFactor );
		result.setSkyFactor( skyFactor );
		result.setSmoothnessFactor( smoothnessFactor );
		result.setMetallnessFactor( metalnessFactor );
		return result;
	}
};

// Using TreeMaps/HashMaps with strings as keys is an anti-pattern.
// This is a hack to get things done.
// TODO: Replace by a sane trie implementation.

namespace std {
	template<>
	struct hash<wsw::StringView> {
		auto operator()( const wsw::StringView &s ) const noexcept -> std::size_t {
			std::string_view stdView( s.data(), s.size() );
			std::hash<std::string_view> hash;
			return hash.operator()( stdView );
		}
	};
}

/**
 * An immutable part, safe to refer from multiple threads
 */
class SurfaceClassData {
	wsw::String m_namesData;
	wsw::HashSet<wsw::StringView> m_simpleNames;
	wsw::Vector<wsw::StringView> m_patterns;

	[[nodiscard]]
	bool loadDataFromFile( const wsw::StringView &prefix );
public:
	explicit SurfaceClassData( const wsw::StringView &prefix );

	[[nodiscard]]
	bool isThisKindOfSurface( const wsw::StringView &name ) const;
};

SurfaceClassData::SurfaceClassData( const wsw::StringView &prefix ) {
	if( !loadDataFromFile( prefix ) ) {
		Com_Printf( S_COLOR_YELLOW "Failed to load a class data for \"%s\" surfaces\n", prefix.data() );
		return;
	}

	wsw::CharLookup separators( wsw::StringView( "\r\n" ) );
	wsw::StringSplitter splitter( wsw::StringView( m_namesData.data(), m_namesData.size() ) );
	while( auto maybeToken = splitter.getNext( separators ) ) {
		auto rawToken = maybeToken->trim();
		if( rawToken.empty() ) {
			continue;
		}
		// Ensure that tokens are zero-terminated
		auto offset = ( rawToken.data() - m_namesData.data() );
		m_namesData[offset + rawToken.length()] = '\0';
		wsw::StringView token( rawToken.data(), rawToken.size(), wsw::StringView::ZeroTerminated );
		if( token.indexOf( '*' ) != std::nullopt ) {
			m_patterns.push_back( token );
		} else {
			m_simpleNames.insert( token );
		}
	}
}

bool SurfaceClassData::loadDataFromFile( const wsw::StringView &prefix ) {
	wsw::StaticString<MAX_QPATH> path;
	path << "sounds/surfaces/"_asView << prefix << ".txt"_asView;

	if( auto maybeHandle = wsw::fs::openAsReadHandle( path.asView() ) ) {
		const auto size = maybeHandle->getInitialFileSize();
		m_namesData.resize( size );
		return maybeHandle->readExact( m_namesData.data(), size );
	}
	return false;
}

bool SurfaceClassData::isThisKindOfSurface( const wsw::StringView &name ) const {
	assert( name.isZeroTerminated() );
	if( m_simpleNames.find( name ) != m_simpleNames.end() ) {
		return true;
	}
	for( const wsw::StringView &pattern: m_patterns ) {
		assert( pattern.isZeroTerminated() );
		if( glob_match( pattern.data(), name.data(), 0 ) ) {
			return true;
		}
	}
	return false;
}

/**
 * An mutable part, one per thread.
 */
class SurfaceClassCache {
	const SurfaceClassData *const m_surfaceClassData;
	mutable wsw::HashMap<int, bool> m_isThisKindOfSurface;
public:
	explicit SurfaceClassCache( const SurfaceClassData *surfaceClassData )
		: m_surfaceClassData( surfaceClassData ) {}

	[[nodiscard]]
	bool isThisKindOfSurface( int shaderRef, const wsw::StringView &shaderName ) const {
		auto it = m_isThisKindOfSurface.find( shaderRef );
		if( it != m_isThisKindOfSurface.end() ) {
			return it->second;
		}

		bool result = m_surfaceClassData->isThisKindOfSurface( shaderName );
		m_isThisKindOfSurface.insert( std::make_pair( shaderRef, result ) );
		return result;
	}
};

struct SurfaceClasses {
	const SurfaceClassData smoothSurfaces { "smooth"_asView };
	const SurfaceClassData absorptiveSurfaces { "absorptive"_asView };
	const SurfaceClassData metallicSurfaces { "metallic"_asView };
};

class LeafPropsSampler: public GenericRaycastSampler {
	static constexpr unsigned MAX_RAYS = 1024;

	// Inline buffers for algorithm intermediates.
	// Note that instances of this class should be allocated dynamically, so do not bother about arrays size.
	vec3_t dirs[MAX_RAYS];
	float distances[MAX_RAYS];
	const unsigned maxRays;

	unsigned numRaysHitSky { 0 };
	unsigned numRaysHitSmoothSurface { 0 };
	unsigned numRaysHitAbsorptiveSurface { 0 };
	unsigned numRaysHitMetal { 0 };

	SurfaceClassCache m_smoothSurfaces;
	SurfaceClassCache m_absorptiveSurfaces;
	SurfaceClassCache m_metallicSurfaces;
public:
	explicit LeafPropsSampler( const SurfaceClasses *classes, bool fastAndCoarse )
		: maxRays( fastAndCoarse ? MAX_RAYS / 2 : MAX_RAYS )
		, m_smoothSurfaces( &classes->smoothSurfaces )
		, m_absorptiveSurfaces( &classes->absorptiveSurfaces )
		, m_metallicSurfaces( &classes->metallicSurfaces ) {
		SetupSamplingRayDirs( dirs, maxRays );
	}

	[[nodiscard]]
	bool CheckAndAddHitSurfaceProps( const trace_t &trace ) override;

	[[nodiscard]]
	auto ComputeLeafProps( const vec3_t origin ) -> std::optional<LeafProps>;
};

class LeafPropsComputationTask: public ParallelComputationHost::PartialTask {
	friend class LeafPropsCache;

	LeafProps *const leafProps;
	LeafPropsSampler sampler;
	int leafsRangeBegin { -1 };
	int leafsRangeEnd { -1 };
	const bool fastAndCoarse;

	LeafProps ComputeLeafProps( int leafNum );
public:
	explicit LeafPropsComputationTask( LeafProps *leafProps_, const SurfaceClasses *classes, bool fastAndCoarse_ )
		: leafProps( leafProps_ ), sampler( classes, fastAndCoarse_ ), fastAndCoarse( fastAndCoarse_ ) {}

	void Exec() override;
};

bool LeafPropsCache::ComputeNewState( bool fastAndCoarse ) {
	leafProps[0] = LeafProps();

	const SurfaceClasses surfaceClasses;

	const int actualNumLeafs = NumLeafs();

	ComputationHostLifecycleHolder computationHostLifecycleHolder;
	auto *const computationHost = computationHostLifecycleHolder.Instance();

	// Up to 64 parallel tasks are supported.
	// Every task does not consume lots of memory compared to computation of the sound propagation table.
	LeafPropsComputationTask *submittedTasks[64];
	int actualNumTasks = 0;
	// Do not spawn more tasks than the actual number of leaves. Otherwise it fails for very small maps
	const int suggestedNumTasks = std::min( actualNumLeafs, std::min( computationHost->SuggestNumberOfTasks(), 64 ) );
	for( int i = 0; i < suggestedNumTasks; ++i ) {
		void *taskMem = Q_malloc( sizeof( LeafPropsComputationTask ) );
		// Never really happens with Q_malloc()... Use just malloc() instead?
		if( !taskMem ) {
			break;
		}
		auto *const task = new( taskMem )LeafPropsComputationTask( leafProps, &surfaceClasses, fastAndCoarse );
		if( !computationHost->TryAddTask( task ) ) {
			break;
		}
		submittedTasks[actualNumTasks++] = task;
	}

	if( !actualNumTasks ) {
		return false;
	}

	const int step = actualNumLeafs / actualNumTasks;
	assert( step > 0 );
	int rangeBegin = 1;
	for( int i = 0; i < actualNumTasks; ++i ) {
		auto *const task = submittedTasks[i];
		task->leafsRangeBegin = rangeBegin;
		if( i + 1 != actualNumTasks ) {
			rangeBegin += step;
			task->leafsRangeEnd = rangeBegin;
		} else {
			task->leafsRangeEnd = actualNumLeafs;
		}
	}

	computationHost->Exec();

	return true;
}

void LeafPropsComputationTask::Exec() {
	// Check whether the range has been assigned
	assert( leafsRangeBegin > 0 );
	assert( leafsRangeEnd > leafsRangeBegin );

	for( int i = leafsRangeBegin; i < leafsRangeEnd; ++i ) {
		leafProps[i] = ComputeLeafProps( i );
	}
}

auto LeafPropsSampler::ComputeLeafProps( const vec3_t origin ) -> std::optional<LeafProps> {
	GenericRaycastSampler::ResetMutableState( dirs, nullptr, distances, origin );
	this->numRaysHitAbsorptiveSurface = 0;
	this->numRaysHitSmoothSurface = 0;
	this->numRaysHitMetal = 0;
	this->numRaysHitSky = 0;

	this->numPrimaryRays = maxRays;

	EmitPrimaryRays();

	// Happens mostly if rays outgoing from origin start in solid
	if( !numPrimaryHits ) {
		return std::nullopt;
	}

	assert( numRaysHitAnySurface >= numPrimaryHits );

	assert( numRaysHitSmoothSurface + numRaysHitAbsorptiveSurface <= numRaysHitAnySurface );
	// A neutral leaf is either surrounded by fully neutral surfaces or numbers of smooth and absorptive surfaces match
	// A frac is  0.0 for a neutral leaf
	// A frac is -1.0 for a leaf that is surrounded by absorptive surfaces
	// A frac is +1.0 for a leaf that is surrounded by smooth surfaces
	float frac = ( (float)numRaysHitSmoothSurface - (float)numRaysHitAbsorptiveSurface );
	frac *= 1.0f / (float)numRaysHitAnySurface;
	assert( frac >= -1.0f && frac <= +1.0f );

	// A smoothness is 0.5 for a neutral leaf
	// A smoothness is 0.0 for a leaf that is surrounded by absorptive surfaces
	// A smoothness is 1.0 for a leaf that is surrounded by smooth surfaces
	const float smoothness = 0.5f + 0.5f * frac;
	assert( smoothness >= 0.0f && smoothness <= 1.0f );

	LeafProps props;
	props.setSmoothnessFactor( smoothness );
	props.setRoomSizeFactor( ComputeRoomSizeFactor() );
	const float invNumPrimaryHits = 1.0f / (float)numPrimaryHits;
	props.setSkyFactor( Q_Sqrt( (float)numRaysHitSky * invNumPrimaryHits ) );
	props.setMetallnessFactor( Q_Sqrt( (float)numRaysHitMetal * invNumPrimaryHits ) );

	return props;
}

bool LeafPropsSampler::CheckAndAddHitSurfaceProps( const trace_t &trace ) {
	const auto contents = trace.contents;
	if( contents & ( CONTENTS_WATER | CONTENTS_SLIME ) ) {
		numRaysHitSmoothSurface++;
	} else if( contents & CONTENTS_LAVA ) {
		numRaysHitAbsorptiveSurface++;
	}

	const auto surfFlags = trace.surfFlags;
	if( surfFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_NOMARKS | SURF_FLESH | SURF_NOSTEPS ) ) {
		if( surfFlags & SURF_SKY ) {
			numRaysHitSky++;
		}
		return false;
	}

	// Already tested for smoothness / absorption
	if( contents & MASK_WATER ) {
		return false;
	}

	const auto shaderNum = trace.shaderNum;
	const wsw::StringView shaderName( S_ShaderrefName( shaderNum ) );
	if( m_smoothSurfaces.isThisKindOfSurface( shaderNum, shaderName ) ) {
		numRaysHitSmoothSurface++;
	} else if( m_absorptiveSurfaces.isThisKindOfSurface( shaderNum, shaderName ) ) {
		numRaysHitAbsorptiveSurface++;
	}

	if( m_metallicSurfaces.isThisKindOfSurface( shaderNum, shaderName ) ) {
		numRaysHitMetal++;
	}

	return true;
}

LeafProps LeafPropsComputationTask::ComputeLeafProps( int leafNum ) {
	const vec3_t *leafBounds = S_GetLeafBounds( leafNum );
	const float *leafMins = leafBounds[0];
	const float *leafMaxs = leafBounds[1];

	vec3_t extent;
	VectorSubtract( leafMaxs, leafMins, extent );
	const float squareExtent = VectorLengthSquared( extent );

	LeafPropsBuilder propsBuilder;

	int maxSamples;
	if( fastAndCoarse ) {
		// Use a linear growth in this case
		maxSamples = (int)( 1 + ( sqrtf( squareExtent ) / 256.0f ) );
	} else {
		// Let maxSamples grow quadratic depending linearly of square extent value.
		// Cubic growth is more "natural" but leads to way too expensive computations.
		maxSamples = (int)( 2 + ( squareExtent / ( 256.0f * 256.0f ) ) );
	}

	int numSamples = 0;
	int numAttempts = 0;

	// Always start at the leaf center
	vec3_t point;
	VectorMA( leafMins, 0.5f, extent, point );
	bool hasValidPoint = true;

	while( numSamples < maxSamples ) {
		numAttempts++;
		// Attempts may fail for 2 reasons: can't pick a valid point and a point sampling has failed.
		// Use the shared loop termination condition for these cases.
		if( numAttempts > 7 + 2 * maxSamples ) {
			return propsBuilder.Result();
		}

		if( !hasValidPoint ) {
			for( int i = 0; i < 3; ++i ) {
				point[i] = leafMins[i] + ( 0.1f + 0.9f * EffectSamplers::SamplingRandom() ) * extent[i];
			}

			// Check whether the point is really in leaf (the leaf is not a box but is inscribed in the bounds box)
			// Some bogus leafs (that are probably degenerate planes) lead to infinite looping otherwise
			if( S_PointLeafNum( point ) != leafNum ) {
				continue;
			}

			hasValidPoint = true;
		}

		// Might fail if the rays outgoing from the point start in solid
		if( const auto maybeProps = sampler.ComputeLeafProps( point ) ) {
			propsBuilder += *maybeProps;
			numSamples++;
			// Invalidate previous point used for sampling
			hasValidPoint = false;
			continue;
		}
	}

	return propsBuilder.Result();
}
