/*
Copyright (C) 1999 Stephen C. Taylor
Copyright (C) 2002-2007 Victor Luchits

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

// r_shader.c

#include "local.h"
#include "program.h"
#include "../qcommon/hash.h"
#include "../qcommon/links.h"
#include "../qcommon/memspecbuilder.h"
#include "../qcommon/singletonholder.h"
#include "../qcommon/wswstringsplitter.h"
#include "../qcommon/wswfs.h"
#include "materiallocal.h"

#include <algorithm>

using wsw::operator""_asView;

static SingletonHolder<MaterialCache> materialCacheInstanceHolder;

void MaterialCache::init() {
	::materialCacheInstanceHolder.Init();
}

void MaterialCache::shutdown() {
	::materialCacheInstanceHolder.Shutdown();
}

auto MaterialCache::instance() -> MaterialCache * {
	return ::materialCacheInstanceHolder.Instance();
}

MaterialCache::MaterialCache() {
	for( const wsw::StringView &dir : { "<scripts"_asView, ">scripts"_asView, "scripts"_asView } ) {
		// TODO: Must be checked if exists
		loadDirContents( dir );
	}

	m_freeMaterialIds.reserve( MAX_SHADERS );
	for( unsigned i = 0; i < MAX_SHADERS; ++i ) {
		m_freeMaterialIds.push_back( i );
	}
}

void MaterialCache::loadDirContents( const wsw::StringView &dir ) {
	wsw::fs::SearchResultHolder searchResultHolder;
	if( const auto callResult = searchResultHolder.findDirFiles( dir, ".shader"_asView ) ) {
		for( const wsw::StringView &fileName: *callResult ) {
			this->addFileContents( fileName );
		}
	}
}

MaterialCache::~MaterialCache() {
}

/*
* R_TouchShader
*/
void R_TouchShader( shader_t *s ) {
	if( s->registrationSequence == rsh.registrationSequence ) {
		return;
	}

	s->registrationSequence = rsh.registrationSequence;

	// touch all images this shader references
	auto *const textureCache = TextureCache::instance();
	for( unsigned i = 0; i < s->numpasses; i++ ) {
		shaderpass_t *pass = s->passes + i;

		for( unsigned j = 0; j < MAX_SHADER_IMAGES; j++ ) {
			Texture *image = pass->images[j];
			if( image ) {
				textureCache->touchTexture( image, s->imagetags );
			} else if( !pass->program_type ) {
				// only programs can have gaps in images
				break;
			}
		}
	}
}

void MaterialCache::freeUnusedMaterialsByType( const shaderType_e *types, unsigned int numTypes ) {
	if( !numTypes ) {
		for( shader_s *material = m_materialsHead; material; material = material->next[shader_t::kListLinks] ) {
			if( material->registrationSequence != rsh.registrationSequence ) {
				continue;
			}
			unlinkAndFree( material );
		}
		return;
	}

	for( shader_s *material = m_materialsHead; material; material = material->next[shader_t::kListLinks] ) {
		if( material->registrationSequence == rsh.registrationSequence ) {
			continue;
		}
		if( std::find( types, types + numTypes, material->type ) == types + numTypes ) {
			continue;
		}
		unlinkAndFree( material );
	}
}

void MaterialCache::freeUnusedObjects() {
	// TODO: Free unused skins

	freeUnusedMaterialsByType( nullptr, 0 );
}

void MaterialCache::unlinkAndFree( shader_t *s ) {
	m_materialById[s->id] = nullptr;
	m_freeMaterialIds.push_back( s->id );

	abort();
}

auto MaterialCache::getNextMaterialId() -> unsigned {
	if( m_freeMaterialIds.empty() ) {
		throw std::logic_error( "underflow" );
	}
	const unsigned result = m_freeMaterialIds.back();
	m_freeMaterialIds.pop_back();
	return result;
}

