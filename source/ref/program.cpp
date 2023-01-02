/*
Copyright (C) 2007 Victor Luchits
Copyright (C) 2023 Chasseur de Bots

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

// r_program.c - OpenGL Shading Language support

#include "local.h"
#include "program.h"
#include "../qcommon/links.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/wswfs.h"
#include "../qcommon/wswstringsplitter.h"

using wsw::operator""_asView;

class ProgramSourceFileCache {
	friend class ProgramSourceLoader;
public:
	ProgramSourceFileCache();
private:

	struct Span {
		unsigned offset { 0 };
		unsigned length { 0 };
	};

	struct RegularInclude {
		Span fileNameSpan;
	};
	struct ConditionalInclude {
		Span fileNameSpan;
		Span conditionSpan;
	};

	using Include = std::variant<RegularInclude, ConditionalInclude>;

	// Either a direct line span an or index of a span in includes
	using LineSpan = std::variant<Span, unsigned>;

	struct FileEntry {
		FileEntry *prev { nullptr }, *next { nullptr };
		Span nameSpan;
		Span linesSpan;
		uint32_t nameHash { 0 };
	};

	[[nodiscard]]
	auto getFileEntryForFile( const wsw::StringView &fileName ) -> const FileEntry *;

	[[nodiscard]]
	auto loadFileEntryFromFile( const wsw::StringView &fileName ) -> FileEntry *;

	[[nodiscard]]
	bool fillFileEntryFromFile( FileEntry *entry, const wsw::StringView &fileName );

	enum ParsingIncludeResult {
		ParsingFailure,
		NoIncludes,
		HasIncludes,
	};

	[[nodiscard]]
	auto tryParsingIncludes( const wsw::StringView &lineToken ) -> ParsingIncludeResult;

	[[nodiscard]]
	auto getStringForSpan( const Span &span ) const -> wsw::StringView {
		assert( span.offset < m_stringData.size() && span.offset + span.length <= m_stringData.size() );
		return { m_stringData.data() + span.offset, span.length };
	}

	[[nodiscard]]
	auto getSpanOfStringView( const wsw::StringView &view ) const -> Span {
		assert( view.data() >= m_stringData.data() && view.data() < m_stringData.data() + m_stringData.size() );
		return { .offset = (unsigned)( view.data() - m_stringData.data() ), .length = (unsigned)view.size() };
	}

	wsw::Vector<LineSpan> m_lineEntries;
	wsw::Vector<char> m_stringData;
	wsw::Vector<Include> m_includesData;

	// Their addresses must be stable, so we use a hash map
	FileEntry *m_fileEntryHashBins[97];
	// TODO: Allow allocating extra entries dynamically from the default heap?
	wsw::MemberBasedFreelistAllocator<sizeof( FileEntry ), 48> m_fileEntriesAllocator;
};

// Kept during the entire program lifecycle
static ProgramSourceFileCache g_programSourceFileCache;

ProgramSourceFileCache::ProgramSourceFileCache() {
	std::fill( std::begin( m_fileEntryHashBins ), std::end( m_fileEntryHashBins ), nullptr );
}

auto ProgramSourceFileCache::getFileEntryForFile( const wsw::StringView &fileName ) -> const FileEntry * {
	// This hash function is case-insensitive
	const uint32_t nameHash = wsw::getHashForLength( fileName.data(), fileName.size() );
	const uint32_t binIndex = nameHash % std::size( m_fileEntryHashBins );

	for( const FileEntry *entry = m_fileEntryHashBins[binIndex]; entry; entry = entry->next ) {
		if( entry->nameHash == nameHash && getStringForSpan( entry->nameSpan ).equalsIgnoreCase( fileName ) ) {
			return entry;
		}
	}

	if( FileEntry *entry = loadFileEntryFromFile( fileName ) ) {
		entry->nameHash = nameHash;
		wsw::link( entry, &m_fileEntryHashBins[binIndex] );
		return entry;
	}

	return nullptr;
}

auto ProgramSourceFileCache::loadFileEntryFromFile( const wsw::StringView &fileName ) -> FileEntry * {
	const size_t oldStringDataSize   = m_stringData.size();
	const size_t oldLineEntriesSize  = m_lineEntries.size();
	const size_t oldIncludesDataSize = m_includesData.size();

	void *mem = m_fileEntriesAllocator.allocOrNull();
	if( !mem ) {
		return nullptr;
	}

	auto *newEntry = new( mem )FileEntry;

	bool result = true;
	try {
		result = fillFileEntryFromFile( newEntry, fileName );
	} catch( ... ) {
		result = false;
	}

	if( !result ) {
		// This should not throw for POD's
		m_stringData.resize( oldStringDataSize );
		m_lineEntries.resize( oldLineEntriesSize );
		m_includesData.resize( oldIncludesDataSize );

		newEntry->~FileEntry();
		m_fileEntriesAllocator.free( newEntry );
		newEntry = nullptr;
	}

	return newEntry;
}

// TODO: This should be shared at the top level...
static const wsw::CharLookup kLineSeparatorChars( wsw::StringView( "\r\n" ) );

bool ProgramSourceFileCache::fillFileEntryFromFile( FileEntry *entry, const wsw::StringView &fileName ) {
	auto maybeFileHandle = wsw::fs::openAsReadHandle( fileName );
	if( !maybeFileHandle ) {
		return false;
	}

	const size_t rawFileSize = maybeFileHandle->getInitialFileSize();
	if( rawFileSize > ( 1 << 20 ) ) {
		Com_Printf( S_COLOR_RED "Bogus source file size for %s: %lu\n", fileName.data(), rawFileSize );
		return false;
	}

	const size_t oldStringDataSize  = m_stringData.size();
	const size_t oldLineEntriesSize = m_lineEntries.size();

	// Allocate an extra space for 2 extra characters
	m_stringData.resize( oldStringDataSize + rawFileSize + 2 );
	if( !maybeFileHandle->readExact( m_stringData.data() + oldStringDataSize, rawFileSize ) ) {
		Com_Printf( S_COLOR_RED "Failed to read the source file\n" );
		return false;
	}

	// Get rid of comments, otherwise we fail on compiling FXAA
	const auto fileDataSize = (size_t)COM_Compress( m_stringData.data() + oldStringDataSize );
	// Let the freed space be used for further additions
	m_stringData.resize( oldStringDataSize + fileDataSize + 2 );

	// Ensure the trailing \n for GLSL (the file could miss it)
	m_stringData[oldStringDataSize + fileDataSize + 0] = '\n';
	// Ensure that the read data is zero-terminated
	m_stringData[oldStringDataSize + fileDataSize + 1] = '\0';

	const size_t bomShift = startsWithUtf8Bom( m_stringData.data() + oldStringDataSize, fileDataSize ) ? 3 : 0;
	wsw::StringSplitter lineSplitter( wsw::StringView( m_stringData.data() + oldStringDataSize + bomShift, fileDataSize ) );
	while( const std::optional<wsw::StringView> &maybeLineToken = lineSplitter.getNext( kLineSeparatorChars ) ) {
		const wsw::StringView lineToken = *maybeLineToken;

		const ParsingIncludeResult parseIncludeResult = tryParsingIncludes( lineToken );
		if( parseIncludeResult == ParsingFailure ) {
			return false;
		}
		if( parseIncludeResult == HasIncludes ) {
			continue;
		}

		assert( lineToken.data() >= m_stringData.data() );
		assert( lineToken.data() < m_stringData.data() + m_stringData.size() );
		const ptrdiff_t offset = lineToken.data() - m_stringData.data();
		assert( (size_t)offset < m_stringData.size() );
		// Convert CR to LF
		if( m_stringData[offset + lineToken.length()] == '\r') {
			m_stringData[offset + lineToken.length()] = '\n';
		}
		assert( m_stringData[offset + lineToken.length()] == '\n' );
		m_lineEntries.push_back( Span { .offset = (unsigned)offset, .length = (unsigned)( lineToken.length() + 1 ) } );
	}

	const auto fileNameOffset = m_stringData.size();
	m_stringData.insert( m_stringData.end(), fileName.data(), fileName.data() + fileName.size() );
	m_stringData.push_back( '\0' );

	entry->nameSpan = {
		.offset = (unsigned)fileNameOffset, .length = (unsigned)fileName.length(),
	};
	entry->linesSpan = {
		.offset = (unsigned)oldLineEntriesSize, .length = (unsigned)( m_lineEntries.size() - oldLineEntriesSize ),
	};

	return true;
}

static void sanitizeFilePath( char *chars, unsigned offset, unsigned length ) {
	for( unsigned i = offset; i < offset + length; ++i ) {
		if( chars[i] == '\\' ) {
			chars[i] = '/';
		}
	}
}

auto ProgramSourceFileCache::tryParsingIncludes( const wsw::StringView &lineToken ) -> ParsingIncludeResult {
	wsw::StringView lineLeftover = lineToken.trimLeft();
	if( !lineLeftover.startsWith( '#' ) ) {
		return NoIncludes;
	}

	const auto isATokenCharacter = []( char ch ) {
		return !isspace( ch ) && ch != '(' && ch != ')';
	};
	const auto isAFileNameCharacter = []( char ch ) {
		return !isspace( ch ) && ch != '"';
	};

	const wsw::StringView firstLineToken  = lineLeftover.takeWhile(isATokenCharacter);

	bool hasACondition = false;
	if( !firstLineToken.equalsIgnoreCase( "#include"_asView ) ) {
		if( !firstLineToken.equalsIgnoreCase( "#include_if"_asView ) ) {
			return NoIncludes;
		}
		hasACondition = true;
	}

	lineLeftover = lineLeftover.drop( firstLineToken.size() ).trimLeft();

	wsw::StringView condition;
	if( hasACondition ) {
		if( !lineLeftover.startsWith( '(' ) ) {
			return ParsingFailure;
		}
		lineLeftover = lineLeftover.drop( 1 ).trimLeft();

		condition = lineLeftover.takeWhile( isATokenCharacter );
		if( condition.empty() ) {
			return ParsingFailure;
		}

		lineLeftover = lineLeftover.drop( condition.size() ).trimLeft();
		if( !lineLeftover.startsWith( ')' ) ) {
			return ParsingFailure;
		}
		lineLeftover = lineLeftover.drop( 1 ).trimLeft();
	}

	if( !lineLeftover.startsWith( '"' ) ) {
		return ParsingFailure;
	}
	lineLeftover = lineLeftover.drop( 1 );

	const wsw::StringView &fileName = lineLeftover.takeWhile( isAFileNameCharacter );
	if( fileName.empty() ) {
		return ParsingFailure;
	}

	lineLeftover = lineLeftover.drop( fileName.length() );

	if( !lineLeftover.startsWith( '"' ) ) {
		return ParsingFailure;
	}

	lineLeftover = lineLeftover.drop( 1 ).trim();
	if( !lineLeftover.empty() ) {
		return ParsingFailure;
	}

	const Span fileNameSpan = getSpanOfStringView( fileName );
	::sanitizeFilePath( m_stringData.data(), fileNameSpan.offset, fileNameSpan.length );

	if( hasACondition ) {
		const Span conditionSpan = getSpanOfStringView( condition );
		::sanitizeFilePath( m_stringData.data(), fileNameSpan.offset, fileNameSpan.length );
		m_includesData.emplace_back( ConditionalInclude { .fileNameSpan = fileNameSpan, .conditionSpan = conditionSpan } );
	} else {
		m_includesData.emplace_back( RegularInclude { .fileNameSpan = fileNameSpan } );
	}

	m_lineEntries.push_back( (unsigned)( m_includesData.size() - 1 ) );

	return HasIncludes;
}

class ProgramSourceLoader {
public:
	ProgramSourceLoader( ProgramSourceFileCache *cache, wsw::Vector<const char *> *lines,
						 wsw::Vector<int> *lengths, wsw::Vector<unsigned> *tmpOffsets )
		: m_sourceFileCache( cache ), m_lines( lines ), m_lengths( lengths ), m_tmpOffsets( tmpOffsets ) {}

	[[nodiscard]]
	bool load( const wsw::StringView &rootFileName, uint64_t features, unsigned programType );

private:
	[[nodiscard]]
	bool loadRecursively( const wsw::StringView &fileName, int depth );

	[[nodiscard]]
	auto getRootFileDir() const -> wsw::StringView;

	[[nodiscard]]
	bool checkIncludeCondition( const wsw::StringView &condition ) const;

	ProgramSourceFileCache *const m_sourceFileCache;

	wsw::Vector<const char *> *const m_lines;
	wsw::Vector<int> *const m_lengths;

	wsw::Vector<unsigned> *const m_tmpOffsets;

	mutable wsw::StaticString<64> m_cachedRootFileDir;
	wsw::StringView m_rootFileName;
	uint64_t m_features { 0 };
	unsigned m_programType { 0 };
};

bool ProgramSourceLoader::load( const wsw::StringView &rootFileName, uint64_t features, unsigned programType ) {
	assert( m_lines->size() == m_lengths->size() );
	const auto oldSize = m_lines->size();

	m_rootFileName = rootFileName;
	m_cachedRootFileDir.clear();

	m_features    = features;
	m_programType = programType;

	m_tmpOffsets->clear();

	const bool loadResult = loadRecursively( rootFileName, 1 );
	if( loadResult ) {
		// We can't store pointers during recursive expansion as the cache data is not stable
		assert( m_lines->size() + m_tmpOffsets->size() == m_lengths->size() );
		m_lines->reserve( m_lines->size() + m_tmpOffsets->size() );
		for( const unsigned offset: *m_tmpOffsets ) {
			m_lines->push_back( m_sourceFileCache->m_stringData.data() + offset );
		}
	} else {
		m_lines->resize( oldSize );
		m_lengths->resize( oldSize );
	}

	assert( m_lines->size() == m_lengths->size() );
	return loadResult;
}

bool ProgramSourceLoader::loadRecursively( const wsw::StringView &fileName, int depth ) {
	if( const ProgramSourceFileCache::FileEntry *fileEntry = m_sourceFileCache->getFileEntryForFile( fileName ) ) {
		const ProgramSourceFileCache::Span &linesSpan = fileEntry->linesSpan;
		for( unsigned i = linesSpan.offset; i < linesSpan.offset + linesSpan.length; ++i ) {
			const ProgramSourceFileCache::LineSpan &spanForLine = m_sourceFileCache->m_lineEntries[i];
			if( const auto *regularLineSpan = std::get_if<ProgramSourceFileCache::Span>( &spanForLine ) ) [[likely]] {
				m_tmpOffsets->push_back( regularLineSpan->offset );
				m_lengths->push_back( (int)regularLineSpan->length );
				continue;
			}

			if( depth + 1 >= 16 ) {
				Com_Printf( "Too many nested includes\n" );
				return false;
			}

			const auto *includeIndex = std::get_if<unsigned>( &spanForLine );
			const ProgramSourceFileCache::Include &include = m_sourceFileCache->m_includesData[*includeIndex];
			ProgramSourceFileCache::Span includeFileNameSpan;

			// std::variant interface sucks
			if( const auto *conditional = std::get_if<ProgramSourceFileCache::ConditionalInclude>( &include ) ) {
				if( !checkIncludeCondition( m_sourceFileCache->getStringForSpan( conditional->conditionSpan ) ) ) {
					continue;
				}
				includeFileNameSpan  = conditional->fileNameSpan;
			} else if( const auto *regular = std::get_if<ProgramSourceFileCache::RegularInclude>( &include ) ) {
				includeFileNameSpan = regular->fileNameSpan;
			}

			const wsw::StringView &includeFileName = m_sourceFileCache->getStringForSpan( includeFileNameSpan );

			// Prepare the file name
			wsw::StaticString<MAX_QPATH> buffer;
			if( includeFileName.startsWith( '/' ) ) {
				buffer << getRootFileDir();
				assert( buffer.endsWith( '/' ) );
				buffer << includeFileName;
			} else {
				buffer << fileName;
				if( const auto maybeLastSlashIndex = buffer.lastIndexOf( '/' ) ) {
					buffer.erase( *maybeLastSlashIndex + 1 );
				}
				buffer << includeFileName;
			}

			if( !loadRecursively( buffer.asView(), depth + 1 ) ) {
				return false;
			}
		}

		return true;
	}

	return false;
}

auto ProgramSourceLoader::getRootFileDir() const -> wsw::StringView {
	if( m_cachedRootFileDir.empty() ) {
		assert( !m_rootFileName.empty() );
		wsw::StringView dir = m_rootFileName;
		if( const auto maybeLastSlashIndex = m_rootFileName.lastIndexOf( '/' ) ) {
			dir = m_rootFileName.take( *maybeLastSlashIndex + 1 );
		}
		m_cachedRootFileDir.assign( dir );
	}
	return m_cachedRootFileDir.asView();
}

// TODO: Should decision-making be performed at this level?
bool ProgramSourceLoader::checkIncludeCondition( const wsw::StringView &condition ) const {
	if( ( m_features & GLSL_SHADER_COMMON_FOG ) && condition.equalsIgnoreCase( "APPLY_FOG"_asView ) ) {
		return true;
	}
	if( ( m_features & GLSL_SHADER_COMMON_DLIGHTS ) && condition.equalsIgnoreCase( "NUM_DLIGHTS"_asView ) ) {
		return true;
	}
	if( ( m_features & GLSL_SHADER_COMMON_GREYSCALE ) && condition.equalsIgnoreCase( "APPLY_GREYSCALE"_asView ) ) {
		return true;
	}
	if( m_programType == GLSL_PROGRAM_TYPE_Q3A_SHADER ) {
		if( ( m_features & GLSL_SHADER_Q3_LIGHTSTYLE ) && condition.equalsIgnoreCase( "NUM_LIGHTMAPS"_asView ) ) {
			return true;
		}
	}
	if( m_programType == GLSL_PROGRAM_TYPE_MATERIAL ) {
		if( ( m_features & GLSL_SHADER_MATERIAL_LIGHTSTYLE ) ) {
			if( condition.equalsIgnoreCase( "NUM_LIGHTMAPS"_asView ) ) {
				return true;
			}
		}
		if( ( m_features & ( GLSL_SHADER_MATERIAL_OFFSETMAPPING | GLSL_SHADER_MATERIAL_RELIEFMAPPING ) ) ) {
			if( condition.equalsIgnoreCase( "APPLY_OFFSETMAPPING"_asView ) ) {
				return true;
			}
		}
		if( ( m_features & GLSL_SHADER_MATERIAL_CELSHADING ) ) {
			if( condition.equalsIgnoreCase( "APPLY_CELSHADING"_asView ) ) {
				return true;
			}
		}
		if( ( m_features & GLSL_SHADER_MATERIAL_DIRECTIONAL_LIGHT ) ) {
			if( condition.equalsIgnoreCase( "APPLY_DIRECTIONAL_LIGHT"_asView ) ) {
				return true;
			}
		}
	}
	return false;
}

#define MAX_GLSL_PROGRAMS           1024
#define GLSL_PROGRAMS_HASH_SIZE     256

class ShaderProgramCache {
	template <typename> friend class SingletonHolder;
public:
	[[nodiscard]]
	auto getProgramForParams( int type, const wsw::StringView &maybeRequestedName, uint64_t features = 0,
							  const DeformSig &deformSig = DeformSig(),
							  std::span<const deformv_t> deforms = {} ) -> int;

	[[nodiscard]]
	auto getProgramForParams( int type, const char *maybeName, uint64_t features = 0,
							  const DeformSig &deformSig = DeformSig(),
							  std::span<const deformv_t> deforms = {} ) -> int {
		return getProgramForParams( type, wsw::StringView( maybeName ? maybeName : "" ), features, deformSig, deforms );
	}

	[[nodiscard]]
	auto getProgramById( int id ) -> ShaderProgram * {
		assert( id > 0 && id < MAX_GLSL_PROGRAMS + 1 );
		return m_programForIndex[id - 1];
	}
private:
	static constexpr unsigned kExtraTrailingProgramBytesSize = 16;
	static constexpr unsigned kAllocatorNameLengthLimit      = 32;

	ShaderProgramCache();
	~ShaderProgramCache();

	[[nodiscard]]
	auto createProgramFromSource( const wsw::StringView &name, int type, uint64_t features, std::span<const deformv_t> deforms )
		-> std::optional<std::tuple<GLuint, GLuint, GLuint>>;

	static void destroyProgramObjects( GLuint programId, GLuint vertexShaderId, GLuint fragmentShaderId );

	[[nodiscard]]
	bool loadShaderSources( const wsw::StringView &name, int type, uint64_t features, std::span<const deformv_t> deforms,
							GLuint vertexShaderId, GLuint fragmentShaderId );

	[[nodiscard]]
	bool compileShader( GLuint id, const char *kind, std::span<const char *> strings, std::span<int> lengths );

	[[nodiscard]]
	bool bindAttributeLocations( GLuint programId );

	[[nodiscard]]
	bool linkProgram( GLuint programId, GLuint vertexShaderId, GLuint fragmentShaderId );
	
	static void setupUniformsAndLocations( ShaderProgram *program );

	wsw::HeapBasedFreelistAllocator m_programsAllocator;
	wsw::HeapBasedFreelistAllocator m_namesAllocator;

	wsw::Vector<const char *> m_tmpStrings;
	wsw::Vector<int> m_tmpLengths;
	wsw::Vector<unsigned> m_tmpOffsets;

	ShaderProgram *m_programListHead { nullptr };
	ShaderProgram *m_programForIndex[MAX_GLSL_PROGRAMS];
	ShaderProgram *m_hashBinsForType[GLSL_PROGRAM_TYPE_MAXTYPE][GLSL_PROGRAMS_HASH_SIZE];
};

static SingletonHolder<ShaderProgramCache> g_programCacheInstanceHolder;
static bool g_programCacheInstanceHolderInitialized;

void RP_Init() {
	if( !g_programCacheInstanceHolderInitialized ) {
		g_programCacheInstanceHolder.init();
		g_programCacheInstanceHolderInitialized = true;
	}
}

void RP_Shutdown() {
	assert( g_programCacheInstanceHolderInitialized );
	qglUseProgram( 0 );
	g_programCacheInstanceHolder.shutdown();
	g_programCacheInstanceHolderInitialized = false;
}

ShaderProgramCache::ShaderProgramCache()
	: m_programsAllocator( sizeof( ShaderProgram ) + kExtraTrailingProgramBytesSize, MAX_GLSL_PROGRAMS )
	, m_namesAllocator( kAllocatorNameLengthLimit + 1, MAX_GLSL_PROGRAMS ) {
	// TODO: The allocators initialization is not exception-safe

	std::memset( m_programForIndex, 0, sizeof( m_programForIndex ) );
	std::memset( m_hashBinsForType, 0, sizeof( m_hashBinsForType ) );

	// Register basic programs

	bool succeeded = true;
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_MATERIAL, DEFAULT_GLSL_MATERIAL_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_MATERIAL, DEFAULT_GLSL_MATERIAL_PROGRAM, GLSL_SHADER_COMMON_BONE_TRANSFORMS1 );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_DISTORTION, DEFAULT_GLSL_DISTORTION_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_RGB_SHADOW, DEFAULT_GLSL_RGB_SHADOW_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_SHADOWMAP, DEFAULT_GLSL_SHADOWMAP_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_OUTLINE, DEFAULT_GLSL_OUTLINE_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_Q3A_SHADER, DEFAULT_GLSL_Q3A_SHADER_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_CELSHADE, DEFAULT_GLSL_CELSHADE_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_FOG, DEFAULT_GLSL_FOG_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_FXAA, DEFAULT_GLSL_FXAA_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_YUV, DEFAULT_GLSL_YUV_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_COLOR_CORRECTION, DEFAULT_GLSL_COLORCORRECTION_PROGRAM );
	succeeded &= (bool)getProgramForParams( GLSL_PROGRAM_TYPE_KAWASE_BLUR, DEFAULT_GLSL_KAWASE_BLUR_PROGRAM );

	if( !succeeded ) {
		Com_Error( ERR_FATAL, "Failed to precache basic GLSL programs\n" );
	}
}

ShaderProgramCache::~ShaderProgramCache() {
	for( ShaderProgram *program = m_programListHead, *next = nullptr; program; program = next ) {
		next = program->nextInList;

		destroyProgramObjects( program->programId, program->vertexShaderId, program->fragmentShaderId );

		if( program->deformSigDataToFree ) {
			Q_free( program->deformSigDataToFree );
		}
		if( program->nameDataToFree ) {
			if( m_namesAllocator.mayOwn( program->nameDataToFree ) ) {
				m_namesAllocator.free( program->nameDataToFree );
			} else {
				Q_free( program->nameDataToFree );
			}
		}

		m_programsAllocator.free( program );
	}
}

static const glsl_feature_t glsl_features_empty[] =
{
	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_material[] =
{
	{ GLSL_SHADER_COMMON_GREYSCALE, "#define APPLY_GREYSCALE\n", "_grey" },

	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS4, "#define QF_NUM_BONE_INFLUENCES 4\n", "_bones4" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS3, "#define QF_NUM_BONE_INFLUENCES 3\n", "_bones3" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS2, "#define QF_NUM_BONE_INFLUENCES 2\n", "_bones2" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS1, "#define QF_NUM_BONE_INFLUENCES 1\n", "_bones1" },

	{ GLSL_SHADER_COMMON_RGB_GEN_ONE_MINUS_VERTEX, "#define APPLY_RGB_ONE_MINUS_VERTEX\n", "_c1-v" },
	{ GLSL_SHADER_COMMON_RGB_GEN_CONST, "#define APPLY_RGB_CONST\n", "_cc" },
	{ GLSL_SHADER_COMMON_RGB_GEN_VERTEX, "#define APPLY_RGB_VERTEX\n", "_cv" },
	{ GLSL_SHADER_COMMON_RGB_DISTANCERAMP, "#define APPLY_RGB_DISTANCERAMP\n", "_rgb_dr" },

	{ GLSL_SHADER_COMMON_SRGB2LINEAR, "#define APPLY_SRGB2LINEAR\n", "_srgb" },
	{ GLSL_SHADER_COMMON_LINEAR2SRB, "#define APPLY_LINEAR2SRGB\n", "_linear" },

	{ GLSL_SHADER_COMMON_ALPHA_GEN_ONE_MINUS_VERTEX, "#define APPLY_ALPHA_ONE_MINUS_VERTEX\n", "_a1-v" },
	{ GLSL_SHADER_COMMON_ALPHA_GEN_VERTEX, "#define APPLY_ALPHA_VERTEX\n", "_av" },
	{ GLSL_SHADER_COMMON_ALPHA_GEN_CONST, "#define APPLY_ALPHA_CONST\n", "_ac" },
	{ GLSL_SHADER_COMMON_ALPHA_DISTANCERAMP, "#define APPLY_ALPHA_DISTANCERAMP\n", "_alpha_dr" },

	{ GLSL_SHADER_COMMON_FOG, "#define APPLY_FOG\n#define APPLY_FOG_IN 1\n", "_fog" },
	{ GLSL_SHADER_COMMON_FOG_RGB, "#define APPLY_FOG_COLOR\n", "_rgb" },

	{ GLSL_SHADER_COMMON_DLIGHTS_16, "#define NUM_DLIGHTS 16\n", "_dl16" },
	{ GLSL_SHADER_COMMON_DLIGHTS_12, "#define NUM_DLIGHTS 12\n", "_dl12" },
	{ GLSL_SHADER_COMMON_DLIGHTS_8, "#define NUM_DLIGHTS 8\n", "_dl8" },
	{ GLSL_SHADER_COMMON_DLIGHTS_4, "#define NUM_DLIGHTS 4\n", "_dl4" },

	{ GLSL_SHADER_COMMON_DRAWFLAT, "#define APPLY_DRAWFLAT\n", "_flat" },

	{ GLSL_SHADER_COMMON_AUTOSPRITE, "#define APPLY_AUTOSPRITE\n", "" },
	{ GLSL_SHADER_COMMON_AUTOSPRITE2, "#define APPLY_AUTOSPRITE2\n", "" },
	{ GLSL_SHADER_COMMON_AUTOPARTICLE, "#define APPLY_AUTOSPRITE\n#define APPLY_AUTOPARTICLE\n", "" },

	{ GLSL_SHADER_COMMON_INSTANCED_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n", "_instanced" },
	{ GLSL_SHADER_COMMON_INSTANCED_ATTRIB_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n"
	  "#define APPLY_INSTANCED_ATTRIB_TRANSFORMS\n", "_instanced_va" },

	{ GLSL_SHADER_COMMON_AFUNC_GE128, "#define QF_ALPHATEST(a) { if ((a) < 0.5) discard; }\n", "_afunc_ge128" },
	{ GLSL_SHADER_COMMON_AFUNC_LT128, "#define QF_ALPHATEST(a) { if ((a) >= 0.5) discard; }\n", "_afunc_lt128" },
	{ GLSL_SHADER_COMMON_AFUNC_GT0, "#define QF_ALPHATEST(a) { if ((a) <= 0.0) discard; }\n", "_afunc_gt0" },

	{ GLSL_SHADER_COMMON_TC_MOD, "#define APPLY_TC_MOD\n", "_tc_mod" },

	{ GLSL_SHADER_MATERIAL_LIGHTSTYLE3, "#define NUM_LIGHTMAPS 4\n#define qf_lmvec01 vec4\n#define qf_lmvec23 vec4\n", "_ls3" },
	{ GLSL_SHADER_MATERIAL_LIGHTSTYLE2, "#define NUM_LIGHTMAPS 3\n#define qf_lmvec01 vec4\n#define qf_lmvec23 vec2\n", "_ls2" },
	{ GLSL_SHADER_MATERIAL_LIGHTSTYLE1, "#define NUM_LIGHTMAPS 2\n#define qf_lmvec01 vec4\n", "_ls1" },
	{ GLSL_SHADER_MATERIAL_LIGHTSTYLE0, "#define NUM_LIGHTMAPS 1\n#define qf_lmvec01 vec2\n", "_ls0" },
	{ GLSL_SHADER_MATERIAL_LIGHTMAP_ARRAYS, "#define LIGHTMAP_ARRAYS\n", "_lmarray" },
	{ GLSL_SHADER_MATERIAL_FB_LIGHTMAP, "#define APPLY_FBLIGHTMAP\n", "_fb" },
	{ GLSL_SHADER_MATERIAL_DIRECTIONAL_LIGHT, "#define APPLY_DIRECTIONAL_LIGHT\n", "_dirlight" },

	{ GLSL_SHADER_MATERIAL_SPECULAR, "#define APPLY_SPECULAR\n", "_gloss" },
	{ GLSL_SHADER_MATERIAL_OFFSETMAPPING, "#define APPLY_OFFSETMAPPING\n", "_offmap" },
	{ GLSL_SHADER_MATERIAL_RELIEFMAPPING, "#define APPLY_RELIEFMAPPING\n", "_relmap" },
	{ GLSL_SHADER_MATERIAL_AMBIENT_COMPENSATION, "#define APPLY_AMBIENT_COMPENSATION\n", "_amb" },
	{ GLSL_SHADER_MATERIAL_DECAL, "#define APPLY_DECAL\n", "_decal" },
	{ GLSL_SHADER_MATERIAL_DECAL_ADD, "#define APPLY_DECAL_ADD\n", "_add" },
	{ GLSL_SHADER_MATERIAL_BASETEX_ALPHA_ONLY, "#define APPLY_BASETEX_ALPHA_ONLY\n", "_alpha" },
	{ GLSL_SHADER_MATERIAL_CELSHADING, "#define APPLY_CELSHADING\n", "_cel" },
	{ GLSL_SHADER_MATERIAL_HALFLAMBERT, "#define APPLY_HALFLAMBERT\n", "_lambert" },

	{ GLSL_SHADER_MATERIAL_ENTITY_DECAL, "#define APPLY_ENTITY_DECAL\n", "_decal2" },
	{ GLSL_SHADER_MATERIAL_ENTITY_DECAL_ADD, "#define APPLY_ENTITY_DECAL_ADD\n", "_decal2_add" },

	// doesn't make sense without APPLY_DIRECTIONAL_LIGHT
	{ GLSL_SHADER_MATERIAL_DIRECTIONAL_LIGHT_MIX, "#define APPLY_DIRECTIONAL_LIGHT_MIX\n", "_mix" },
	{ GLSL_SHADER_MATERIAL_DIRECTIONAL_LIGHT_FROM_NORMAL, "#define APPLY_DIRECTIONAL_LIGHT_FROM_NORMAL\n", "_normlight" },

	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_distortion[] =
{
	{ GLSL_SHADER_COMMON_GREYSCALE, "#define APPLY_GREYSCALE\n", "_grey" },

	{ GLSL_SHADER_COMMON_RGB_GEN_ONE_MINUS_VERTEX, "#define APPLY_RGB_ONE_MINUS_VERTEX\n", "_c1-v" },
	{ GLSL_SHADER_COMMON_RGB_GEN_CONST, "#define APPLY_RGB_CONST\n", "_cc" },
	{ GLSL_SHADER_COMMON_RGB_GEN_VERTEX, "#define APPLY_RGB_VERTEX\n", "_cv" },
	{ GLSL_SHADER_COMMON_RGB_DISTANCERAMP, "#define APPLY_RGB_DISTANCERAMP\n", "_rgb_dr" },

	{ GLSL_SHADER_COMMON_SRGB2LINEAR, "#define APPLY_SRGB2LINEAR\n", "_srgb" },
	{ GLSL_SHADER_COMMON_LINEAR2SRB, "#define APPLY_LINEAR2SRGB\n", "_linear" },

	{ GLSL_SHADER_COMMON_ALPHA_GEN_ONE_MINUS_VERTEX, "#define APPLY_ALPHA_ONE_MINUS_VERTEX\n", "_a1-v" },
	{ GLSL_SHADER_COMMON_ALPHA_GEN_CONST, "#define APPLY_ALPHA_CONST\n", "_ac" },
	{ GLSL_SHADER_COMMON_ALPHA_GEN_VERTEX, "#define APPLY_ALPHA_VERTEX\n", "_av" },
	{ GLSL_SHADER_COMMON_ALPHA_DISTANCERAMP, "#define APPLY_ALPHA_DISTANCERAMP\n", "_alpha_dr" },

	{ GLSL_SHADER_COMMON_AUTOSPRITE, "#define APPLY_AUTOSPRITE\n", "" },
	{ GLSL_SHADER_COMMON_AUTOSPRITE2, "#define APPLY_AUTOSPRITE2\n", "" },
	{ GLSL_SHADER_COMMON_AUTOPARTICLE, "#define APPLY_AUTOSPRITE\n#define APPLY_AUTOPARTICLE\n", "" },

	{ GLSL_SHADER_COMMON_INSTANCED_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n", "_instanced" },
	{ GLSL_SHADER_COMMON_INSTANCED_ATTRIB_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n"
	  "#define APPLY_INSTANCED_ATTRIB_TRANSFORMS\n", "_instanced_va" },

	{ GLSL_SHADER_COMMON_FOG, "#define APPLY_FOG\n#define APPLY_FOG_IN 1\n", "_fog" },
	{ GLSL_SHADER_COMMON_FOG_RGB, "#define APPLY_FOG_COLOR\n", "_rgb" },

	{ GLSL_SHADER_DISTORTION_DUDV, "#define APPLY_DUDV\n", "_dudv" },
	{ GLSL_SHADER_DISTORTION_EYEDOT, "#define APPLY_EYEDOT\n", "_eyedot" },
	{ GLSL_SHADER_DISTORTION_DISTORTION_ALPHA, "#define APPLY_DISTORTION_ALPHA\n", "_alpha" },
	{ GLSL_SHADER_DISTORTION_REFLECTION, "#define APPLY_REFLECTION\n", "_refl" },
	{ GLSL_SHADER_DISTORTION_REFRACTION, "#define APPLY_REFRACTION\n", "_refr" },

	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_rgbshadow[] =
{
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS4, "#define QF_NUM_BONE_INFLUENCES 4\n", "_bones4" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS3, "#define QF_NUM_BONE_INFLUENCES 3\n", "_bones3" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS2, "#define QF_NUM_BONE_INFLUENCES 2\n", "_bones2" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS1, "#define QF_NUM_BONE_INFLUENCES 1\n", "_bones1" },

	{ GLSL_SHADER_COMMON_INSTANCED_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n", "_instanced" },
	{ GLSL_SHADER_COMMON_INSTANCED_ATTRIB_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n#define APPLY_INSTANCED_ATTRIB_TRANSFORMS\n", "_instanced_va" },

	{ GLSL_SHADER_RGBSHADOW_24BIT, "#define APPLY_RGB_SHADOW_24BIT\n", "_rgb24" },

	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_shadowmap[] =
{
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS4, "#define QF_NUM_BONE_INFLUENCES 4\n", "_bones4" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS3, "#define QF_NUM_BONE_INFLUENCES 3\n", "_bones3" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS2, "#define QF_NUM_BONE_INFLUENCES 2\n", "_bones2" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS1, "#define QF_NUM_BONE_INFLUENCES 1\n", "_bones1" },

	{ GLSL_SHADER_COMMON_INSTANCED_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n", "_instanced" },
	{ GLSL_SHADER_COMMON_INSTANCED_ATTRIB_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n#define APPLY_INSTANCED_ATTRIB_TRANSFORMS\n", "_instanced_va" },

	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_outline[] =
{
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS4, "#define QF_NUM_BONE_INFLUENCES 4\n", "_bones4" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS3, "#define QF_NUM_BONE_INFLUENCES 3\n", "_bones3" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS2, "#define QF_NUM_BONE_INFLUENCES 2\n", "_bones2" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS1, "#define QF_NUM_BONE_INFLUENCES 1\n", "_bones1" },

	{ GLSL_SHADER_COMMON_RGB_GEN_CONST, "#define APPLY_RGB_CONST\n", "_cc" },
	{ GLSL_SHADER_COMMON_ALPHA_GEN_CONST, "#define APPLY_ALPHA_CONST\n", "_ac" },

	{ GLSL_SHADER_COMMON_FOG, "#define APPLY_FOG\n#define APPLY_FOG_IN 1\n", "_fog" },
	{ GLSL_SHADER_COMMON_FOG_RGB, "#define APPLY_FOG_COLOR\n", "_rgb" },

	{ GLSL_SHADER_COMMON_INSTANCED_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n", "_instanced" },
	{ GLSL_SHADER_COMMON_INSTANCED_ATTRIB_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n#define APPLY_INSTANCED_ATTRIB_TRANSFORMS\n", "_instanced_va" },

	{ GLSL_SHADER_OUTLINE_OUTLINES_CUTOFF, "#define APPLY_OUTLINES_CUTOFF\n", "_outcut" },

	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_q3a[] =
{
	{ GLSL_SHADER_COMMON_GREYSCALE, "#define APPLY_GREYSCALE\n", "_grey" },

	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS4, "#define QF_NUM_BONE_INFLUENCES 4\n", "_bones4" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS3, "#define QF_NUM_BONE_INFLUENCES 3\n", "_bones3" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS2, "#define QF_NUM_BONE_INFLUENCES 2\n", "_bones2" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS1, "#define QF_NUM_BONE_INFLUENCES 1\n", "_bones1" },

	{ GLSL_SHADER_COMMON_RGB_GEN_ONE_MINUS_VERTEX, "#define APPLY_RGB_ONE_MINUS_VERTEX\n", "_c1-v" },
	{ GLSL_SHADER_COMMON_RGB_GEN_CONST, "#define APPLY_RGB_CONST\n", "_cc" },
	{ GLSL_SHADER_COMMON_RGB_GEN_VERTEX, "#define APPLY_RGB_VERTEX\n", "_cv" },
	{ GLSL_SHADER_COMMON_RGB_DISTANCERAMP, "#define APPLY_RGB_DISTANCERAMP\n", "_rgb_dr" },

	{ GLSL_SHADER_COMMON_ALPHA_GEN_ONE_MINUS_VERTEX, "#define APPLY_ALPHA_ONE_MINUS_VERTEX\n", "_a1-v" },
	{ GLSL_SHADER_COMMON_ALPHA_GEN_CONST, "#define APPLY_ALPHA_CONST\n", "_ac" },
	{ GLSL_SHADER_COMMON_ALPHA_GEN_VERTEX, "#define APPLY_ALPHA_VERTEX\n", "_av" },
	{ GLSL_SHADER_COMMON_ALPHA_DISTANCERAMP, "#define APPLY_ALPHA_DISTANCERAMP\n", "_alpha_dr" },

	{ GLSL_SHADER_COMMON_FOG, "#define APPLY_FOG\n#define APPLY_FOG_IN 1\n", "_fog" },
	{ GLSL_SHADER_COMMON_FOG_RGB, "#define APPLY_FOG_COLOR\n", "_rgb" },

	{ GLSL_SHADER_COMMON_DLIGHTS_16, "#define NUM_DLIGHTS 16\n", "_dl16" },
	{ GLSL_SHADER_COMMON_DLIGHTS_12, "#define NUM_DLIGHTS 12\n", "_dl12" },
	{ GLSL_SHADER_COMMON_DLIGHTS_8, "#define NUM_DLIGHTS 8\n", "_dl8" },
	{ GLSL_SHADER_COMMON_DLIGHTS_4, "#define NUM_DLIGHTS 4\n", "_dl4" },

	{ GLSL_SHADER_COMMON_DRAWFLAT, "#define APPLY_DRAWFLAT\n", "_flat" },

	{ GLSL_SHADER_COMMON_AUTOSPRITE, "#define APPLY_AUTOSPRITE\n", "" },
	{ GLSL_SHADER_COMMON_AUTOSPRITE2, "#define APPLY_AUTOSPRITE2\n", "" },
	{ GLSL_SHADER_COMMON_AUTOPARTICLE, "#define APPLY_AUTOSPRITE\n#define APPLY_AUTOPARTICLE\n", "" },

	{ GLSL_SHADER_COMMON_INSTANCED_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n", "_instanced" },
	{ GLSL_SHADER_COMMON_INSTANCED_ATTRIB_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n#define APPLY_INSTANCED_ATTRIB_TRANSFORMS\n", "_instanced_va" },

	{ GLSL_SHADER_COMMON_SOFT_PARTICLE, "#define APPLY_SOFT_PARTICLE\n", "_sp" },

	{ GLSL_SHADER_COMMON_AFUNC_GE128, "#define QF_ALPHATEST(a) { if ((a) < 0.5) discard; }\n", "_afunc_ge128" },
	{ GLSL_SHADER_COMMON_AFUNC_LT128, "#define QF_ALPHATEST(a) { if ((a) >= 0.5) discard; }\n", "_afunc_lt128" },
	{ GLSL_SHADER_COMMON_AFUNC_GT0, "#define QF_ALPHATEST(a) { if ((a) <= 0.0) discard; }\n", "_afunc_gt0" },

	{ GLSL_SHADER_COMMON_TC_MOD, "#define APPLY_TC_MOD\n", "_tc_mod" },

	{ GLSL_SHADER_COMMON_SRGB2LINEAR, "#define APPLY_SRGB2LINEAR\n", "_srgb" },
	{ GLSL_SHADER_COMMON_LINEAR2SRB, "#define APPLY_LINEAR2SRGB\n", "_linear" },

	{ GLSL_SHADER_Q3_TC_GEN_CELSHADE, "#define APPLY_TC_GEN_CELSHADE\n", "_tc_cel" },
	{ GLSL_SHADER_Q3_TC_GEN_PROJECTION, "#define APPLY_TC_GEN_PROJECTION\n", "_tc_proj" },
	{ GLSL_SHADER_Q3_TC_GEN_REFLECTION, "#define APPLY_TC_GEN_REFLECTION\n", "_tc_refl" },
	{ GLSL_SHADER_Q3_TC_GEN_ENV, "#define APPLY_TC_GEN_ENV\n", "_tc_env" },
	{ GLSL_SHADER_Q3_TC_GEN_VECTOR, "#define APPLY_TC_GEN_VECTOR\n", "_tc_vec" },
	{ GLSL_SHADER_Q3_TC_GEN_SURROUND, "#define APPLY_TC_GEN_SURROUND\n", "_tc_surr" },

	{ GLSL_SHADER_Q3_LIGHTSTYLE3, "#define NUM_LIGHTMAPS 4\n#define qf_lmvec01 vec4\n#define qf_lmvec23 vec4\n", "_ls3" },
	{ GLSL_SHADER_Q3_LIGHTSTYLE2, "#define NUM_LIGHTMAPS 3\n#define qf_lmvec01 vec4\n#define qf_lmvec23 vec2\n", "_ls2" },
	{ GLSL_SHADER_Q3_LIGHTSTYLE1, "#define NUM_LIGHTMAPS 2\n#define qf_lmvec01 vec4\n", "_ls1" },
	{ GLSL_SHADER_Q3_LIGHTSTYLE0, "#define NUM_LIGHTMAPS 1\n#define qf_lmvec01 vec2\n", "_ls0" },

	{ GLSL_SHADER_Q3_LIGHTMAP_ARRAYS, "#define LIGHTMAP_ARRAYS\n", "_lmarray" },

	{ GLSL_SHADER_Q3_ALPHA_MASK, "#define APPLY_ALPHA_MASK\n", "_alpha_mask" },

	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_celshade[] =
{
	{ GLSL_SHADER_COMMON_GREYSCALE, "#define APPLY_GREYSCALE\n", "_grey" },

	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS4, "#define QF_NUM_BONE_INFLUENCES 4\n", "_bones4" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS3, "#define QF_NUM_BONE_INFLUENCES 3\n", "_bones3" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS2, "#define QF_NUM_BONE_INFLUENCES 2\n", "_bones2" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS1, "#define QF_NUM_BONE_INFLUENCES 1\n", "_bones1" },

	{ GLSL_SHADER_COMMON_AUTOSPRITE, "#define APPLY_AUTOSPRITE\n", "" },
	{ GLSL_SHADER_COMMON_AUTOSPRITE2, "#define APPLY_AUTOSPRITE2\n", "" },
	{ GLSL_SHADER_COMMON_AUTOPARTICLE, "#define APPLY_AUTOSPRITE\n#define APPLY_AUTOPARTICLE\n", "" },

	{ GLSL_SHADER_COMMON_RGB_GEN_ONE_MINUS_VERTEX, "#define APPLY_RGB_ONE_MINUS_VERTEX\n", "_c1-v" },
	{ GLSL_SHADER_COMMON_RGB_GEN_CONST, "#define APPLY_RGB_CONST\n", "_cc" },
	{ GLSL_SHADER_COMMON_RGB_GEN_VERTEX, "#define APPLY_RGB_VERTEX\n", "_cv" },

	{ GLSL_SHADER_COMMON_SRGB2LINEAR, "#define APPLY_SRGB2LINEAR\n", "_srgb" },
	{ GLSL_SHADER_COMMON_LINEAR2SRB, "#define APPLY_LINEAR2SRGB\n", "_linear" },

	{ GLSL_SHADER_COMMON_ALPHA_GEN_ONE_MINUS_VERTEX, "#define APPLY_ALPHA_ONE_MINUS_VERTEX\n", "_a1-v" },
	{ GLSL_SHADER_COMMON_ALPHA_GEN_VERTEX, "#define APPLY_ALPHA_VERTEX\n", "_av" },
	{ GLSL_SHADER_COMMON_ALPHA_GEN_CONST, "#define APPLY_ALPHA_CONST\n", "_ac" },

	{ GLSL_SHADER_COMMON_FOG, "#define APPLY_FOG\n#define APPLY_FOG_IN 1\n", "_fog" },
	{ GLSL_SHADER_COMMON_FOG_RGB, "#define APPLY_FOG_COLOR\n", "_rgb" },

	{ GLSL_SHADER_COMMON_INSTANCED_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n", "_instanced" },
	{ GLSL_SHADER_COMMON_INSTANCED_ATTRIB_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n#define APPLY_INSTANCED_ATTRIB_TRANSFORMS\n", "_instanced_va" },

	{ GLSL_SHADER_COMMON_AFUNC_GE128, "#define QF_ALPHATEST(a) { if ((a) < 0.5) discard; }\n", "_afunc_ge128" },
	{ GLSL_SHADER_COMMON_AFUNC_LT128, "#define QF_ALPHATEST(a) { if ((a) >= 0.5) discard; }\n", "_afunc_lt128" },
	{ GLSL_SHADER_COMMON_AFUNC_GT0, "#define QF_ALPHATEST(a) { if ((a) <= 0.0) discard; }\n", "_afunc_gt0" },

	{ GLSL_SHADER_CELSHADE_DIFFUSE, "#define APPLY_DIFFUSE\n", "_diff" },
	{ GLSL_SHADER_CELSHADE_DECAL, "#define APPLY_DECAL\n", "_decal" },
	{ GLSL_SHADER_CELSHADE_DECAL_ADD, "#define APPLY_DECAL_ADD\n", "_decal" },
	{ GLSL_SHADER_CELSHADE_ENTITY_DECAL, "#define APPLY_ENTITY_DECAL\n", "_edecal" },
	{ GLSL_SHADER_CELSHADE_ENTITY_DECAL_ADD, "#define APPLY_ENTITY_DECAL_ADD\n", "_add" },
	{ GLSL_SHADER_CELSHADE_STRIPES, "#define APPLY_STRIPES\n", "_stripes" },
	{ GLSL_SHADER_CELSHADE_STRIPES_ADD, "#define APPLY_STRIPES_ADD\n", "_stripes_add" },
	{ GLSL_SHADER_CELSHADE_CEL_LIGHT, "#define APPLY_CEL_LIGHT\n", "_light" },
	{ GLSL_SHADER_CELSHADE_CEL_LIGHT_ADD, "#define APPLY_CEL_LIGHT_ADD\n", "_add" },

	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_fog[] =
{
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS4, "#define QF_NUM_BONE_INFLUENCES 4\n", "_bones4" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS3, "#define QF_NUM_BONE_INFLUENCES 3\n", "_bones3" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS2, "#define QF_NUM_BONE_INFLUENCES 2\n", "_bones2" },
	{ GLSL_SHADER_COMMON_BONE_TRANSFORMS1, "#define QF_NUM_BONE_INFLUENCES 1\n", "_bones1" },

	{ GLSL_SHADER_COMMON_SRGB2LINEAR, "#define APPLY_SRGB2LINEAR\n", "_srgb" },
	{ GLSL_SHADER_COMMON_LINEAR2SRB, "#define APPLY_LINEAR2SRGB\n", "_linear" },

	{ GLSL_SHADER_COMMON_FOG, "#define APPLY_FOG\n", "_fog" },
	{ GLSL_SHADER_COMMON_FOG_RGB, "#define APPLY_FOG_COLOR\n", "_rgb" },

	{ GLSL_SHADER_COMMON_AUTOSPRITE, "#define APPLY_AUTOSPRITE\n", "" },
	{ GLSL_SHADER_COMMON_AUTOSPRITE2, "#define APPLY_AUTOSPRITE2\n", "" },
	{ GLSL_SHADER_COMMON_AUTOPARTICLE, "#define APPLY_AUTOSPRITE\n#define APPLY_AUTOPARTICLE\n", "" },

	{ GLSL_SHADER_COMMON_INSTANCED_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n", "_instanced" },
	{ GLSL_SHADER_COMMON_INSTANCED_ATTRIB_TRANSFORMS, "#define APPLY_INSTANCED_TRANSFORMS\n#define APPLY_INSTANCED_ATTRIB_TRANSFORMS\n", "_instanced_va" },

	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_fxaa[] =
{
	{ GLSL_SHADER_FXAA_FXAA3, "#define APPLY_FXAA3\n", "_fxaa3" },

	{ 0, NULL, NULL }
};

static const glsl_feature_t glsl_features_colcorrection[] =
{
	{ GLSL_SHADER_COMMON_SRGB2LINEAR, "#define APPLY_SRGB2LINEAR\n", "_srgb" },
	{ GLSL_SHADER_COMMON_LINEAR2SRB, "#define APPLY_LINEAR2SRGB\n", "_linear" },

	{ GLSL_SHADER_COLOR_CORRECTION_LUT, "#define APPLY_LUT\n", "_lut" },
	{ GLSL_SHADER_COLOR_CORRECTION_HDR, "#define APPLY_HDR\n", "_hdr" },
	{ GLSL_SHADER_COLOR_CORRECTION_OVERBRIGHT, "#define APPLY_OVEBRIGHT\n", "_obloom" },
	{ GLSL_SHADER_COLOR_CORRECTION_BLOOM, "#define APPLY_BLOOM\n", "_bloom" },

	{ 0, NULL, NULL }
};


static const glsl_feature_t * const glsl_programtypes_features[] =
{
	// GLSL_PROGRAM_TYPE_NONE
	NULL,
	// GLSL_PROGRAM_TYPE_MATERIAL
	glsl_features_material,
	// GLSL_PROGRAM_TYPE_DISTORTION
	glsl_features_distortion,
	// GLSL_PROGRAM_TYPE_RGB_SHADOW
	glsl_features_rgbshadow,
	// GLSL_PROGRAM_TYPE_SHADOWMAP
	glsl_features_shadowmap,
	// GLSL_PROGRAM_TYPE_OUTLINE
	glsl_features_outline,
	// GLSL_PROGRAM_TYPE_UNUSED
	glsl_features_empty,
	// GLSL_PROGRAM_TYPE_Q3A_SHADER
	glsl_features_q3a,
	// GLSL_PROGRAM_TYPE_CELSHADE
	glsl_features_celshade,
	// GLSL_PROGRAM_TYPE_FOG
	glsl_features_fog,
	// GLSL_PROGRAM_TYPE_FXAA
	glsl_features_fxaa,
	// GLSL_PROGRAM_TYPE_YUV
	glsl_features_empty,
	// GLSL_PROGRAM_TYPE_COLOR_CORRECTION
	glsl_features_colcorrection,
	// GLSL_PROGRAM_TYPE_KAWASE_BLUR
	glsl_features_empty,
};

// ======================================================================================

#ifndef STR_HELPER
#define STR_HELPER( s )                 # s
#define STR_TOSTR( x )                  STR_HELPER( x )
#endif

#define QF_GLSL_VERSION120 "#version 120\n"
#define QF_GLSL_VERSION130 "#version 130\n"
#define QF_GLSL_VERSION140 "#version 140\n"

#define QF_GLSL_ENABLE_ARB_GPU_SHADER5 "#extension GL_ARB_gpu_shader5 : enable\n"
#define QF_GLSL_ENABLE_ARB_DRAW_INSTANCED "#extension GL_ARB_draw_instanced : enable\n"
#define QF_GLSL_ENABLE_EXT_TEXTURE_ARRAY "#extension GL_EXT_texture_array : enable\n"

#define QF_BUILTIN_GLSL_MACROS "" \
	"#if !defined(myhalf)\n" \
	"//#if !defined(__GLSL_CG_DATA_TYPES)\n" \
	"#define myhalf float\n" \
	"#define myhalf2 vec2\n" \
	"#define myhalf3 vec3\n" \
	"#define myhalf4 vec4\n" \
	"//#else\n" \
	"//#define myhalf half\n" \
	"//#define myhalf2 half2\n" \
	"//#define myhalf3 half3\n" \
	"//#define myhalf4 half4\n" \
	"//#endif\n" \
	"#endif\n" \
	"#ifdef GL_ES\n" \
	"#define qf_lowp_float lowp float\n" \
	"#define qf_lowp_vec2 lowp vec2\n" \
	"#define qf_lowp_vec3 lowp vec3\n" \
	"#define qf_lowp_vec4 lowp vec4\n" \
	"#else\n" \
	"#define qf_lowp_float float\n" \
	"#define qf_lowp_vec2 vec2\n" \
	"#define qf_lowp_vec3 vec3\n" \
	"#define qf_lowp_vec4 vec4\n" \
	"#endif\n" \
	"\n"

#define QF_BUILTIN_GLSL_MACROS_GLSL120 "" \
	"#define qf_varying varying\n" \
	"#define qf_flat_varying varying\n" \
	"#ifdef VERTEX_SHADER\n" \
	"# define qf_FrontColor gl_FrontColor\n" \
	"# define qf_attribute attribute\n" \
	"#endif\n" \
	"#ifdef FRAGMENT_SHADER\n" \
	"# define qf_FrontColor gl_Color\n" \
	"# define qf_FragColor gl_FragColor\n" \
	"# define qf_BrightColor gl_FragData[1]\n" \
	"#endif\n" \
	"#define qf_texture texture2D\n" \
	"#define qf_textureLod texture2DLod\n" \
	"#define qf_textureCube textureCube\n" \
	"#define qf_textureArray texture2DArray\n" \
	"#define qf_texture3D texture3D\n" \
	"#define qf_textureOffset(a,b,c,d) texture2DOffset(a,b,ivec2(c,d))\n" \
	"#define qf_shadow shadow2D\n" \
	"\n"

#define QF_BUILTIN_GLSL_MACROS_GLSL130 "" \
	"precision highp float;\n" \
	"#ifdef VERTEX_SHADER\n" \
	"  out myhalf4 qf_FrontColor;\n" \
	"# define qf_varying out\n" \
	"# define qf_flat_varying flat out\n" \
	"# define qf_attribute in\n" \
	"#endif\n" \
	"#ifdef FRAGMENT_SHADER\n" \
	"  in myhalf4 qf_FrontColor;\n" \
	"  out myhalf4 qf_FragColor;\n" \
	"  out myhalf4 qf_BrightColor;\n" \
	"# define qf_varying in\n" \
	"# define qf_flat_varying flat in\n" \
	"#endif\n" \
	"#define qf_texture texture\n" \
	"#define qf_textureCube texture\n" \
	"#define qf_textureLod textureLod\n" \
	"#define qf_textureArray texture\n" \
	"#define qf_texture3D texture\n" \
	"#define qf_textureOffset(a,b,c,d) textureOffset(a,b,ivec2(c,d))\n" \
	"#define qf_shadow texture\n" \
	"\n"

#define QF_GLSL_PI "" \
	"#ifndef M_PI\n" \
	"#define M_PI 3.14159265358979323846\n" \
	"#endif\n" \
	"#ifndef M_TWOPI\n" \
	"#define M_TWOPI 6.28318530717958647692\n" \
	"#endif\n"

#define QF_BUILTIN_GLSL_CONSTANTS \
	QF_GLSL_PI \
	"\n" \
	"#ifndef MAX_UNIFORM_INSTANCES\n" \
	"#define MAX_UNIFORM_INSTANCES " STR_TOSTR( MAX_GLSL_UNIFORM_INSTANCES ) "\n" \
	"#endif\n"

#define QF_BUILTIN_GLSL_UNIFORMS \
	"uniform vec3 u_QF_ViewOrigin;\n" \
	"uniform mat3 u_QF_ViewAxis;\n" \
	"uniform float u_QF_MirrorSide;\n" \
	"uniform vec3 u_QF_EntityOrigin;\n" \
	"uniform float u_QF_ShaderTime;\n"

#define QF_BUILTIN_GLSL_QUAT_TRANSFORM_OVERLOAD \
	"#ifdef QF_DUAL_QUAT_TRANSFORM_TANGENT\n" \
	"void QF_VertexDualQuatsTransform_Tangent(inout vec4 Position, inout vec3 Normal, inout vec3 Tangent)\n" \
	"#else\n" \
	"void QF_VertexDualQuatsTransform(inout vec4 Position, inout vec3 Normal)\n" \
	"#endif\n" \
	"{\n" \
	"	ivec4 Indices = ivec4(a_BonesIndices * 2.0);\n" \
	"	vec4 DQReal = u_DualQuats[Indices.x];\n" \
	"	vec4 DQDual = u_DualQuats[Indices.x + 1];\n" \
	"#if QF_NUM_BONE_INFLUENCES >= 2\n" \
	"	DQReal *= a_BonesWeights.x;\n" \
	"	DQDual *= a_BonesWeights.x;\n" \
	"	vec4 DQReal1 = u_DualQuats[Indices.y];\n" \
	"	vec4 DQDual1 = u_DualQuats[Indices.y + 1];\n" \
	"	float Scale = mix(-1.0, 1.0, step(0.0, dot(DQReal1, DQReal))) * a_BonesWeights.y;\n" \
	"	DQReal += DQReal1 * Scale;\n" \
	"	DQDual += DQDual1 * Scale;\n" \
	"#if QF_NUM_BONE_INFLUENCES >= 3\n" \
	"	DQReal1 = u_DualQuats[Indices.z];\n" \
	"	DQDual1 = u_DualQuats[Indices.z + 1];\n" \
	"	Scale = mix(-1.0, 1.0, step(0.0, dot(DQReal1, DQReal))) * a_BonesWeights.z;\n" \
	"	DQReal += DQReal1 * Scale;\n" \
	"	DQDual += DQDual1 * Scale;\n" \
	"#if QF_NUM_BONE_INFLUENCES >= 4\n" \
	"	DQReal1 = u_DualQuats[Indices.w];\n" \
	"	DQDual1 = u_DualQuats[Indices.w + 1];\n" \
	"	Scale = mix(-1.0, 1.0, step(0.0, dot(DQReal1, DQReal))) * a_BonesWeights.w;\n" \
	"	DQReal += DQReal1 * Scale;\n" \
	"	DQDual += DQDual1 * Scale;\n" \
	"#endif // QF_NUM_BONE_INFLUENCES >= 4\n" \
	"#endif // QF_NUM_BONE_INFLUENCES >= 3\n" \
	"	float Len = 1.0 / length(DQReal);\n" \
	"	DQReal *= Len;\n" \
	"	DQDual *= Len;\n" \
	"#endif // QF_NUM_BONE_INFLUENCES >= 2\n" \
	"	Position.xyz += (cross(DQReal.xyz, cross(DQReal.xyz, Position.xyz) + Position.xyz * DQReal.w + DQDual.xyz) +\n" \
	"		DQDual.xyz*DQReal.w - DQReal.xyz*DQDual.w) * 2.0;\n" \
	"	Normal += cross(DQReal.xyz, cross(DQReal.xyz, Normal) + Normal * DQReal.w) * 2.0;\n" \
	"#ifdef QF_DUAL_QUAT_TRANSFORM_TANGENT\n" \
	"	Tangent += cross(DQReal.xyz, cross(DQReal.xyz, Tangent) + Tangent * DQReal.w) * 2.0;\n" \
	"#endif\n" \
	"}\n" \
	"\n"

#define QF_BUILTIN_GLSL_QUAT_TRANSFORM \
	"qf_attribute vec4 a_BonesIndices, a_BonesWeights;\n" \
	"uniform vec4 u_DualQuats[MAX_UNIFORM_BONES*2];\n" \
	"\n" \
	QF_BUILTIN_GLSL_QUAT_TRANSFORM_OVERLOAD \
	"#define QF_DUAL_QUAT_TRANSFORM_TANGENT\n" \
	QF_BUILTIN_GLSL_QUAT_TRANSFORM_OVERLOAD \
	"#undef QF_DUAL_QUAT_TRANSFORM_TANGENT\n"

#define QF_BUILTIN_GLSL_INSTANCED_TRANSFORMS \
	"#if defined(APPLY_INSTANCED_ATTRIB_TRANSFORMS)\n" \
	"qf_attribute vec4 a_InstanceQuat, a_InstancePosAndScale;\n" \
	"#elif defined(GL_ARB_draw_instanced) || (defined(GL_ES) && (__VERSION__ >= 300))\n" \
	"uniform vec4 u_InstancePoints[MAX_UNIFORM_INSTANCES*2];\n" \
	"#define a_InstanceQuat u_InstancePoints[gl_InstanceID*2]\n" \
	"#define a_InstancePosAndScale u_InstancePoints[gl_InstanceID*2+1]\n" \
	"#else\n" \
	"uniform vec4 u_InstancePoints[2];\n" \
	"#define a_InstanceQuat u_InstancePoints[0]\n" \
	"#define a_InstancePosAndScale u_InstancePoints[1]\n" \
	"#endif // APPLY_INSTANCED_ATTRIB_TRANSFORMS\n" \
	"\n" \
	"void QF_InstancedTransform(inout vec4 Position, inout vec3 Normal)\n" \
	"{\n" \
	"	Position.xyz = (cross(a_InstanceQuat.xyz,\n" \
	"		cross(a_InstanceQuat.xyz, Position.xyz) + Position.xyz*a_InstanceQuat.w)*2.0 +\n" \
	"		Position.xyz) * a_InstancePosAndScale.w + a_InstancePosAndScale.xyz;\n" \
	"	Normal = cross(a_InstanceQuat.xyz, cross(a_InstanceQuat.xyz, Normal) + Normal*a_InstanceQuat.w)*2.0 + Normal;\n" \
	"}\n" \
	"\n"

// We have to use these #ifdefs here because #defining prototypes
// of these functions to nothing results in a crash on Intel GPUs.
#define QF_BUILTIN_GLSL_TRANSFORM_VERTS \
	"void QF_TransformVerts(inout vec4 Position, inout vec3 Normal, inout vec2 TexCoord)\n" \
	"{\n" \
	"#	ifdef QF_NUM_BONE_INFLUENCES\n" \
	"		QF_VertexDualQuatsTransform(Position, Normal);\n" \
	"#	endif\n" \
	"#	ifdef QF_APPLY_DEFORMVERTS\n" \
	"		QF_DeformVerts(Position, Normal, TexCoord);\n" \
	"#	endif\n" \
	"#	ifdef APPLY_INSTANCED_TRANSFORMS\n" \
	"		QF_InstancedTransform(Position, Normal);\n" \
	"#	endif\n" \
	"}\n" \
	"\n" \
	"void QF_TransformVerts_Tangent(inout vec4 Position, inout vec3 Normal, inout vec3 Tangent, inout vec2 TexCoord)\n" \
	"{\n" \
	"#	ifdef QF_NUM_BONE_INFLUENCES\n" \
	"		QF_VertexDualQuatsTransform_Tangent(Position, Normal, Tangent);\n" \
	"#	endif\n" \
	"#	ifdef QF_APPLY_DEFORMVERTS\n" \
	"		QF_DeformVerts(Position, Normal, TexCoord);\n" \
	"#	endif\n" \
	"#	ifdef APPLY_INSTANCED_TRANSFORMS\n" \
	"		QF_InstancedTransform(Position, Normal);\n" \
	"#	endif\n" \
	"}\n" \
	"\n"

#define QF_GLSL_WAVEFUNCS \
	"\n" \
	QF_GLSL_PI \
	"\n" \
	"#ifndef WAVE_SIN\n" \
	"float QF_WaveFunc_Sin(float x)\n" \
	"{\n" \
	"return sin(fract(x) * M_TWOPI);\n" \
	"}\n" \
	"float QF_WaveFunc_Triangle(float x)\n" \
	"{\n" \
	"x = fract(x);\n" \
	"return step(x, 0.25) * x * 4.0 + (2.0 - 4.0 * step(0.25, x) * step(x, 0.75) * x) + ((step(0.75, x) * x - 0.75) * 4.0 - 1.0);\n" \
	"}\n" \
	"float QF_WaveFunc_Square(float x)\n" \
	"{\n" \
	"return step(fract(x), 0.5) * 2.0 - 1.0;\n" \
	"}\n" \
	"float QF_WaveFunc_Sawtooth(float x)\n" \
	"{\n" \
	"return fract(x);\n" \
	"}\n" \
	"float QF_WaveFunc_InverseSawtooth(float x)\n" \
	"{\n" \
	"return 1.0 - fract(x);\n" \
	"}\n" \
	"\n" \
	"#define WAVE_SIN(time,base,amplitude,phase,freq) (((base)+(amplitude)*QF_WaveFunc_Sin((phase)+(time)*(freq))))\n" \
	"#define WAVE_TRIANGLE(time,base,amplitude,phase,freq) (((base)+(amplitude)*QF_WaveFunc_Triangle((phase)+(time)*(freq))))\n" \
	"#define WAVE_SQUARE(time,base,amplitude,phase,freq) (((base)+(amplitude)*QF_WaveFunc_Square((phase)+(time)*(freq))))\n" \
	"#define WAVE_SAWTOOTH(time,base,amplitude,phase,freq) (((base)+(amplitude)*QF_WaveFunc_Sawtooth((phase)+(time)*(freq))))\n" \
	"#define WAVE_INVERSESAWTOOTH(time,base,amplitude,phase,freq) (((base)+(amplitude)*QF_WaveFunc_InverseSawtooth((phase)+(time)*(freq))))\n" \
	"#endif\n" \
	"\n"

#define QF_GLSL_MATH \
	"#define QF_LatLong2Norm(ll) vec3(cos((ll).y) * sin((ll).x), sin((ll).y) * sin((ll).x), cos((ll).x))\n" \
	"\n"

/*
* R_GLSLBuildDeformv
*
* Converts some of the Q3A vertex deforms to a GLSL vertex shader.
* Supported deforms are: wave, move, bulge.
* NOTE: Autosprite deforms can only be performed in a geometry shader.
* NULL is returned in case an unsupported deform is passed.
*/
static const char *R_GLSLBuildDeformv( const deformv_t *deformv, int numDeforms ) {
	int i;
	int funcType;
	char tmp[256];
	static char program[40 * 1024];
	static const char * const funcs[] = {
		NULL, "WAVE_SIN", "WAVE_TRIANGLE", "WAVE_SQUARE", "WAVE_SAWTOOTH", "WAVE_INVERSESAWTOOTH", NULL
	};
	static const int numSupportedFuncs = sizeof( funcs ) / sizeof( funcs[0] ) - 1;

	if( !numDeforms ) {
		return NULL;
	}

	program[0] = '\0';
	Q_strncpyz( program,
				"#define QF_APPLY_DEFORMVERTS\n"
				"#if defined(APPLY_AUTOSPRITE) || defined(APPLY_AUTOSPRITE2)\n"
				"qf_attribute vec4 a_SpritePoint;\n"
				"#else\n"
				"#define a_SpritePoint vec4(0.0)\n"
				"#endif\n"
				"\n"
				"#if defined(APPLY_AUTOSPRITE2)\n"
				"qf_attribute vec4 a_SpriteRightUpAxis;\n"
				"#else\n"
				"#define a_SpriteRightUpAxis vec4(0.0)\n"
				"#endif\n"
				"\n"
				"void QF_DeformVerts(inout vec4 Position, inout vec3 Normal, inout vec2 TexCoord)\n"
				"{\n"
				"float t = 0.0;\n"
				"vec3 dist;\n"
				"vec3 right, up, forward, newright;\n"
				"\n"
				"#if defined(WAVE_SIN)\n"
				, sizeof( program ) );

	for( i = 0; i < numDeforms; i++, deformv++ ) {
		switch( deformv->type ) {
			case DEFORMV_WAVE:
				funcType = deformv->func.type;
				if( funcType <= SHADER_FUNC_NONE || funcType > numSupportedFuncs || !funcs[funcType] ) {
					return NULL;
				}

				Q_strncatz( program, va_r( tmp, sizeof( tmp ), "Position.xyz += %s(u_QF_ShaderTime,%f,%f,%f+%f*(Position.x+Position.y+Position.z),%f) * Normal.xyz;\n",
										   funcs[funcType], deformv->func.args[0], deformv->func.args[1], deformv->func.args[2], deformv->func.args[3] ? deformv->args[0] : 0.0, deformv->func.args[3] ),
							sizeof( program ) );
				break;
			case DEFORMV_MOVE:
				funcType = deformv->func.type;
				if( funcType <= SHADER_FUNC_NONE || funcType > numSupportedFuncs || !funcs[funcType] ) {
					return NULL;
				}

				Q_strncatz( program, va_r( tmp, sizeof( tmp ), "Position.xyz += %s(u_QF_ShaderTime,%f,%f,%f,%f) * vec3(%f, %f, %f);\n",
										   funcs[funcType], deformv->func.args[0], deformv->func.args[1], deformv->func.args[2], deformv->func.args[3],
										   deformv->args[0], deformv->args[1], deformv->args[2] ),
							sizeof( program ) );
				break;
			case DEFORMV_BULGE:
				Q_strncatz( program, va_r( tmp, sizeof( tmp ),
										   "t = sin(TexCoord.s * %f + u_QF_ShaderTime * %f);\n"
										   "Position.xyz += max (-1.0 + %f, t) * %f * Normal.xyz;\n",
										   deformv->args[0], deformv->args[2], deformv->args[3], deformv->args[1] ),
							sizeof( program ) );
				break;
			case DEFORMV_AUTOSPRITE:
				Q_strncatz( program,
							"right = (1.0 + step(0.5, TexCoord.s) * -2.0) * u_QF_ViewAxis[1] * u_QF_MirrorSide;\n;"
							"up = (1.0 + step(0.5, TexCoord.t) * -2.0) * u_QF_ViewAxis[2];\n"
							"forward = -1.0 * u_QF_ViewAxis[0];\n"
							"Position.xyz = a_SpritePoint.xyz + (right + up) * a_SpritePoint.w;\n"
							"Normal.xyz = forward;\n"
							"TexCoord.st = vec2(step(0.5, TexCoord.s),step(0.5, TexCoord.t));\n",
							sizeof( program ) );
				break;
			case DEFORMV_AUTOPARTICLE:
				Q_strncatz( program,
							"right = (1.0 + TexCoord.s * -2.0) * u_QF_ViewAxis[1] * u_QF_MirrorSide;\n;"
							"up = (1.0 + TexCoord.t * -2.0) * u_QF_ViewAxis[2];\n"
							"forward = -1.0 * u_QF_ViewAxis[0];\n"
				            // prevent the particle from disappearing at large distances
							"t = dot(a_SpritePoint.xyz + u_QF_EntityOrigin - u_QF_ViewOrigin, u_QF_ViewAxis[0]);\n"
							"t = 1.5 + step(20.0, t) * t * 0.006;\n"
							"Position.xyz = a_SpritePoint.xyz + (right + up) * t * a_SpritePoint.w;\n"
							"Normal.xyz = forward;\n",
							sizeof( program ) );
				break;
			case DEFORMV_AUTOSPRITE2:
				Q_strncatz( program,
				            // local sprite axes
							"right = QF_LatLong2Norm(a_SpriteRightUpAxis.xy) * u_QF_MirrorSide;\n"
							"up = QF_LatLong2Norm(a_SpriteRightUpAxis.zw);\n"

				            // mid of quad to camera vector
							"dist = u_QF_ViewOrigin - u_QF_EntityOrigin - a_SpritePoint.xyz;\n"

				            // filter any longest-axis-parts off the camera-direction
							"forward = normalize(dist - up * dot(dist, up));\n"

				            // the right axis vector as it should be to face the camera
							"newright = cross(up, forward);\n"

				            // rotate the quad vertex around the up axis vector
							"t = dot(right, Position.xyz - a_SpritePoint.xyz);\n"
							"Position.xyz += t * (newright - right);\n"
							"Normal.xyz = forward;\n",
							sizeof( program ) );
				break;
			default:
				return NULL;
		}
	}

	Q_strncatz( program,
				"#endif\n"
				"}\n"
				"\n"
				, sizeof( program ) );

	return program;
}

