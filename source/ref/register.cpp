/*
Copyright (C) 2007 Victor Luchits

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

// r_register.c
#include "local.h"
#include "../qcommon/hash.h"
#include "../qcommon/qcommon.h"
#include "materiallocal.h"

#include <algorithm>

glconfig_t glConfig;

r_shared_t rsh;

cvar_t *r_norefresh;
cvar_t *r_drawentities;
cvar_t *r_drawworld;
cvar_t *r_speeds;
cvar_t *r_drawelements;
cvar_t *r_fullbright;
cvar_t *r_lightmap;
cvar_t *r_novis;
cvar_t *r_nocull;
cvar_t *r_lerpmodels;
cvar_t *r_brightness;
cvar_t *r_sRGB;

cvar_t *r_dynamiclight;
cvar_t *r_detailtextures;
cvar_t *r_subdivisions;
cvar_t *r_showtris;
cvar_t *r_shownormals;
cvar_t *r_draworder;

cvar_t *r_fastsky;
cvar_t *r_portalonly;
cvar_t *r_portalmaps;
cvar_t *r_portalmaps_maxtexsize;

cvar_t *r_lighting_deluxemapping;
cvar_t *r_lighting_specular;
cvar_t *r_lighting_glossintensity;
cvar_t *r_lighting_glossexponent;
cvar_t *r_lighting_ambientscale;
cvar_t *r_lighting_directedscale;
cvar_t *r_lighting_packlightmaps;
cvar_t *r_lighting_maxlmblocksize;
cvar_t *r_lighting_vertexlight;
cvar_t *r_lighting_maxglsldlights;
cvar_t *r_lighting_grayscale;
cvar_t *r_lighting_intensity;

cvar_t *r_offsetmapping;
cvar_t *r_offsetmapping_scale;
cvar_t *r_offsetmapping_reliefmapping;

cvar_t *r_shadows;
cvar_t *r_shadows_alpha;
cvar_t *r_shadows_nudge;
cvar_t *r_shadows_projection_distance;
cvar_t *r_shadows_maxtexsize;
cvar_t *r_shadows_pcf;
cvar_t *r_shadows_self_shadow;
cvar_t *r_shadows_dither;

cvar_t *r_outlines_world;
cvar_t *r_outlines_scale;
cvar_t *r_outlines_cutoff;

cvar_t *r_soft_particles;
cvar_t *r_soft_particles_scale;

cvar_t *r_hdr;
cvar_t *r_hdr_gamma;
cvar_t *r_hdr_exposure;

cvar_t *r_bloom;

cvar_t *r_fxaa;
cvar_t *r_samples;

cvar_t *r_lodbias;
cvar_t *r_lodscale;

cvar_t *r_stencilbits;
cvar_t *r_gamma;
cvar_t *r_texturemode;
cvar_t *r_texturefilter;
cvar_t *r_texturecompression;
cvar_t *r_picmip;
cvar_t *r_nobind;
cvar_t *r_polyblend;
cvar_t *r_lockpvs;
cvar_t *r_screenshot_fmtstr;
cvar_t *r_screenshot_jpeg;
cvar_t *r_screenshot_jpeg_quality;
cvar_t *r_swapinterval;
cvar_t *r_swapinterval_min;

cvar_t *r_temp1;

cvar_t *r_drawflat;
cvar_t *r_wallcolor;
cvar_t *r_floorcolor;

cvar_t *r_usenotexture;

cvar_t *r_maxglslbones;

cvar_t *gl_driver;
cvar_t *gl_cull;
cvar_t *r_multithreading;

cvar_t *r_showShaderCache;

static bool r_verbose;
static bool r_postinit;

static void R_FinalizeGLExtensions( void );

static void R_InitVolatileAssets( void );
static void R_DestroyVolatileAssets( void );

//=======================================================================

#define GLINF_FOFS( x ) offsetof( glextinfo_t,x )
#define GLINF_EXMRK() GLINF_FOFS( _extMarker )
#define GLINF_FROM( from,ofs ) ( *( (char *)from + ofs ) )

typedef struct {
	const char *name;               // constant pointer to constant string
	void **pointer;                 // constant pointer to function's pointer (function itself)
} gl_extension_func_t;

typedef struct {
	const char * prefix;            // constant pointer to constant string
	const char * name;
	const char * cvar_default;
	bool cvar_readonly;
	bool mandatory;
	gl_extension_func_t *funcs;     // constant pointer to array of functions
	size_t offset;                  // offset to respective variable
	size_t depOffset;               // offset to required pre-initialized variable
} gl_extension_t;

#define GL_EXTENSION_FUNC_EXT( name,func ) { name, (void ** const)func }
#define GL_EXTENSION_FUNC( name ) GL_EXTENSION_FUNC_EXT( "gl"#name,&( qgl ## name ) )

/* GL_ARB_get_program_binary */
static const gl_extension_func_t gl_ext_get_program_binary_ARB_funcs[] =
{
	GL_EXTENSION_FUNC( ProgramParameteri )
	,GL_EXTENSION_FUNC( GetProgramBinary )
	,GL_EXTENSION_FUNC( ProgramBinary )

	,GL_EXTENSION_FUNC_EXT( NULL,NULL )
};