auto MaterialCache::makeCleanName( const wsw::StringView &name ) -> wsw::HashedStringView {
	m_cleanNameBuffer.clear();
	m_cleanNameBuffer.reserve( name.length() + 1 );

	unsigned i = 0;
	for( char ch: name ) {
		if( !( ( ch == '/' ) | ( ch == '\\' ) ) ) {
			break;
		}
		i++;
	}

	unsigned lastDot = 0;
	unsigned lastSlash = 0;
	unsigned len = 0;

	uint32_t hash = 0;
	uint32_t hashBackup = 0;
	for(; i < name.length(); ++i ) {
		char ch = name[i];
		if( ch == '.' ) {
			lastDot = len;
			hashBackup = hash;
		}
		char cleanCh = ch != '\\' ? tolower( ch ) : '/';
		m_cleanNameBuffer.push_back( cleanCh );
		hash = wsw::nextHashStep( hash, cleanCh );
		if( m_cleanNameBuffer.back() == '/' ) {
			lastSlash = len;
		}
		len++;
	}

	if( !len ) {
		return wsw::HashedStringView();
	}

	if( lastDot < lastSlash ) {
		lastDot = 0;
	}
	if( lastDot ) {
		m_cleanNameBuffer.resize( lastDot );
		hash = hashBackup;
	}

	return wsw::HashedStringView( m_cleanNameBuffer.data(), m_cleanNameBuffer.length(), hash );
}

/*
* R_PackShaderOrder
*
* Sort opaque shaders by this value for better caching of GL/program state.
*/
unsigned R_PackShaderOrder( const shader_t *shader ) {
	int order;
	int program_type;
	const shaderpass_t *pass;

	if( !shader->numpasses ) {
		return 0;
	}

	pass = &shader->passes[0];
	program_type = pass->program_type;

	if( program_type == GLSL_PROGRAM_TYPE_MATERIAL ) {
		// this is not a material shader in case all images are missing except for the defuse
		if( !pass->images[1] && !pass->images[2] && !pass->images[3] && !pass->images[4] ) {
			program_type = GLSL_PROGRAM_TYPE_Q3A_SHADER;
		}
	}

	// sort by base program type
	order = program_type & 0x1F;

	// check presence of gloss for materials
	if( program_type == GLSL_PROGRAM_TYPE_MATERIAL && pass->images[2] != NULL ) {
		order |= 0x20;
	}

	return order;
}

void MaterialCache::touchMaterialsByName( const wsw::StringView &name ) {
	const wsw::HashedStringView cleanView( makeCleanName( name ) );
	const auto binIndex = cleanView.getHash() % kNumBins;
	for( shader_t *material = m_materialBins[binIndex]; material; material = material->next[shader_t::kBinLinks] ) {
		if( cleanView.equalsIgnoreCase( wsw::HashedStringView( material->name ) ) ) {
			R_TouchShader( material );
		}
	}
}

auto MaterialCache::loadMaterial( const wsw::StringView &name, int type, bool forceDefault, Texture * )
	-> shader_t * {
	const wsw::HashedStringView cleanName( makeCleanName( name ) );
	const auto binIndex = cleanName.getHash() % kNumBins;

	for( shader_t *material = m_materialBins[binIndex]; material; material = material->next[shader_t::kBinLinks] ) {
		if( cleanName.equalsIgnoreCase( material->name ) ) {
			// TODO: This should be a method
			if( material->type == type || ( type == SHADER_TYPE_2D && material->type == SHADER_TYPE_2D_RAW ) ) {
				R_TouchShader( material );
				return material;
			}
		}
	}

	shader_t *material = nullptr;
	if( forceDefault ) {
		material = m_factory.newDefaultMaterial( type, cleanName, name );
	} else {
		if( !( material = m_factory.newMaterial( type, cleanName, name ) ) ) {
			material = m_factory.newDefaultMaterial( type, cleanName, name );
		}
	}

	if( material ) {
		material->id = getNextMaterialId();
		m_materialById[material->id] = material;
		material->registrationSequence = rsh.registrationSequence;
		wsw::link( material, &m_materialBins[binIndex], shader_t::kBinLinks );
		return material;
	}

	return nullptr;
}

shader_t *MaterialCache::loadDefaultMaterial( const wsw::StringView &name, int type ) {
    return loadMaterial( name, type, true, TextureCache::instance()->noTexture() );
}

shader_t *R_RegisterPic( const char *name ) {
	return MaterialCache::instance()->loadMaterial( wsw::StringView( name ), SHADER_TYPE_2D, false, TextureCache::instance()->noTexture() );
}

