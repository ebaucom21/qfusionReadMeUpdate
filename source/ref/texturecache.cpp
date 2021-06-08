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

#include "../qcommon/singletonholder.h"
#include "../qcommon/links.h"
#include "local.h"

using wsw::operator""_asView;

static SingletonHolder<TextureCache> g_instanceHolder;
static bool g_initialized;

wsw::StaticString<64> TextureManagementShared::s_cleanNameBuffer;

void TextureCache::init() {
	g_instanceHolder.Init();
	g_initialized = true;
}

void TextureCache::shutdown() {
	g_instanceHolder.Shutdown();
	g_initialized = false;
}

auto TextureCache::instance() -> TextureCache * {
	return g_instanceHolder.Instance();
}

auto TextureCache::maybeInstance() -> TextureCache * {
	if( g_initialized ) {
		return g_instanceHolder.Instance();
	}
	return nullptr;
}

TextureCache::TextureCache() {
	std::fill( std::begin( m_builtinTextures ), std::end( m_builtinTextures ), nullptr );
	m_builtinTextures[(unsigned)BuiltinTexNum::No] = m_factory.createBuiltinNoTextureTexture();
	m_builtinTextures[(unsigned)BuiltinTexNum::White] = m_factory.createSolidColorBuiltinTexture( colorWhite );
	m_builtinTextures[(unsigned)BuiltinTexNum::WhiteCubemap] = m_factory.createBuiltinWhiteCubemap();
	m_builtinTextures[(unsigned)BuiltinTexNum::Black] = m_factory.createSolidColorBuiltinTexture( colorBlack );
	m_builtinTextures[(unsigned)BuiltinTexNum::Grey] = m_factory.createSolidColorBuiltinTexture( colorMdGrey );
	m_builtinTextures[(unsigned)BuiltinTexNum::BlankBump] = m_factory.createBuiltinBlankNormalmap();
	m_builtinTextures[(unsigned)BuiltinTexNum::Particle] = m_factory.createBuiltinParticleTexture();
	m_builtinTextures[(unsigned)BuiltinTexNum::Corona] = m_factory.createBuiltinCoronaTexture();

	std::fill( std::begin( m_portalTextures ), std::end( m_portalTextures ), nullptr );

	m_uiTextureWrapper = m_factory.createUITextureHandleWrapper();
}

TextureCache::~TextureCache() {
	// TODO: Portal textures
	m_factory.releaseBuiltinTexture( m_uiTextureWrapper );
	for( Texture *texture: m_builtinTextures ) {
		m_factory.releaseBuiltinTexture( texture );
	}
}

auto TextureManagementShared::makeCleanName( const wsw::StringView &rawName, const wsw::StringView &suffix )
	-> std::optional<wsw::StringView> {
	wsw::StringView name( rawName );
	if( name.startsWith( '/' ) || name.startsWith( '\\' ) ) {
		name = name.drop( 1 );
	}
	if( name.empty() || name.length() + suffix.length() >= s_cleanNameBuffer.capacity() ) {
		return std::nullopt;
	}

	int lastDotIndex = -1, lastSlashIndex = -1;
	s_cleanNameBuffer.clear();
	for( int i = 0; i < (int)name.size(); ++i ) {
		const char ch = name[i];
		if( ch == '.' ) {
			lastDotIndex = i;
		}
		if( ch == '\\' ) {
			s_cleanNameBuffer.append( '/' );
		} else {
			s_cleanNameBuffer.append( ch );
		}
		if( s_cleanNameBuffer.back() == '/' ) {
			lastSlashIndex = i;
		}
	}

	if( lastDotIndex >= 0 && lastDotIndex < lastSlashIndex ) {
		s_cleanNameBuffer.erase( (unsigned)lastDotIndex );
	}
	s_cleanNameBuffer.append( suffix );
	return s_cleanNameBuffer.asView();
}

