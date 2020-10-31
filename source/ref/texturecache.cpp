/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "local.h"
#include "../qcommon/hash.h"
#include "../qcommon/links.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/wswfs.h"
#include "../qcommon/singletonholder.h"

using wsw::operator""_asView;
using wsw::operator""_asHView;

#include <algorithm>
#include <tuple>
#include <memory>
#include <utility>

#define STB_IMAGE_IMPLEMENTATION
#include "../../third-party/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third-party/stb/stb_image_write.h"

static SingletonHolder<TextureCache> textureCacheHolder;
static bool initialized;

void TextureCache::init() {
	textureCacheHolder.Init();
	initialized = true;
}

void TextureCache::shutdown() {
	textureCacheHolder.Shutdown();
	initialized = false;
}

auto TextureCache::instance() -> TextureCache * {
	return textureCacheHolder.Instance();
}

auto TextureCache::maybeInstance() -> TextureCache * {
	if( initialized ) {
		return textureCacheHolder.Instance();
	}
	return nullptr;
}

TextureCache::TextureCache() {
	std::fill( std::begin( m_hashBins ), std::end( m_hashBins ), nullptr );
	std::fill( std::begin( m_portalTextures ), std::end( m_portalTextures ), nullptr );
	std::fill( std::begin( m_builtinTextures ), std::end( m_builtinTextures ), nullptr );

	for( Texture &texture: m_textureStorage ) {
		wsw::link( std::addressof( texture ), &m_freeTexturesHead, Texture::ListLinks );
	}

	m_nameDataStorage.reset( new char[kMaxTextures * kNameDataStride]);

	qglPixelStorei( GL_PACK_ALIGNMENT, 1 );
	qglPixelStorei( GL_UNPACK_ALIGNMENT, 1 );

	initBuiltinTextures();
}

void TextureCache::bindToModify( GLenum target, GLuint texture ) {
	qglBindTexture( target, texture );
	RB_FlushTextureCache();
	assert( qglGetError() == GL_NO_ERROR );
}

void TextureCache::unbindModified( GLenum target, GLuint texture ) {
	// TODO: Check whether it's actively bound (it must be) for debugging purposes
	qglBindTexture( target, 0 );
	RB_FlushTextureCache();
	assert( qglGetError() == GL_NO_ERROR );
}

auto TextureCache::generateHandle( const wsw::StringView &label ) -> GLuint {
	assert( qglGetError() == GL_NO_ERROR );

	GLuint handle = 0;
	qglGenTextures( 1, &handle );
	// TODO
	//if( qglObjectLabel ) {
	//	qglObjectLabel( GL_TEXTURE, handle, std::min( label.length(), (size_t)glConfig.maxObjectLabelLen ), label.data() );
	//}

	assert( qglGetError() == GL_NO_ERROR );
	return handle;
}

void TextureCache::touchTexture( Texture *texture, unsigned lifetimeTags ) {
	texture->registrationSequence = rsh.registrationSequence;
	texture->tags |= lifetimeTags;
	// TODO: Old code used to touch FBO... do we need doing that?
}

const std::pair<wsw::StringView, TextureCache::TextureFilter> TextureCache::kTextureFilterNames[3] {
	{ "nearest"_asView, Nearest },
	{ "bilinear"_asView, Bilinear },
	{ "trilinear"_asView, Trilinear }
};

const std::pair<GLuint, GLuint> TextureCache::kTextureFilterGLValues[3] {
	{ GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
	{ GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
	{ GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};

auto TextureCache::findFilterByName( const wsw::StringView &name ) -> std::optional<TextureFilter> {
	for( const auto &[filterName, filter]: kTextureFilterNames ) {
		if( name.equalsIgnoreCase( filterName ) ) {
			return filter;
		}
	}
	return std::nullopt;
}

void TextureCache::applyFilter( const wsw::StringView &name, int anisoLevel ) {
	const auto maybeFilter = findFilterByName( name );
	if( !maybeFilter ) {
		assert( name.isZeroTerminated() );
		Com_Printf( "Failed to find a texture filter by name %s\n", name.data() );
		return;
	}

	const TextureFilter filter = *maybeFilter;
	const bool doApplyFilter = m_textureFilter != filter;
	const int anisoToApply = std::clamp( anisoLevel, 1, glConfig.maxTextureFilterAnisotropic );
	// TODO: The extension spec allows specifying aniso with non-trilinear filtering modes.
	// Should we really allow doing that?
	const bool shouldApplyAniso = m_anisoLevel != anisoToApply;
	const bool canApplyAniso = glConfig.ext.texture_filter_anisotropic;
	const bool doApplyAniso = shouldApplyAniso && canApplyAniso;
	if( !doApplyFilter && !doApplyAniso ) {
		return;
	}

	m_textureFilter = filter;
	m_anisoLevel = anisoToApply;

	assert( (unsigned)filter < std::size( kTextureFilterGLValues ) );
	const auto [minify, magnify] = kTextureFilterGLValues[(unsigned)filter];

	Texture *lastTexture = nullptr;
	for( Texture *texture = m_usedTexturesHead; texture; texture = texture->nextInList() ) {
		// There are individual checks in each apply() subroutine.
		// We add another early check to avoid doing a fruitless bind.
		if( texture->flags & ( IT_NOFILTERING | IT_DEPTH ) ) {
			continue;
		}
		bindToModify( texture );
		lastTexture = texture;
		if( doApplyFilter ) {
			applyFilter( texture, minify, magnify );
		}
		if( doApplyAniso ) {
			applyAniso( texture, anisoToApply );
		}
	}
	if( lastTexture ) {
		unbindModified( lastTexture );
	}
}

void TextureCache::applyFilter( Texture *texture, GLuint minify, GLuint magnify ) {
	// TODO: Check whether it's bound for modifying
	// TODO: What about usign the bindless API?
	if( texture->flags & ( IT_NOFILTERING | IT_DEPTH ) ) {
		return;
	}
	GLuint minifyToApply = ( texture->flags & IT_NOMIPMAP ) ? magnify : minify;
	qglTexParameteri( texture->target, GL_TEXTURE_MIN_FILTER, minifyToApply );
	qglTexParameteri( texture->target, GL_TEXTURE_MAG_FILTER, magnify );
}

void TextureCache::applyAniso( Texture *texture, int level ) {
	assert( glConfig.ext.texture_filter_anisotropic );
	assert( level >= 1 && level <= glConfig.maxTextureFilterAnisotropic );
	if( ( texture->flags & ( IT_NOFILTERING | IT_DEPTH | IT_NOMIPMAP ) ) ) {
		return;
	}
	qglTexParameteri( texture->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, level );
}

auto TextureCache::getNextMip( unsigned width, unsigned height, unsigned minMipSize )
	-> std::optional<std::pair<unsigned int, unsigned int>> {
	if( width > minMipSize || height > minMipSize ) {
		// TODO: check the lower bound, shouldn't it be minMipSize?
		return std::make_pair( std::min( 1u, width / 2 ), std::min( 1u, height / 2 ) );
	}
	return std::nullopt;
}

auto TextureCache::getLodForMinMipSize( unsigned width, unsigned height, unsigned minMipSize ) -> int {
	// TODO: `minMipSize` not used!
	int mip = 0;
	while( const auto maybeNextMip = getNextMip( width, height ) ) {
		std::tie( width, height ) = *maybeNextMip;
		mip++;
	}
	return mip;
}

void TextureCache::setupFilterMode( GLuint target, unsigned flags, unsigned w, unsigned h, unsigned minMipSize ) {
	if( flags & IT_NOFILTERING ) {
		qglTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		qglTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	} else if( flags & IT_DEPTH ) {
		qglTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		if( glConfig.ext.texture_filter_anisotropic ) {
			qglTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );
		}
	// TODO: Reverse the condition, reorder branches
	} else if( !( flags & IT_NOMIPMAP ) ) {
		const auto [minify, magnify] = kTextureFilterGLValues[m_textureFilter];
		qglTexParameteri( target, GL_TEXTURE_MIN_FILTER, minify );
		qglTexParameteri( target, GL_TEXTURE_MAG_FILTER, magnify );
		if( glConfig.ext.texture_filter_anisotropic ) {
			qglTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, m_anisoLevel );
		}
		if( minMipSize > 1 ) {
			const int lod = getLodForMinMipSize( w, h, minMipSize );
			qglTexParameteri( target, GL_TEXTURE_MAX_LOD, lod );
			qglTexParameteri( target, GL_TEXTURE_MAX_LEVEL, lod );
		}
	} else {
		const GLuint magnify = kTextureFilterGLValues[m_textureFilter].second;
		assert( magnify == GL_LINEAR || magnify == GL_NEAREST );
		// TODO: Use magnify for both??
		qglTexParameteri( target, GL_TEXTURE_MIN_FILTER, magnify );
		qglTexParameteri( target, GL_TEXTURE_MAG_FILTER, magnify );
		if( glConfig.ext.texture_filter_anisotropic ) {
			qglTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );
		}
	}
}