#ifndef USE_SDL2

#ifdef GLX_VERSION

/* GLX_SGI_swap_control */
static const gl_extension_func_t glx_ext_swap_control_SGI_funcs[] =
{
	GL_EXTENSION_FUNC_EXT( "glXSwapIntervalSGI",&qglXSwapIntervalSGI )

	,GL_EXTENSION_FUNC_EXT( NULL,NULL )
};

#endif

#endif // USE_SDL2

#undef GL_EXTENSION_FUNC
#undef GL_EXTENSION_FUNC_EXT

//=======================================================================

#define GL_EXTENSION_EXT( pre,name,val,ro,mnd,funcs,dep ) { #pre, #name, #val, ro, mnd, (gl_extension_func_t * const)funcs, GLINF_FOFS( name ), GLINF_FOFS( dep ) }
#define GL_EXTENSION( pre,name,ro,mnd,funcs ) GL_EXTENSION_EXT( pre,name,1,ro,mnd,funcs,_extMarker )

//
// OpenGL extensions list
//
// short notation: vendor, name, default value, list of functions
// extended notation: vendor, name, default value, list of functions, required extension
static const gl_extension_t gl_extensions_decl[] =
{
	GL_EXTENSION( ARB, get_program_binary, false, false, &gl_ext_get_program_binary_ARB_funcs )
	,GL_EXTENSION( ARB, ES3_compatibility, false, false, NULL )
	,GL_EXTENSION( EXT, texture_array, false, false, NULL )
	,GL_EXTENSION( ARB, gpu_shader5, false, false, NULL )

	// memory info
	,GL_EXTENSION( NVX, gpu_memory_info, true, false, NULL )
	,GL_EXTENSION( ATI, meminfo, true, false, NULL )

	,GL_EXTENSION( EXT, texture_filter_anisotropic, true, false, NULL )
	,GL_EXTENSION( EXT, bgra, true, false, NULL )

#ifndef USE_SDL2
#ifdef GLX_VERSION
	,GL_EXTENSION( GLX_SGI, swap_control, true, false, &glx_ext_swap_control_SGI_funcs )
#endif
#endif
};

static const int num_gl_extensions = sizeof( gl_extensions_decl ) / sizeof( gl_extensions_decl[0] );

#undef GL_EXTENSION
#undef GL_EXTENSION_EXT

static bool isExtensionSupported( const char *ext ) {
	GLint numExtensions;
	qglGetIntegerv( GL_NUM_EXTENSIONS, &numExtensions );
	// CBA to speed it up as this is required only on starting up
	for( unsigned i = 0; i < numExtensions; ++i ) {
		if( !Q_stricmp( ext, (const char *)qglGetStringi( GL_EXTENSIONS, i ) ) ) {
			return true;
		}
	}
	return false;
}

static bool isPlatformExtensionSupported( const char *ext ) {
	return strstr( qglGetGLWExtensionsString(), ext ) != nullptr;
}

/*
* R_RegisterGLExtensions
*/
static bool R_RegisterGLExtensions( void ) {
	int i;
	char *var, name[128];
	cvar_t *cvar;
	cvar_flag_t cvar_flags;
	gl_extension_func_t *func;
	const gl_extension_t *extension;

	memset( &glConfig.ext, 0, sizeof( glextinfo_t ) );

	for( i = 0, extension = gl_extensions_decl; i < num_gl_extensions; i++, extension++ ) {
		Q_snprintfz( name, sizeof( name ), "gl_ext_%s", extension->name );

		// register a cvar and check if this extension is explicitly disabled
		cvar_flags = CVAR_ARCHIVE | CVAR_LATCH_VIDEO;
#ifdef PUBLIC_BUILD
		if( extension->cvar_readonly ) {
			cvar_flags |= CVAR_READONLY;
		}
#endif

		cvar = Cvar_Get( name, extension->cvar_default ? extension->cvar_default : "0", cvar_flags );
		if( !cvar->integer ) {
			continue;
		}

		// an alternative extension of higher priority is available so ignore this one
		var = &( GLINF_FROM( &glConfig.ext, extension->offset ) );
		if( *var ) {
			continue;
		}

		// required extension is not available, ignore
		if( extension->depOffset != GLINF_EXMRK() && !GLINF_FROM( &glConfig.ext, extension->depOffset ) ) {
			continue;
		}

		// let's see what the driver's got to say about this...
		if( *extension->prefix ) {
			auto testFunc = isExtensionSupported;
			for( const char *prefix : { "WGL", "GLX", "EGL" } ) {
				if( !strncmp( extension->prefix, prefix, 3 ) ) {
					testFunc = isPlatformExtensionSupported;
					break;
				}
			}

			Q_snprintfz( name, sizeof( name ), "%s_%s", extension->prefix, extension->name );
			if( !testFunc( name ) ) {
				continue;
			}
		}

		// initialize function pointers
		func = extension->funcs;
		if( func ) {
			do {
				*( func->pointer ) = ( void * )qglGetProcAddress( (const GLubyte *)func->name );
				if( !*( func->pointer ) ) {
					break;
				}
			} while( ( ++func )->name );

			// some function is missing
			if( func->name ) {
				gl_extension_func_t *func2 = extension->funcs;

				// whine about buggy driver
				if( *extension->prefix ) {
					Com_Printf( "R_RegisterGLExtensions: broken %s support, contact your video card vendor\n",
								cvar->name );
				}

				// reset previously initialized functions back to NULL
				do {
					*( func2->pointer ) = NULL;
				} while( ( ++func2 )->name && func2 != func );

				continue;
			}
		}

		// mark extension as available
		*var = true;

	}

	for( i = 0, extension = gl_extensions_decl; i < num_gl_extensions; i++, extension++ ) {
		if( !extension->mandatory ) {
			continue;
		}

		var = &( GLINF_FROM( &glConfig.ext, extension->offset ) );

		if( !*var ) {
			Sys_Error( "R_RegisterGLExtensions: '%s_%s' is not available, aborting\n",
					   extension->prefix, extension->name );
			return false;
		}
	}

	R_FinalizeGLExtensions();
	return true;
}