class ProgramSourceBuilder {
public:
	ProgramSourceBuilder( wsw::Vector<const char *> *strings, wsw::Vector<int> *lengths )
		: m_strings( strings ), m_lengths( lengths ) {}

	void truncateToSize( unsigned index ) {
		m_strings->erase( m_strings->begin() + index, m_strings->end() );
		m_lengths->erase( m_lengths->begin() + index, m_lengths->end() );
	}

	[[nodiscard]]
	auto size() const -> unsigned {
		assert( m_strings->size() == m_lengths->size() );
		return m_strings->size();
	}

	[[maybe_unused]]
	auto add( const char *string ) -> unsigned {
		const size_t result = m_lengths->size();
		m_strings->push_back( string );
		m_lengths->push_back( (int)std::strlen( string ) );
		return result;
	}

	[[maybe_unused]]
	auto add( const char *string, size_t length ) -> unsigned {
		const size_t result = m_lengths->size();
		m_strings->push_back( string );
		m_lengths->push_back( (int)length );
		return result;
	}

	void setAtIndex( unsigned index, const char *string ) {
		m_strings->operator[]( index ) = string;
		m_lengths->operator[]( index ) = (int)std::strlen( string );
	}

private:
	static_assert( std::is_same_v<int, GLint> );
	wsw::Vector<const char *> *const m_strings;
	wsw::Vector<int> *const m_lengths;
};