void TextureCache::setupWrapMode( GLuint target, unsigned flags ) {
	// TODO: check why is it IT_CLAMP by default
	const int wrap = ( flags & IT_CLAMP ) && false ? GL_CLAMP_TO_EDGE : GL_REPEAT;
	qglTexParameteri( target, GL_TEXTURE_WRAP_S, wrap );
	qglTexParameteri( target, GL_TEXTURE_WRAP_T, wrap );
}

static const GLint kSwizzleMaskIdentity[] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };
static const GLint kSwizzleMaskAlpha[] = { GL_ZERO, GL_ZERO, GL_ZERO, GL_RED };
static const GLint kSwizzleMaskLuminance[] = { GL_RED, GL_RED, GL_RED, GL_ALPHA };
static const GLint kSwizzleMaskLuminanceAlpha[] = { GL_RED, GL_RED, GL_RED, GL_GREEN };

[[nodiscard]]
static auto getTextureInternalFormat( int samples, int flags ) -> std::pair<GLuint, const GLint *> {
	// TODO: Unset this flag in LDR modes
	const bool sRGB = false;//( flags & IT_SRGB ) != 0;

	if( !( flags & IT_NOCOMPRESS ) && r_texturecompression->integer ) {
		if( samples == 4 ) {
			return { sRGB ? GL_COMPRESSED_SRGB_ALPHA : GL_COMPRESSED_RGBA, kSwizzleMaskIdentity };
		}
		if( samples == 3 ) {
			return { sRGB ? GL_COMPRESSED_SRGB :  GL_COMPRESSED_RGB, kSwizzleMaskIdentity };
		}
		if( samples == 2 ) {
			return { sRGB ? GL_COMPRESSED_SLUMINANCE_ALPHA : GL_COMPRESSED_RG, kSwizzleMaskLuminanceAlpha };
		}
		if( ( samples == 1 ) && !( flags & IT_ALPHAMASK ) ) {
			return { sRGB ? GL_COMPRESSED_SLUMINANCE : GL_COMPRESSED_RED, kSwizzleMaskLuminance };
		}
		// TODO: is the combination left illegal?
		assert( 0 );
	}

	if( samples == 3 ) {
		return { sRGB ? GL_SRGB8 : GL_RGB8, kSwizzleMaskIdentity };
	}

	if( samples == 2 ) {
		return { sRGB ? GL_RG16F : GL_RG, kSwizzleMaskLuminanceAlpha };
	}

	if( samples == 1 ) {
		const GLint *mask = ( flags & IT_ALPHAMASK ) ? kSwizzleMaskAlpha : kSwizzleMaskLuminance;
		return { sRGB ? GL_R16F : GL_R8, mask };
	}

	return { sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8, kSwizzleMaskIdentity };
}

struct GLTexImageFormats {
	const GLint *swizzleMask;
	GLint internalFormat;
	GLenum format;
	GLenum type;
};

[[nodiscard]]
static auto getDepthTexImageFormats( int flags ) -> GLTexImageFormats {
	assert( flags & IT_DEPTH );

	GLint internalFormat;
	GLenum format, type;
	if( flags & IT_STENCIL ) {
		internalFormat = format = GL_DEPTH_STENCIL;
		type = GL_UNSIGNED_INT_24_8;
	} else {
		internalFormat = format = GL_DEPTH_COMPONENT;
		type = GL_UNSIGNED_INT;
	}

	return { kSwizzleMaskIdentity, internalFormat, format, type };
}

