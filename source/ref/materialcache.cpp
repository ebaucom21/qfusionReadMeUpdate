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

	freeMaterialIds.reserve( MAX_SHADERS );
	for( unsigned i = 0; i < MAX_SHADERS; ++i ) {
		freeMaterialIds.push_back( i );
	}
}

void MaterialCache::loadDirContents( const wsw::StringView &dir ) {
	wsw::fs::SearchResultHolder searchResultHolder;
	if( auto callResult = searchResultHolder.findDirFiles( dir, ".shader"_asView ) ) {
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
	for( unsigned i = 0; i < s->numpasses; i++ ) {
		shaderpass_t *pass = s->passes + i;

		auto *const textureCache = TextureCache::instance();
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
		for( shader_s *material = materialsHead; material; material = material->next[shader_t::kListLinks] ) {
			if( material->registrationSequence != rsh.registrationSequence ) {
				continue;
			}
			unlinkAndFree( material );
		}
		return;
	}

	for( shader_s *material = materialsHead; material; material = material->next[shader_t::kListLinks] ) {
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
	materialById[s->id] = nullptr;
	freeMaterialIds.push_back( s->id );

	abort();
}

auto MaterialCache::getNextMaterialId() -> unsigned {
	if( freeMaterialIds.empty() ) {
		Com_Error( ERR_FATAL, "Out of free material ids\n" );
	}
	auto result = freeMaterialIds.back();
	freeMaterialIds.pop_back();
	return result;
}

auto MaterialCache::makeCleanName( const wsw::StringView &name ) -> wsw::HashedStringView {
	cleanNameBuffer.clear();
	cleanNameBuffer.reserve( name.length() + 1 );

	unsigned i = 0;
	for( char ch: name ) {
		if( !( ch == '/' | ch == '\\' ) ) {
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
		cleanNameBuffer.push_back( cleanCh );
		hash = wsw::nextHashStep( hash, cleanCh );
		if( cleanNameBuffer.back() == '/' ) {
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
		cleanNameBuffer.resize( lastDot );
		hash = hashBackup;
	}

	return wsw::HashedStringView( cleanNameBuffer.data(), cleanNameBuffer.length(), hash );
}

auto MaterialCache::getTokenStreamForShader( const wsw::HashedStringView &cleanName ) -> TokenStream * {
	MaterialSource *source = findSourceByName( cleanName );
	if( !source ) {
		return nullptr;
	}

	if( !primaryTokenStreamHolder.empty() ) {
		primaryTokenStreamHolder.pop_back();
	}

	void *mem = primaryTokenStreamHolder.unsafe_grow_back();
	const auto [spans, numTokens] = source->getTokenSpans();
	return new( mem )TokenStream( source->getCharData(), spans, numTokens );
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
		if( ( !pass->images[1] || pass->images[1]->missing || pass->images[1]->isAPlaceholder ) &&
			( !pass->images[2] || pass->images[2]->missing ) &&
			( !pass->images[3] || pass->images[3]->missing ) &&
			( !pass->images[4] || pass->images[4]->missing ) ) {
			program_type = GLSL_PROGRAM_TYPE_Q3A_SHADER;
		}
	}

	// sort by base program type
	order = program_type & 0x1F;

	// check presence of gloss for materials
	if( program_type == GLSL_PROGRAM_TYPE_MATERIAL && pass->images[2] != NULL && !pass->images[2]->missing ) {
		order |= 0x20;
	}

	return order;
}

void MaterialCache::touchMaterialsByName( const wsw::StringView &name ) {
	wsw::HashedStringView cleanView( makeCleanName( name ) );
	auto binIndex = cleanView.getHash() % kNumBins;
	for( shader_t *material = materialBins[binIndex]; material; material = material->next[shader_t::kBinLinks] ) {
		if( cleanView.equalsIgnoreCase( wsw::HashedStringView( material->name ) ) ) {
			R_TouchShader( material );
		}
	}
}

auto MaterialCache::loadMaterial( const wsw::StringView &name, int type, bool forceDefault, Texture * )
	-> shader_t * {
	wsw::HashedStringView cleanName( makeCleanName( name ) );
	const auto binIndex = cleanName.getHash() % kNumBins;

	for( shader_t *material = materialBins[binIndex]; material; material = material->next[shader_t::kBinLinks] ) {
		if( !cleanName.equalsIgnoreCase( material->name ) ) {
			continue;
		}
		// TODO: This should be a method
		if( material->type == type || ( type == SHADER_TYPE_2D && material->type == SHADER_TYPE_2D_RAW ) ) {
			R_TouchShader( material );
			return material;
		}
	}

	TokenStream *tokenStream = forceDefault ? nullptr : getTokenStreamForShader( cleanName );
	auto *material = loadMaterial( cleanName, name, type, tokenStream );
	if( !material ) {
		return nullptr;
	}

	material->registrationSequence = rsh.registrationSequence;
	wsw::link( material, &materialBins[binIndex], shader_t::kBinLinks );
	return material;
}

shader_t *MaterialCache::loadDefaultMaterial( const wsw::StringView &name, int type ) {
    return loadMaterial( name, type, true, TextureCache::instance()->noTexture() );
}

/*
* R_RegisterPic
*/
shader_t *R_RegisterPic( const char *name ) {
	return MaterialCache::instance()->loadMaterial( wsw::StringView( name ), SHADER_TYPE_2D, false, TextureCache::instance()->noTexture() );
}

shader_t *R_RegisterRawAlphaMask( const char *name, int width, int height, uint8_t *data ) {
	const wsw::StringView nameView( name );
	auto *const material = MaterialCache::instance()->loadDefaultMaterial( nameView, SHADER_TYPE_2D_RAW );
	if( !material ) {
	    return nullptr;
	}

	TextureCache *textureCache = TextureCache::instance();
	// unlink and delete the old image from memory, unless it's the default one
	Texture *image = material->passes[0].images[0];
	if( !image || image->isAPlaceholder ) {
		// try to load new image
		material->passes[0].images[0] = textureCache->createFontMask( nameView, width, height, data );
	} else {
		// replace current texture data
		textureCache->replaceFontMaskSamples( image, width, height, data );
	}
	return material;
}

/*
* R_RegisterShader
*/
shader_t *R_RegisterShader( const char *name, shaderType_e type ) {
	return MaterialCache::instance()->loadMaterial( wsw::StringView( name ), type, false );
}

/*
* R_RegisterSkin
*/
shader_t *R_RegisterSkin( const char *name ) {
	return MaterialCache::instance()->loadMaterial( wsw::StringView( name ), SHADER_TYPE_DIFFUSE, false );
}

/*
* R_RegisterVideo
*/
shader_t *R_RegisterVideo( const char *name ) {
	return MaterialCache::instance()->loadMaterial( wsw::StringView( name ), SHADER_TYPE_VIDEO, false );
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
void R_GetShaderDimensions( const shader_t *shader, int *width, int *height ) {
	Texture *baseImage;

	assert( shader );
	if( !shader || !shader->numpasses ) {
		return;
	}

	baseImage = shader->passes[0].images[0];
	if( !baseImage ) {
		//Com_DPrintf( S_COLOR_YELLOW "R_GetShaderDimensions: shader %s is missing base image\n", shader->name );
		return;
	}

	if( width ) {
		*width = baseImage->width;
	}
	if( height ) {
		*height = baseImage->height;
	}
}

/*
* R_ReplaceRawSubPic
*
* Adds a new subimage to the specified raw pic.
* Must not be used to overwrite previously written areas when doing batched drawing.
*/
void R_ReplaceRawSubPic( shader_t *shader, int x, int y, int width, int height, uint8_t *data ) {
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

	assert( ( ( x + width ) <= baseImage->upload_width ) && ( ( y + height ) <= baseImage->upload_height ) );
	if( ( ( x + width ) > baseImage->upload_width ) || ( ( y + height ) > baseImage->upload_height ) ) {
		return;
	}

	TextureCache::instance()->replaceFontMaskSamples( baseImage, width, height, data );
}

auto MaterialCache::initMaterial( int type, const wsw::HashedStringView &cleanName, wsw::MemSpecBuilder memSpec )
	-> shader_t * {
	assert( memSpec.sizeSoFar() >= sizeof( shader_t ) );
	auto nameSpec = memSpec.add<char>( cleanName.size() + 1 );

	// TODO: Overload a global delete operator for material
	void *mem = malloc( memSpec.sizeSoFar() );
	if( !mem ) {
		throw std::bad_alloc();
	}

	// TODO: Should call constructors for all contained items
	std::memset( mem, 0, memSpec.sizeSoFar() );

	auto *s = new( mem )shader_t;
	// TODO... all this initialization belongs to a (not yet implemented) constructor
	s->type = (decltype( s->type ) )type;

	if( type >= SHADER_TYPE_BSP_MIN && type <= SHADER_TYPE_BSP_MAX ) {
		s->imagetags = IMAGE_TAG_WORLD;
	} else {
		s->imagetags = IMAGE_TAG_GENERIC;
	}

	// set defaults
	s->flags = SHADER_CULL_FRONT;
	s->vattribs = 0;
	s->glossIntensity = 0;
	s->glossExponent = 0;
	s->offsetmappingScale = 1;

	char *nameData = nameSpec.get( s );
	cleanName.copyTo( nameData, cleanName.size() + 1 );
	s->name = wsw::HashedStringView( nameData, cleanName.size(), cleanName.getHash(), wsw::StringView::ZeroTerminated );

	return s;
}

auto MaterialCache::newDefaultVertexMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name )
	-> shader_t * {
	wsw::MemSpecBuilder memSpec( wsw::MemSpecBuilder::withInitialSizeOf<shader_t>() );
	const auto passSpec = memSpec.add<shaderpass_t>();

	shader_t *s = initMaterial( SHADER_TYPE_VERTEX, cleanName, memSpec );

	// vertex lighting
	s->flags = SHADER_DEPTHWRITE | SHADER_CULL_FRONT;
	s->vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT | VATTRIB_COLOR0_BIT;
	s->sort = SHADER_SORT_OPAQUE;
	s->numpasses = 1;
	auto *pass = s->passes = passSpec.get( s );

	pass->flags = GLSTATE_DEPTHWRITE;
	pass->tcgen = TC_GEN_BASE;
	pass->rgbgen.type = RGB_GEN_VERTEX;
	pass->alphagen.type = ALPHA_GEN_IDENTITY;
	pass->images[0] = findImage( name,  s ->flags, IT_SRGB );

	return s;
}

auto MaterialCache::newDefaultDeluxeMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name )
	-> shader_t * {
	wsw::MemSpecBuilder memSpec( wsw::MemSpecBuilder::withInitialSizeOf<shader_t>() );
	const auto passSpec = memSpec.add<shaderpass_t>();
	shader_t *s = initMaterial( SHADER_TYPE_DELUXEMAP, cleanName, memSpec );

	// deluxemapping

    Texture *images[3] { nullptr, nullptr, nullptr };
	// TODO: Name or clean name?
	loadMaterial( images, name, 0, s->imagetags );

	s->flags = SHADER_DEPTHWRITE | SHADER_CULL_FRONT | SHADER_LIGHTMAP;
	s->vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT | VATTRIB_LMCOORDS0_BIT | VATTRIB_NORMAL_BIT | VATTRIB_SVECTOR_BIT;
	s->sort = SHADER_SORT_OPAQUE;
	s->numpasses = 1;
	s->passes = passSpec.get( s );

	auto *pass = &s->passes[0];
	pass->flags = GLSTATE_DEPTHWRITE;
	pass->tcgen = TC_GEN_BASE;
	pass->rgbgen.type = RGB_GEN_IDENTITY;
	pass->alphagen.type = ALPHA_GEN_IDENTITY;
	pass->program_type = GLSL_PROGRAM_TYPE_MATERIAL;
	pass->images[0] = findImage( name, s->flags, IT_SRGB );
	pass->images[1] = images[0]; // normalmap
	pass->images[2] = images[1]; // glossmap
	pass->images[3] = images[2]; // decalmap

	return s;
}

auto MaterialCache::newDefaultCoronaMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name )
	-> shader_t * {
	wsw::MemSpecBuilder memSpec( wsw::MemSpecBuilder::withInitialSizeOf<shader_t>() );
	const auto passSpec = memSpec.add<shaderpass_t>();

	shader_t *s = initMaterial( SHADER_TYPE_CORONA, cleanName, memSpec );

	s->vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT | VATTRIB_COLOR0_BIT;
	s->sort = SHADER_SORT_ADDITIVE;
	s->numpasses = 1;
	s->passes = passSpec.get( s );
	s->flags = SHADER_SOFT_PARTICLE;

	auto *pass = &s->passes[0];
	pass->flags = GLSTATE_SRCBLEND_ONE | GLSTATE_DSTBLEND_ONE;
	pass->rgbgen.type = RGB_GEN_VERTEX;
	pass->alphagen.type = ALPHA_GEN_IDENTITY;
	pass->tcgen = TC_GEN_BASE;
	pass->images[0] = findImage( wsw::StringView( "*corona" ), s->flags, IT_SPECIAL );

	return s;
}

auto MaterialCache::newDefaultDiffuseMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name )
	-> shader_t * {
	wsw::MemSpecBuilder memSpec( wsw::MemSpecBuilder::withInitialSizeOf<shader_t>() );
	auto passSpec = memSpec.add<shaderpass_t>();

	shader_s *s = initMaterial( SHADER_TYPE_DIFFUSE, cleanName, memSpec );

	Texture *materialImages[3];

	// load material images
	// TODO: Name or clean name?
	loadMaterial( materialImages, cleanName, 0, s->imagetags );

	s->flags = SHADER_DEPTHWRITE | SHADER_CULL_FRONT;
	s->vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT | VATTRIB_NORMAL_BIT;
	s->sort = SHADER_SORT_OPAQUE;
	s->numpasses = 1;
	s->passes = passSpec.get( s );

	auto *pass = &s->passes[0];
	pass->flags = GLSTATE_DEPTHWRITE;
	pass->rgbgen.type = RGB_GEN_IDENTITY;
	pass->alphagen.type = ALPHA_GEN_IDENTITY;
	pass->tcgen = TC_GEN_BASE;
	pass->program_type = GLSL_PROGRAM_TYPE_MATERIAL;
	pass->images[0] = findImage( name, s->flags, IT_SRGB );
	pass->images[1] = materialImages[0]; // normalmap
	pass->images[2] = materialImages[1]; // glossmap
	pass->images[3] = materialImages[2]; // decalmap
	s->vattribs |= VATTRIB_SVECTOR_BIT | VATTRIB_NORMAL_BIT;

	return s;
}

