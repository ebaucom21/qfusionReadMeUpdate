#include "common.h"
#include "wswexceptions.h"
#include "glob.h"

#if ( defined( _MSC_VER ) && ( defined( _M_IX86 ) || defined( _M_AMD64 ) || defined( _M_X64 ) ) )
// For __cpuid() intrinsic
#include <intrin.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#	ifdef __APPLE__
#	include <sys/sysctl.h>
#	endif
#else
#define NOMINMAX
#include <windows.h>
#endif

int Com_GlobMatch( const char *pattern, const char *text, const bool casecmp ) {
	return glob_match( pattern, text, casecmp );
}

//============================================================================

/*
* Com_AddPurePakFile
*/
void Com_AddPakToPureList( purelist_t **purelist, const char *pakname, const unsigned checksum ) {
	purelist_t *purefile;
	const size_t len = strlen( pakname ) + 1;

	purefile = ( purelist_t* )Q_malloc( sizeof( purelist_t ) + len );
	purefile->filename = ( char * )( ( uint8_t * )purefile + sizeof( *purefile ) );
	memcpy( purefile->filename, pakname, len );
	purefile->checksum = checksum;
	purefile->next = *purelist;
	*purelist = purefile;
}

/*
* Com_CountPureListFiles
*/
unsigned Com_CountPureListFiles( purelist_t *purelist ) {
	unsigned numpure;
	purelist_t *iter;

	numpure = 0;
	iter = purelist;
	while( iter ) {
		numpure++;
		iter = iter->next;
	}

	return numpure;
}

/*
* Com_FindPakInPureList
*/
purelist_t *Com_FindPakInPureList( purelist_t *purelist, const char *pakname ) {
	purelist_t *purefile = purelist;

	while( purefile ) {
		if( !strcmp( purefile->filename, pakname ) ) {
			break;
		}
		purefile = purefile->next;
	}

	return purefile;
}

/*
* Com_FreePureList
*/
void Com_FreePureList( purelist_t **purelist ) {
	purelist_t *purefile = *purelist;

	while( purefile ) {
		purelist_t *next = purefile->next;
		Q_free( purefile );
		purefile = next;
	}

	*purelist = NULL;
}

void *Q_malloc( size_t size ) {
	// TODO: Ensure 16-byte alignment
	// Zero memory as lots of old stuff rely on the old mempool behaviour
	void *buf = std::calloc( size, 1 );

	if( !buf ) {
		wsw::failWithBadAlloc();
	}

	return buf;
}

void *Q_realloc( void *buf, size_t newsize ) {
	void *newbuf = realloc( buf, newsize );

	if( !newbuf && newsize ) {
		wsw::failWithBadAlloc();
	}

	// TODO: Zero memory too? There's no portable way of doing that

	return newbuf;
}

void Q_free( void *buf ) {
	std::free( buf );
}

char *Q_strdup( const char *str ) {
	auto len = std::strlen( str );
	auto *result = (char *)Q_malloc( len + 1 );
	std::memcpy( result, str, len + 1 );
	return result;
}

/**
 * Keeping all this stuff in a single object in a single file
 * feels much better than scattering it over platform files
 * that should be included for compilation via CMake.
 * TODO: It's turned out its exactly the opposite, move the code where it belongs
 */
class SystemFeaturesHolder {
	friend void Qcommon_Init( int argc, char **argv );

	unsigned processorFeatures { 0 };
	unsigned physicalProcessorsNumber { 0 };
	unsigned logicalProcessorsNumber { 0 };
	bool initialized { false };

	unsigned TestProcessorFeatures();
	void TestNumberOfProcessors( unsigned *physical, unsigned *logical );

#ifdef __linux__
	bool TestUsingLscpu( unsigned *physical, unsigned *logical );
	static const char *SkipWhiteSpace( const char *p );
	static const char *StringForKey( const char *key, const char *line );
	static double NumberForKey( const char *key, const char *line );
#endif

#ifdef _WIN32
	void TestUsingLogicalProcessorInformation( unsigned *physical, unsigned *logical );
#endif

public:
	void EnsureInitialized();
	unsigned GetProcessorFeatures();
	bool GetNumberOfProcessors( unsigned *physical, unsigned *logical );
};

static SystemFeaturesHolder systemFeaturesHolder;

void Sys_InitProcessorFeatures() {
	::systemFeaturesHolder.EnsureInitialized();
}

unsigned Sys_GetProcessorFeatures() {
	return ::systemFeaturesHolder.GetProcessorFeatures();
}

bool Sys_GetNumberOfProcessors( unsigned *physical, unsigned *logical ) {
	return ::systemFeaturesHolder.GetNumberOfProcessors( physical, logical );
}