[[nodiscard]]
static auto getFramebufferTexImageFormats( int flags, int samples ) -> GLTexImageFormats {
	assert( flags & IT_FRAMEBUFFER );

	GLint internalFormat;
	GLenum format, type;

	if( flags & IT_FLOAT ) {
		type = GL_FLOAT;
		if( samples == 4 ) {
			std::tie( format, internalFormat ) = std::make_pair( GL_RGBA, GL_RGBA16F );
		} else {
			std::tie( format, internalFormat ) = std::make_pair( GL_RGB, GL_RGB16F );
		}
	} else {
		type = GL_UNSIGNED_BYTE;
		if( samples == 4 ) {
			std::tie( format, internalFormat ) = std::make_pair( GL_RGBA, GL_RGBA8 );
		} else {
			std::tie( format, internalFormat ) = std::make_pair( GL_RGB, GL_RGB8 );
		}
	}

	return { kSwizzleMaskIdentity, internalFormat, format, type };
}

static const std::pair<unsigned, GLint> kFloatInternalFormatForSamples[4] {
	{ 4, GL_RGBA16F },
	{ 3, GL_RGB16F },
	{ 2, GL_RG16F },
	{ 1, GL_RG16F }
};

[[nodiscard]]
auto getRegularTexImageFormats( int flags, int samples ) -> GLTexImageFormats {
	const GLint *swizzleMask = kSwizzleMaskIdentity;
	GLenum format, type;
	GLint internalFormat;

	if( samples == 4 ) {
		format = ( flags & IT_BGRA ? GL_BGRA_EXT : GL_RGBA );
	} else if( samples == 3 ) {
		format = ( flags & IT_BGRA ? GL_BGR_EXT : GL_RGB );
	} else if( samples == 2 ) {
		format = GL_RG;
		swizzleMask = kSwizzleMaskLuminanceAlpha;
	} else if( flags & IT_ALPHAMASK ) {
		format = GL_RED;
		swizzleMask = kSwizzleMaskAlpha;
	} else {
		format = GL_RED;
		swizzleMask = kSwizzleMaskLuminance;
	}

	if( flags & IT_FLOAT ) {
		assert( samples >= 1 && samples <= 4 );
		const auto [tableSamples, tableFormat] = kFloatInternalFormatForSamples[samples - 1];
		assert( tableSamples == samples );
		// TODO: No swizzles?
		std::tie( type, internalFormat ) = std::make_pair( GL_FLOAT, tableFormat );
	} else {
		type = GL_UNSIGNED_BYTE;
		std::tie( internalFormat, swizzleMask ) = getTextureInternalFormat( samples, flags );
	}

	return { swizzleMask, internalFormat, format, type };
}

static ImageBuffer readFileBuffer;
static ImageBuffer loadingBuffer;
static ImageBuffer scalingBuffer;
static ImageBuffer lineBuffer;
static ImageBuffer flipBuffer;

auto TextureCache::loadTextureDataFromFile( const wsw::StringView &name ) -> std::optional<TextureFileData> {
	assert( name.isZeroTerminated() );
	assert( NUM_IMAGE_EXTENSIONS == 3 );
	// TODO: Adopt the sane FS interface over the codebase
	const wsw::StringView extensions[3] = {
		wsw::StringView( IMAGE_EXTENSIONS[0] ),
		wsw::StringView( IMAGE_EXTENSIONS[1] ),
		wsw::StringView( IMAGE_EXTENSIONS[2] )
	};

	const auto maybePartsPair = wsw::fs::findFirstExtension( name, std::begin( extensions ), std::end( extensions ) );
	if( !maybePartsPair ) {
		return std::nullopt;
	}

	wsw::StaticString<MAX_QPATH> path;
	path << maybePartsPair->first << maybePartsPair->second;
	auto maybeHandle = wsw::fs::openAsReadHandle( path.asView() );
	if( !maybeHandle ) {
		return std::nullopt;
	}

	constexpr size_t kMaxSaneImageDataSize = 2048 * 2048 * 4;
	const size_t fileSize = maybeHandle->getInitialFileSize();
	if( fileSize > kMaxSaneImageDataSize + 4096 ) {
		return std::nullopt;
	}

	uint8_t *const fileBuffer = readFileBuffer.reserveAndGet( fileSize );
	if( !maybeHandle->readExact( fileBuffer, fileSize ) ) {
		return std::nullopt;
	}

	int width = 0, height = 0, samples = 0;
	stbi_uc *bytes = stbi_load_from_memory( (const stbi_uc *)fileBuffer, (int)fileSize, &width, &height, &samples, 0 );
	if( !bytes ) {
		return std::nullopt;
	}

	// TODO: Provide allocators for stb that use the loading buffer?
	// This is not that easy as we use stb in the UI code as well.
	// TODO: Unify image loading code with the UI?
	assert( width > 0 && height > 0 && samples > 0 );
	const size_t imageDataSize = (size_t)width * (size_t)height * (size_t)samples;
	// Sanity check (disallow huge bogus allocations)
	if( imageDataSize > kMaxSaneImageDataSize ) {
		return std::nullopt;
	}

	uint8_t *const imageData = loadingBuffer.reserveAndGet( imageDataSize );
	std::memcpy( imageData, bytes, imageDataSize );
	stbi_image_free( bytes );

	return TextureFileData { imageData, (uint16_t)width, (uint16_t)height, (uint16_t)samples };
}

auto TextureCache::makeCleanName( const wsw::StringView &rawName, const wsw::StringView &suffix )
	-> std::optional<wsw::StringView> {
	wsw::StringView name( rawName );
	if( name.startsWith( '/' ) || name.startsWith( '\\' ) ) {
		name = name.drop( 1 );
	}

	if( name.empty() ) {
		return std::nullopt;
	}
	if( name.length() + suffix.length() >= m_cleanNameBuffer.capacity() ) {
		return std::nullopt;
	}

	int lastDotIndex = -1;
	int lastSlashIndex = -1;
	m_cleanNameBuffer.clear();
	for( int i = 0; i < (int)name.size(); ++i ) {
		const char ch = name[i];
		if( ch == '.' ) {
			lastDotIndex = i;
		}
		if( ch == '\\' ) {
			m_cleanNameBuffer.append( '/' );
		} else {
			m_cleanNameBuffer.append( ch );
		}
		if( m_cleanNameBuffer.back() == '/' ) {
			lastSlashIndex = i;
		}
	}

	if( lastDotIndex >= 0 && lastDotIndex < lastSlashIndex ) {
		m_cleanNameBuffer.erase( (unsigned)lastDotIndex );
	}

	m_cleanNameBuffer.append( suffix );
	return m_cleanNameBuffer.asView();
}