auto MaterialCache::newDefault2DLikeMaterial( int type,
											  const wsw::HashedStringView &cleanName,
											  const wsw::StringView &name )
												-> shader_t * {
	wsw::MemSpecBuilder memSpec( wsw::MemSpecBuilder::withInitialSizeOf<shader_t>() );
	const auto passSpec = memSpec.add<shaderpass_t>();

	shader_t *s = initMaterial( type, cleanName, memSpec );

	s->flags = 0;
	s->vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT | VATTRIB_COLOR0_BIT;
	s->sort = SHADER_SORT_ADDITIVE;
	s->numpasses = 1;
	s->passes = passSpec.get( s );

	auto *pass = &s->passes[0];
	pass->flags = GLSTATE_SRCBLEND_SRC_ALPHA | GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	pass->rgbgen.type = RGB_GEN_VERTEX;
	pass->alphagen.type = ALPHA_GEN_VERTEX;
	pass->tcgen = TC_GEN_BASE;
	if( type == SHADER_TYPE_2D_LINEAR ) {
		pass->images[0] = findImage( name, s->flags, IT_SPECIAL | IT_SYNC );
	} else if( type != SHADER_TYPE_2D_RAW ) {
		pass->images[0] = findImage( name, s->flags, IT_SPECIAL | IT_SYNC | IT_SRGB );
	}

	return s;
}