void SystemFeaturesHolder::EnsureInitialized() {
	if( initialized ) {
		return;
	}

	processorFeatures = TestProcessorFeatures();
	TestNumberOfProcessors( &physicalProcessorsNumber, &logicalProcessorsNumber );
	initialized = true;
}

bool SystemFeaturesHolder::GetNumberOfProcessors( unsigned *physical, unsigned *logical ) {
	EnsureInitialized();
	if( !physicalProcessorsNumber || !logicalProcessorsNumber ) {
		*physical = 1;
		*logical = 1;
		return false;
	}

	*physical = physicalProcessorsNumber;
	*logical = logicalProcessorsNumber;
	return true;
}

unsigned SystemFeaturesHolder::GetProcessorFeatures() {
	EnsureInitialized();
	return processorFeatures;
}

unsigned SystemFeaturesHolder::TestProcessorFeatures() {
	unsigned features = 0;

#if ( defined ( __i386__ ) || defined ( __x86_64__ ) || defined( _M_IX86 ) || defined( _M_AMD64 ) || defined( _M_X64 ) )
#ifdef _MSC_VER
	int cpuInfo[4];
	__cpuid( cpuInfo, 0 );
	// Check whether CPUID is supported at all (is it relevant nowadays?)
	if( cpuInfo[0] == 0 ) {
		return 0;
	}
	// Get standard feature bits (look for description here https://en.wikipedia.org/wiki/CPUID)
	__cpuid( cpuInfo, 1 );
	const int ECX = cpuInfo[2];
	const int EDX = cpuInfo[3];
	if( ECX & ( 1 << 28 ) ) {
		features |= Q_CPU_FEATURE_AVX;
	} else if( ECX & ( 1 << 20 ) ) {
		features |= Q_CPU_FEATURE_SSE42;
	} else if( ECX & ( 1 << 19 ) ) {
		features |= Q_CPU_FEATURE_SSE41;
	} else if( EDX & ( 1 << 26 ) ) {
		features |= Q_CPU_FEATURE_SSE2;
	}
#else // not MSVC
#ifndef __clang__
	// Clang does not even have this intrinsic, executables work fine without it.
	__builtin_cpu_init();
#endif // clang-specific code
	if( __builtin_cpu_supports( "avx" ) ) {
		features |= Q_CPU_FEATURE_AVX;
	} else if( __builtin_cpu_supports( "sse4.2" ) ) {
		features |= Q_CPU_FEATURE_SSE42;
	} else if( __builtin_cpu_supports( "sse4.1" ) ) {
		features |= Q_CPU_FEATURE_SSE41;
	} else if( __builtin_cpu_supports( "sse2" ) ) {
		features |= Q_CPU_FEATURE_SSE2;
	}
#endif // gcc/clang - specific code
#endif // x86-specific code

	// We have only set the most significant feature bit for code clarity.
	// Since this bit implies all least-significant bits presence, set these bits
	if( features ) {
		// Check whether it's a power of 2
		assert( !( features & ( features - 1 ) ) );
		features |= ( features - 1 );
	}

	return features;
}

void SystemFeaturesHolder::TestNumberOfProcessors( unsigned *physical, unsigned *logical ) {
	*physical = 0;
	*logical = 0;

#ifdef __linux__
	if( TestUsingLscpu( physical, logical ) ) {
		return;
	}

	Com_Printf( S_COLOR_YELLOW "Warning: `lscpu` command can't be executed. Falling back to inferior methods\n" );

	// This is quite poor.
	// We hope that `lscpu` works for the most client installationsq
	long confValue = sysconf( _SC_NPROCESSORS_ONLN );
	// Sanity/error checks
	Q_clamp( confValue, 1, 256 );
	*physical = (unsigned)confValue;
	*logical = (unsigned)confValue;
#endif

#ifdef _WIN32
	TestUsingLogicalProcessorInformation( physical, logical );
#endif

#ifdef __APPLE__
	// They should not get changed but lets ensure no surprises
	size_t len1 = sizeof( *physical ), len2 = sizeof( *logical );
	// Never fails if parameters are correct
	assert( !sysctlbyname( "hw.physicalcpu", physical, &len1, nullptr, 0 ) );
	assert( !sysctlbyname( "hw.logicalcpu", logical, &len2, nullptr, 0 ) );
#endif
}

#ifdef __linux__

/**
 * An utility to read lines from stdout of a spawned command
 */
class ProcessPipeReader {
	FILE *fp;
public:
	explicit ProcessPipeReader( const char *command ) {
		fp = ::popen( command, "r" );
	}

	~ProcessPipeReader() {
		if( fp ) {
			(void)::pclose( fp );
		}
	}