auto ShaderProgramCache::getProgramForParams( int type, const wsw::StringView &maybeRequestedName, uint64_t features,
											  const DeformSig &deformSig, std::span<const deformv_t> deforms ) -> int {
	assert( qglGetError() == GL_NO_ERROR );
	if( type <= GLSL_PROGRAM_TYPE_NONE || type >= GLSL_PROGRAM_TYPE_MAXTYPE ) {
		return 0;
	}

	// TODO: Shuffle bits better
	const auto featureWord1 = (uint32_t)( features >> 32 );
	const auto featureWord2 = (uint32_t)( features & 0xFFFF'FFFFu );
	const auto featureHash  = featureWord1 ^ featureWord2;
	const auto binIndex     = featureHash % GLSL_PROGRAMS_HASH_SIZE;

	for( ShaderProgram *program = m_hashBinsForType[type][binIndex]; program; program = program->nextInHashBin ) {
		if( program->features == features && program->deformSig == deformSig ) {
			return (int)( program->index + 1 );
		}
	}

	wsw::StringView requestedNameToUse = maybeRequestedName;
	if( requestedNameToUse.empty() ) {
		for( const ShaderProgram *program = m_programListHead; program; program = program->nextInList ) {
			if( program->type == type && !program->features ) {
				assert( !program->name.empty() );
				requestedNameToUse = program->name;
				break;
			}
		}
		if( requestedNameToUse.empty() ) {
			Com_Printf( S_COLOR_RED "Failed to find an existing program for the type 0x%X\n", type );
			return 0;
		}
	}

	assert( requestedNameToUse.isZeroTerminated() );

	if( m_programsAllocator.isFull() ) {
		Com_Printf( S_COLOR_RED "Failed to create a program %s: too many programs\n", requestedNameToUse.data() );
		return 0;
	}

	const auto maybeObjectIds = createProgramFromSource( requestedNameToUse, type, features, deforms );
	if( !maybeObjectIds ) {
		Com_Printf( S_COLOR_RED "Failed to create a program %s from source\n", requestedNameToUse.data() );
		return 0;
	}

	unsigned programIndex = 0;
	void *const mem        = m_programsAllocator.allocOrNull( &programIndex );
	auto *const program    = new( mem )ShaderProgram;

	char *nameData;
	if( requestedNameToUse.size() > kAllocatorNameLengthLimit ) {
		nameData = (char *)Q_malloc( requestedNameToUse.size() + 1 );
	} else {
		assert( !m_namesAllocator.isFull() );
		nameData = (char *)m_namesAllocator.allocOrNull();
	}

	requestedNameToUse.copyTo( nameData, requestedNameToUse.size() + 1 );
	program->name           = wsw::StringView( nameData, requestedNameToUse.size(), wsw::StringView::ZeroTerminated );
	program->nameDataToFree = nameData;

	program->deformSig = deformSig;
	if( deformSig.data && deformSig.len ) {
		const size_t deformSigDataSize = sizeof( int ) * deformSig.len;
		int *storedDeformSigData;
		if( deformSigDataSize > kExtraTrailingProgramBytesSize ) {
			storedDeformSigData          = (int *)Q_malloc( deformSigDataSize );
			program->deformSigDataToFree = storedDeformSigData;
		} else {
			storedDeformSigData = (int *)( program + 1 );
		}
		std::memcpy( storedDeformSigData, deformSig.data, deformSigDataSize );
		program->deformSig.data = storedDeformSigData;
	}

	program->programId        = std::get<0>( *maybeObjectIds );
	program->vertexShaderId   = std::get<1>( *maybeObjectIds );
	program->fragmentShaderId = std::get<2>( *maybeObjectIds );

	program->type     = type;
	program->features = features;

	program->nextInHashBin            = m_hashBinsForType[type][binIndex];
	m_hashBinsForType[type][binIndex] = program;

	program->nextInList = m_programListHead;
	m_programListHead   = program;

	program->index                  = programIndex;
	m_programForIndex[programIndex] = program;

	qglUseProgram( program->programId );
	setupUniformsAndLocations( program );
	assert( qglGetError() == GL_NO_ERROR );

	return (int)( programIndex + 1 );
}

auto ShaderProgramCache::createProgramFromSource( const wsw::StringView &name, int type, uint64_t features,
												  std::span<const deformv_t> deforms )
												  -> std::optional<std::tuple<GLuint, GLuint, GLuint>> {
	GLuint programId        = 0;
	GLuint vertexShaderId   = 0;
	GLuint fragmentShaderId = 0;

	bool succeeded = false;

	try {
		// This is a "structured-goto" that allows early exits
		do {
			if( !( programId = qglCreateProgram() ) ) {
				break;
			}
			if( !( vertexShaderId = qglCreateShader( GL_VERTEX_SHADER ) ) ) {
				break;
			}
			if( !( fragmentShaderId = qglCreateShader( GL_FRAGMENT_SHADER ) ) ) {
				break;
			}
			if( !bindAttributeLocations( programId ) ) {
				break;
			}
			if( !loadShaderSources( name, type, features, deforms, vertexShaderId, fragmentShaderId ) ) {
				break;
			}
			if( !linkProgram( programId, vertexShaderId, fragmentShaderId ) ) {
				break;
			}
			succeeded = true;
		} while( false );
	} catch( std::exception &ex ) {
		Com_Printf( S_COLOR_RED "Caught an exception while trying to create a program: %s\n", ex.what() );
		succeeded = false;
	} catch( ... ) {
		succeeded = false;
	}

	if( !succeeded ) {
		destroyProgramObjects( programId, vertexShaderId, fragmentShaderId );
		return std::nullopt;
	}

	return std::make_tuple( programId, vertexShaderId, fragmentShaderId );
}

void ShaderProgramCache::destroyProgramObjects( GLuint programId, GLuint vertexShaderId, GLuint fragmentShaderId ) {
	if( programId ) {
		GLsizei numAttachedShaders = 0;
		GLuint attachedShaders[2] { 0, 0 };
		qglGetAttachedShaders( programId, 2, &numAttachedShaders, attachedShaders );
		for( GLsizei i = 0; i < numAttachedShaders; ++i ) {
			qglDetachShader( programId, attachedShaders[i] );
		}
	}
	if( fragmentShaderId ) {
		qglDeleteShader( fragmentShaderId );
	}
	if( vertexShaderId ) {
		qglDeleteShader( vertexShaderId );
	}
	if( programId ) {
		qglDeleteProgram( programId );
	}
}

bool ShaderProgramCache::loadShaderSources( const wsw::StringView &name, int type, uint64_t features,
											std::span<const deformv_t> deforms,
											GLuint vertexShaderId, GLuint fragmentShaderId ) {
	assert( !name.empty() && name.isZeroTerminated() );

	m_tmpStrings.clear();
	m_tmpLengths.clear();
	m_tmpOffsets.clear();

	wsw::StaticString<64> shaderVersion;
	shaderVersion << "#define QF_GLSL_VERSION "_asView << glConfig.shadingLanguageVersion << "\n"_asView;
	wsw::StaticString<64> maxBones;
	maxBones << "#define MAX_UNIFORM_BONES "_asView << glConfig.maxGLSLBones << "\n"_asView;

	ProgramSourceBuilder sourceBuilder( &m_tmpStrings, &m_tmpLengths );

	if( glConfig.shadingLanguageVersion >= 140 ) {
		sourceBuilder.add( QF_GLSL_VERSION140 );
	} else if( glConfig.shadingLanguageVersion >= 130 ) {
		sourceBuilder.add( QF_GLSL_VERSION130 );
	} else {
		sourceBuilder.add( QF_GLSL_VERSION120 );
	}

	if( glConfig.ext.gpu_shader5 ) {
		sourceBuilder.add( QF_GLSL_ENABLE_ARB_GPU_SHADER5 );
	}

	const unsigned enableTextureArrayIdx = sourceBuilder.add( "\n" );

	std::optional<unsigned> enableInstancedIdx;
	if( glConfig.shadingLanguageVersion < 400 ) {
		enableInstancedIdx = sourceBuilder.add( QF_GLSL_ENABLE_ARB_DRAW_INSTANCED );
	}

	sourceBuilder.add( shaderVersion.data() );
	const unsigned shaderTypeIdx = sourceBuilder.add( "\n" );

	sourceBuilder.add( QF_BUILTIN_GLSL_MACROS );
	if( glConfig.shadingLanguageVersion >= 130 ) {
		sourceBuilder.add( QF_BUILTIN_GLSL_MACROS_GLSL130 );
	} else {
		sourceBuilder.add( QF_BUILTIN_GLSL_MACROS_GLSL120 );
	}

	sourceBuilder.add( QF_BUILTIN_GLSL_CONSTANTS );
	sourceBuilder.add( maxBones.data() );
	sourceBuilder.add( QF_BUILTIN_GLSL_UNIFORMS );

	const unsigned wavefuncsIdx = sourceBuilder.add( QF_GLSL_WAVEFUNCS );
	sourceBuilder.add( QF_GLSL_MATH );

	wsw::StaticString<1024> fullName;
	fullName << wsw::StringView( name );

	if( const glsl_feature_t *const type_features = glsl_programtypes_features[type] ) {
		uint64_t unsatisfiedFeatures = features;
		int featureRowNum                = 0;
		for(;; ) {
			if( !unsatisfiedFeatures ) {
				break;
			}
			const glsl_feature_t &featureRow = type_features[featureRowNum];
			if( !featureRow.featureBit ) {
				break;
			}
			if( ( featureRow.featureBit & unsatisfiedFeatures ) == featureRow.featureBit ) {
				sourceBuilder.add( featureRow.define );
				fullName << wsw::StringView( featureRow.suffix );
				unsatisfiedFeatures &= ~featureRow.featureBit;
			}
			featureRowNum++;
		}
	}

	// forward declare QF_DeformVerts
	const char *deformv = R_GLSLBuildDeformv( deforms.data(), deforms.size() );
	const unsigned deformIndex = sourceBuilder.add( deformv ? deformv : "\n" );

	std::optional<unsigned> dualQuatsIdx;
	if( features & GLSL_SHADER_COMMON_BONE_TRANSFORMS ) {
		dualQuatsIdx = sourceBuilder.add( QF_BUILTIN_GLSL_QUAT_TRANSFORM );
	}

	std::optional<unsigned> instancedIdx;
	if( features & ( GLSL_SHADER_COMMON_INSTANCED_TRANSFORMS | GLSL_SHADER_COMMON_INSTANCED_ATTRIB_TRANSFORMS ) ) {
		instancedIdx = sourceBuilder.add( QF_BUILTIN_GLSL_INSTANCED_TRANSFORMS );
	}

	const unsigned transformsIndex = sourceBuilder.add( QF_BUILTIN_GLSL_TRANSFORM_VERTS );

	const unsigned numCommonStrings = sourceBuilder.size();

	// vertex shader
	sourceBuilder.setAtIndex( shaderTypeIdx, "#define VERTEX_SHADER\n" );

	Com_DPrintf( "Registering GLSL program %s\n", fullName.data() );

	char fileName[1024];
	Q_snprintfz( fileName, sizeof( fileName ), "glsl/%s.vert.glsl", name.data() );

	ProgramSourceLoader vertexShaderLoader( &g_programSourceFileCache, &m_tmpStrings, &m_tmpLengths, &m_tmpOffsets );
	if( !vertexShaderLoader.load( wsw::StringView( fileName ), features, type ) ) {
		Com_DPrintf( "Failed to load the source of %s\n", fileName );
		return false;
	}

	if( !compileShader( vertexShaderId, "vertex", m_tmpStrings, m_tmpLengths ) ) {
		Com_DPrintf( "Failed to compile %s\n", fileName );
		return false;
	}

	sourceBuilder.truncateToSize( numCommonStrings );

	// fragment shader
	if( glConfig.ext.texture_array ) {
		sourceBuilder.setAtIndex( enableTextureArrayIdx, QF_GLSL_ENABLE_EXT_TEXTURE_ARRAY );
	}

	if( enableInstancedIdx ) {
		sourceBuilder.setAtIndex( *enableInstancedIdx, "\n" );
	}

	sourceBuilder.setAtIndex( shaderTypeIdx, "#define FRAGMENT_SHADER\n" );
	sourceBuilder.setAtIndex( wavefuncsIdx, "\n" );
	sourceBuilder.setAtIndex( deformIndex, "\n" );
	if( dualQuatsIdx ) {
		sourceBuilder.setAtIndex( *dualQuatsIdx, "\n" );
	}
	if( instancedIdx ) {
		sourceBuilder.setAtIndex( *instancedIdx, "\n" );
	}
	sourceBuilder.setAtIndex( transformsIndex, "\n" );

	Q_snprintfz( fileName, sizeof( fileName ), "glsl/%s.frag.glsl", name.data() );

	ProgramSourceLoader fragmentShaderLoader( &g_programSourceFileCache, &m_tmpStrings, &m_tmpLengths, &m_tmpOffsets );
	if( !fragmentShaderLoader.load( wsw::StringView( fileName ), features, type ) ) {
		Com_DPrintf( "Failed to load source of %s\n", fileName );
		return false;
	}

	if( !compileShader( fragmentShaderId, "fragment", m_tmpStrings, m_tmpLengths ) ) {
		Com_DPrintf( "Failed to compile %s\n", fileName );
		return false;
	}

	Com_DPrintf( "Has compiled sources of %s successfully\n", name.data() );
	return true;
}

bool ShaderProgramCache::compileShader( GLuint id, const char *kind, std::span<const char *> strings,
										std::span<int> lengths ) {
	assert( strings.size() == lengths.size() );
	qglShaderSource( id, (GLsizei)strings.size(), strings.data(), lengths.data() );
	qglCompileShader( id );

	GLint compileStatus = 0;
	qglGetShaderiv( id, GL_COMPILE_STATUS, &compileStatus );
	if( compileStatus != GL_TRUE ) {
		char log[1024];
		GLsizei logLength = 0;
		qglGetShaderInfoLog( id, (GLsizei)sizeof( log ), &logLength, log );
		log[logLength] = log[sizeof( log ) - 1] = '\0';
		Com_Printf( "Failed to compile a %s shader: %s\n", kind, log );
		return false;
	}

	return true;
}

bool ShaderProgramCache::linkProgram( GLuint programId, GLuint vertexShaderId, GLuint fragmentShaderId ) {
	qglAttachShader( programId, vertexShaderId );
	qglAttachShader( programId, fragmentShaderId );

	GLint linkStatus = 0;
	qglLinkProgram( programId );
	qglGetProgramiv( programId, GL_LINK_STATUS, &linkStatus );
	if( linkStatus != GL_TRUE ) {
		char log[1024];
		GLsizei logLength = 0;
		qglGetProgramInfoLog( programId, (GLsizei)sizeof( log ), &logLength, log );
		log[logLength] = log[sizeof( log ) - 1] = '\0';
		Com_Printf( "Failed to link a program: %s\n", log );
		return false;
	}

	return true;
}

int RP_RegisterProgram( int type, const char *name, const DeformSig &deformSig, const deformv_t *deforms, int numDeforms, uint64_t features ) {
	return g_programCacheInstanceHolder.instance()->getProgramForParams( type, name, features, deformSig, { deforms, (size_t)numDeforms } );
}

int RP_GetProgramObject( int elem ) {
	if( ShaderProgram *program = g_programCacheInstanceHolder.instance()->getProgramById( elem ) ) {
		return (int)program->programId;
	}
	return 0;
}

/*
* RP_UpdateShaderUniforms
*/
void RP_UpdateShaderUniforms( int elem,
							  float shaderTime,
							  const vec3_t entOrigin, const vec3_t entDist, const uint8_t *entityColor,
							  const uint8_t *constColor, const float *rgbGenFuncArgs, const float *alphaGenFuncArgs,
							  const mat4_t texMatrix, float colorMod ) {
	GLfloat m[9];
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( entOrigin ) {
		if( program->loc.EntityOrigin >= 0 ) {
			qglUniform3fv( program->loc.EntityOrigin, 1, entOrigin );
		}
		if( program->loc.builtin.EntityOrigin >= 0 ) {
			qglUniform3fv( program->loc.builtin.EntityOrigin, 1, entOrigin );
		}
	}

	if( program->loc.EntityDist >= 0 && entDist ) {
		qglUniform3fv( program->loc.EntityDist, 1, entDist );
	}
	if( program->loc.EntityColor >= 0 && entityColor ) {
		qglUniform4f( program->loc.EntityColor, entityColor[0] * 1.0 / 255.0, entityColor[1] * 1.0 / 255.0, entityColor[2] * 1.0 / 255.0, entityColor[3] * 1.0 / 255.0 );
	}

	if( program->loc.ShaderTime >= 0 ) {
		qglUniform1f( program->loc.ShaderTime, shaderTime );
	}
	if( program->loc.builtin.ShaderTime >= 0 ) {
		qglUniform1f( program->loc.builtin.ShaderTime, shaderTime );
	}

	if( program->loc.ConstColor >= 0 && constColor ) {
		qglUniform4f( program->loc.ConstColor, constColor[0] * 1.0 / 255.0, constColor[1] * 1.0 / 255.0, constColor[2] * 1.0 / 255.0, constColor[3] * 1.0 / 255.0 );
	}
	if( program->loc.RGBGenFuncArgs >= 0 && rgbGenFuncArgs ) {
		qglUniform4fv( program->loc.RGBGenFuncArgs, 1, rgbGenFuncArgs );
	}
	if( program->loc.AlphaGenFuncArgs >= 0 && alphaGenFuncArgs ) {
		qglUniform4fv( program->loc.AlphaGenFuncArgs, 1, alphaGenFuncArgs );
	}

	// FIXME: this looks shit...
	if( program->loc.TextureMatrix >= 0 ) {
		m[0] = texMatrix[0], m[1] = texMatrix[4];
		m[2] = texMatrix[1], m[3] = texMatrix[5];
		m[4] = texMatrix[12], m[5] = texMatrix[13];

		qglUniform4fv( program->loc.TextureMatrix, 2, m );
	}

	if( program->loc.LightingIntensity >= 0 ) {
		qglUniform1f( program->loc.LightingIntensity, 1.0 );
	}
	if( program->loc.ColorMod >= 0 ) {
		qglUniform1f( program->loc.ColorMod, colorMod );
	}
}

/*
* RP_UpdateViewUniforms
*/
void RP_UpdateViewUniforms( int elem,
							const mat4_t modelviewMatrix, const mat4_t modelviewProjectionMatrix,
							const vec3_t viewOrigin, const mat3_t viewAxis,
							const float mirrorSide,
							int viewport[4],
							float zNear, float zFar ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.ModelViewMatrix >= 0 ) {
		qglUniformMatrix4fv( program->loc.ModelViewMatrix, 1, GL_FALSE, modelviewMatrix );
	}
	if( program->loc.ModelViewProjectionMatrix >= 0 ) {
		qglUniformMatrix4fv( program->loc.ModelViewProjectionMatrix, 1, GL_FALSE, modelviewProjectionMatrix );
	}

	if( program->loc.ZRange >= 0 ) {
		qglUniform2f( program->loc.ZRange, zNear, zFar );
	}

	if( viewOrigin ) {
		if( program->loc.ViewOrigin >= 0 ) {
			qglUniform3fv( program->loc.ViewOrigin, 1, viewOrigin );
		}
		if( program->loc.builtin.ViewOrigin >= 0 ) {
			qglUniform3fv( program->loc.builtin.ViewOrigin, 1, viewOrigin );
		}
	}

	if( viewAxis ) {
		if( program->loc.ViewAxis >= 0 ) {
			qglUniformMatrix3fv( program->loc.ViewAxis, 1, GL_FALSE, viewAxis );
		}
		if( program->loc.builtin.ViewAxis >= 0 ) {
			qglUniformMatrix3fv( program->loc.builtin.ViewAxis, 1, GL_FALSE, viewAxis );
		}
	}

	if( program->loc.Viewport >= 0 ) {
		qglUniform4iv( program->loc.Viewport, 1, viewport );
	}

	if( program->loc.MirrorSide >= 0 ) {
		qglUniform1f( program->loc.MirrorSide, mirrorSide );
	}
	if( program->loc.builtin.MirrorSide >= 0 ) {
		qglUniform1f( program->loc.builtin.MirrorSide, mirrorSide );
	}
}