auto MaterialCache::newOpaqueEnvMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_s * {
	wsw::MemSpecBuilder memSpec( wsw::MemSpecBuilder::withInitialSizeOf<shader_t>() );
	const auto passSpec = memSpec.add<shaderpass_t>();

	auto *s = initMaterial( SHADER_TYPE_OPAQUE_ENV, cleanName, memSpec );

	s->vattribs = VATTRIB_POSITION_BIT;
	s->sort = SHADER_SORT_OPAQUE;
	s->flags = SHADER_CULL_FRONT | SHADER_DEPTHWRITE;
	s->numpasses = 1;
	s->passes = passSpec.get( s );

	auto *pass = &s->passes[0];
	pass->flags = GLSTATE_DEPTHWRITE;
	pass->rgbgen.type = RGB_GEN_ENVIRONMENT;
	VectorClear( pass->rgbgen.args );
	pass->alphagen.type = ALPHA_GEN_IDENTITY;
	pass->tcgen = TC_GEN_NONE;
	pass->images[0] = TextureCache::instance()->whiteTexture();

	return s;
}

auto MaterialCache::newFogMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_t * {
	wsw::MemSpecBuilder memSpec( wsw::MemSpecBuilder::withInitialSizeOf<shader_t>() );

	auto *s = initMaterial( SHADER_TYPE_FOG, cleanName, memSpec );
	s->vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT;
	s->sort = SHADER_SORT_FOG;
	s->flags = SHADER_CULL_FRONT;
	s->numpasses = 0;
	return s;
}