template <typename T>
auto TextureCache::findCachedTextureInBin( T *binHead, const wsw::HashedStringView &name, unsigned flags ) -> T * {
	for( T *texture = binHead; texture; texture = (T *)texture->next[Texture::BinLinks] ) {
		if( texture->getName().equalsIgnoreCase( name ) ) {
			if( ( texture->flags & ~IT_LOADFLAGS ) == ( flags & ~IT_LOADFLAGS ) ) {
				return texture;
			}
		}
	}

	return nullptr;
}

template <typename T, typename Method>
auto TextureCache::getTexture( const wsw::StringView &name, const wsw::StringView &suffix,
							   unsigned flags, unsigned tags,
							   T **listHead, T **bins, Method methodOfFactory ) -> T * {
	T *texture = nullptr;
	if( const auto maybeCleanName = makeCleanName( name, suffix ) ) {
		const wsw::HashedStringView hashedCleanName( *maybeCleanName );
		const auto binIndex = hashedCleanName.getHash() % kNumHashBins;
		if( !( texture = findCachedTextureInBin( bins[binIndex], hashedCleanName, flags ) ) ) {
			if( ( texture = ( &m_factory->*methodOfFactory )( hashedCleanName, flags, tags ) ) ) {
				wsw::link( texture, &bins[binIndex], Texture::BinLinks );
				wsw::link( texture, listHead, Texture::ListLinks );
			}
		}
	}
	if( texture ) {
		touchTexture( texture, tags );
	}
	return texture;
}

auto TextureCache::getMaterial2DTexture( const wsw::StringView &name, const wsw::StringView &suffix,
									     unsigned flags, unsigned tags ) -> Material2DTexture * {
	return getTexture( name, suffix, flags, tags, &m_materialTexturesHead,
					   m_materialTextureBins, &TextureFactory::loadMaterialTexture );
}

auto TextureCache::getMaterialCubemap( const wsw::StringView &name, unsigned flags,
									   unsigned tags ) -> MaterialCubemap * {
	return getTexture( name, ""_asView, flags, tags, &m_materialCubemapsHead,
					   m_materialCubemapBins, &TextureFactory::loadMaterialCubemap );
}

auto TextureCache::wrapUITextureHandle( GLuint externalHandle ) -> Texture * {
	assert( m_uiTextureWrapper );
	Texture *texture = m_uiTextureWrapper;
	texture->texnum = externalHandle;
	texture->width = rf.width2D;
	texture->height = rf.height2D;
	texture->samples = 1;
	return texture;
}