/*
* RP_UpdateBlendMixUniform
*
* The first component corresponds to RGB, the second to ALPHA.
* Whenever the program needs to scale source colors, the mask needs
* to be used in the following manner:
* color *= mix(myhalf4(1.0), myhalf4(scale), u_BlendMix.xxxy);
*/
void RP_UpdateBlendMixUniform( int elem, vec2_t blendMix ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.BlendMix >= 0 ) {
		qglUniform2fv( program->loc.BlendMix, 1, blendMix );
	}
}

/*
* RP_UpdateSoftParticlesUniforms
*/
void RP_UpdateSoftParticlesUniforms( int elem, float scale ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.SoftParticlesScale >= 0 ) {
		qglUniform1f( program->loc.SoftParticlesScale, scale );
	}
}

/*
* RP_UpdateDiffuseLightUniforms
*/
void RP_UpdateDiffuseLightUniforms( int elem,
									const vec3_t lightDir, const vec4_t lightAmbient, const vec4_t lightDiffuse ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.LightDir >= 0 && lightDir ) {
		qglUniform3fv( program->loc.LightDir, 1, lightDir );
	}
	if( program->loc.LightAmbient >= 0 && lightAmbient ) {
		qglUniform3f( program->loc.LightAmbient, lightAmbient[0], lightAmbient[1], lightAmbient[2] );
	}
	if( program->loc.LightDiffuse >= 0 && lightDiffuse ) {
		qglUniform3f( program->loc.LightDiffuse, lightDiffuse[0], lightDiffuse[1], lightDiffuse[2] );
	}
	if( program->loc.LightingIntensity >= 0 ) {
		qglUniform1f( program->loc.LightingIntensity, r_lighting_intensity->value );
	}
}