shader_t *R_CreateExplicitlyManaged2DMaterial() {
	return MaterialCache::instance()->getUnderlyingFactory()->create2DMaterialBypassingCache();
}
void R_ReleaseExplicitlyManaged2DMaterial( shader_t *material ) {
	return MaterialCache::instance()->getUnderlyingFactory()->release2DMaterialBypassingCache( material );
}

bool R_UpdateExplicitlyManaged2DMaterialImage( shader_t *material, const char *name, const ImageOptions &options ) {
	return MaterialCache::instance()->getUnderlyingFactory()->update2DMaterialImageBypassingCache( material, wsw::StringView( name ), options );
}

shader_t *R_RegisterRawAlphaMask( const char *name, int width, int height, const uint8_t *data ) {
	const wsw::StringView nameView( name );
	if( auto *const material = MaterialCache::instance()->loadDefaultMaterial( nameView, SHADER_TYPE_2D_RAW ) ) {
		auto *const textureFactory = TextureCache::instance()->getUnderlyingFactory();
		// unlink and delete the old image from memory, unless it's the default one
		Texture *image = material->passes[0].images[0];
		if ( !image ) {
			// try to load new image
			material->passes[0].images[0] = textureFactory->createFontMask( width, height, data );
		} else {
			// replace current texture data
			textureFactory->replaceFontMaskSamples( image, 0, 0, width, height, data );
		}
		return material;
	}
	return nullptr;
}

/*
* R_RegisterShader
*/
shader_t *R_RegisterShader( const char *name, int type ) {
	return MaterialCache::instance()->loadMaterial( wsw::StringView( name ), type, false );
}

/*
* R_RegisterSkin
*/
shader_t *R_RegisterSkin( const char *name ) {
	return MaterialCache::instance()->loadMaterial( wsw::StringView( name ), SHADER_TYPE_DIFFUSE, false );
}

/*
* R_RegisterLinearPic
*/
shader_t *R_RegisterLinearPic( const char *name ) {
	return MaterialCache::instance()->loadMaterial( wsw::StringView( name ), SHADER_TYPE_2D_LINEAR, false );
}


/*
* R_GetShaderDimensions
*
* Returns dimensions for shader's base (taken from the first pass) image
*/
auto R_GetShaderDimensions( const shader_t *shader ) -> std::optional<std::pair<unsigned, unsigned>> {
	if( shader && shader->numpasses ) {
		if( const Texture *image = shader->passes[0].images[0] ) {
			return std::make_pair( image->width, image->height );
		}
	}
	return std::nullopt;
}

/*
* R_ReplaceRawSubPic
*
* Adds a new subimage to the specified raw pic.
* Must not be used to overwrite previously written areas when doing batched drawing.
*/
void R_ReplaceRawSubPic( shader_t *shader, int x, int y, int width, int height, const uint8_t *data ) {
	Texture *baseImage;

	assert( shader );
	if( !shader ) {
		return;
	}

	assert( shader->type == SHADER_TYPE_2D_RAW );
	if( shader->type != SHADER_TYPE_2D_RAW ) {
		return;
	}

	baseImage = shader->passes[0].images[0];

	assert( ( ( x + width ) <= baseImage->width ) && ( ( y + height ) <= baseImage->height ) );
	if( ( ( x + width ) > baseImage->width ) || ( ( y + height ) > baseImage->height ) ) {
		return;
	}

	TextureCache::instance()->getUnderlyingFactory()->replaceFontMaskSamples( baseImage, x, y, width, height, data );
}

auto MaterialCache::readRawContents( const wsw::StringView &fileName ) -> const wsw::String * {
	wsw::String &pathName = m_pathNameBuffer;
	pathName.clear();
	pathName.append( "scripts/" );
	pathName.append( fileName.data(), fileName.size() );

	if ( r_showShaderCache && r_showShaderCache->integer ) {
		Com_Printf( "...loading '%s'\n", pathName.data() );
	}

	auto maybeHandle = wsw::fs::openAsReadHandle( wsw::StringView( pathName.data(), pathName.size() ) );
	if( !maybeHandle ) {
		return nullptr;
	}

	const auto size = maybeHandle->getInitialFileSize();
	m_fileContentsBuffer.resize( size + 1 );
	if( !maybeHandle->readExact( m_fileContentsBuffer.data(), size ) ) {
		return nullptr;
	}

	// Put the terminating zero, this is not mandatory as tokens aren't supposed
	// to be zero terminated but allows printing contents using C-style facilities
	m_fileContentsBuffer[size] = '\0';
	return &m_fileContentsBuffer;
}