auto MaterialCache::loadMaterial( const wsw::HashedStringView &cleanName,
								  const wsw::StringView &name,
								  int type, TokenStream *stream )
									-> shader_t * {
	shader_s *result = nullptr;
	if( stream ) {
		MaterialParser parser( this, stream, name, cleanName, (shaderType_e)type );
		result = parser.exec();
	}

	if( !result ) {
		result = newDefaultMaterial( type, cleanName, name );
	}

	assert( result );
	result->id = getNextMaterialId();
	materialById[result->id] = result;
	return result;
}

auto MaterialCache::newDefaultMaterial( int type, const wsw::HashedStringView &cleanName, const wsw::StringView &name )
	-> shader_t * {
	switch( type ) {
		case SHADER_TYPE_VERTEX:
			return newDefaultVertexMaterial( cleanName, name );
		case SHADER_TYPE_DELUXEMAP:
			return newDefaultDeluxeMaterial( cleanName, name );
		case SHADER_TYPE_CORONA:
			return newDefaultCoronaMaterial( cleanName, name );
		case SHADER_TYPE_DIFFUSE:
			return newDefaultDiffuseMaterial( cleanName, name );
		case SHADER_TYPE_2D:
		case SHADER_TYPE_2D_RAW:
		case SHADER_TYPE_VIDEO:
		case SHADER_TYPE_2D_LINEAR:
			return newDefault2DLikeMaterial( type, cleanName, name );
		case SHADER_TYPE_OPAQUE_ENV:
			return newOpaqueEnvMaterial( cleanName, name );
		case SHADER_TYPE_FOG:
			return newFogMaterial( cleanName, name );
	}

	return nullptr;
}