/*
* RP_UpdateMaterialUniforms
*/
void RP_UpdateMaterialUniforms( int elem,
								float offsetmappingScale, float glossIntensity, float glossExponent ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.GlossFactors >= 0 ) {
		qglUniform2f( program->loc.GlossFactors, glossIntensity, glossExponent );
	}
	if( program->loc.OffsetMappingScale >= 0 ) {
		qglUniform1f( program->loc.OffsetMappingScale, offsetmappingScale );
	}
}

/*
* RP_UpdateDistortionUniforms
*/
void RP_UpdateDistortionUniforms( int elem, bool frontPlane ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.FrontPlane >= 0 ) {
		qglUniform1f( program->loc.FrontPlane, frontPlane ? 1 : -1 );
	}
}

/*
* RP_UpdateTextureUniforms
*/
void RP_UpdateTextureUniforms( int elem, int TexWidth, int TexHeight ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.TextureParams >= 0 ) {
		qglUniform4f( program->loc.TextureParams, TexWidth, TexHeight,
						 TexWidth ? 1.0 / TexWidth : 1.0, TexHeight ? 1.0 / TexHeight : 1.0 );
	}
}

/*
* RP_UpdateOutlineUniforms
*/
void RP_UpdateOutlineUniforms( int elem, float projDistance ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.OutlineHeight >= 0 ) {
		qglUniform1f( program->loc.OutlineHeight, projDistance );
	}
	if( program->loc.OutlineCutOff >= 0 ) {
		qglUniform1f( program->loc.OutlineCutOff, wsw::max( 0.0f, r_outlines_cutoff->value ) );
	}
}

