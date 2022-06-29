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

#include <tuple>
#include <memory>
#include <utility>

// TODO: This is a cheap hack to make the stuff working
namespace wsw::ui {
[[nodiscard]]
auto rasterizeSvg( const void *rawSvgData, size_t rawSvgDataSize, void *dest, size_t destCapacity,
				   const ImageOptions &options ) -> std::optional<std::pair<unsigned, unsigned>>;
}

#define STB_IMAGE_IMPLEMENTATION
#include "../../third-party/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third-party/stb/stb_image_write.h"

TextureFactory::TextureFactory() {
	// Cubemap names are put after material ones in the same chunk
	m_nameDataStorage.reset( new char[( kMaxMaterialTextures + kMaxMaterialCubemaps ) * kNameDataStride] );

	qglPixelStorei( GL_PACK_ALIGNMENT, 1 );
	qglPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
}

[[nodiscard]]
bool TextureFactory::tryUpdatingFilterOrAniso( TextureFilter filter, int givenAniso, int *anisoToApply,
											   bool *doApplyFilter, bool *doApplyAniso ) {
	*doApplyFilter = m_textureFilter != filter;

	*anisoToApply = wsw::clamp( givenAniso, 1, glConfig.maxTextureFilterAnisotropic );
	// TODO: The extension spec allows specifying aniso with non-trilinear filtering modes.
	// Should we really allow doing that?
	const bool shouldApplyAniso = m_anisoLevel != *anisoToApply;
	const bool canApplyAniso = glConfig.ext.texture_filter_anisotropic;
	*doApplyAniso = shouldApplyAniso && canApplyAniso;

	m_textureFilter = filter;
	m_anisoLevel = *anisoToApply;
	return *doApplyFilter || *doApplyAniso;
}

auto TextureFactory::generateHandle( const wsw::StringView &label ) -> GLuint {
	assert( qglGetError() == GL_NO_ERROR );

	GLuint handle = 0;
	qglGenTextures( 1, &handle );
	// TODO
	//if( qglObjectLabel ) {
	//	qglObjectLabel( GL_TEXTURE, handle, wsw::min( label.length(), (size_t)glConfig.maxObjectLabelLen ), label.data() );
	//}

	assert( qglGetError() == GL_NO_ERROR );
	return handle;
}