auto MaterialCache::loadFileContents( const wsw::StringView &fileName ) -> MaterialFileContents * {
	const wsw::String *rawContents = readRawContents( fileName );
	if( !rawContents ) {
		return nullptr;
	}

	int offsetShift = 0;
	// Strip an UTF8 BOM (if any)
	if( rawContents->size() > 2 ) {
		// The data must be cast to an unsigned type first, otherwise a comparison gets elided by a compiler
		const auto *p = (const uint8_t *)rawContents->data();
		if( ( p[0] == 0xEFu ) && ( p[1] == 0xBBu ) && ( p[2] == 0xBFu ) ) {
			offsetShift = 3;
		}
	}

	TokenSplitter splitter( rawContents->data() + offsetShift, rawContents->size() - offsetShift );

	m_fileTokenSpans.clear();

	uint32_t lineNum = 0;
	size_t numKeptChars = 0;
	while( !splitter.isAtEof() ) {
		while( auto maybeToken = splitter.fetchNextTokenInLine() ) {
			const auto &[off, len] = *maybeToken;
			m_fileTokenSpans.emplace_back( TokenSpan { (int)( off + offsetShift ), len, lineNum } );
			numKeptChars += len;
		}
		lineNum++;
	}

	wsw::MemSpecBuilder memSpec( wsw::MemSpecBuilder::initiallyEmpty() );
	const auto headerSpec = memSpec.add<MaterialFileContents>();
	const auto spansSpec = memSpec.add<TokenSpan>( m_fileTokenSpans.size() );
	const auto contentsSpec = memSpec.add<char>( numKeptChars );

	auto *const mem = (uint8_t *)::malloc( memSpec.sizeSoFar() );
	if( !mem ) {
		return nullptr;
	}

	auto *const result = new( headerSpec.get( mem ) )MaterialFileContents();
	result->spans = spansSpec.get( mem );
	result->data = contentsSpec.get( mem );
	assert( !result->dataSize && !result->numSpans );

	// Copy spans and compactified data
	char *const data = contentsSpec.get( mem );
	for( const auto &parsedSpan: m_fileTokenSpans ) {
		auto *copiedSpan = &result->spans[result->numSpans++];
		*copiedSpan = parsedSpan;
		copiedSpan->offset = result->dataSize;
		std::memcpy( data + copiedSpan->offset, rawContents->data() + parsedSpan.offset, parsedSpan.len );
		result->dataSize += parsedSpan.len;
		assert( parsedSpan.len == copiedSpan->len && parsedSpan.line == copiedSpan->line );
	}

	assert( result->numSpans == m_fileTokenSpans.size() );
	assert( result->dataSize == numKeptChars );

	return result;
}

void MaterialCache::addFileContents( const wsw::StringView &fileName ) {
	if( MaterialFileContents *contents = loadFileContents( fileName ) ) {
		if( tryAddingFileContents( contents ) ) {
			assert( !contents->next );
			contents->next = m_fileContentsHead;
			m_fileContentsHead = contents;
		} else {
			contents->~MaterialFileContents();
			free( contents );
		}
	}
}