/*
* RP_UpdateFogUniforms
*/
void RP_UpdateFogUniforms( int elem, byte_vec4_t color, float clearDist, float opaqueDist, cplane_t *fogPlane, cplane_t *eyePlane, float eyeDist ) {
	GLfloat fog_color[3] = { 0, 0, 0 };
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	VectorScale( color, ( 1.0 / 255.0 ), fog_color );

	if( program->loc.Fog.Color >= 0 ) {
		qglUniform3fv( program->loc.Fog.Color, 1, fog_color );
	}
	if( program->loc.Fog.ScaleAndEyeDist >= 0 ) {
		qglUniform2f( program->loc.Fog.ScaleAndEyeDist, 1.0 / ( opaqueDist - clearDist ), eyeDist );
	}
	if( program->loc.Fog.Plane >= 0 ) {
		qglUniform4f( program->loc.Fog.Plane, fogPlane->normal[0], fogPlane->normal[1], fogPlane->normal[2], fogPlane->dist );
	}
	if( program->loc.Fog.EyePlane >= 0 ) {
		qglUniform4f( program->loc.Fog.EyePlane, eyePlane->normal[0], eyePlane->normal[1], eyePlane->normal[2], eyePlane->dist );
	}
}

void RP_UpdateDynamicLightsUniforms( const FrontendToBackendShared *fsh,
									 int elem, const superLightStyle_t *superLightStyle,
									 const vec3_t entOrigin, const mat3_t entAxis, unsigned int dlightbits ) {
	int i, n, c;
	vec3_t dlorigin, tvec;
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );
	bool identityAxis = Matrix3_Compare( entAxis, axis_identity );
	vec4_t shaderColor[4];

	if( superLightStyle ) {
		GLfloat rgb[3];
		static float deluxemapOffset[( MAX_LIGHTMAPS + 3 ) & ( ~3 )];

		for( i = 0; i < MAX_LIGHTMAPS && superLightStyle->lightmapStyles[i] != 255; i++ ) {
			VectorCopy( lightStyles[superLightStyle->lightmapStyles[i]].rgb, rgb );

			if( program->loc.LightstyleColor[i] >= 0 ) {
				qglUniform3fv( program->loc.LightstyleColor[i], 1, rgb );
			}
			if( program->loc.DeluxemapOffset >= 0 ) {
				deluxemapOffset[i] = superLightStyle->stOffset[i][0];
			}
		}

		if( i && ( program->loc.DeluxemapOffset >= 0 ) ) {
			qglUniform4fv( program->loc.DeluxemapOffset, ( i + 3 ) / 4, deluxemapOffset );
		}
	}

	if( dlightbits ) {
		memset( shaderColor, 0, sizeof( vec4_t ) * 3 );
		Vector4Set( shaderColor[3], 1.0f, 1.0f, 1.0f, 1.0f );
		n = 0;

		for( i = 0; i < (int)fsh->visibleProgramLightIndices.size(); ++i ) {
			const unsigned lightBit = 1 << i;
			if( !( dlightbits & lightBit ) ) {
				continue;
			}

			dlightbits &= ~lightBit;

			if( program->loc.DynamicLightsPosition[n] < 0 ) {
				break;
			}

			const auto *const light = fsh->dynamicLights + fsh->visibleProgramLightIndices[i];
			assert( light->hasProgramLight && light->programRadius >= 1.0f );

			VectorSubtract( light->origin, entOrigin, dlorigin );
			if( !identityAxis ) {
				VectorCopy( dlorigin, tvec );
				Matrix3_TransformVector( entAxis, tvec, dlorigin );
			}

			qglUniform3fv( program->loc.DynamicLightsPosition[n], 1, dlorigin );

			c = n & 3;
			shaderColor[0][c] = light->color[0];
			shaderColor[1][c] = light->color[1];
			shaderColor[2][c] = light->color[2];
			shaderColor[3][c] = Q_Rcp( light->programRadius );

			// DynamicLightsDiffuseAndInvRadius is transposed for SIMD, but it's still 4x4
			if( c == 3 ) {
				qglUniform4fv( program->loc.DynamicLightsDiffuseAndInvRadius[n >> 2], 4, shaderColor[0] );
				memset( shaderColor, 0, sizeof( vec4_t ) * 3 );
				Vector4Set( shaderColor[3], 1.0f, 1.0f, 1.0f, 1.0f );
			}

			n++;

			dlightbits &= ~lightBit;
			if( !dlightbits ) {
				break;
			}
		}

		if( n & 3 ) {
			qglUniform4fv( program->loc.DynamicLightsDiffuseAndInvRadius[n >> 2], 4, shaderColor[0] );
			memset( shaderColor, 0, sizeof( vec4_t ) * 3 ); // to set to zero for the remaining lights
			Vector4Set( shaderColor[3], 1.0f, 1.0f, 1.0f, 1.0f );
			n = ALIGN( n, 4 );
		}

		if( program->loc.NumDynamicLights >= 0 ) {
			qglUniform1i( program->loc.NumDynamicLights, n );
		}

		for( ; n < MAX_DLIGHTS; n += 4 ) {
			if( program->loc.DynamicLightsPosition[n] < 0 ) {
				break;
			}
			qglUniform4fv( program->loc.DynamicLightsDiffuseAndInvRadius[n >> 2], 4, shaderColor[0] );
		}
	}
}