auto MaterialCache::readRawContents( const wsw::StringView &fileName ) -> const wsw::String * {
	wsw::String &pathName = pathNameBuffer;
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
	fileContentsBuffer.resize( size + 1 );
	if( !maybeHandle->readExact( fileContentsBuffer.data(), size ) ) {
		return nullptr;
	}

	// Put the terminating zero, this is not mandatory as tokens aren't supposed
	// to be zero terminated but allows printing contents using C-style facilities
	fileContentsBuffer[size] = '\0';
	return &fileContentsBuffer;
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

	fileTokenSpans.clear();

	uint32_t lineNum = 0;
	size_t numKeptChars = 0;
	while( !splitter.isAtEof() ) {
		while( auto maybeToken = splitter.fetchNextTokenInLine() ) {
			const auto &[off, len] = *maybeToken;
			fileTokenSpans.emplace_back( TokenSpan { (int)( off + offsetShift ), len, lineNum } );
			numKeptChars += len;
		}
		lineNum++;
	}

	wsw::MemSpecBuilder memSpec( wsw::MemSpecBuilder::initiallyEmpty() );
	const auto headerSpec = memSpec.add<MaterialFileContents>();
	const auto spansSpec = memSpec.add<TokenSpan>( fileTokenSpans.size() );
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
	for( const auto &parsedSpan: fileTokenSpans ) {
		auto *copiedSpan = &result->spans[result->numSpans++];
		*copiedSpan = parsedSpan;
		copiedSpan->offset = result->dataSize;
		std::memcpy( data + copiedSpan->offset, rawContents->data() + parsedSpan.offset, parsedSpan.len );
		result->dataSize += parsedSpan.len;
		assert( parsedSpan.len == copiedSpan->len && parsedSpan.line == copiedSpan->line );
	}

	assert( result->numSpans == fileTokenSpans.size() );
	assert( result->dataSize == numKeptChars );

	return result;
}

