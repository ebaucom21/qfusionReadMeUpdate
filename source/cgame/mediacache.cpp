/*
Copyright (C) 2002-2003 Victor Luchits

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

#include "cg_local.h"
#include "../client/snd_public.h"
#include "../ref/frontend.h"
#include "../qcommon/qcommon.h"
#include "../client/client.h"
#include "mediacache.h"

MediaCache::CachedSound::CachedSound( MediaCache *parent, const wsw::StringView &name )
	: MediaCache::CachedHandle<sfx_s>( name ) {
	parent->link( this, &parent->m_sounds );
}

MediaCache::CachedModel::CachedModel( MediaCache *parent, const wsw::StringView &name )
	: MediaCache::CachedHandle<model_s>( name ) {
	parent->link( this, &parent->m_models );
}

MediaCache::CachedMaterial::CachedMaterial( MediaCache *parent, const wsw::StringView &name )
	: MediaCache::CachedHandle<shader_s>( name ) {
	parent->link( this, &parent->m_materials );
}

template <typename T>
MediaCache::ArbitraryLengthHandlesArray<T>::ArbitraryLengthHandlesArray( const char *format,
																		 unsigned int numHandles,
																		 unsigned int indexShift )
	: m_format( format ), m_numHandles( numHandles ), m_indexShift( indexShift ) {}

template <typename T, unsigned Length>
MediaCache::CachedHandlesArray<T, Length>::CachedHandlesArray( const char *format, unsigned indexShift )
	: ArbitraryLengthHandlesArray<T>( format, Length, indexShift ) {
	this->m_handles = m_handlesStorage;
	std::fill_n( m_handlesStorage, Length, nullptr );
}

template <unsigned Length>
MediaCache::CachedSoundsArray<Length>::CachedSoundsArray( MediaCache *parent,
														  const char *format,
														  unsigned indexShift )
	: CachedHandlesArray<sfx_s, Length>( format, indexShift ) {
	parent->link( ( MediaCache::LinkedSoundsArray *)this, &parent->m_soundsArrays );
}

template <unsigned Length>
MediaCache::CachedMaterialsArray<Length>::CachedMaterialsArray( MediaCache *parent,
																const char *format,
																unsigned indexShift )
	: CachedHandlesArray<shader_s, Length>( format, indexShift ) {
	parent->link( ( MediaCache::LinkedMaterialsArray *)this, &parent->m_materialsArrays );
}

// Make sure it's defined here so the inline setup of fields does not get replicated over inclusion places
MediaCache::MediaCache() {}

void MediaCache::registerSounds() {
	for( CachedSound *sound = m_sounds; sound; sound = (CachedSound *)sound->m_next ) {
		registerSound( sound );
	}
	for( LinkedSoundsArray *array = m_soundsArrays; array; array = array->m_next ) {
		registerSoundsArray( array );
	}
}

void MediaCache::registerModels() {
	for( CachedModel *model = m_models; model; model = (CachedModel *)model->m_next ) {
		registerModel( model );
	}
}

void MediaCache::registerMaterials() {
	for( CachedMaterial *material = m_materials; material; material = (CachedMaterial *)material->m_next ) {
		registerMaterial( material );
	}
	for( LinkedMaterialsArray *array = m_materialsArrays; array; array = array->m_next ) {
		registerMaterialsArray( array );
	}
}

void MediaCache::registerSound( CachedSound *sound ) {
	if( !sound->m_handle ) {
		assert( sound->m_name.isZeroTerminated() );
		sound->m_handle = SoundSystem::Instance()->RegisterSound( sound->m_name.data());
	}
}

template <typename T>
auto MediaCache::formatName( char *buffer, ArbitraryLengthHandlesArray<T> *array, unsigned index ) -> const char * {
	return va_r( buffer, 1024, array->m_format, index + array->m_indexShift );
}

void MediaCache::registerSoundsArray( MediaCache::LinkedSoundsArray *array ) {
	char buffer[1024];
	for( unsigned i = 0; i < array->m_numHandles; ++i ) {
		if( !array->m_handles[i] ) {
			array->m_handles[i] = SoundSystem::Instance()->RegisterSound( formatName( buffer, array, i ) );
		}
	}
}

void MediaCache::registerModel( CachedModel *model ) {
	if( !model->m_handle ) {
		assert( model->m_name.isZeroTerminated() );
		model->m_handle = CG_RegisterModel( model->m_name.data() );
	}
}

void MediaCache::registerMaterial( CachedMaterial *material ) {
	if( !material->m_handle ) {
		assert( material->m_name.isZeroTerminated() );
		material->m_handle = R_RegisterPic( material->m_name.data() );
	}
}

void MediaCache::registerMaterialsArray( MediaCache::LinkedMaterialsArray *array ) {
	char buffer[1024];
	for( unsigned i = 0; i < array->m_numHandles; ++i ) {
		if( !array->m_handles[i] ) {
			array->m_handles[i] = R_RegisterPic( formatName( buffer, array, i ) );
		}
	}
}

/*
* CG_RegisterModel
*/
struct model_s *CG_RegisterModel( const char *name ) {
	struct model_s *model;