/*
* RP_UpdateTexGenUniforms
*/
void RP_UpdateTexGenUniforms( int elem, const mat4_t reflectionMatrix, const mat4_t vectorMatrix ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.ReflectionTexMatrix >= 0 ) {
		mat3_t m;
		memcpy( &m[0], &reflectionMatrix[0], 3 * sizeof( vec_t ) );
		memcpy( &m[3], &reflectionMatrix[4], 3 * sizeof( vec_t ) );
		memcpy( &m[6], &reflectionMatrix[8], 3 * sizeof( vec_t ) );
		qglUniformMatrix3fv( program->loc.ReflectionTexMatrix, 1, GL_FALSE, m );
	}
	if( program->loc.VectorTexMatrix >= 0 ) {
		qglUniformMatrix4fv( program->loc.VectorTexMatrix, 1, GL_FALSE, vectorMatrix );
	}
}

/*
* RP_UpdateBonesUniforms
*
* Set uniform values for animation dual quaternions
*/
void RP_UpdateBonesUniforms( int elem, unsigned int numBones, dualquat_t *animDualQuat ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( numBones > glConfig.maxGLSLBones ) {
		return;
	}
	if( program->loc.DualQuats < 0 ) {
		return;
	}
	qglUniform4fv( program->loc.DualQuats, numBones * 2, &animDualQuat[0][0] );
}