	char *ReadNext( char *buffer, size_t bufferSize ) {
		if( !fp || ::feof( fp ) ) {
			return nullptr;
		}

		assert( bufferSize <= (size_t)std::numeric_limits<int>::max() );
		char *result = fgets( buffer, (int)bufferSize, fp );
		if( !result && ::ferror( fp ) ) {
			(void)pclose( fp );
			fp = nullptr;
		}
		return result;
	}
};

const char *SystemFeaturesHolder::SkipWhiteSpace( const char *p ) {
	while( ::isspace( *p ) ) {
		p++;
	}
	return p;
}

const char *SystemFeaturesHolder::StringForKey( const char *key, const char *line ) {
	// Skip a whitespace before line contents
	const char *p = SkipWhiteSpace( line );
	if( !*p ) {
		return nullptr;
	}
	// Skip a whitespace before key characters (ignoring this could lead to hard-to-find bugs)
	const char *k = SkipWhiteSpace( key );
	if( !*k ) {
		return nullptr;
	}

	// Match a line part by the key
	while( *k && ( ::tolower( *k ) == ::tolower( *p ) ) ) {
		k++, p++;
	}

	// If there is an unmatched key part
	if( *k ) {
		return nullptr;
	}

	// Skip a whitespace before the separating colon
	p = SkipWhiteSpace( p );
	if( *p++ != ':' ) {
		return nullptr;
	}

	// Skip a whitespace before contents
	return SkipWhiteSpace( p );
}

double SystemFeaturesHolder::NumberForKey( const char *key, const char *line ) {
	if( const char *s = StringForKey( key, line ) ) {
		char *endptr;
		long value = strtol( s, &endptr, 10 );
		if( !*endptr || ::isspace( *endptr ) ) {
			static_assert( sizeof( long ) >= sizeof( int ), "incorrect strtol() result checks" );
			const auto min = std::numeric_limits<int>::min();
			const auto max = std::numeric_limits<int>::max();
			if( value >= min && value <= max ) {
				return value;
			}
		}
	}
	return std::numeric_limits<double>::quiet_NaN();
}

bool SystemFeaturesHolder::TestUsingLscpu( unsigned *physical, unsigned *logical ) {
	// We could try parsing "/proc/cpuinfo" but it is really complicated.
	// This utility provides much more convenient output and IPC details is hidden anyway.
	ProcessPipeReader reader( "lscpu" );
	char buffer[3072];

	unsigned cpus = 0;
	unsigned threadsPerCore = 0;
	double n;
	while( !cpus || !threadsPerCore ) {
		const char *line = reader.ReadNext( buffer, sizeof( buffer ) );
		if( !line ) {
			return false;
		}
		if( !cpus ) {
			n = NumberForKey( "CPU(s)", line );
			if( !std::isnan( n ) && n > 0 ) {
				cpus = (unsigned)n;
			}
		}
		if( !threadsPerCore ) {
			n = NumberForKey( "Thread(s) per core", line );
			if( !std::isnan( n ) && n > 0 ) {
				threadsPerCore = (unsigned)n;
			}
		}
	}

	if( cpus % threadsPerCore ) {
		Com_Printf( S_COLOR_YELLOW "Weird number of CPU(s) for threads(s) per core: %d for %d", cpus, threadsPerCore );
		return false;
	}

	*physical = cpus / threadsPerCore;
	*logical = cpus;
	return true;
}

#endif

#ifdef _WIN32
void SystemFeaturesHolder::TestUsingLogicalProcessorInformation( unsigned *physical, unsigned *logical ) {
	assert( !*physical );
	assert( !*logical );

	DWORD bufferLen;
	::GetLogicalProcessorInformation( nullptr, &bufferLen );
	if( ::GetLastError() != ERROR_INSUFFICIENT_BUFFER ) {
		return;
	}

	auto *const buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)::malloc( bufferLen );
	if( !::GetLogicalProcessorInformation( buffer, &bufferLen ) ) {
		::free( buffer );
		return;
	}

	for( int i = 0; i < bufferLen / sizeof( *buffer ); ++i ) {
		if( buffer[i].Relationship != RelationProcessorCore ) {
			continue;
		}
		( *physical )++;

		const ULONG_PTR processorMask = buffer[i].ProcessorMask;
		static_assert( sizeof( processorMask ) == sizeof( ptrdiff_t ), "" );
		// We can't rely on popcnt instruction support
		for( uint64_t j = 0, bitMask = 1; j < sizeof( ptrdiff_t ) * 8; ++j, bitMask <<= 1 ) {
			if( processorMask & bitMask ) {
				( *logical )++;
			}
		}
	}

	::free( buffer );
}
#endif