void TextureFactory::setupFilterMode( GLuint target, unsigned flags ) {
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
		auto [minify, magnify] = kTextureFilterGLValues[m_textureFilter];
		if( flags & IT_CUBEMAP ) {
			// Turn mipmap filtering off for cubemaps.
			// There's no visual difference for real kinds of surfaces.
			// This should aid texturing performance especially on a low-end hardware.
			if( minify == GL_LINEAR_MIPMAP_LINEAR ) {
				minify = GL_LINEAR_MIPMAP_NEAREST;
			} else if( minify == GL_NEAREST_MIPMAP_LINEAR ) {
				minify = GL_NEAREST_MIPMAP_NEAREST;
			}
		}
		qglTexParameteri( target, GL_TEXTURE_MIN_FILTER, minify );
		qglTexParameteri( target, GL_TEXTURE_MAG_FILTER, magnify );
		if( glConfig.ext.texture_filter_anisotropic ) {
			qglTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, m_anisoLevel );
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

void TextureFactory::setupWrapMode( GLuint target, unsigned flags ) {
	const int wrap = ( flags & IT_CLAMP ) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
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

struct alignas( 1 ) Rgba {
	uint8_t r, g, b, a;
};

static_assert( alignof( Rgba ) == 1 && sizeof( Rgba ) == 4 );

static void applyOutlineEffectRgba( Rgba *__restrict dest, const Rgba *__restrict src,
									unsigned width, unsigned height ) {
	// Sanity checks
	assert( width > 0 && height > 0 && width < ( 1u << 16 ) && height < ( 1u << 16 ) );

	for( unsigned line = 0; line < height; ++line ) {
		for( unsigned column = 0; column < width; ++column ) {
			float accumAlpha = 0.0f;
			if( line > 0 ) {
				accumAlpha += (float)src[width * ( line - 1 ) + column].a;
			}
			if( line + 1 < height ) {
				accumAlpha += (float)src[width * ( line + 1 ) + column].a;
			}
			if( column > 0 ) {
				accumAlpha += (float)src[width * line + ( column - 1 )].a;
			}
			if( column + 1 < width ) {
				accumAlpha += (float)src[width * line + ( column + 1)].a;
			}
			const Rgba blendDest { 0, 0, 0, (uint8_t)wsw::min( 255.0f, accumAlpha ) };
			const Rgba &blendSrc = src[width * line + column];
			const auto srcFrac   = (float)blendSrc.a * ( 1.0f / 255.0f );
			const auto destFrac  = 1.0f - srcFrac;
			dest[line * width + column] = Rgba {
				(uint8_t)( srcFrac * (float)blendSrc.r + destFrac * (float)blendDest.r ),
				(uint8_t)( srcFrac * (float)blendSrc.g + destFrac * (float)blendDest.g ),
				(uint8_t)( srcFrac * (float)blendSrc.b + destFrac * (float)blendDest.b ),
				(uint8_t)( srcFrac * (float)blendSrc.a + destFrac * (float)blendDest.a )
			};
		}
	}
}

static ImageBuffer readFileBuffer;
static ImageBuffer cubemapBuffer[6];
static ImageBuffer loadingBuffer;
static ImageBuffer conversionBuffer;

auto TextureFactory::loadTextureDataFromFile( const wsw::StringView &name,
											  ImageBuffer *readBuffer,
											  ImageBuffer *dataBuffer,
											  ImageBuffer *conversionBuffer,
											  const ImageOptions &options )
											-> std::optional<std::pair<uint8_t *, BitmapProps>> {
	assert( name.isZeroTerminated() );
	assert( NUM_IMAGE_EXTENSIONS == 4 );
	// TODO: Adopt the sane FS interface over the codebase
	const wsw::StringView extensions[4] = {
		wsw::StringView( IMAGE_EXTENSIONS[0] ),
		wsw::StringView( IMAGE_EXTENSIONS[1] ),
		wsw::StringView( IMAGE_EXTENSIONS[2] ),
		wsw::StringView( IMAGE_EXTENSIONS[3] )
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

	const bool isSvg = maybePartsPair->second.equalsIgnoreCase( ".svg"_asView );

	constexpr size_t kMaxSaneBitmapDataSize = 2048 * 2048 * 4;
	// In case of regular images, consider that the data size cannot be greater than 2048x2048 RGBA + some header bytes
	const size_t maxSaneImageDataSize = isSvg ? 1024 * 1024 : ( kMaxSaneBitmapDataSize + 8192 );
	const size_t fileSize = maybeHandle->getInitialFileSize();
	if( fileSize > maxSaneImageDataSize ) {
		return std::nullopt;
	}

	uint8_t *const fileBufferBytes = readBuffer->reserveAndGet( fileSize );
	if( !maybeHandle->readExact( fileBufferBytes, fileSize ) ) {
		return std::nullopt;
	}

	int width = 0, height = 0, samples = 0;
	size_t imageDataSize = 0;
	uint8_t *bytes = nullptr;
	if( isSvg ) {
		// It's only gets used in this case.
		// TODO: Redesign supplying of desired texture parameters
		// so it can be meaningful in case of regular images.
		if( !options.desiredSize ) {
			return std::nullopt;
		}
		samples = 4;
		const auto [desiredWidth, desiredHeight] = *options.desiredSize;
		const auto bufferDataSize = (size_t)desiredWidth * (size_t)desiredHeight * (size_t)samples;
		bytes = conversionBuffer->reserveAndGet( bufferDataSize );
		const auto maybeSize = wsw::ui::rasterizeSvg( fileBufferBytes, fileSize, bytes, bufferDataSize, options );
		if( !maybeSize ) {
			return std::nullopt;
		}
		std::tie( width, height ) = *maybeSize;
		imageDataSize = (size_t)width * (size_t)height * (size_t)samples;
		assert( imageDataSize <= bufferDataSize );
	} else {
		bytes = stbi_load_from_memory( (const stbi_uc *)fileBufferBytes, (int)fileSize, &width, &height, &samples, 0 );
		if( !bytes ) {
			return std::nullopt;
		}
		assert( width > 0 && height > 0 && samples > 0 );
		imageDataSize = (size_t)width * (size_t)height * (size_t)samples;
	}

	assert( imageDataSize );
	// Sanity check (disallow huge bogus allocations)
	if( imageDataSize > kMaxSaneBitmapDataSize ) {
		return std::nullopt;
	}

	uint8_t *const imageData = dataBuffer->reserveAndGet( imageDataSize );
	if( options.useOutlineEffect ) {
		if( samples == 4 && width && height ) {
			applyOutlineEffectRgba( (Rgba *)imageData, (const Rgba *)bytes, (unsigned)width, (unsigned)height );
		} else {
			return std::nullopt;
		}
	} else {
		std::memcpy( imageData, bytes, imageDataSize );
	}

	if( !isSvg ) {
		// This is not that easy as we use stb in the UI code as well.
		// TODO: Provide allocators for stb that use the loading buffer?
		// TODO: Unify image loading code with the UI?
		stbi_image_free( bytes );
	}

	return std::make_pair( imageData, BitmapProps { (uint16_t)width, (uint16_t)height, (uint16_t)samples } );
}

auto TextureFactory::internTextureName( unsigned storageIndex,
										const wsw::HashedStringView &name ) -> wsw::HashedStringView {
	assert( name.length() <= kMaxNameLen );
	char *const data = m_nameDataStorage.get() + kNameDataStride * storageIndex;
	std::memcpy( data, name.data(), name.length() );
	data[name.length()] = '\0';
	return wsw::HashedStringView( data, name.length(), name.getHash(), wsw::StringView::ZeroTerminated );
}

auto TextureFactory::createRaw2DTexture() -> Raw2DTexture * {
	if( m_raw2DTexturesAllocator.isFull() ) {
		return nullptr;
	}

	const GLuint handle = generateHandle( wsw::StringView() );
	const GLenum target = GL_TEXTURE_2D;
	// TODO: Skip?
	bindToModify( target, handle );
	unbindModified( target, handle );

	return new( m_raw2DTexturesAllocator.allocOrNull() )Raw2DTexture( handle );
}

void TextureFactory::releaseRaw2DTexture( Raw2DTexture *texture ) {
	if( texture ) {
		qglDeleteTextures( 1, &texture->texnum );
		m_raw2DTexturesAllocator.free( texture );
	}
}

bool TextureFactory::updateRaw2DTexture( Raw2DTexture *texture, const wsw::StringView &name,
										 const ImageOptions &options ) {
	const auto maybeCleanName = makeCleanName( name, wsw::StringView() );
	if( !maybeCleanName ) {
		return false;
	}

	auto maybeFileData = loadTextureDataFromFile( *maybeCleanName, &::readFileBuffer, &::loadingBuffer,
												  &::conversionBuffer, options );
	if( !maybeFileData ) {
		return false;
	}

	auto [fileBytes, bitmapProps] = *maybeFileData;

	qglPixelStorei( GL_UNPACK_ALIGNMENT, 1 );

	const GLenum target = GL_TEXTURE_2D;

	bindToModify( target, texture->texnum );

	auto [swizzleMask, internalFormat, format, type] =
		getRegularTexImageFormats( texture->flags, bitmapProps.samples );
	setupFilterMode( target, texture->flags );
	setupWrapMode( target, texture->flags );

	if( swizzleMask != kSwizzleMaskIdentity ) {
		qglTexParameteriv( target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
	}

	qglTexImage2D( target, 0, internalFormat, bitmapProps.width, bitmapProps.height, 0, format, type, fileBytes );

	if( true || !( texture->flags & IT_NOMIPMAP ) ) {
		qglGenerateMipmap( target );
	}

	unbindModified( target, texture->texnum );

	texture->width = bitmapProps.width;
	texture->height = bitmapProps.height;
	texture->samples = bitmapProps.samples;

	return true;
}

auto TextureFactory::loadMaterialTexture( const wsw::HashedStringView &name, unsigned flags ) -> Material2DTexture * {
	if( m_materialTexturesAllocator.isFull() ) {
		return nullptr;
	}

	auto maybeFileData = loadTextureDataFromFile( name, &::readFileBuffer, &::loadingBuffer,
												  &::conversionBuffer, ImageOptions {} );
	if( !maybeFileData ) {
		return nullptr;
	}

	auto [fileBytes, bitmapProps] = *maybeFileData;

	qglPixelStorei( GL_UNPACK_ALIGNMENT, 1 );

	const GLuint handle = generateHandle( name );
	const GLenum target = GL_TEXTURE_2D;

	bindToModify( target, handle );

	const auto [swizzleMask, internalFormat, format, type] = getRegularTexImageFormats( flags, bitmapProps.samples );
	setupFilterMode( target, flags );
	setupWrapMode( target, flags );

	if( swizzleMask != kSwizzleMaskIdentity ) {
		qglTexParameteriv( target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
	}

	qglTexImage2D( target, 0, internalFormat, bitmapProps.width, bitmapProps.height, 0, format, type, fileBytes );

	if( true || !( flags & IT_NOMIPMAP ) ) {
		qglGenerateMipmap( target );
	}

	unbindModified( target, handle );

	unsigned index = 0;
	void *mem = m_materialTexturesAllocator.allocOrNull( &index );
	const wsw::HashedStringView ownedName( internTextureName( index, name ) );
	return new( mem )Material2DTexture( ownedName, handle, bitmapProps, flags );
}

auto TextureFactory::loadMaterialCubemap( const wsw::HashedStringView &name, unsigned flags ) -> MaterialCubemap * {
	if( m_materialCubemapsAllocator.isFull() ) {
		return nullptr;
	}

	const char signLetters[2] { 'p', 'n' };
	const char axisLetters[3] { 'x', 'y', 'z' };

	wsw::StaticString<MAX_QPATH> nameBuffer;
	nameBuffer << name << '_';
	const auto prefixLen = nameBuffer.size();

	const void *dataOfSides[6];
	BitmapProps firstProps { 0, 0, 0 };
	for( unsigned i = 0; i < 6; ++i ) {
		nameBuffer.erase( prefixLen );
		nameBuffer.append( signLetters[i % 2] );
		nameBuffer.append( axisLetters[i / 2] );
		const auto maybeData = loadTextureDataFromFile( nameBuffer.asView(), &::loadingBuffer,
														&::cubemapBuffer[i], &::conversionBuffer, ImageOptions {} );
		if( !maybeData ) {
			return nullptr;
		}
		const auto &[bytes, props] = *maybeData;
		if( i ) {
			if( props != firstProps ) {
				return nullptr;
			}
		} else {
			firstProps = props;
		}
		dataOfSides[i] = bytes;
	}

	const GLenum target = GL_TEXTURE_CUBE_MAP;
	const GLuint handle = generateHandle( name );

	bindToModify( target, handle );

	const auto [swizzleMask, internalFormat, format, type] = getRegularTexImageFormats( (int)flags, firstProps.samples );

	for( unsigned i = 0; i < 6; ++i ) {
		const GLenum sideTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + i;
		if( swizzleMask != kSwizzleMaskIdentity ) {
			qglTexParameteriv( sideTarget, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
		}
		qglTexImage2D( sideTarget, 0, internalFormat, firstProps.width, firstProps.height, 0, format, type, dataOfSides[i] );
	}

	setupFilterMode( target, flags );
	setupWrapMode( target, flags );

	qglGenerateMipmap( target );

	unbindModified( target, handle );

	unsigned index = 0;
	void *const mem = m_materialCubemapsAllocator.allocOrNull( &index );
	const wsw::HashedStringView ownedName( internTextureName( kMaxRaw2DTextures + index, name ) );
	return new( mem )MaterialCubemap( ownedName, handle, firstProps, flags );
}

auto TextureFactory::createFontMask( unsigned w, unsigned h, const uint8_t *data ) -> Texture * {
	const auto flags = IT_NOFILTERING | IT_NOMIPMAP | IT_ALPHAMASK;
	const GLenum target = GL_TEXTURE_2D;
	const GLuint handle = generateHandle( wsw::StringView() );

	bindToModify( target, handle );

	const auto [swizzleMask, internalFormat, format, type] = getRegularTexImageFormats( flags, 1 );
	if( swizzleMask != kSwizzleMaskIdentity ) {
		qglTexParameteriv( target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
	}

	qglTexImage2D( target, 0, internalFormat, w, h, 0, format, type, data );

	setupFilterMode( target, flags );
	setupWrapMode( target, flags );

	unbindModified( target, handle );

	return new( m_fontMasksAllocator.allocOrNull() )FontMask( handle, w, h, flags );
}

static auto getLightmapFormatsForSamples( unsigned samples ) -> std::pair<GLint, GLenum> {
	assert( samples == 1 || samples == 3 );
	if( samples == 3 ) {
		return std::make_pair( GL_RGB8, GL_RGB );
	}
	return std::make_pair( GL_R8, GL_RED );
}

auto TextureFactory::createLightmap( unsigned w, unsigned h, unsigned samples, const uint8_t *data ) -> Texture * {
	if( m_lightmapsAllocator.isFull() ) {
		return nullptr;
	}

	const auto [internalFormat, format] = getLightmapFormatsForSamples( samples );
	const GLenum target = GL_TEXTURE_2D;
	const GLuint handle = generateHandle( wsw::StringView() );
	bindToModify( target, handle );

	qglTexImage2D( target, 0, internalFormat, w, h, 0, format, GL_UNSIGNED_BYTE, data );

	const unsigned flags = IT_CLAMP | IT_NOMIPMAP | IT_CUSTOMFILTERING;
	setupWrapMode( target, flags );

	qglTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	if( glConfig.ext.texture_filter_anisotropic ) {
		qglTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );
	}

	unbindModified( target, handle );

	// TODO: Is lifetime management explicit?
	return new( m_lightmapsAllocator.allocOrNull() )Lightmap( handle, w, h, samples, flags );
}

auto TextureFactory::createLightmapArray( unsigned w, unsigned h, unsigned numLayers, unsigned samples ) -> Texture * {
	if( m_lightmapsAllocator.isFull() ) {
		return nullptr;
	}

	const auto [internalFormat, format] = getLightmapFormatsForSamples( samples );
	const GLenum target = GL_TEXTURE_2D_ARRAY_EXT;
	const GLuint handle = generateHandle( wsw::StringView() );
	bindToModify( target, handle );

	const unsigned flags = IT_CLAMP | IT_NOMIPMAP | IT_CUSTOMFILTERING;
	qglTexImage3D( target, 0, internalFormat, w, h, numLayers, 0, format, GL_UNSIGNED_BYTE, nullptr );
	setupWrapMode( target, flags );

	qglTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	if( glConfig.ext.texture_filter_anisotropic ) {
		qglTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );
	}

	unbindModified( target, 0 );

	return new( m_lightmapsAllocator.allocOrNull() )LightmapArray( handle, w, h, samples, numLayers, flags );
}

void TextureFactory::replaceLightmapLayer( Texture *texture, unsigned layer, const uint8_t *data ) {
	assert( texture );
	assert( layer < texture->layers );

	// TODO: Store these properties in a LightmapTextureArray instance
	const auto [internalFormat, format] = getLightmapFormatsForSamples( texture->samples );

	bindToModify( texture );

	qglTexSubImage3D( texture->target, 0, 0, 0, layer, texture->width, texture->height, 1, format, GL_UNSIGNED_BYTE, data );

	unbindModified( texture );
}

// TODO: Pass a reference to some "rectangle" type instead of these parameters
void TextureFactory::replaceFontMaskSamples( Texture *texture, unsigned x, unsigned y, unsigned w, unsigned h, const uint8_t *data ) {
	const GLenum target = texture->target;
	assert( target == GL_TEXTURE_2D );

	bindToModify( target, texture->texnum );

	// TODO: We must know statically
	const auto [swizzleMask, internalFormat, format, type] = getRegularTexImageFormats( IT_ALPHAMASK | IT_NOMIPMAP | IT_NOFILTERING, 1 );
	if( swizzleMask != kSwizzleMaskIdentity ) {
		qglTexParameteriv( target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
	}

	qglTexSubImage2D( target, 0, x, y, w, h, format, type, data );

	unbindModified( target, 0 );

	assert( qglGetError() == GL_NO_ERROR );
}

[[nodiscard]]
auto TextureFactory::createBuiltin2DTexture( const Builtin2DTextureData &data ) -> Texture * {
	assert( !m_builtinTexturesAllocator.isFull() );

	assert( qglGetError() == GL_NO_ERROR );

	const GLenum target = GL_TEXTURE_2D;
	const GLuint handle = generateHandle( wsw::StringView() );

	bindToModify( target, handle );

	const auto [swizzleMask, internalFormat, format, type] = getRegularTexImageFormats( data.flags, data.samples );
	if( swizzleMask != kSwizzleMaskIdentity ) {
		qglTexParameteriv( target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask );
	}

	qglTexImage2D( target, 0, internalFormat, data.width, data.height, 0, format, type, data.bytes );

	setupWrapMode( target, data.flags );
	if( data.flags & IT_CUSTOMFILTERING ) {
		assert( data.nearestFilteringOnly );
		qglTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR );
		qglTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		if( glConfig.ext.texture_filter_anisotropic ) {
			qglTexParameteri( target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );
		}
	} else {
		assert( !data.nearestFilteringOnly );
		setupFilterMode( target, data.flags );
	}

	// !!!!!!!!
	qglGenerateMipmap( target );

	unbindModified( target, handle );

	auto *const texture = (Texture *)m_builtinTexturesAllocator.allocOrNull();
	texture->texnum = handle;
	texture->target = GL_TEXTURE_2D;
	texture->width = data.width;
	texture->height = data.height;
	texture->flags = data.flags;
	texture->samples = data.samples;
	texture->tags = IMAGE_TAG_BUILTIN;
	return texture;
}

[[nodiscard]]
auto TextureFactory::createSolidColorBuiltinTexture( const float *color ) -> Texture * {
	auto *bytes = ::loadingBuffer.reserveAndGet( 3 );
	bytes[0] = (uint8_t)( 255 * color[0] );
	bytes[1] = (uint8_t)( 255 * color[1] );
	bytes[2] = (uint8_t)( 255 * color[2] );
	Builtin2DTextureData data;
	data.bytes = bytes;
	data.width = data.height = 1;
	data.samples = 3;
	data.flags = IT_NOPICMIP | IT_NOCOMPRESS;
	return createBuiltin2DTexture( data );
}

[[nodiscard]]
auto TextureFactory::createBuiltinNoTextureTexture() -> Texture * {
	constexpr const unsigned side = 8;

	const uint8_t wswPurple[] { 53, 34, 69 };
	const uint8_t wswOrange[] { 95, 39, 9 };
	uint8_t markPurple[3], markOrange[3];
	const uint8_t red[3] = { 128, 0, 0 };
	for( int i = 0; i < 3; ++i ) {
		markPurple[i] = (uint8_t)( ( 3 * wswPurple[i] + red[i] ) / 4 );
		markOrange[i] = (uint8_t)( ( 3 * wswOrange[i] + red[i] ) / 4 );
	}

	ptrdiff_t offset = 0;
	uint8_t *const __restrict p = loadingBuffer.reserveAndGet( side * side * 3 );
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

	Builtin2DTextureData data;
	data.width = data.height = side;
	data.flags = IT_SRGB | IT_CUSTOMFILTERING;
	data.samples = 3;
	data.nearestFilteringOnly = true;
	data.bytes = p;
	return createBuiltin2DTexture( data );
}

[[nodiscard]]
auto TextureFactory::createBuiltinBlankNormalmap() -> Texture * {
	auto *const bytes = ::loadingBuffer.reserveAndGet( 4 );
	bytes[0] = bytes[1] = 128;
	bytes[2] = 255;
	bytes[3] = 128; // Displacement height

	Builtin2DTextureData data;
	data.width = data.height = 1;
	data.samples = 4;
	data.flags = IT_NOPICMIP | IT_NOCOMPRESS;
	data.bytes = bytes;
	return createBuiltin2DTexture( data );
}

[[nodiscard]]
auto TextureFactory::createBuiltinWhiteCubemap() -> Texture * {
	const GLenum target = GL_TEXTURE_CUBE_MAP;
	const GLuint handle = generateHandle( wsw::StringView() );

	bindToModify( target, handle );

	const auto flags = IT_NOMIPMAP | IT_CUBEMAP;
	const uint8_t data[3] = { 255, 255, 255 };
	for( unsigned i = 0; i < 6; ++i ) {
		qglTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, data );
	}

	setupWrapMode( target, flags );
	unbindModified( target, handle );

	Texture *const texture = (Texture *)m_builtinTexturesAllocator.allocOrNull();
	texture->target = target;
	texture->texnum = handle;
	texture->width = 1;
	texture->height = 1;
	texture->layers = 0;
	texture->samples = 3;
	texture->flags = flags;
	texture->tags = IMAGE_TAG_BUILTIN;
	return texture;
}

[[nodiscard]]
auto TextureFactory::createBuiltinParticleTexture() -> Texture * {
	constexpr unsigned side = 16;

	uint8_t *const __restrict p = loadingBuffer.reserveAndGet( side * side * 4 );

	ptrdiff_t offset = 0;
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

	Builtin2DTextureData data;
	data.width = data.height = side;
	data.samples = 4;
	data.flags = IT_NOPICMIP | IT_NOMIPMAP | IT_SRGB;
	data.bytes = p;
	return createBuiltin2DTexture( data );
}

[[nodiscard]]
auto TextureFactory::createBuiltinCoronaTexture() -> Texture * {
	constexpr unsigned side = 32;

	uint8_t *const __restrict p = ::loadingBuffer.reserveAndGet( side * side * 4 );

	ptrdiff_t offset = 0;
	constexpr int halfSide = (int)side / 2;
	constexpr float invHalfSide = 1.0f / (float)halfSide;
	for( int pixNum = 0; pixNum < (int)( side * side ); ++pixNum ) {
		const int x = pixNum % (int)side;
		const int y = pixNum / (int)side;

		const auto unitDX = (float)( x - halfSide ) * invHalfSide;
		const auto unitDY = (float)( y - halfSide ) * invHalfSide;

		float frac = 1.0f - Q_Sqrt( unitDX * unitDX + unitDY * unitDY );
		frac = wsw::clamp( frac, 0.0f, 1.0f );
		frac = frac * frac;

		const auto value = (uint8_t)( 128.0f * frac );
		p[offset + 0] = value;
		p[offset + 1] = value;
		p[offset + 2] = value;
		p[offset + 3] = value;
		offset += 4;
	}

	Builtin2DTextureData data;
	data.width = data.height = side;
	data.flags = IT_SPECIAL | IT_SRGB;
	data.samples = 4;
	data.bytes = p;
	return createBuiltin2DTexture( data );
}

[[nodiscard]]
auto TextureFactory::createUITextureHandleWrapper() -> Texture * {
	assert( !m_builtinTexturesAllocator.isFull() );
	auto *const texture = (Texture *)m_builtinTexturesAllocator.allocOrNull();
	texture->width = 0;
	texture->height = 0;
	texture->tags = IMAGE_TAG_BUILTIN;
	texture->flags = IT_SPECIAL;
	texture->samples = 0;
	texture->texnum = 0;
	texture->target = GL_TEXTURE_2D;
	return texture;
}

void TextureFactory::releaseBuiltinTexture( Texture *texture ) {
	if( texture ) {
		qglDeleteTextures( 1, &texture->texnum );
		m_builtinTexturesAllocator.free( texture );
	}
}

void TextureFactory::releaseMaterialTexture( Material2DTexture *texture ) {
	qglDeleteTextures( 1, &texture->texnum );
	m_materialTexturesAllocator.free( texture );
}

void TextureFactory::releaseMaterialCubemap( MaterialCubemap *cubemap ) {
	qglDeleteTextures( 1, &cubemap->texnum );
	m_materialCubemapsAllocator.free( cubemap );
}

static void wsw_stb_write_func( void *context, void *data, int size ) {
	auto handle = *( (int *)( context ) );
	FS_Write( data, size, handle );
}

/*
* R_ScreenShot
*/
void R_ScreenShot( const char *filename, int x, int y, int width, int height, int quality, bool silent ) {
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
		limit = wsw::min( limit, *inLimit );
	}
	limit = wsw::max( 1u, limit );
	return std::make_pair( wsw::min( inWidth, limit ), wsw::min( inHeight, limit ) );
}