/*
* RP_UpdateInstancesUniforms
*
* Set uniform values for instance points (quaternion + xyz + scale)
*/
void RP_UpdateInstancesUniforms( int elem, unsigned int numInstances, instancePoint_t *instances ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( numInstances > MAX_GLSL_UNIFORM_INSTANCES ) {
		numInstances = MAX_GLSL_UNIFORM_INSTANCES;
	}
	if( program->loc.InstancePoints < 0 ) {
		return;
	}
	qglUniform4fv( program->loc.InstancePoints, numInstances * 2, &instances[0][0] );
}

/*
* RP_UpdateColorCorrectionUniforms
*/
void RP_UpdateColorCorrectionUniforms( int elem, float hdrGamma, float hdrExposure ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.hdrGamma >= 0 ) {
		qglUniform1f( program->loc.hdrGamma, hdrGamma );
	}
	if( program->loc.hdrExposure >= 0 ) {
		qglUniform1f( program->loc.hdrExposure, hdrExposure );
	}
}

/*
* RP_UpdateDrawFlatUniforms
*/
void RP_UpdateDrawFlatUniforms( int elem, const vec3_t wallColor, const vec3_t floorColor ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.WallColor >= 0 ) {
		qglUniform3f( program->loc.WallColor, wallColor[0], wallColor[1], wallColor[2] );
	}
	if( program->loc.FloorColor >= 0 ) {
		qglUniform3f( program->loc.FloorColor, floorColor[0], floorColor[1], floorColor[2] );
	}
}

/*
* RP_UpdateKawaseUniforms
*/
void RP_UpdateKawaseUniforms( int elem, int TexWidth, int TexHeight, int iteration ) {
	ShaderProgram *const program = g_programCacheInstanceHolder.instance()->getProgramById( elem );

	if( program->loc.TextureParams >= 0 ) {
		qglUniform4f( program->loc.TextureParams,
						 TexWidth ? 1.0 / TexWidth : 1.0, TexHeight ? 1.0 / TexHeight : 1.0, (float)iteration, 1.0 );
	}
}

void ShaderProgramCache::setupUniformsAndLocations( ShaderProgram *program ) {
	char tmp[1024];
	unsigned int i;
	int locBaseTexture,
		locNormalmapTexture,
		locGlossTexture,
		locDecalTexture,
		locEntityDecalTexture,
		locLightmapTexture[MAX_LIGHTMAPS],
		locDuDvMapTexture,
		locReflectionTexture,
		locRefractionTexture,
		locCelShadeTexture,
		locCelLightTexture,
		locDiffuseTexture,
		locStripesTexture,
		locDepthTexture,
		locYUVTextureY,
		locYUVTextureU,
		locYUVTextureV,
		locColorLUT
	;

	memset( &program->loc, -1, sizeof( program->loc ) );

	program->loc.ModelViewMatrix = qglGetUniformLocation( program->programId, "u_ModelViewMatrix" );
	program->loc.ModelViewProjectionMatrix = qglGetUniformLocation( program->programId, "u_ModelViewProjectionMatrix" );

	program->loc.ZRange = qglGetUniformLocation( program->programId, "u_ZRange" );

	program->loc.ViewOrigin = qglGetUniformLocation( program->programId, "u_ViewOrigin" );
	program->loc.ViewAxis = qglGetUniformLocation( program->programId, "u_ViewAxis" );

	program->loc.MirrorSide = qglGetUniformLocation( program->programId, "u_MirrorSide" );

	program->loc.Viewport = qglGetUniformLocation( program->programId, "u_Viewport" );

	program->loc.LightDir = qglGetUniformLocation( program->programId, "u_LightDir" );
	program->loc.LightAmbient = qglGetUniformLocation( program->programId, "u_LightAmbient" );
	program->loc.LightDiffuse = qglGetUniformLocation( program->programId, "u_LightDiffuse" );
	program->loc.LightingIntensity = qglGetUniformLocation( program->programId, "u_LightingIntensity" );

	program->loc.TextureMatrix = qglGetUniformLocation( program->programId, "u_TextureMatrix" );

	locBaseTexture = qglGetUniformLocation( program->programId, "u_BaseTexture" );
	locNormalmapTexture = qglGetUniformLocation( program->programId, "u_NormalmapTexture" );
	locGlossTexture = qglGetUniformLocation( program->programId, "u_GlossTexture" );
	locDecalTexture = qglGetUniformLocation( program->programId, "u_DecalTexture" );
	locEntityDecalTexture = qglGetUniformLocation( program->programId, "u_EntityDecalTexture" );

	locDuDvMapTexture = qglGetUniformLocation( program->programId, "u_DuDvMapTexture" );
	locReflectionTexture = qglGetUniformLocation( program->programId, "u_ReflectionTexture" );
	locRefractionTexture = qglGetUniformLocation( program->programId, "u_RefractionTexture" );

	locCelShadeTexture = qglGetUniformLocation( program->programId, "u_CelShadeTexture" );
	locCelLightTexture = qglGetUniformLocation( program->programId, "u_CelLightTexture" );
	locDiffuseTexture = qglGetUniformLocation( program->programId, "u_DiffuseTexture" );
	locStripesTexture = qglGetUniformLocation( program->programId, "u_StripesTexture" );

	locDepthTexture = qglGetUniformLocation( program->programId, "u_DepthTexture" );

	locYUVTextureY = qglGetUniformLocation( program->programId, "u_YUVTextureY" );
	locYUVTextureU = qglGetUniformLocation( program->programId, "u_YUVTextureU" );
	locYUVTextureV = qglGetUniformLocation( program->programId, "u_YUVTextureV" );

	locColorLUT = qglGetUniformLocation( program->programId, "u_ColorLUT" );

	program->loc.DeluxemapOffset = qglGetUniformLocation( program->programId, "u_DeluxemapOffset" );

	for( i = 0; i < MAX_LIGHTMAPS; i++ ) {
		// arrays of samplers are broken on ARM Mali so get u_LightmapTexture%i instead of u_LightmapTexture[%i]
		locLightmapTexture[i] = qglGetUniformLocation( program->programId,
														  va_r( tmp, sizeof( tmp ), "u_LightmapTexture%i", i ) );

		if( locLightmapTexture[i] < 0 ) {
			break;
		}

		program->loc.LightstyleColor[i] = qglGetUniformLocation( program->programId,
																	va_r( tmp, sizeof( tmp ), "u_LightstyleColor[%i]", i ) );
	}

	program->loc.GlossFactors = qglGetUniformLocation( program->programId, "u_GlossFactors" );

	program->loc.OffsetMappingScale = qglGetUniformLocation( program->programId, "u_OffsetMappingScale" );

	program->loc.OutlineHeight = qglGetUniformLocation( program->programId, "u_OutlineHeight" );
	program->loc.OutlineCutOff = qglGetUniformLocation( program->programId, "u_OutlineCutOff" );

	program->loc.FrontPlane = qglGetUniformLocation( program->programId, "u_FrontPlane" );

	program->loc.TextureParams = qglGetUniformLocation( program->programId, "u_TextureParams" );

	program->loc.EntityDist = qglGetUniformLocation( program->programId, "u_EntityDist" );
	program->loc.EntityOrigin = qglGetUniformLocation( program->programId, "u_EntityOrigin" );
	program->loc.EntityColor = qglGetUniformLocation( program->programId, "u_EntityColor" );
	program->loc.ConstColor = qglGetUniformLocation( program->programId, "u_ConstColor" );
	program->loc.RGBGenFuncArgs = qglGetUniformLocation( program->programId, "u_RGBGenFuncArgs" );
	program->loc.AlphaGenFuncArgs = qglGetUniformLocation( program->programId, "u_AlphaGenFuncArgs" );

	program->loc.Fog.Plane = qglGetUniformLocation( program->programId, "u_FogPlane" );
	program->loc.Fog.Color = qglGetUniformLocation( program->programId, "u_FogColor" );
	program->loc.Fog.ScaleAndEyeDist = qglGetUniformLocation( program->programId, "u_FogScaleAndEyeDist" );
	program->loc.Fog.EyePlane = qglGetUniformLocation( program->programId, "u_FogEyePlane" );

	program->loc.ShaderTime = qglGetUniformLocation( program->programId, "u_ShaderTime" );

	program->loc.ReflectionTexMatrix = qglGetUniformLocation( program->programId, "u_ReflectionTexMatrix" );
	program->loc.VectorTexMatrix = qglGetUniformLocation( program->programId, "u_VectorTexMatrix" );

	program->loc.builtin.ViewOrigin = qglGetUniformLocation( program->programId, "u_QF_ViewOrigin" );
	program->loc.builtin.ViewAxis = qglGetUniformLocation( program->programId, "u_QF_ViewAxis" );
	program->loc.builtin.MirrorSide = qglGetUniformLocation( program->programId, "u_QF_MirrorSide" );
	program->loc.builtin.EntityOrigin = qglGetUniformLocation( program->programId, "u_QF_EntityOrigin" );
	program->loc.builtin.ShaderTime = qglGetUniformLocation( program->programId, "u_QF_ShaderTime" );

	// dynamic lights
	for( i = 0; i < MAX_DLIGHTS; i++ ) {
		program->loc.DynamicLightsPosition[i] = qglGetUniformLocation( program->programId,
																		  va_r( tmp, sizeof( tmp ), "u_DlightPosition[%i]", i ) );

		if( !( i & 3 ) ) {
			// 4x4 transposed, so we can index it with `i`
			program->loc.DynamicLightsDiffuseAndInvRadius[i >> 2] =
				qglGetUniformLocation( program->programId, va_r( tmp, sizeof( tmp ), "u_DlightDiffuseAndInvRadius[%i]", i ) );
		}
	}
	program->loc.NumDynamicLights = qglGetUniformLocation( program->programId, "u_NumDynamicLights" );

	program->loc.BlendMix = qglGetUniformLocation( program->programId, "u_BlendMix" );
	program->loc.ColorMod = qglGetUniformLocation( program->programId, "u_ColorMod" );

	program->loc.SoftParticlesScale = qglGetUniformLocation( program->programId, "u_SoftParticlesScale" );

	program->loc.DualQuats = qglGetUniformLocation( program->programId, "u_DualQuats" );

	program->loc.InstancePoints = qglGetUniformLocation( program->programId, "u_InstancePoints" );

	program->loc.WallColor = qglGetUniformLocation( program->programId, "u_WallColor" );
	program->loc.FloorColor = qglGetUniformLocation( program->programId, "u_FloorColor" );

	program->loc.hdrGamma = qglGetUniformLocation( program->programId, "u_HDRGamma" );
	program->loc.hdrExposure = qglGetUniformLocation( program->programId, "u_HDRExposure" );

	if( locBaseTexture >= 0 ) {
		qglUniform1i( locBaseTexture, 0 );
	}
	if( locDuDvMapTexture >= 0 ) {
		qglUniform1i( locDuDvMapTexture, 0 );
	}

	if( locNormalmapTexture >= 0 ) {
		qglUniform1i( locNormalmapTexture, 1 );
	}
	if( locGlossTexture >= 0 ) {
		qglUniform1i( locGlossTexture, 2 );
	}
	if( locDecalTexture >= 0 ) {
		qglUniform1i( locDecalTexture, 3 );
	}
	if( locEntityDecalTexture >= 0 ) {
		qglUniform1i( locEntityDecalTexture, 4 );
	}

	if( locReflectionTexture >= 0 ) {
		qglUniform1i( locReflectionTexture, 2 );
	}
	if( locRefractionTexture >= 0 ) {
		qglUniform1i( locRefractionTexture, 3 );
	}

	if( locCelShadeTexture >= 0 ) {
		qglUniform1i( locCelShadeTexture, 1 );
	}
	if( locDiffuseTexture >= 0 ) {
		qglUniform1i( locDiffuseTexture, 2 );
	}
	if( locStripesTexture >= 0 ) {
		qglUniform1i( locStripesTexture, 5 );
	}
	if( locCelLightTexture >= 0 ) {
		qglUniform1i( locCelLightTexture, 6 );
	}

	if( locDepthTexture >= 0 ) {
		qglUniform1i( locDepthTexture, 3 );
	}

	for( i = 0; i < MAX_LIGHTMAPS && locLightmapTexture[i] >= 0; i++ )
		qglUniform1i( locLightmapTexture[i], i + 4 );

	if( locYUVTextureY >= 0 ) {
		qglUniform1i( locYUVTextureY, 0 );
	}
	if( locYUVTextureU >= 0 ) {
		qglUniform1i( locYUVTextureU, 1 );
	}
	if( locYUVTextureV >= 0 ) {
		qglUniform1i( locYUVTextureV, 2 );
	}

	if( locColorLUT >= 0 ) {
		qglUniform1i( locColorLUT, 1 );
	}

	// TODO: Flush GL errors, if any?
	(void)qglGetError();
}

bool ShaderProgramCache::bindAttributeLocations( GLuint programId ) {
	qglBindAttribLocation( programId, VATTRIB_POSITION, "a_Position" );
	qglBindAttribLocation( programId, VATTRIB_SVECTOR, "a_SVector" );
	qglBindAttribLocation( programId, VATTRIB_NORMAL, "a_Normal" );
	qglBindAttribLocation( programId, VATTRIB_COLOR0, "a_Color" );
	qglBindAttribLocation( programId, VATTRIB_TEXCOORDS, "a_TexCoord" );

	qglBindAttribLocation( programId, VATTRIB_SPRITEPOINT, "a_SpritePoint" );
	qglBindAttribLocation( programId, VATTRIB_SVECTOR, "a_SpriteRightUpAxis" );

	qglBindAttribLocation( programId, VATTRIB_BONESINDICES, "a_BonesIndices" );
	qglBindAttribLocation( programId, VATTRIB_BONESWEIGHTS, "a_BonesWeights" );

	qglBindAttribLocation( programId, VATTRIB_LMCOORDS01, "a_LightmapCoord01" );
	qglBindAttribLocation( programId, VATTRIB_LMCOORDS23, "a_LightmapCoord23" );

	if( glConfig.ext.texture_array ) {
		qglBindAttribLocation( programId, VATTRIB_LMLAYERS0123, "a_LightmapLayer0123" );
	}

	qglBindAttribLocation( programId, VATTRIB_INSTANCE_QUAT, "a_InstanceQuat" );
	qglBindAttribLocation( programId, VATTRIB_INSTANCE_XYZS, "a_InstancePosAndScale" );

	if( glConfig.shadingLanguageVersion >= 130 ) {
		qglBindFragDataLocation( programId, 0, "qf_FragColor" );
		qglBindFragDataLocation( programId, 1, "qf_BrightColor" );
	}

	return true;
}