/*
* R_PrintGLExtensionsInfo
*/
static void R_PrintGLExtensionsInfo( void ) {
	int i;
	size_t lastOffset;
	const gl_extension_t *extension;

	for( i = 0, lastOffset = 0, extension = gl_extensions_decl; i < num_gl_extensions; i++, extension++ ) {
		if( lastOffset != extension->offset ) {
			lastOffset = extension->offset;
			Com_Printf( "%s: %s\n", extension->name, GLINF_FROM( &glConfig.ext, lastOffset ) ? "enabled" : "disabled" );
		}
	}
}

/*
* R_FinalizeGLExtensions
*
* Verify correctness of values provided by the driver, init some variables
*/
static void R_FinalizeGLExtensions( void ) {
	int versionMajor, versionMinor;
	cvar_t *cvar;
	char tmp[128];

	versionMajor = versionMinor = 0;
	sscanf( glConfig.versionString, "%d.%d", &versionMajor, &versionMinor );
	glConfig.version = versionMajor * 100 + versionMinor * 10;

	glConfig.maxTextureSize = 0;
	qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.maxTextureSize );
	if( glConfig.maxTextureSize <= 0 ) {
		glConfig.maxTextureSize = 256;
	}
	glConfig.maxTextureSize = 1 << Q_log2( glConfig.maxTextureSize );

	Cvar_Get( "gl_max_texture_size", "0", CVAR_READONLY );
	Cvar_ForceSet( "gl_max_texture_size", va_r( tmp, sizeof( tmp ), "%i", glConfig.maxTextureSize ) );

	/* GL_ARB_texture_cube_map */
	glConfig.maxTextureCubemapSize = 0;
	qglGetIntegerv( GL_MAX_CUBE_MAP_TEXTURE_SIZE, &glConfig.maxTextureCubemapSize );
	glConfig.maxTextureCubemapSize = 1 << Q_log2( glConfig.maxTextureCubemapSize );

	/* GL_ARB_multitexture */
	glConfig.maxTextureUnits = 1;
	qglGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &glConfig.maxTextureUnits );
	Q_clamp( glConfig.maxTextureUnits, 1, MAX_TEXTURE_UNITS );

	/* GL_EXT_framebuffer_object */
	glConfig.maxRenderbufferSize = 0;
	qglGetIntegerv( GL_MAX_RENDERBUFFER_SIZE, &glConfig.maxRenderbufferSize );
	glConfig.maxRenderbufferSize = 1 << Q_log2( glConfig.maxRenderbufferSize );
	if( glConfig.maxRenderbufferSize > glConfig.maxTextureSize ) {
		glConfig.maxRenderbufferSize = glConfig.maxTextureSize;
	}

	/* GL_EXT_texture_filter_anisotropic */
	glConfig.maxTextureFilterAnisotropic = 0;
	if( isExtensionSupported( "GL_EXT_texture_filter_anisotropic" ) ) {
		qglGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.maxTextureFilterAnisotropic );
	}

	/* GL_EXT_texture3D and GL_EXT_texture_array */
	qglGetIntegerv( GL_MAX_3D_TEXTURE_SIZE, &glConfig.maxTexture3DSize );

	glConfig.maxTextureLayers = 0;
	if( glConfig.ext.texture_array ) {
		qglGetIntegerv( GL_MAX_ARRAY_TEXTURE_LAYERS_EXT, &glConfig.maxTextureLayers );
	}

	versionMajor = versionMinor = 0;
	sscanf( glConfig.shadingLanguageVersionString, "%d.%d", &versionMajor, &versionMinor );
	glConfig.shadingLanguageVersion = versionMajor * 100 + versionMinor;

	glConfig.maxVertexUniformComponents = glConfig.maxFragmentUniformComponents = 0;
	glConfig.maxVaryingFloats = 0;

	qglGetIntegerv( GL_MAX_VERTEX_ATTRIBS, &glConfig.maxVertexAttribs );
	qglGetIntegerv( GL_MAX_VERTEX_UNIFORM_COMPONENTS, &glConfig.maxVertexUniformComponents );
	qglGetIntegerv( GL_MAX_VARYING_FLOATS, &glConfig.maxVaryingFloats );
	qglGetIntegerv( GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &glConfig.maxFragmentUniformComponents );

	// keep the maximum number of bones we can do in GLSL sane
	if( r_maxglslbones->integer > MAX_GLSL_UNIFORM_BONES ) {
		Cvar_ForceSet( r_maxglslbones->name, r_maxglslbones->dvalue );
	}

	// require GLSL 1.20+ for GPU skinning
	if( glConfig.shadingLanguageVersion >= 120 ) {
		// the maximum amount of bones we can handle in a vertex shader (2 vec4 uniforms per vertex)
		glConfig.maxGLSLBones = bound( 0, glConfig.maxVertexUniformComponents / 8 - 19, r_maxglslbones->integer );
	} else {
		glConfig.maxGLSLBones = 0;
	}

	glConfig.depthEpsilon = 1.0 / ( 1 << 22 );

	glConfig.sSRGB = r_sRGB->integer;

	cvar = Cvar_Get( "gl_ext_vertex_buffer_object_hack", "0", CVAR_ARCHIVE | CVAR_NOSET );
	if( cvar && !cvar->integer ) {
		Cvar_ForceSet( cvar->name, "1" );
		Cvar_ForceSet( "gl_ext_vertex_buffer_object", "1" );
	}

	qglGetIntegerv( GL_MAX_SAMPLES, &glConfig.maxFramebufferSamples );

	Cvar_Get( "r_texturefilter_max", "0", CVAR_READONLY );
	Cvar_ForceSet( "r_texturefilter_max", va_r( tmp, sizeof( tmp ), "%i", glConfig.maxTextureFilterAnisotropic ) );

	Cvar_Get( "r_soft_particles_available", "1", CVAR_READONLY );

	// don't allow too high values for lightmap block size as they negatively impact performance
	if( r_lighting_maxlmblocksize->integer > glConfig.maxTextureSize / 4 &&
		!( glConfig.maxTextureSize / 4 < std::min( QF_LIGHTMAP_WIDTH,QF_LIGHTMAP_HEIGHT ) * 2 ) ) {
		Cvar_ForceSet( "r_lighting_maxlmblocksize", va_r( tmp, sizeof( tmp ), "%i", glConfig.maxTextureSize / 4 ) );
	}
}