auto TextureCache::internTextureName( const Texture *texture, const wsw::StringView &name ) -> wsw::StringView {
	assert( texture >= std::begin( m_textureStorage ) );
	assert( texture < std::end( m_textureStorage ) );
	assert( name.length() <= kMaxNameLen );

	const auto index = texture - std::begin( m_textureStorage );
	char *const data = m_nameDataStorage.get() + kNameDataStride * index;
	std::memcpy( data, name.data(), name.length() );
	data[name.length()] = '\0';
	return wsw::StringView( data, name.length(), wsw::StringView::ZeroTerminated );
}

auto TextureCache::findTextureInBin( unsigned bin, const wsw::StringView &name, unsigned minMipSize, unsigned flags )
	-> Texture * {
	assert( bin < kNumHashBins );

	for( Texture *texture = m_hashBins[bin]; texture; texture = texture->nextInBin() ) {
		if( texture->name.equalsIgnoreCase( name ) ) {
			if( ( texture->flags & ~IT_LOADFLAGS ) == ( flags & ~IT_LOADFLAGS ) ) {
				if( texture->minmipsize == minMipSize ) {
					return texture;
				}
			}
		}
	}

	return nullptr;
}

auto TextureCache::getMaterialTexture( const wsw::StringView &name, const wsw::StringView &suffix,
									   unsigned flags, unsigned minMipSize, unsigned tags ) -> Texture * {
	const auto maybeCleanName = makeCleanName( name, suffix );
	if( !maybeCleanName ) {
		return nullptr;
	}

	// TODO: Keep a cache of 100% missing textures during material loading to reduce FS pressure

	const auto cleanName = *maybeCleanName;
	const auto binIndex = wsw::HashedStringView( cleanName ).getHash() % kNumHashBins;
	if( Texture *texture = findTextureInBin( binIndex, cleanName, minMipSize, flags ) ) {
		touchTexture( texture, tags );
		return texture;
	}

	if( !m_freeTexturesHead ) {
		return nullptr;
	}

	// TODO: Track disk loading failures while loading materials to avoid excessive FS calls?

	auto maybeFileData = loadTextureDataFromFile( cleanName );
	if( !maybeFileData ) {
		return nullptr;
	}

	auto [fileBytes, width, height, samples] = *maybeFileData;

	qglPixelStorei( GL_UNPACK_ALIGNMENT, 1 );

	const GLuint handle = generateHandle( cleanName );
	const GLenum target = GL_TEXTURE_2D;

	bindToModify( target, handle );

	const auto [swizzleMask, internalFormat, format, type] = getRegularTexImageFormats( flags, samples );
	setupFilterMode( target, flags, width, height, minMipSize );
	setupWrapMode( target, flags );

	if( swizzleMask != kSwizzleMaskIdentity ) {
		qglTexParameteriv( target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
	}

	qglTexImage2D( target, 0, internalFormat, width, height, 0, format, type, fileBytes );

	if( true || !( flags & IT_NOMIPMAP ) ) {
		qglGenerateMipmap( target );
	}

	unbindModified( target, handle );

	Texture *texture = wsw::unlink( m_freeTexturesHead, &m_freeTexturesHead, Texture::ListLinks );
	texture->name = internTextureName( texture, cleanName );
	texture->texnum = handle;
	texture->target = target;
	texture->flags = flags;
	texture->minmipsize = minMipSize;
	texture->width = texture->upload_width = width;
	texture->height = texture->upload_height = height;
	texture->samples = samples;
	texture->registrationSequence = rsh.registrationSequence;
	texture->fbo = 0;
	texture->isAPlaceholder = false;
	texture->framenum = 0;
	texture->missing = false;
	texture->tags = tags;

	wsw::link( texture, &m_usedTexturesHead, Texture::ListLinks );
	wsw::link( texture, &m_hashBins[binIndex], Texture::BinLinks );
	return texture;
}

auto TextureCache::createFontMask( const wsw::StringView &name, unsigned w, unsigned h, const uint8_t *data ) -> Texture * {
	const auto maybeCleanName = makeCleanName( name, wsw::StringView() );
	if( !maybeCleanName ) {
		return nullptr;
	}

	const auto flags = IT_NOFILTERING | IT_NOMIPMAP | IT_ALPHAMASK;

	const auto cleanName = *maybeCleanName;
	const auto binIndex = wsw::getHashForLength( cleanName.data(), cleanName.size() ) % kNumHashBins;
	if( Texture *texture = findTextureInBin( binIndex, cleanName, 1, flags ) ) {
		touchTexture( texture, IMAGE_TAG_BUILTIN );
		return texture;
	}

	const GLenum target = GL_TEXTURE_2D;
	const GLuint handle = generateHandle( cleanName );

	bindToModify( target, handle );

	const auto [swizzleMask, internalFormat, format, type] = getRegularTexImageFormats( flags, 1 );
	if( swizzleMask != kSwizzleMaskIdentity ) {
		qglTexParameteriv( target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
	}

	qglTexImage2D( target, 0, internalFormat, w, h, 0, format, type, data );

	setupFilterMode( target, flags, w, h, 1 );
	setupWrapMode( target, flags );

	unbindModified( target, handle );

	Texture *texture = wsw::unlink( m_freeTexturesHead, &m_freeTexturesHead, Texture::ListLinks );
	texture->name = internTextureName( texture, cleanName );
	texture->texnum = handle;
	texture->target = target;
	texture->flags = flags;
	texture->minmipsize = 1;
	texture->width = texture->upload_width = w;
	texture->height = texture->upload_height = h;
	texture->samples = 1;
	texture->registrationSequence = rsh.registrationSequence;
	texture->fbo = 0;
	texture->framenum = 0;
	texture->missing = false;
	texture->tags = IMAGE_TAG_GENERIC;

	wsw::link( texture, &m_usedTexturesHead, Texture::ListLinks );
	wsw::link( texture, &m_hashBins[binIndex], Texture::BinLinks );
	return texture;
}

static auto getLightmapFormatsForSamples( unsigned samples ) -> std::pair<GLint, GLenum> {
	assert( samples == 1 || samples == 3 );
	if( samples == 3 ) {
		return std::make_pair( GL_RGB8, GL_RGB );
	}
	return std::make_pair( GL_R8, GL_RED );
}

static int lmCounter = 0;

auto TextureCache::createLightmap( unsigned w, unsigned h, unsigned samples, const uint8_t *data ) -> Texture * {
	assert( samples == 1 || samples == 3 );

	if( !m_freeTexturesHead ) {
		return nullptr;
	}

	const auto [internalFormat, format] = getLightmapFormatsForSamples( samples );

	// TODO: We don't need to name it and put it in bins, just try finding a best match by parameters
	wsw::StaticString<MAX_QPATH> nameBuffer;
	nameBuffer << "lm"_asView << lmCounter++;

	const GLenum target = GL_TEXTURE_2D;
	const GLuint handle = generateHandle( nameBuffer.asView() );
	bindToModify( target, handle );

	qglTexImage2D( target, 0, internalFormat, w, h, 0, format, GL_UNSIGNED_BYTE, data );

	setupWrapMode( target, IT_CLAMP );
	setupFilterMode( target, IT_CLAMP | IT_NOMIPMAP, w, h, 1 );

	unbindModified( target, handle );

	// TODO: Reduce this boilerplate/use different lists for different texture types
	Texture *texture = wsw::unlink( m_freeTexturesHead, &m_freeTexturesHead, Texture::ListLinks );
	texture->name = internTextureName( texture, nameBuffer.asView() );
	texture->texnum = handle;
	texture->target = target;
	texture->flags = IT_CLAMP | IT_NOMIPMAP;
	texture->minmipsize = 1;
	texture->width = texture->upload_width = w;
	texture->height = texture->upload_height = h;
	texture->samples = samples;
	texture->registrationSequence = rsh.registrationSequence;
	texture->fbo = 0;
	texture->framenum = 0;
	texture->layers = 0;
	texture->missing = false;
	texture->tags = IMAGE_TAG_WORLD;

	const unsigned binIndex = wsw::HashedStringView( nameBuffer.asView() ).getHash() % kNumHashBins;
	wsw::link( texture, &m_usedTexturesHead, Texture::ListLinks );
	wsw::link( texture, &m_hashBins[binIndex], Texture::BinLinks );
	return texture;
}

static int lmArrayCounter = 0;

auto TextureCache::createLightmapArray( unsigned w, unsigned h, unsigned numLayers, unsigned samples ) -> Texture * {
	assert( samples == 1 || samples == 3 );

	// TODO: Separate freelists for different texture groups/types
	if( !m_freeTexturesHead ) {
		return nullptr;
	}

	const auto [internalFormat, format] = getLightmapFormatsForSamples( samples );

	// TODO: We don't need to name it and put it in bins, just try finding a best match by parameters
	wsw::StaticString<MAX_QPATH> nameBuffer;
	nameBuffer << "lmarray"_asView << lmArrayCounter++;

	const GLenum target = GL_TEXTURE_2D_ARRAY_EXT;
	const GLuint handle = generateHandle( nameBuffer.asView() );
	bindToModify( target, handle );

	qglTexImage3D( target, 0, internalFormat, w, h, numLayers, 0, format, GL_UNSIGNED_BYTE, nullptr );
	setupWrapMode( target, IT_CLAMP );
	setupFilterMode( target, IT_CLAMP | IT_NOMIPMAP, w, h, 1 );

	unbindModified( target, 0 );

	Texture *texture = wsw::unlink( m_freeTexturesHead, &m_freeTexturesHead, Texture::ListLinks );
	texture->name = internTextureName( texture, nameBuffer.asView() );
	texture->texnum = handle;
	texture->target = target;
	texture->flags = IT_CLAMP | IT_NOMIPMAP;
	texture->minmipsize = 1;
	texture->width = texture->upload_width = w;
	texture->height = texture->upload_height = h;
	texture->samples = samples;
	texture->registrationSequence = rsh.registrationSequence;
	texture->fbo = 0;
	texture->framenum = 0;
	texture->layers = numLayers;
	texture->missing = false;
	texture->tags = IMAGE_TAG_WORLD;

	const unsigned binIndex = wsw::HashedStringView( nameBuffer.asView() ).getHash() % kNumHashBins;
	wsw::link( texture, &m_usedTexturesHead, Texture::ListLinks );
	wsw::link( texture, &m_hashBins[binIndex], Texture::BinLinks );
	return texture;
}

void TextureCache::replaceLightmapLayer( Texture *texture, unsigned layer, const uint8_t *data ) {
	assert( texture );
	assert( layer < texture->layers );

	// TODO: Store these properties in a LightmapTextureArray instance
	const auto [internalFormat, format] = getLightmapFormatsForSamples( texture->samples );

	bindToModify( texture );

	qglTexSubImage3D( texture->target, 0, 0, 0, layer, texture->width, texture->height, 1, format, GL_UNSIGNED_BYTE, data );

	unbindModified( texture );
}

void TextureCache::replaceFontMaskSamples( Texture *texture, unsigned w, unsigned h, const uint8_t *data ) {
	const GLenum target = texture->target;
	assert( target == GL_TEXTURE_2D );

	bindToModify( target, texture->texnum );

	// TODO: We must know statically
	const auto [swizzleMask, internalFormat, format, type] = getRegularTexImageFormats( IT_ALPHAMASK | IT_NOMIPMAP | IT_NOFILTERING, 1 );
	if( swizzleMask != kSwizzleMaskIdentity ) {
		qglTexParameteriv( target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
	}

	qglTexSubImage2D( target, 0, 0, 0, w, h, format, type, data );

	unbindModified( target, 0 );

	assert( qglGetError() == GL_NO_ERROR );
}

auto TextureCache::findFreePortalTexture( unsigned width, unsigned height, int flags, unsigned frameNum )
	-> std::optional<std::tuple<Texture *, unsigned, bool>> {
	std::optional<unsigned> bestSlot;
	std::optional<unsigned> freeSlot;
	for( unsigned i = 0; i < std::size( m_portalTextures ); ++i ) {
		Texture *texture = m_portalTextures[i];
		if( !texture ) {
			if( freeSlot == std::nullopt ) {
				freeSlot = i;
			}
			continue;
		}
		// TODO: Don't mark images, track used images on their own.
		// Even if the framenum approach is used it should not belong to the Texture class
		if( texture->framenum == frameNum ) {
			// the texture is used in the current scene
			continue;
		}
		if( texture->width == width && texture->height == height && texture->flags == flags ) {
			return std::make_tuple( texture, i, true );
		}
		if( bestSlot == std::nullopt ) {
			bestSlot = i;
		}
	}
	if( bestSlot ) {
		return std::make_tuple( m_portalTextures[*bestSlot], *bestSlot, false );
	}
	if( freeSlot ) {
		return std::make_tuple( nullptr, *freeSlot, false );
	}
	return std::nullopt;
}

auto TextureCache::getPortalTexture( unsigned viewportWidth, unsigned viewportHeight,
									 int flags, unsigned frameNum ) -> Texture * {
	if( Texture *texture = getPortalTexture_( viewportWidth, viewportHeight, flags, frameNum ) ) {
		texture->framenum = frameNum;
		return texture;
	}
	return nullptr;
}

auto TextureCache::getPortalTexture_( unsigned viewportWidth, unsigned viewportHeight, int flags, unsigned frameNum ) -> Texture * {
	const auto sizeLimit = (unsigned)std::max( 0, r_portalmaps_maxtexsize->integer );
	const auto [realWidth, realHeight] = R_GetRenderBufferSize( viewportWidth, viewportHeight, sizeLimit );

	const int realFlags = IT_SPECIAL | IT_FRAMEBUFFER | IT_DEPTHRB | flags;
	auto maybeResult = findFreePortalTexture( realWidth, realHeight, realFlags, frameNum );
	if( !maybeResult ) {
		return nullptr;
	}

	auto [existing, slotNum, perfectMatch] = *maybeResult;
	if( perfectMatch ) {
		return existing;
	}

	if( existing ) {
		bindToModify( existing );
		// Just resize/change format
		unbindModified( existing );
		// TODO: Update attached FBOS!!!!!!!!!!!!!!!!!!!!
		// update FBO, if attached
		/*
		if( t->fbo ) {
			RFB_UnregisterObject( t->fbo );
			t->fbo = 0;
		}
		if( t->flags & IT_FRAMEBUFFER ) {
			t->fbo = RFB_RegisterObject( t->upload_width, t->upload_height, ( tags & IMAGE_TAG_BUILTIN ) != 0,
										 ( flags & IT_DEPTHRB ) != 0, ( flags & IT_STENCIL ) != 0, false, 0, false );
			RFB_AttachTextureToObject( t->fbo, ( t->flags & IT_DEPTH ) != 0, 0, t );
		}*/
		return existing;
	}

	abort();

	// TODO: Create FBOS if needed!!!!!!!!!!!!!!!!!!!!!!

	// Create a new texture

}

auto TextureCache::wrapUITextureHandle( GLuint externalHandle ) -> Texture * {
	assert( m_externalHandleWrapper );
	Texture *texture = m_externalHandleWrapper;
	texture->texnum = externalHandle;
	texture->width = texture->upload_width = rf.width2D;
	texture->height = texture->upload_height = rf.height2D;
	texture->missing = false;
	texture->samples = 1;
	return texture;
}

void TextureCache::freeUnusedWorldTextures() {}
void TextureCache::freeAllUnusedTextures() {}

struct BuiltinTextureFactory {
	const wsw::StringView name;
	const BuiltinTexNum builtinTexNum;
	uint8_t *data { nullptr };
	unsigned width { 0 }, height { 0 };
	unsigned flags { 0 };
	unsigned samples { 0 };
	BuiltinTextureFactory( const wsw::StringView &name_, BuiltinTexNum builtinTexNum_ )
		: name( name_ ), builtinTexNum( builtinTexNum_ ) {}
};

void TextureCache::initBuiltinTexture( BuiltinTextureFactory &&factory ) {
	assert( factory.data );
	assert( factory.name.startsWith( "***"_asView ) && factory.name.endsWith( "***"_asView ) );
	assert( !factory.name.drop( 3 ).dropRight( 3 ).contains( '*' ) );
	assert( factory.name.length() > 7 );
	assert( factory.width && factory.height );
	assert( factory.samples > 1 && factory.samples <= 4 );
	assert( factory.flags );

	assert( qglGetError() == GL_NO_ERROR );

	// TODO: There is a cubemap in the complete textures set
	const GLenum target = GL_TEXTURE_2D;
	const GLuint handle = generateHandle( factory.name );

	bindToModify( target, handle );

	const auto [swizzleMask, internalFormat, format, type] = getRegularTexImageFormats( factory.flags, factory.samples );
	if( swizzleMask != kSwizzleMaskIdentity ) {
		qglTexParameteriv( target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
	}

	qglTexImage2D( target, 0, internalFormat, factory.width, factory.height, 0, format, type, factory.data );

	setupWrapMode( target, factory.flags );

	// !!!!!!!!
	qglGenerateMipmap( target );

	unbindModified( target, handle );

	assert( makeCleanName( factory.name, wsw::StringView() ) == factory.name );
	const unsigned binIndex = wsw::HashedStringView( factory.name ).getHash() % kNumHashBins;
	assert( findTextureInBin( binIndex, factory.name, 1, factory.flags ) == nullptr );

	assert( m_freeTexturesHead );

	Texture *texture = wsw::unlink( m_freeTexturesHead, &m_freeTexturesHead, Texture::ListLinks );
	texture->target = target;
	texture->texnum = handle;
	texture->width = texture->upload_width = factory.width;
	texture->height = texture->upload_height = factory.height;
	texture->registrationSequence = rsh.registrationSequence;
	texture->missing = false;
	texture->isAPlaceholder = false;
	texture->fbo = 0;
	texture->minmipsize = 1;
	texture->samples = factory.samples;
	texture->flags = factory.flags;
	texture->tags = IMAGE_TAG_GENERIC;
	texture->name = internTextureName( texture, factory.name );

	wsw::link( texture, &m_usedTexturesHead, Texture::ListLinks );
	wsw::link( texture, &m_hashBins[binIndex], Texture::BinLinks );

	assert( !m_builtinTextures[(unsigned)factory.builtinTexNum] );
	m_builtinTextures[(unsigned)factory.builtinTexNum] = texture;
}

struct SolidColorTextureFactory : public BuiltinTextureFactory {
	SolidColorTextureFactory( const wsw::StringView &name, BuiltinTexNum builtinTexNum, const vec4_t color )
		: BuiltinTextureFactory( name, builtinTexNum ) {
		samples = 3;
		width = height = 1;
		flags = IT_NOPICMIP | IT_NOCOMPRESS;
		data = ::loadingBuffer.reserveAndGet( 3 );
		data[0] = (uint8_t)( 255 * color[0] );
		data[1] = (uint8_t)( 255 * color[1] );
		data[2] = (uint8_t)( 255 * color[2] );
	}
};

struct NoTextureTextureFactory : public BuiltinTextureFactory {
	NoTextureTextureFactory() : BuiltinTextureFactory( "***notexture***"_asView, BuiltinTexNum::No ) {
		constexpr unsigned side = 8;

		width = height = side;
		flags = IT_SRGB;
		samples = 3;
		data = loadingBuffer.reserveAndGet( side * side * samples );

		const uint8_t wswPurple[] { 53, 34, 69 };
		const uint8_t wswOrange[] { 95, 39, 9 };
		uint8_t markPurple[3], markOrange[3];
		const uint8_t red[3] = { 128, 0, 0 };
		for( int i = 0; i < 3; ++i ) {
			markPurple[i] = (uint8_t)( ( 3 * wswPurple[i] + red[i] ) / 4 );
			markOrange[i] = (uint8_t)( ( 3 * wswOrange[i] + red[i] ) / 4 );
		}

		ptrdiff_t offset = 0;
		uint8_t *const __restrict p = data;
		for( unsigned pixNum = 0; pixNum < side * side; ++pixNum ) {
			const unsigned x = pixNum % side;
			const unsigned y = pixNum / side;
			const unsigned xySum = x + y;
			const uint8_t *__restrict color;
			if( !xySum ) {
				color = markPurple;
			} else if( xySum == 1 ) {
				color = markOrange;
			} else if( xySum % 2 ) {
				color = wswOrange;
			} else {
				color = wswPurple;
			}
			p[offset + 0] = color[0];
			p[offset + 1] = color[1];
			p[offset + 2] = color[2];
			// Increment after addressing to avoid AGI
			offset += 3;
		}
	}
};

struct CoronaTextureFactory : public BuiltinTextureFactory {
	CoronaTextureFactory() : BuiltinTextureFactory( "***corona***"_asView, BuiltinTexNum::Corona ) {
		constexpr unsigned side = 32;

		width = height = side;
		flags = IT_SPECIAL | IT_SRGB;
		samples = 4;
		data = loadingBuffer.reserveAndGet( side * side * samples );

		ptrdiff_t offset = 0;
		uint8_t *const __restrict p = data;
		constexpr int halfSide = (int)side / 2;
		constexpr float invHalfSide = 1.0f / (float)halfSide;
		for( int pixNum = 0; pixNum < (int)( side * side ); ++pixNum ) {
			const int x = pixNum % (int)side;
			const int y = pixNum / (int)side;

			const auto unitDX = (float)( x - halfSide ) * invHalfSide;
			const auto unitDY = (float)( y - halfSide ) * invHalfSide;

			float frac = 1.0f - Q_Sqrt( unitDX * unitDX + unitDY * unitDY );
			frac = std::clamp( frac, 0.0f, 1.0f );
			frac = frac * frac;

			const auto value = (uint8_t)( 128.0f * frac );
			p[offset + 0] = value;
			p[offset + 1] = value;
			p[offset + 2] = value;
			p[offset + 3] = value;
			offset += 4;
		}
	}
};

struct ParticleTextureFactory : public BuiltinTextureFactory {
	ParticleTextureFactory() : BuiltinTextureFactory( "***particle***"_asView, BuiltinTexNum::Particle ) {
		constexpr unsigned side = 16;

		width = height = side;
		flags = IT_NOPICMIP | IT_NOMIPMAP | IT_SRGB;
		samples = 4;
		data = loadingBuffer.reserveAndGet( side * side * samples );

		ptrdiff_t offset = 0;
		uint8_t *const __restrict p = data;
		constexpr int halfSide = (int)side / 2;
		for( int pixNum = 0; pixNum < (int)side * side; ++pixNum ) {
			const int x = pixNum % (int)side;
			const int y = pixNum / (int)side;
			const auto dx = x - halfSide;
			const auto dy = y - halfSide;
			const uint8_t value = ( dx * dx + dy * dy < halfSide * halfSide ) ? 255 : 0;
			p[offset + 0] = value;
			p[offset + 1] = value;
			p[offset + 2] = value;
			p[offset + 3] = value;
			offset += 4;
		}
	}
};

struct BlankNormalMapFactory : public BuiltinTextureFactory {
	BlankNormalMapFactory() : BuiltinTextureFactory( "***blanknormalmap***"_asView, BuiltinTexNum::BlankBump ) {
		width = height = 1;
		samples = 4;
		flags = IT_NOPICMIP | IT_NOCOMPRESS;
		data = ::loadingBuffer.reserveAndGet( 4 );
		data[0] = data[1] = 128;
		data[2] = 255;
		data[3] = 128; // Displacement height
	}
};

void TextureCache::initBuiltinTextures() {
	initBuiltinTexture( SolidColorTextureFactory( "***black***"_asView, BuiltinTexNum::Black, colorBlack ) );
	initBuiltinTexture( SolidColorTextureFactory( "***grey***"_asView, BuiltinTexNum::Grey, colorMdGrey ) );
	initBuiltinTexture( SolidColorTextureFactory( "***white***"_asView, BuiltinTexNum::White, colorWhite ) );
	initBuiltinTexture( NoTextureTextureFactory() );
	initBuiltinTexture( CoronaTextureFactory() );
	initBuiltinTexture( ParticleTextureFactory() );
	initBuiltinTexture( BlankNormalMapFactory() );

	assert( m_freeTexturesHead );
	const wsw::HashedStringView hashedName( "***external***"_asHView );
	Texture *const texture = wsw::unlink( m_freeTexturesHead, &m_freeTexturesHead, Texture::ListLinks );
	texture->name = wsw::StringView( hashedName.data(), hashedName.size(), wsw::StringView::ZeroTerminated );
	texture->width = texture->upload_width = 0;
	texture->height = texture->upload_height = 0;
	texture->tags = IMAGE_TAG_BUILTIN;
	texture->flags = IT_SPECIAL;
	texture->isAPlaceholder = false;
	texture->samples = 0;
	texture->minmipsize = 0;
	texture->fbo = 0;
	texture->missing = false;
	texture->registrationSequence = std::nullopt;
	texture->texnum = 0;
	texture->target = GL_TEXTURE_2D;

	const unsigned binIndex = hashedName.getHash() % kNumHashBins;
	wsw::link( texture, &m_usedTexturesHead, Texture::ListLinks );
	wsw::link( texture, &m_hashBins[binIndex], Texture::BinLinks );
	m_externalHandleWrapper = texture;
}

static void wsw_stb_write_func( void *context, void *data, int size ) {
	auto handle = *( (int *)( context ) );
	FS_Write( data, size, handle );
}

/*
* R_ScreenShot
*/
void R_ScreenShot( const char *filename, int x, int y, int width, int height, int quality, bool silent ) {
	if( !R_IsRenderingToScreen() ) {
		return;
	}

	const char *extension = COM_FileExtension( filename );
	if( !extension ) {
		Com_Printf( "R_ScreenShot: Invalid filename\n" );
		return;
	}

	const bool isJpeg = !Q_stricmp( extension, ".jpg" );
	const size_t bufferSize = width * ( height + 1 ) * ( isJpeg ? 3 : 4 );
	// We've purged the demoavi code so nobody takes screenshots every frame.
	// Doing a short-living allocation in this case is fine.
	std::unique_ptr<uint8_t[]> buffer( new uint8_t[bufferSize] );
	if( isJpeg ) {
		qglReadPixels( 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, buffer.get() );
	} else {
		qglReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.get() );
	}

	// TODO: Flip

	// TODO: Add wsw::fs interfaces for doing that
	int handle = 0;
	if( FS_FOpenAbsoluteFile( filename, &handle, FS_WRITE ) < 0 ) {
		Com_Printf( "R_ScreenShot: Failed to open %s\n", filename );
		return;
	}

	auto *context = (void *)&handle;
	int result;
	if( isJpeg ) {
		result = stbi_write_jpg_to_func( wsw_stb_write_func, context, width, height, 3, buffer.get(), quality );
	} else {
		result = stbi_write_tga_to_func( wsw_stb_write_func, context, width, height, 4, buffer.get() );
	}

	FS_FCloseFile( handle );

	if( result ) {
		Com_Printf( "Wrote %s\n", filename );
	}

}

auto R_GetRenderBufferSize( unsigned inWidth, unsigned inHeight, std::optional<unsigned> inLimit )
-> std::pair<unsigned, unsigned> {
	// limit the texture size to either screen resolution in case we can't use FBO
	// or hardware limits and ensure it's a POW2-texture if we don't support such textures
	unsigned limit = glConfig.maxRenderbufferSize;
	if( inLimit ) {
		limit = std::min( limit, *inLimit );
	}
	limit = std::max( 1u, limit );
	return std::make_pair( std::min( inWidth, limit ), std::min( inHeight, limit ) );
}

auto R_InitViewportTexture( Texture *existing, const wsw::StringView &name, unsigned width, unsigned height, int flags, int tags, int samples ) -> Texture * {
	if( existing ) {
		if( existing->width == width && existing->height == height ) {
			return existing;
		}
		// TODO: Bind, resize, and unbind
		// TODO: Update width/upload_width, height/upload_height fields
	} else {
		// TODO: Check whether there is a free slot
		// TODO: Generate
		// TODO: Bind to modify
		// TODO: Resize
		// TODO: Unbind
		// TODO: Create a new entry and fill fields
	}

	// TODO: Texture manager should not call render target manager directly
	// The render target manager should be a caller of this method!

	Texture *t = existing;
	// update FBO, if attached
	if( t->fbo ) {
		RFB_UnregisterObject( t->fbo );
		t->fbo = 0;
	}

	t->fbo = RFB_RegisterObject( t->upload_width, t->upload_height, ( tags & IMAGE_TAG_BUILTIN ) != 0,
								 ( flags & IT_DEPTHRB ) != 0, ( flags & IT_STENCIL ) != 0, false, 0, false );
	RFB_AttachTextureToObject( t->fbo, ( t->flags & IT_DEPTH ) != 0, 0, t );
}

enum FloatUsage {
	NoFloat,
	UseFloat,
};


// TODO: This call should belong to RenderTargetManager

static auto R_InitScreenFboAttachments( const wsw::StringView &name, std::pair<Texture *, Texture *> existing, FloatUsage floatUsage )
-> std::pair<Texture *, Texture *> {
	Texture *existingColor = existing.first;
	Texture *existingDepth = existing.second;

	const unsigned width = std::max( glConfig.width, 1 );
	const unsigned height = std::max( glConfig.height, 1 );

	const int flags = IT_SPECIAL;

	int colorFlags = flags | IT_FRAMEBUFFER;
	int depthFlags = flags | ( IT_DEPTH | IT_NOFILTERING );
	if( !existingDepth ) {
		colorFlags |= IT_DEPTHRB;
	}

	if( glConfig.stencilBits ) {
		if( existingDepth ) {
			depthFlags |= IT_STENCIL;
		} else {
			colorFlags |= IT_STENCIL;
		}
	}

	if( floatUsage ) {
		colorFlags |= IT_FLOAT;
	}

	Texture *resultColor = R_InitViewportTexture( existingColor, name, width, height, colorFlags, IMAGE_TAG_BUILTIN, 3 );
	if( !resultColor ) {
		return std::make_pair( nullptr, nullptr );
	}

	wsw::StaticString<MAX_QPATH> depthName;
	depthName << name << "_depth"_asView;
	Texture *resultDepth = R_InitViewportTexture( existingDepth, depthName.asView(), width, height, depthFlags, IMAGE_TAG_BUILTIN, 1 );
	if( colorFlags & IT_FRAMEBUFFER ) {
		RFB_AttachTextureToObject( resultColor->fbo, true, 0, resultDepth );
	}

	return std::make_pair( resultColor, resultDepth );
}

static void R_InitBuiltinScreenImageSet( refScreenTexSet_t *st, FloatUsage useFloat ) {
	char name[128];
	const char *postfix = useFloat ? "16f" : "";

	Q_snprintfz( name, sizeof( name ), "r_screenTex%s", postfix );
	std::tie( st->screenTex, st->screenDepthTex ) =
		R_InitScreenFboAttachments( wsw::StringView( name ), std::make_pair( st->screenTex, st->screenDepthTex ), useFloat );

	// TODO: Do we need copies?
	// TODO: We can share some auxiliary computations

	// stencil is required in the copy for depth/stencil formats to match when blitting.
	Q_snprintfz( name, sizeof( name ), "r_screenTexCopy%s", postfix );
	std::tie( st->screenTexCopy, st->screenDepthTexCopy ) =
		R_InitScreenFboAttachments( wsw::StringView( name ), std::make_pair( st->screenTexCopy, st->screenDepthTexCopy ), useFloat );
}

static void R_InitBuiltinScreenImages() {
	R_InitBuiltinScreenImageSet( &rsh.st, NoFloat );
	R_InitBuiltinScreenImageSet( &rsh.stf, UseFloat );
}

void TextureCache::initScreenTextures() {

}

void TextureCache::releaseScreenTextures() {

}