void MaterialCache::addFileContents( const wsw::StringView &fileName ) {
	if( MaterialFileContents *contents = loadFileContents( fileName ) ) {
		if( tryAddingFileContents( contents ) ) {
			assert( !contents->next );
			contents->next = fileContentsHead;
			fileContentsHead = contents;
		} else {
			contents->~MaterialFileContents();
			free( contents );
		}
	}
}

bool MaterialCache::tryAddingFileContents( const MaterialFileContents *contents ) {
	fileMaterialNames.clear();
	fileSourceSpans.clear();

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
			if( auto maybeBlockToken = stream.getNextToken() ) {
				tokenNum++;
				auto blockToken = *maybeBlockToken;
				char ch = blockToken.maybeFront().value_or( '\0' );
				depth += ( ch == '{' ) ? +1 : 0;
				depth += ( ch == '}' ) ? -1 : 0;
			} else {
				Com_Printf( S_COLOR_YELLOW "Missing closing brace(s) at the end of file\n" );
				// TODO: Include the line and the token in report
				// TODO: Use new logging facilities
				return false;
			}
		}

		fileMaterialNames.emplace_back( *maybeNameToken );
		assert( tokenNum > shaderSpanStart );
		// Exclude the closing brace from the range
		fileSourceSpans.emplace_back( std::make_pair( shaderSpanStart, tokenNum - shaderSpanStart - 1 ) );
	}

	auto *mem = (uint8_t *)::malloc( sizeof( MaterialSource ) * fileMaterialNames.size() );
	if( !mem ) {
		return false;
	}

	auto *const firstInSameMemChunk = (MaterialSource *)mem;

	assert( fileMaterialNames.size() == fileSourceSpans.size() );
	for( size_t i = 0; i < fileMaterialNames.size(); ++i ) {
		auto *const source = new( mem )MaterialSource;
		mem += sizeof( MaterialSource );

		auto [from, len] = fileSourceSpans[i];
		source->m_tokenSpansOffset = from;
		source->m_numTokens = len;
		source->m_fileContents = contents;
		source->m_firstInSameMemChunk = firstInSameMemChunk;
		source->m_name = wsw::HashedStringView( fileMaterialNames[i] );
		source->nextInList = sourcesHead;
		sourcesHead = source;

		auto binIndex = source->getName().getHash() % kNumBins;
		source->nextInBin = sourceBins[binIndex];
		sourceBins[binIndex] = source;
	}

	return true;
}

auto MaterialCache::findSourceByName( const wsw::HashedStringView &name ) -> MaterialSource * {
	auto binIndex = name.getHash() % kNumBins;
	for( MaterialSource *source = sourceBins[binIndex]; source; source = source->nextInBin ) {
		if( source->getName().equalsIgnoreCase( name ) ) {
			return source;
		}
	}
	return nullptr;
}

auto MaterialCache::expandTemplate( const wsw::StringView &name, const wsw::StringView *args, size_t numArgs )
	-> MaterialLexer * {
	MaterialSource *source = findSourceByName( name );
	if( !source ) {
		return nullptr;
	}

	expansionBuffer.clear();
	templateTokenSpans.clear();
	if( !source->expandTemplate( args, numArgs, expansionBuffer, templateTokenSpans ) ) {
		return nullptr;
	}

	if( !templateLexerHolder.empty() ) {
		templateLexerHolder.pop_back();
		templateTokenStreamHolder.pop_back();
	}

	void *streamMem = templateTokenStreamHolder.unsafe_grow_back();
	new( streamMem )TokenStream( source->getCharData(), templateTokenSpans.data(),templateTokenSpans.size(), expansionBuffer.data() );

	void *lexerMem = templateLexerHolder.unsafe_grow_back();
	new( lexerMem )MaterialLexer( templateTokenStreamHolder.begin() );

	return templateLexerHolder.begin();
}

class BuiltinTexMatcher {
	// We have to compose tokens based on a supplied name. Do that without touching heap.
	struct Chunk {
		char data[16];
	};

	wsw::StaticVector<Chunk, 12> stringChunks;