	model = R_RegisterModel( name );

	// precache bones
	if( R_SkeletalGetNumBones( model, NULL ) ) {
		CG_SkeletonForModel( model );
	}

	return model;
}

void CG_RegisterLevelMinimap( void ) {
	size_t i;
	int file;
	char minimap[MAX_QPATH];

	cgs.shaderMiniMap = NULL;

	const char *name = cgs.configStrings.getMapName()->data();

	for( i = 0; i < NUM_IMAGE_EXTENSIONS; i++ ) {
		Q_snprintfz( minimap, sizeof( minimap ), "minimaps/%s%s", name, IMAGE_EXTENSIONS[i] );
		file = FS_FOpenFile( minimap, NULL, FS_READ );
		if( file != -1 ) {
			cgs.shaderMiniMap = R_RegisterPic( minimap );
			break;
		}
	}
}

/*
* CG_RegisterFonts
*/
void CG_RegisterFonts( void ) {
	cvar_t *con_fontSystemFamily = Cvar_Get( "con_fontSystemFamily", DEFAULT_SYSTEM_FONT_FAMILY, CVAR_ARCHIVE );
	cvar_t *con_fontSystemMonoFamily = Cvar_Get( "con_fontSystemMonoFamily", DEFAULT_SYSTEM_FONT_FAMILY_MONO, CVAR_ARCHIVE );
	cvar_t *con_fontSystemSmallSize = Cvar_Get( "con_fontSystemSmallSize", STR_TOSTR( DEFAULT_SYSTEM_FONT_SMALL_SIZE ), CVAR_ARCHIVE );
	cvar_t *con_fontSystemMediumSize = Cvar_Get( "con_fontSystemMediumSize", STR_TOSTR( DEFAULT_SYSTEM_FONT_MEDIUM_SIZE ), CVAR_ARCHIVE );
	cvar_t *con_fontSystemBigSize = Cvar_Get( "con_fontSystemBigSize", STR_TOSTR( DEFAULT_SYSTEM_FONT_BIG_SIZE ), CVAR_ARCHIVE );

	// register system fonts
	Q_strncpyz( cgs.fontSystemFamily, con_fontSystemFamily->string, sizeof( cgs.fontSystemFamily ) );
	Q_strncpyz( cgs.fontSystemMonoFamily, con_fontSystemMonoFamily->string, sizeof( cgs.fontSystemMonoFamily ) );
	if( con_fontSystemSmallSize->integer <= 0 ) {
		Cvar_Set( con_fontSystemSmallSize->name, con_fontSystemSmallSize->dvalue );
	}
	if( con_fontSystemMediumSize->integer <= 0 ) {
		Cvar_Set( con_fontSystemMediumSize->name, con_fontSystemMediumSize->dvalue );
	}
	if( con_fontSystemBigSize->integer <= 0 ) {
		Cvar_Set( con_fontSystemBigSize->name, con_fontSystemBigSize->dvalue );
	}

	float scale = ( float )( cgs.vidHeight ) / 600.0f;

	cgs.fontSystemSmallSize = ceilf( con_fontSystemSmallSize->integer * scale );
	cgs.fontSystemSmall = SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemSmallSize );
	if( !cgs.fontSystemSmall ) {
		Q_strncpyz( cgs.fontSystemFamily, DEFAULT_SYSTEM_FONT_FAMILY, sizeof( cgs.fontSystemFamily ) );
		cgs.fontSystemSmallSize = ceilf( DEFAULT_SYSTEM_FONT_SMALL_SIZE * scale );

		cgs.fontSystemSmall = SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemSmallSize );
		if( !cgs.fontSystemSmall ) {
			CG_Error( "Couldn't load default font \"%s\"", cgs.fontSystemFamily );
		}
	}

	cgs.fontSystemMediumSize = ceilf( con_fontSystemMediumSize->integer * scale );
	cgs.fontSystemMedium = SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemMediumSize );
	if( !cgs.fontSystemMedium ) {
		cgs.fontSystemMediumSize = ceilf( DEFAULT_SYSTEM_FONT_MEDIUM_SIZE * scale );
		cgs.fontSystemMedium = SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemMediumSize );
	}

	cgs.fontSystemBigSize = ceilf( con_fontSystemBigSize->integer * scale );
	cgs.fontSystemBig = SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemBigSize );
	if( !cgs.fontSystemBig ) {
		cgs.fontSystemBigSize = ceilf( DEFAULT_SYSTEM_FONT_BIG_SIZE * scale );
		cgs.fontSystemBig = SCR_RegisterFont( cgs.fontSystemFamily, QFONT_STYLE_NONE, cgs.fontSystemBigSize );
	}
}