/*
* R_FillStartupBackgroundColor
*
* Fills the window with a color during the initialization.
*/
static void R_FillStartupBackgroundColor( float r, float g, float b ) {
	qglClearColor( r, g, b, 1.0 );
	GLimp_BeginFrame();
	qglClear( GL_COLOR_BUFFER_BIT );
	qglFinish();
	GLimp_EndFrame();
}

static void R_Register( const char *screenshotsPrefix ) {
	char tmp[128];
	const qgl_driverinfo_t *driver;

	r_norefresh = Cvar_Get( "r_norefresh", "0", 0 );
	r_fullbright = Cvar_Get( "r_fullbright", "0", CVAR_LATCH_VIDEO );
	r_lightmap = Cvar_Get( "r_lightmap", "0", 0 );
	r_drawentities = Cvar_Get( "r_drawentities", "1", CVAR_CHEAT );
	r_drawworld = Cvar_Get( "r_drawworld", "1", CVAR_CHEAT );
	r_novis = Cvar_Get( "r_novis", "0", 0 );
	r_nocull = Cvar_Get( "r_nocull", "0", 0 );
	r_lerpmodels = Cvar_Get( "r_lerpmodels", "1", 0 );
	r_speeds = Cvar_Get( "r_speeds", "0", 0 );
	r_drawelements = Cvar_Get( "r_drawelements", "1", 0 );
	r_showtris = Cvar_Get( "r_showtris", "0", CVAR_CHEAT );
	r_lockpvs = Cvar_Get( "r_lockpvs", "0", CVAR_CHEAT );
	r_nobind = Cvar_Get( "r_nobind", "0", 0 );
	r_picmip = Cvar_Get( "r_picmip", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_polyblend = Cvar_Get( "r_polyblend", "1", 0 );

	r_brightness = Cvar_Get( "r_brightness", "0", CVAR_ARCHIVE );
	r_sRGB = Cvar_Get( "r_sRGB", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );

	r_detailtextures = Cvar_Get( "r_detailtextures", "1", CVAR_ARCHIVE );

	r_dynamiclight = Cvar_Get( "r_dynamiclight", "-1", CVAR_ARCHIVE );
	r_subdivisions = Cvar_Get( "r_subdivisions", STR_TOSTR( SUBDIVISIONS_DEFAULT ), CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_shownormals = Cvar_Get( "r_shownormals", "0", CVAR_CHEAT );
	r_draworder = Cvar_Get( "r_draworder", "0", CVAR_CHEAT );

	r_fastsky = Cvar_Get( "r_fastsky", "0", CVAR_ARCHIVE );
	r_portalonly = Cvar_Get( "r_portalonly", "0", 0 );
	r_portalmaps = Cvar_Get( "r_portalmaps", "1", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_portalmaps_maxtexsize = Cvar_Get( "r_portalmaps_maxtexsize", "1024", CVAR_ARCHIVE );

	r_lighting_deluxemapping = Cvar_Get( "r_lighting_deluxemapping", "1", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_lighting_specular = Cvar_Get( "r_lighting_specular", "1", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_lighting_glossintensity = Cvar_Get( "r_lighting_glossintensity", "1.5", CVAR_ARCHIVE );
	r_lighting_glossexponent = Cvar_Get( "r_lighting_glossexponent", "24", CVAR_ARCHIVE );
	r_lighting_ambientscale = Cvar_Get( "r_lighting_ambientscale", "1", 0 );
	r_lighting_directedscale = Cvar_Get( "r_lighting_directedscale", "1", 0 );

	r_lighting_packlightmaps = Cvar_Get( "r_lighting_packlightmaps", "1", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_lighting_maxlmblocksize = Cvar_Get( "r_lighting_maxlmblocksize", "2048", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_lighting_vertexlight = Cvar_Get( "r_lighting_vertexlight", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_lighting_maxglsldlights = Cvar_Get( "r_lighting_maxglsldlights", "16", CVAR_ARCHIVE );
	r_lighting_grayscale = Cvar_Get( "r_lighting_grayscale", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_lighting_intensity = Cvar_Get( "r_lighting_intensity", "1.75", CVAR_ARCHIVE );

	r_offsetmapping = Cvar_Get( "r_offsetmapping", "2", CVAR_ARCHIVE );
	r_offsetmapping_scale = Cvar_Get( "r_offsetmapping_scale", "0.02", CVAR_ARCHIVE );
	r_offsetmapping_reliefmapping = Cvar_Get( "r_offsetmapping_reliefmapping", "0", CVAR_ARCHIVE );

#ifdef CGAMEGETLIGHTORIGIN
	r_shadows = Cvar_Get( "cg_shadows", "1", CVAR_ARCHIVE );
#else
	r_shadows = Cvar_Get( "r_shadows", "0", CVAR_ARCHIVE );
#endif
	r_shadows_alpha = Cvar_Get( "r_shadows_alpha", "0.7", CVAR_ARCHIVE );
	r_shadows_nudge = Cvar_Get( "r_shadows_nudge", "1", CVAR_ARCHIVE );
	r_shadows_projection_distance = Cvar_Get( "r_shadows_projection_distance", "64", CVAR_CHEAT );
	r_shadows_maxtexsize = Cvar_Get( "r_shadows_maxtexsize", "64", CVAR_ARCHIVE );
	r_shadows_pcf = Cvar_Get( "r_shadows_pcf", "1", CVAR_ARCHIVE );
	r_shadows_self_shadow = Cvar_Get( "r_shadows_self_shadow", "0", CVAR_ARCHIVE );
	r_shadows_dither = Cvar_Get( "r_shadows_dither", "0", CVAR_ARCHIVE );

	r_outlines_world = Cvar_Get( "r_outlines_world", "1.8", CVAR_ARCHIVE );
	r_outlines_scale = Cvar_Get( "r_outlines_scale", "1", CVAR_ARCHIVE );
	r_outlines_cutoff = Cvar_Get( "r_outlines_cutoff", "712", CVAR_ARCHIVE );

	r_soft_particles = Cvar_Get( "r_soft_particles", "1", CVAR_ARCHIVE );
	r_soft_particles_scale = Cvar_Get( "r_soft_particles_scale", "0.02", CVAR_ARCHIVE );

	r_hdr = Cvar_Get( "r_hdr", "1", CVAR_ARCHIVE );
	r_hdr_gamma = Cvar_Get( "r_hdr_gamma", "2.2", CVAR_ARCHIVE );
	r_hdr_exposure = Cvar_Get( "r_hdr_exposure", "1.0", CVAR_ARCHIVE );

	r_bloom = Cvar_Get( "r_bloom", "1", CVAR_ARCHIVE );

	r_fxaa = Cvar_Get( "r_fxaa", "0", CVAR_ARCHIVE );
	r_samples = Cvar_Get( "r_samples", "0", CVAR_ARCHIVE );

	r_lodbias = Cvar_Get( "r_lodbias", "0", CVAR_ARCHIVE );
	r_lodscale = Cvar_Get( "r_lodscale", "5.0", CVAR_ARCHIVE );

	r_gamma = Cvar_Get( "r_gamma", "1.0", CVAR_ARCHIVE );
	r_texturemode = Cvar_Get( "r_texturemode", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE );
	r_texturefilter = Cvar_Get( "r_texturefilter", "4", CVAR_ARCHIVE );
	r_texturecompression = Cvar_Get( "r_texturecompression", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	r_stencilbits = Cvar_Get( "r_stencilbits", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );

	r_screenshot_jpeg = Cvar_Get( "r_screenshot_jpeg", "1", CVAR_ARCHIVE );
	r_screenshot_jpeg_quality = Cvar_Get( "r_screenshot_jpeg_quality", "90", CVAR_ARCHIVE );
	r_screenshot_fmtstr = Cvar_Get( "r_screenshot_fmtstr", va_r( tmp, sizeof( tmp ), "%s%%y%%m%%d_%%H%%M%%S", screenshotsPrefix ), CVAR_ARCHIVE );

#if defined( GLX_VERSION ) && !defined( USE_SDL2 )
	r_swapinterval = Cvar_Get( "r_swapinterval", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
#else
	r_swapinterval = Cvar_Get( "r_swapinterval", "0", CVAR_ARCHIVE );
#endif
	r_swapinterval_min = Cvar_Get( "r_swapinterval_min", "0", CVAR_READONLY ); // exposes vsync support to UI

	r_temp1 = Cvar_Get( "r_temp1", "0", 0 );

	r_drawflat = Cvar_Get( "r_drawflat", "0", CVAR_ARCHIVE );
	r_wallcolor = Cvar_Get( "r_wallcolor", "255 255 255", CVAR_ARCHIVE );
	r_floorcolor = Cvar_Get( "r_floorcolor", "255 153 0", CVAR_ARCHIVE );

	// make sure we rebuild our 3D texture after vid_restart
	r_wallcolor->modified = r_floorcolor->modified = true;

	// set to 1 to enable use of the checkerboard texture for missing world and model images
	r_usenotexture = Cvar_Get( "r_usenotexture", "0", CVAR_ARCHIVE );

	r_maxglslbones = Cvar_Get( "r_maxglslbones", STR_TOSTR( MAX_GLSL_UNIFORM_BONES ), CVAR_LATCH_VIDEO );

	r_multithreading = Cvar_Get( "r_multithreading", "0", CVAR_ARCHIVE | CVAR_LATCH_VIDEO );

	r_showShaderCache = Cvar_Get( "r_showShaderCache", "1", CVAR_ARCHIVE );

	gl_cull = Cvar_Get( "gl_cull", "1", 0 );

	driver = QGL_GetDriverInfo();
	if( driver && driver->dllcvarname ) {
		gl_driver = Cvar_Get( driver->dllcvarname, driver->dllname, CVAR_ARCHIVE | CVAR_LATCH_VIDEO );
	} else {
		gl_driver = NULL;
	}

	Cmd_AddCommand( "screenshot", R_ScreenShot_f );
}

static void R_PrintInfo() {
	Com_Printf( "\n" );
	Com_Printf( "GL_VENDOR: %s\n", glConfig.vendorString );
	Com_Printf( "GL_RENDERER: %s\n", glConfig.rendererString );
	Com_Printf( "GL_VERSION: %s\n", glConfig.versionString );
	Com_Printf( "GL_SHADING_LANGUAGE_VERSION: %s\n", glConfig.shadingLanguageVersionString );

	Com_Printf( "GL_EXTENSIONS:\n" );
	GLint numExtensions;
	qglGetIntegerv( GL_NUM_EXTENSIONS, &numExtensions );
	for( int i = 0; i < numExtensions; ++i ) {
		Com_Printf( "%s ", (const char *)qglGetStringi( GL_EXTENSIONS, i ) );
	}
	Com_Printf( "\n" );

	Com_Printf( "GLXW_EXTENSIONS:\n%s\n", qglGetGLWExtensionsString() );

	Com_Printf( "GL_MAX_TEXTURE_SIZE: %i\n", glConfig.maxTextureSize );
	Com_Printf( "GL_MAX_TEXTURE_IMAGE_UNITS: %i\n", glConfig.maxTextureUnits );
	Com_Printf( "GL_MAX_CUBE_MAP_TEXTURE_SIZE: %i\n", glConfig.maxTextureCubemapSize );
	Com_Printf( "GL_MAX_3D_TEXTURE_SIZE: %i\n", glConfig.maxTexture3DSize );
	if( glConfig.ext.texture_array ) {
		Com_Printf( "GL_MAX_ARRAY_TEXTURE_LAYERS: %i\n", glConfig.maxTextureLayers );
	}
	if( glConfig.ext.texture_filter_anisotropic ) {
		Com_Printf( "GL_MAX_TEXTURE_MAX_ANISOTROPY: %i\n", glConfig.maxTextureFilterAnisotropic );
	}
	Com_Printf( "GL_MAX_RENDERBUFFER_SIZE: %i\n", glConfig.maxRenderbufferSize );
	Com_Printf( "GL_MAX_VARYING_FLOATS: %i\n", glConfig.maxVaryingFloats );
	Com_Printf( "GL_MAX_VERTEX_UNIFORM_COMPONENTS: %i\n", glConfig.maxVertexUniformComponents );
	Com_Printf( "GL_MAX_VERTEX_ATTRIBS: %i\n", glConfig.maxVertexAttribs );
	Com_Printf( "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS: %i\n", glConfig.maxFragmentUniformComponents );
	Com_Printf( "GL_MAX_SAMPLES: %i\n", glConfig.maxFramebufferSamples );
	Com_Printf( "\n" );

	Com_Printf( "mode: %ix%i%s\n", glConfig.width, glConfig.height,
				glConfig.fullScreen ? ", fullscreen" : ", windowed" );
	Com_Printf( "picmip: %i\n", r_picmip->integer );
	Com_Printf( "texturemode: %s\n", r_texturemode->string );
	Com_Printf( "anisotropic filtering: %i\n", r_texturefilter->integer );
	Com_Printf( "vertical sync: %s\n", ( r_swapinterval->integer || r_swapinterval_min->integer ) ? "enabled" : "disabled" );

	R_PrintGLExtensionsInfo();
}

/*
* R_GLVersionHash
*/
static unsigned R_GLVersionHash( const char *vendorString,
								 const char *rendererString, const char *versionString ) {
	uint8_t *tmp;
	size_t csize;
	size_t tmp_size, pos;
	unsigned hash;

	tmp_size = strlen( vendorString ) + strlen( rendererString ) +
			   strlen( versionString ) + strlen( ARCH ) + 1;

	pos = 0;
	tmp = (uint8_t *)Q_malloc( tmp_size );

	csize = strlen( vendorString );
	memcpy( tmp + pos, vendorString, csize );
	pos += csize;

	csize = strlen( rendererString );
	memcpy( tmp + pos, rendererString, csize );
	pos += csize;

	csize = strlen( versionString );
	memcpy( tmp + pos, versionString, csize );
	pos += csize;

	// shaders are not compatible between 32-bit and 64-bit at least on Nvidia
	csize = strlen( ARCH );
	memcpy( tmp + pos, ARCH, csize );
	pos += csize;

	hash = COM_SuperFastHash( tmp, tmp_size, tmp_size );

	Q_free( tmp );

	return hash;
}

/*
* R_Init
*/
rserr_t R_Init( const char *applicationName, const char *screenshotPrefix, int startupColor,
				int iconResource, const int *iconXPM,
				void *hinstance, void *wndproc, void *parenthWnd,
				bool verbose ) {
	const qgl_driverinfo_t *driver;
	const char *dllname = "";
	qgl_initerr_t initerr;

	r_verbose = verbose;

	r_postinit = true;

	if( !applicationName ) {
		applicationName = "Qfusion";
	}
	if( !screenshotPrefix ) {
		screenshotPrefix = "";
	}

	R_Register( screenshotPrefix );

	memset( &glConfig, 0, sizeof( glConfig ) );

	// initialize our QGL dynamic bindings
	driver = QGL_GetDriverInfo();
	if( driver ) {
		dllname = driver->dllname;
	}
init_qgl:
	initerr = QGL_Init( gl_driver ? gl_driver->string : dllname );
	if( initerr != qgl_initerr_ok ) {
		QGL_Shutdown();
		Com_Printf( "ref_gl::R_Init() - could not load \"%s\"\n", gl_driver ? gl_driver->string : dllname );

		if( ( initerr == qgl_initerr_invalid_driver ) && gl_driver && strcmp( gl_driver->string, dllname ) ) {
			Cvar_ForceSet( gl_driver->name, dllname );
			goto init_qgl;
		}

		return rserr_invalid_driver;
	}

	// initialize OS-specific parts of OpenGL
	if( !GLimp_Init( applicationName, hinstance, wndproc, parenthWnd, iconResource, iconXPM ) ) {
		QGL_Shutdown();
		return rserr_unknown;
	}

	// FIXME: move this elsewhere?
	glConfig.applicationName = Q_strdup( applicationName );
	glConfig.screenshotPrefix = Q_strdup( screenshotPrefix );
	glConfig.startupColor = startupColor;

	return rserr_ok;
}

/*
* R_PostInit
*/
static rserr_t R_PostInit( void ) {
	int i;
	GLenum glerr;

	if( QGL_PostInit() != qgl_initerr_ok ) {
		return rserr_unknown;
	}

	glConfig.hwGamma = GLimp_GetGammaRamp( GAMMARAMP_STRIDE, &glConfig.gammaRampSize, glConfig.originalGammaRamp );
	if( glConfig.hwGamma ) {
		r_gamma->modified = true;
	}

	/*
	** get our various GL strings
	*/
	glConfig.vendorString = (const char *)qglGetString( GL_VENDOR );
	glConfig.rendererString = (const char *)qglGetString( GL_RENDERER );
	glConfig.versionString = (const char *)qglGetString( GL_VERSION );
	glConfig.shadingLanguageVersionString = (const char *)qglGetString( GL_SHADING_LANGUAGE_VERSION );

	if( !glConfig.vendorString ) {
		glConfig.vendorString = "";
	}
	if( !glConfig.rendererString ) {
		glConfig.rendererString = "";
	}
	if( !glConfig.versionString ) {
		glConfig.versionString = "";
	}
	if( !glConfig.shadingLanguageVersionString ) {
		glConfig.shadingLanguageVersionString = "";
	}

	glConfig.versionHash = R_GLVersionHash( glConfig.vendorString, glConfig.rendererString,
											glConfig.versionString );

	memset( &rsh, 0, sizeof( rsh ) );
	memset( &rf, 0, sizeof( rf ) );

	rsh.registrationSequence = 1;
	rsh.registrationOpen = false;

	rsh.worldModelSequence = 1;

	for( i = 0; i < 256; i++ )
		rsh.sinTableByte[i] = sin( (float)i / 255.0 * M_TWOPI );

	rf.frameTime.average = 1;
	rf.swapInterval = -1;
	rf.speedsMsgLock = QMutex_Create();
	rf.debugSurfaceLock = QMutex_Create();

	R_InitDrawLists();

	if( !R_RegisterGLExtensions() ) {
		QGL_Shutdown();
		return rserr_unknown;
	}

	R_SetSwapInterval( 0, -1 );

	R_FillStartupBackgroundColor( COLOR_R( glConfig.startupColor ) / 255.0f,
								  COLOR_G( glConfig.startupColor ) / 255.0f, COLOR_B( glConfig.startupColor ) / 255.0f );

	R_TextureMode( r_texturemode->string );

	R_AnisotropicFilter( r_texturefilter->integer );

	if( r_verbose ) {
		R_PrintInfo();
	}

	// load and compile GLSL programs
	RP_Init();

	R_InitVBO();

	R_InitImages();

	MaterialCache::init();

	R_InitSkinFiles();

	R_InitModels();

	Scene::Init();

	R_ClearScene();

	R_InitVolatileAssets();

	R_ClearRefInstStack();

	glerr = qglGetError();
	if( glerr != GL_NO_ERROR ) {
		Com_Printf( "glGetError() = 0x%x\n", glerr );
	}

	return rserr_ok;
}

rserr_t R_SetMode( int x, int y, int width, int height, int displayFrequency, bool fullScreen, bool borderless ) {
	rserr_t err = GLimp_SetMode( x, y, width, height, displayFrequency, fullScreen, borderless );
	if( err != rserr_ok ) {
		Com_Printf( "Could not GLimp_SetMode()\n" );
		return err;
	}

	if( r_postinit ) {
		err = R_PostInit();
		r_postinit = false;
	}

	return err;
}

/*
* R_InitVolatileAssets
*/
static void R_InitVolatileAssets( void ) {
	// init volatile data
	R_InitSkeletalCache();
	R_InitCustomColors();

	Scene::Instance()->InitVolatileAssets();

	auto *materialCache = MaterialCache::instance();
	rsh.envShader = materialCache->loadDefaultMaterial( wsw::StringView( "$environment" ), SHADER_TYPE_OPAQUE_ENV );
	rsh.whiteShader = materialCache->loadDefaultMaterial( wsw::StringView( "$whiteimage" ), SHADER_TYPE_2D );
	rsh.emptyFogShader = materialCache->loadDefaultMaterial( wsw::StringView( "$emptyfog" ), SHADER_TYPE_FOG );

	if( !rsh.nullVBO ) {
		rsh.nullVBO = R_InitNullModelVBO();
	} else {
		R_TouchMeshVBO( rsh.nullVBO );
	}

	if( !rsh.postProcessingVBO ) {
		rsh.postProcessingVBO = R_InitPostProcessingVBO();
	} else {
		R_TouchMeshVBO( rsh.postProcessingVBO );
	}
}

/*
* R_DestroyVolatileAssets
*/
static void R_DestroyVolatileAssets( void ) {
	Scene::Instance()->DestroyVolatileAssets();

	// kill volatile data
	R_ShutdownCustomColors();
	R_ShutdownSkeletalCache();
}

/*
* R_BeginRegistration
*/
void R_BeginRegistration( void ) {
	R_DestroyVolatileAssets();

	rsh.registrationSequence++;
	if( !rsh.registrationSequence ) {
		// make sure assumption that an asset is free it its registrationSequence is 0
		// since rsh.registrationSequence never equals 0
		rsh.registrationSequence = 1;
	}
	rsh.registrationOpen = true;

	R_InitVolatileAssets();

	R_DeferDataSync();

	R_DataSync();
}

/*
* R_EndRegistration
*/
void R_EndRegistration( void ) {
	if( rsh.registrationOpen == false ) {
		return;
	}

	rsh.registrationOpen = false;

	R_FreeUnusedModels();
	R_FreeUnusedVBOs();
	R_FreeUnusedSkinFiles();

	MaterialCache::instance()->freeUnusedMaterials();

	R_FreeUnusedImages();

	R_DeferDataSync();

	R_DataSync();
}

/*
* R_Shutdown
*/
void R_Shutdown( bool verbose ) {
	Cmd_RemoveCommand( "screenshot" );

	// free shaders, models, etc.

	R_DestroyVolatileAssets();

	Scene::Shutdown();

	R_ShutdownModels();

	R_ShutdownSkinFiles();

	R_ShutdownVBO();

	MaterialCache::shutdown();

	R_ShutdownImages();

	// destroy compiled GLSL programs
	RP_Shutdown();

	// restore original gamma
	if( glConfig.hwGamma ) {
		GLimp_SetGammaRamp( GAMMARAMP_STRIDE, glConfig.gammaRampSize, glConfig.originalGammaRamp );
	}

	QMutex_Destroy( &rf.speedsMsgLock );
	QMutex_Destroy( &rf.debugSurfaceLock );

	// shut down OS specific OpenGL stuff like contexts, etc.
	GLimp_Shutdown();

	// shutdown our QGL subsystem
	QGL_Shutdown();
}