bool MaterialCache::tryAddingFileContents( const MaterialFileContents *contents ) {
	m_fileMaterialNames.clear();
	m_fileSourceSpans.clear();

	unsigned tokenNum = 0;
	TokenStream stream( contents->data, contents->spans, contents->numSpans );
	for(;;) {
		auto maybeNameToken = stream.getNextToken();
		if( !maybeNameToken ) {
			break;
		}

		tokenNum++;

		auto maybeNextToken = stream.getNextToken();
		if( !maybeNextToken ) {
			return false;
		}

		tokenNum++;
		const unsigned shaderSpanStart = tokenNum;

		auto nextToken = *maybeNextToken;
		if( nextToken.length() != 1 || nextToken[0] != '{' ) {
			// TODO: Include the line and the token in report
			// TODO: Use new logging facilities
			Com_Printf( S_COLOR_YELLOW "Expected an opening brace after the name\n" );
			return false;
		}

		// TODO: Count how many tokens are in the shader
		for( int depth = 1; depth; ) {
			if( const auto maybeBlockToken = stream.getNextToken() ) {
				tokenNum++;
				const auto blockToken = *maybeBlockToken;
				const char ch = blockToken.maybeFront().value_or( '\0' );
				depth += ( ch == '{' ) ? +1 : 0;
				depth += ( ch == '}' ) ? -1 : 0;
			} else {
				Com_Printf( S_COLOR_YELLOW "Missing closing brace(s) at the end of file\n" );
				// TODO: Include the line and the token in report
				// TODO: Use new logging facilities
				return false;
			}
		}

		m_fileMaterialNames.emplace_back( *maybeNameToken );
		assert( tokenNum > shaderSpanStart );
		// Exclude the closing brace from the range
		m_fileSourceSpans.emplace_back( std::make_pair( shaderSpanStart, tokenNum - shaderSpanStart - 1 ) );
	}

	auto *mem = (uint8_t *)::malloc( sizeof( MaterialSource ) * m_fileMaterialNames.size() );
	if( !mem ) {
		return false;
	}

	auto *const firstInSameMemChunk = (MaterialSource *)mem;

	assert( m_fileMaterialNames.size() == m_fileSourceSpans.size() );
	for( size_t i = 0; i < m_fileMaterialNames.size(); ++i ) {
		auto *const source = new( mem )MaterialSource;
		mem += sizeof( MaterialSource );

		const auto &[from, len] = m_fileSourceSpans[i];
		source->m_tokenSpansOffset = from;
		source->m_numTokens = len;
		source->m_fileContents = contents;
		source->m_firstInSameMemChunk = firstInSameMemChunk;
		source->m_name = wsw::HashedStringView( m_fileMaterialNames[i] );
		source->m_nextInList = m_sourcesHead;
		m_sourcesHead = source;

		const auto binIndex = source->getName().getHash() % kNumBins;
		source->m_nextInBin = m_sourceBins[binIndex];
		m_sourceBins[binIndex] = source;
	}

	return true;
}

auto MaterialCache::findSourceByName( const wsw::HashedStringView &name ) -> MaterialSource * {
	const auto binIndex = name.getHash() % kNumBins;
	for( MaterialSource *source = m_sourceBins[binIndex]; source; source = source->m_nextInBin ) {
		if( source->getName().equalsIgnoreCase( name ) ) {
			return source;
		}
	}
	return nullptr;
}

// TODO: Can it be const?
auto MaterialCache::findSkinByName( const wsw::StringView &name ) -> Skin * {
	for( Skin &skin: m_skins ) {
		if( skin.getName().equalsIgnoreCase( name ) ) {
			return std::addressof( skin );
		}
	}
	return nullptr;
}

auto MaterialCache::readSkinFileData( const wsw::StringView &name, char *buffer,
									  size_t bufferSize ) -> std::optional<wsw::StringView> {
	wsw::StaticString<MAX_QPATH> pathBuffer;
	wsw::StringView filePath = name;
	if( !pathBuffer.endsWith( ".skin"_asView ) ) {
		pathBuffer << name << ".skin"_asView;
		filePath = pathBuffer.asView();
	}

	auto maybeHandle = wsw::fs::openAsReadHandle( filePath );
	if( !maybeHandle ) {
		Com_Printf( "Failed to load skin %s: Failed to open %s\n", name.data(), pathBuffer.data() );
		return std::nullopt;
	}

	const auto fileSize = maybeHandle->getInitialFileSize();
	if( !fileSize || fileSize >= bufferSize ) {
		Com_Printf( "Failed to load skin %s: The file %s has a bogus size\n", name.data(), pathBuffer.data() );
		return std::nullopt;
	}

	if( !maybeHandle->readExact( buffer, fileSize ) ) {
		Com_Printf( "Failed to load %s: Failed to read %s\n", name.data(), pathBuffer.data() );
		return std::nullopt;
	}

	buffer[fileSize] = '\0';
	return wsw::StringView( buffer, fileSize, wsw::StringView::ZeroTerminated );
}