	using TexNumberViews = wsw::StaticVector<std::pair<wsw::StringView, BuiltinTexNum>, 6>;

	TexNumberViews longTexNumbers;
	TexNumberViews shortTexNumbers;

	auto makeView( const char *prefix, const char *name, const char *suffix ) -> wsw::StringView {
		char *data = stringChunks.unsafe_grow_back()->data;
		Q_snprintfz( data, sizeof( Chunk::data ), "%s%s%s", prefix, name, suffix );
		return wsw::StringView( data );
	}

	void add( const char *name, BuiltinTexNum texNumber ) noexcept {
		assert( std::strlen( name ) < sizeof( Chunk::data ) );

		longTexNumbers.emplace_back( std::make_pair( makeView( "$", name, "image" ), texNumber ) );
		shortTexNumbers.emplace_back( std::make_pair( makeView( "*", name, "" ), texNumber ) );
	}

	static auto matchInList( const wsw::StringView &image, const TexNumberViews &views )
		-> std::optional<BuiltinTexNum> {
		for( const auto &[name, texNum] : views ) {
			if( name.equalsIgnoreCase( image ) ) {
				return std::optional( texNum );
			}
		}
		return std::nullopt;
	}
public:
	BuiltinTexMatcher() noexcept {
		add( "white", BuiltinTexNum::White );
		add( "black", BuiltinTexNum::Black );
		add( "grey", BuiltinTexNum::Grey );
		add( "blankbump", BuiltinTexNum::BlankBump );
		add( "particle", BuiltinTexNum::Particle );
		add( "corona", BuiltinTexNum::Corona );
	}

	auto match( const wsw::StringView &image ) -> std::optional<BuiltinTexNum> {
		// Try matching long tokens (they're more likely to be met in wsw assets)
		if( auto num = matchInList( image, longTexNumbers ) ) {
			return num;
		}
		return matchInList( image, shortTexNumbers );
	}
};

static BuiltinTexMatcher builtinTexMatcher;

static const wsw::StringView kLightmapPrefix( "*lm" );

auto MaterialCache::findImage( const wsw::StringView &name, int flags, int imageTags, int minMipSize ) -> Texture * {
	assert( minMipSize );

	// TODO: Move this to ImageCache?
	if( auto maybeBuiltinTexNum = builtinTexMatcher.match( name ) ) {
		return TextureCache::instance()->getBuiltinTexture( *maybeBuiltinTexNum );
	}

	if( kLightmapPrefix.equalsIgnoreCase( name.take( 3 ) ) ) {
	    // TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		//Com_DPrintf( S_COLOR_YELLOW "WARNING: shader %s has a stage with explicit lightmap image\n", shader->name );
		return TextureCache::instance()->whiteTexture();
	}

	// TODO: Passing params this way is error-prone!!!!!! Pass a struct!!!!!!!!!!!!!!!!!!!!!!!!
	Texture *texture = TextureCache::instance()->getMaterialTexture( name, flags, minMipSize, imageTags );
	if( !texture ) {
	    // TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		//Com_Printf( S_COLOR_YELLOW "WARNING: shader %s has a stage with no image: %s\n", shader->name, name.data() );
		return TextureCache::instance()->noTexture();
	}

	return texture;
}

void MaterialCache::loadMaterial( Texture **images, const wsw::StringView &fullName, int addFlags, int imagetags, int minMipSize ) {
    // set defaults
    images[0] = images[1] = images[2] = nullptr;

    auto *const cache = TextureCache::instance();
    // load normalmap image
    images[0] = cache->getMaterialTexture( fullName, kNormSuffix, ( addFlags | IT_NORMALMAP ), minMipSize, imagetags );

    // load glossmap image
    if( r_lighting_specular->integer ) {
        images[1] = cache->getMaterialTexture( fullName, kGlossSuffix, addFlags, minMipSize, imagetags );
    }

    images[2] = cache->getMaterialTexture( fullName, kDecalSuffix, addFlags, minMipSize, imagetags );
    if( !images[2] ) {
        images[2] = cache->getMaterialTexture( fullName, kAddSuffix, addFlags, minMipSize, imagetags );
    }
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
		if( skin->m_meshPartMaterials.size() == skin->m_meshPartMaterials.capacity() ) {
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
	if( m_skins.size() == m_skins.capacity() ) {
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