auto TextureCache::findFreePortalTexture( unsigned width, unsigned height, int flags, unsigned frameNum )
	-> std::optional<std::tuple<PortalTexture *, unsigned, bool>> {
	std::optional<unsigned> bestSlot;
	std::optional<unsigned> freeSlot;
	for( unsigned i = 0; i < std::size( m_portalTextures ); ++i ) {
		PortalTexture *const texture = m_portalTextures[i];
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
	if( PortalTexture *texture = getPortalTexture_( viewportWidth, viewportHeight, flags, frameNum ) ) {
		texture->framenum = frameNum;
		return texture;
	}
	return nullptr;
}

auto TextureCache::getPortalTexture_( unsigned viewportWidth, unsigned viewportHeight,
									  int flags, unsigned frameNum ) -> PortalTexture * {
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
		// TODO:!!!!!!!!
		// bindToModify( existing );
		// Just resize/change format
		// unbindModified( existing );
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

const std::pair<wsw::StringView, TextureManagementShared::TextureFilter> TextureManagementShared::kTextureFilterNames[3] {
	{ "nearest"_asView, Nearest },
	{ "bilinear"_asView, Bilinear },
	{ "trilinear"_asView, Trilinear }
};

const std::pair<GLuint, GLuint> TextureManagementShared::kTextureFilterGLValues[3] {
	// We have decided to apply mipmap filtering for the "nearest" filter.
	{ GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
	{ GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
	{ GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};

auto TextureManagementShared::findFilterByName( const wsw::StringView &name ) -> std::optional<TextureFilter> {
	for( const auto &[filterName, filter]: kTextureFilterNames ) {
		if( name.equalsIgnoreCase( filterName ) ) {
			return filter;
		}
	}
	return std::nullopt;
}

void TextureManagementShared::bindToModify( GLenum target, GLuint texture ) {
	qglBindTexture( target, texture );
	RB_FlushTextureCache();
	assert( qglGetError() == GL_NO_ERROR );
}

void TextureManagementShared::unbindModified( GLenum target, GLuint texture ) {
	// TODO: Check whether it's actively bound (it must be) for debugging purposes
	qglBindTexture( target, 0 );
	RB_FlushTextureCache();
	assert( qglGetError() == GL_NO_ERROR );
}

void TextureManagementShared::applyFilter( Texture *texture, GLuint minify, GLuint magnify ) {
	// TODO: Check whether it's bound for modifying
	// TODO: What about usign the bindless API?
	if( texture->flags & ( IT_NOFILTERING | IT_DEPTH | IT_CUSTOMFILTERING ) ) {
		return;
	}
	GLuint minifyToApply = ( texture->flags & IT_NOMIPMAP ) ? magnify : minify;
	qglTexParameteri( texture->target, GL_TEXTURE_MIN_FILTER, minifyToApply );
	qglTexParameteri( texture->target, GL_TEXTURE_MAG_FILTER, magnify );
}

void TextureManagementShared::applyAniso( Texture *texture, int level ) {
	assert( glConfig.ext.texture_filter_anisotropic );
	assert( level >= 1 && level <= glConfig.maxTextureFilterAnisotropic );
	if( ( texture->flags & ( IT_NOFILTERING | IT_DEPTH | IT_NOMIPMAP | IT_CUSTOMFILTERING ) ) ) {
		return;
	}
	qglTexParameteri( texture->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, level );
}

void TextureCache::applyFilterOrAnisoInList( Texture *listHead, TextureFilter filter,
											 int aniso, bool applyFilter, bool applyAniso ) {
	assert( (unsigned)filter < std::size( kTextureFilterGLValues ) );
	const auto [minify, magnify] = kTextureFilterGLValues[(unsigned)filter];

	Texture *lastTexture = nullptr;
	for( Texture *texture = listHead; texture; texture = texture->nextInList() ) {
		// There are individual checks in each apply() subroutine.
		// We add another early check to avoid doing a fruitless bind.
		if( texture->flags & ( IT_NOFILTERING | IT_DEPTH | IT_CUSTOMFILTERING ) ) {
			continue;
		}
		bindToModify( texture );
		lastTexture = texture;
		if( applyFilter ) {
			TextureManagementShared::applyFilter( texture, minify, magnify );
		}
		if( applyAniso ) {
			TextureManagementShared::applyAniso( texture, aniso );
		}
	}
	if( lastTexture ) {
		unbindModified( lastTexture );
	}
}

void TextureCache::applyFilter( const wsw::StringView &name, int anisoLevel ) {
	const auto maybeFilter = findFilterByName( name );
	if( !maybeFilter ) {
		assert( name.isZeroTerminated() );
		Com_Printf( "Failed to find a texture filter by name %s\n", name.data() );
		return;
	}

	int anisoToApply = 1;
	const TextureFilter filter = *maybeFilter;
	bool doApplyFilter = false, doApplyAniso = false;
	if( !m_factory.tryUpdatingFilterOrAniso( filter, anisoLevel, &anisoToApply, &doApplyFilter, &doApplyAniso ) ) {
		return;
	}

	applyFilterOrAnisoInList( m_materialTexturesHead, filter, anisoToApply, doApplyFilter, doApplyAniso );
	applyFilterOrAnisoInList( m_materialCubemapsHead, filter, anisoToApply, doApplyFilter, doApplyAniso );
}

void TextureCache::touchTexture( Texture *texture, unsigned tags ) {
	texture->tags |= tags;
}

void TextureCache::freeUnusedWorldTextures() {}
void TextureCache::freeAllUnusedTextures() {}