bool MaterialCache::parseSkinFileData( Skin *skin, const wsw::StringView &fileData ) {
	assert( skin->m_stringDataStorage.empty() );
	assert( skin->m_meshPartMaterials.empty() );

	wsw::StringSplitter splitter( fileData );
	// TODO: There should be static standard lookup instances
	wsw::CharLookup newlineChars( "\r\n"_asView );
	while( const auto maybeToken = splitter.getNext( newlineChars ) ) {
		if( skin->m_meshPartMaterials.full() ) {
			return false;
		}
		const wsw::StringView token( maybeToken->trim() );
		const auto maybeCommaIndex = token.indexOf( ',' );
		if( maybeCommaIndex == std::nullopt ) {
			if( token.startsWith( "//"_asView ) ) {
				continue;
			}
			return false;
		}
		auto [left, right] = token.dropMid( *maybeCommaIndex, 1 );
		std::tie( left, right ) = std::make_pair( left.trim(), right.trim() );
		if( left.empty() || right.empty() ) {
			return false;
		}
		if( right.indexOf( ',' ) != std::nullopt ) {
			return false;
		}
		const unsigned meshNameSpanNum = skin->m_stringDataStorage.add( left );
		// This is an inlined body of the old R_RegisterSkin()
		// We hope no zero termination is required
		shader_s *material = this->loadMaterial( right, SHADER_TYPE_DIFFUSE, false );
		skin->m_meshPartMaterials.push_back( std::make_pair( material, meshNameSpanNum ) );
	}

	return true;
}

auto MaterialCache::parseSkinFileData( const wsw::StringView &name, const wsw::StringView &fileData ) -> Skin * {
	// Hacks! The method code would've been much nicer with a stack construction
	// of an instance and a further call of emplace_back( std::move( ... ) )
	// but Skin is not movable and we do not want to make one of its member types movable.
	const auto oldSize = m_skins.size();
	void *mem = m_skins.unsafe_grow_back();
	Skin *skin;
	try {
		skin = new( mem )Skin();
	} catch( ... ) {
		m_skins.unsafe_set_size( oldSize );
		throw;
	}

	try {
		if( !parseSkinFileData( skin, fileData ) ) {
			m_skins.pop_back();
			return nullptr;
		}
		skin->m_registrationSequence = rsh.registrationSequence;
		skin->m_stringDataStorage.add( name );
		return skin;
	} catch( ... ) {
		m_skins.pop_back();
		throw;
	}
}

auto MaterialCache::registerSkin( const wsw::StringView &name ) -> Skin * {
	if( Skin *existing = findSkinByName( name ) ) {
		existing->m_registrationSequence = rsh.registrationSequence;
		for( auto &[material, _] : existing->m_meshPartMaterials ) {
			R_TouchShader( material );
		}
		return existing;
	}

	assert( name.isZeroTerminated() && name.size() < MAX_QPATH );
	if( m_skins.full() ) {
		Com_Printf( "Failed to load skin %s: Too many skins\n", name.data() );
		return nullptr;
	}

	char buffer[1024];
	if( const auto maybeFileData = readSkinFileData( name, buffer, sizeof( buffer ) ) ) {
		return parseSkinFileData( name, *maybeFileData );
	}

	return nullptr;
}

auto MaterialCache::findMeshMaterialInSkin( const Skin *skin, const wsw::StringView &materialName ) -> shader_s * {
	assert( skin );
	for( const auto &[material, nameSpanNum] : skin->m_meshPartMaterials ) {
		const wsw::StringView name( skin->m_stringDataStorage[nameSpanNum] );
		if( name.equalsIgnoreCase( materialName ) ) {
			return material;
		}
	}
	return nullptr;
}

Skin *R_RegisterSkinFile( const char *name ) {
	return MaterialCache::instance()->registerSkin( wsw::StringView( name ) );
}

shader_t *R_FindShaderForSkinFile( const Skin *skin, const char *meshname ) {
	return MaterialCache::instance()->findMeshMaterialInSkin( skin, wsw::StringView( meshname ) );
}