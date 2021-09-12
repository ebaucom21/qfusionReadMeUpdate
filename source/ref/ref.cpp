/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2013 Victor Luchits

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

// r_main.c

#include "local.h"
#include "program.h"
#include "materiallocal.h"
#include "scene.h"
#include "../qcommon/hash.h"
#include "../qcommon/singletonholder.h"
#include <algorithm>

r_globals_t rf;

mapconfig_t mapConfig;

refinst_t rn;

r_scene_t rsc;

#ifdef QGL_USE_CALL_WRAPPERS

QGLFunc *QGLFunc::listHead = nullptr;

#ifdef QGL_VALIDATE_CALLS

const char *QGLFunc::checkForError() {
	// Never try to fetch errors for qglGetError itself
	if ( !strcmp( name, "qglGetError" ) ) {
		return nullptr;
	}

	// Hacks: never try to fetch errors for qglBufferData().
	// This is currently the only routine that has an additional custom error handling logic.
	// We could try using something like `qglBufferData.unchecked().operator()(...`
	// but this loses a structural compatibility with plain (unwrapped functions) code
	if ( !strcmp( name, "qglBufferData" ) ) {
		return nullptr;
	}

	// Get the underlying raw function pointer
	typedef GLenum ( APIENTRY *GetErrorFn )();
	switch( ( (GetErrorFn)qglGetError.address )() ) {
		case GL_NO_ERROR:
			return nullptr;
		case GL_INVALID_ENUM:
			return "GL_INVALID_ENUM";
		case GL_INVALID_VALUE:
			return "GL_INVALID_VALUE";
		case GL_INVALID_OPERATION:
			return "GL_INVALID_OPERATION";
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			return "GL_INVALID_FRAMEBUFFER_OPERATION";
		case GL_OUT_OF_MEMORY:
			return "GL_OUT_OF_MEMORY";
		case GL_STACK_UNDERFLOW:
			return "GL_STACK_UNDERFLOW";
		case GL_STACK_OVERFLOW:
			return "GL_STACK_OVERFLOW";
		default:
			return "UNKNOWN";
	}
}

#endif
#endif

void R_TransformBounds( const vec3_t origin, const mat3_t axis, vec3_t mins, vec3_t maxs, vec3_t bbox[8] ) {
	int i;
	vec3_t tmp;
	mat3_t axis_;

	Matrix3_Transpose( axis, axis_ );   // switch row-column order

	// rotate local bounding box and compute the full bounding box
	for( i = 0; i < 8; i++ ) {
		vec_t *corner = bbox[i];

		corner[0] = ( ( i & 1 ) ? mins[0] : maxs[0] );
		corner[1] = ( ( i & 2 ) ? mins[1] : maxs[1] );
		corner[2] = ( ( i & 4 ) ? mins[2] : maxs[2] );

		Matrix3_TransformVector( axis_, corner, tmp );
		VectorAdd( tmp, origin, corner );
	}
}

void R_TransformForWorld( void ) {
	Matrix4_Identity( rn.objectMatrix );
	Matrix4_Copy( rn.cameraMatrix, rn.modelviewMatrix );

	RB_LoadObjectMatrix( mat4x4_identity );
}

void R_TranslateForEntity( const entity_t *e ) {
	Matrix4_Identity( rn.objectMatrix );

	rn.objectMatrix[0] = e->scale;
	rn.objectMatrix[5] = e->scale;
	rn.objectMatrix[10] = e->scale;
	rn.objectMatrix[12] = e->origin[0];
	rn.objectMatrix[13] = e->origin[1];
	rn.objectMatrix[14] = e->origin[2];

	RB_LoadObjectMatrix( rn.objectMatrix );
}

void R_TransformForEntity( const entity_t *e ) {
	if( e->rtype != RT_MODEL ) {
		R_TransformForWorld();
		return;
	}
	if( e == rsc.worldent ) {
		R_TransformForWorld();
		return;
	}

	if( e->scale != 1.0f ) {
		rn.objectMatrix[0] = e->axis[0] * e->scale;
		rn.objectMatrix[1] = e->axis[1] * e->scale;
		rn.objectMatrix[2] = e->axis[2] * e->scale;
		rn.objectMatrix[4] = e->axis[3] * e->scale;
		rn.objectMatrix[5] = e->axis[4] * e->scale;
		rn.objectMatrix[6] = e->axis[5] * e->scale;
		rn.objectMatrix[8] = e->axis[6] * e->scale;
		rn.objectMatrix[9] = e->axis[7] * e->scale;
		rn.objectMatrix[10] = e->axis[8] * e->scale;
	} else {
		rn.objectMatrix[0] = e->axis[0];
		rn.objectMatrix[1] = e->axis[1];
		rn.objectMatrix[2] = e->axis[2];
		rn.objectMatrix[4] = e->axis[3];
		rn.objectMatrix[5] = e->axis[4];
		rn.objectMatrix[6] = e->axis[5];
		rn.objectMatrix[8] = e->axis[6];
		rn.objectMatrix[9] = e->axis[7];
		rn.objectMatrix[10] = e->axis[8];
	}

	rn.objectMatrix[3] = 0;
	rn.objectMatrix[7] = 0;
	rn.objectMatrix[11] = 0;
	rn.objectMatrix[12] = e->origin[0];
	rn.objectMatrix[13] = e->origin[1];
	rn.objectMatrix[14] = e->origin[2];
	rn.objectMatrix[15] = 1.0;

	Matrix4_MultiplyFast( rn.cameraMatrix, rn.objectMatrix, rn.modelviewMatrix );

	RB_LoadObjectMatrix( rn.objectMatrix );
}

mfog_t *R_FogForBounds( const vec3_t mins, const vec3_t maxs ) {
	unsigned int i, j;
	mfog_t *fog;

	if( !rsh.worldModel || ( rn.refdef.rdflags & RDF_NOWORLDMODEL ) || !rsh.worldBrushModel->numfogs ) {
		return NULL;
	}
	if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
		return NULL;
	}
	if( rsh.worldBrushModel->globalfog ) {
		return rsh.worldBrushModel->globalfog;
	}

	fog = rsh.worldBrushModel->fogs;
	for( i = 0; i < rsh.worldBrushModel->numfogs; i++, fog++ ) {
		if( !fog->shader ) {
			continue;
		}

		for( j = 0; j < 3; j++ ) {
			if( mins[j] >= fog->maxs[j] ) {
				break;
			}
			if( maxs[j] <= fog->mins[j] ) {
				break;
			}
		}

		if( j == 3 ) {
			return fog;
		}
	}

	return NULL;
}

mfog_t *R_FogForSphere( const vec3_t centre, const float radius ) {
	int i;
	vec3_t mins, maxs;

	for( i = 0; i < 3; i++ ) {
		mins[i] = centre[i] - radius;
		maxs[i] = centre[i] + radius;
	}

	return R_FogForBounds( mins, maxs );
}

bool R_CompletelyFogged( const mfog_t *fog, vec3_t origin, float radius ) {
	// note that fog->distanceToEye < 0 is always true if
	// globalfog is not NULL and we're inside the world boundaries
	if( fog && fog->shader && fog == rn.fog_eye ) {
		float vpnDist = ( ( rn.viewOrigin[0] - origin[0] ) * rn.viewAxis[AXIS_FORWARD + 0] +
						  ( rn.viewOrigin[1] - origin[1] ) * rn.viewAxis[AXIS_FORWARD + 1] +
						  ( rn.viewOrigin[2] - origin[2] ) * rn.viewAxis[AXIS_FORWARD + 2] );
		return ( ( vpnDist + radius ) / fog->shader->fog_dist ) < -1;
	}

	return false;
}

int R_LODForSphere( const vec3_t origin, float radius ) {
	float dist;
	int lod;

	dist = DistanceFast( origin, rn.lodOrigin );
	dist *= rn.lod_dist_scale_for_fov;

	lod = (int)( dist / radius );
	if( r_lodscale->integer ) {
		lod /= r_lodscale->integer;
	}
	lod += r_lodbias->integer;

	if( lod < 1 ) {
		return 0;
	}
	return lod;
}

void R_InitCustomColors( void ) {
	memset( rsh.customColors, 255, sizeof( rsh.customColors ) );
}

void R_SetCustomColor( int num, int r, int g, int b ) {
	if( num < 0 || num >= NUM_CUSTOMCOLORS ) {
		return;
	}
	Vector4Set( rsh.customColors[num], (uint8_t)r, (uint8_t)g, (uint8_t)b, 255 );
}

int R_GetCustomColor( int num ) {
	if( num < 0 || num >= NUM_CUSTOMCOLORS ) {
		return COLOR_RGBA( 255, 255, 255, 255 );
	}
	return *(int *)rsh.customColors[num];
}

void R_ShutdownCustomColors( void ) {
	memset( rsh.customColors, 255, sizeof( rsh.customColors ) );
}

static drawSurfaceType_t spriteDrawSurf = ST_SPRITE;

void R_BatchSpriteSurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, unsigned int shadowBits, drawSurfaceType_t *drawSurf ) {
	int i;
	vec3_t point;
	vec3_t v_left, v_up;
	elem_t elems[6] = { 0, 1, 2, 0, 2, 3 };
	vec4_t xyz[4] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
	vec4_t normals[4] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
	byte_vec4_t colors[4];
	vec2_t texcoords[4] = { {0, 1}, {0, 0}, {1,0}, {1,1} };
	mesh_t mesh;
	float radius = e->radius * e->scale;
	float rotation = e->rotation;

	if( rotation ) {
		RotatePointAroundVector( v_left, &rn.viewAxis[AXIS_FORWARD], &rn.viewAxis[AXIS_RIGHT], rotation );
		CrossProduct( &rn.viewAxis[AXIS_FORWARD], v_left, v_up );
	} else {
		VectorCopy( &rn.viewAxis[AXIS_RIGHT], v_left );
		VectorCopy( &rn.viewAxis[AXIS_UP], v_up );
	}

	if( rn.renderFlags & ( RF_MIRRORVIEW | RF_FLIPFRONTFACE ) ) {
		VectorInverse( v_left );
	}

	VectorMA( e->origin, -radius, v_up, point );
	VectorMA( point, radius, v_left, xyz[0] );
	VectorMA( point, -radius, v_left, xyz[3] );

	VectorMA( e->origin, radius, v_up, point );
	VectorMA( point, radius, v_left, xyz[1] );
	VectorMA( point, -radius, v_left, xyz[2] );

	for( i = 0; i < 4; i++ ) {
		VectorNegate( &rn.viewAxis[AXIS_FORWARD], normals[i] );
		Vector4Copy( e->color, colors[i] );
	}

	mesh.elems = elems;
	mesh.numElems = 6;
	mesh.numVerts = 4;
	mesh.xyzArray = xyz;
	mesh.normalsArray = normals;
	mesh.lmstArray[0] = NULL;
	mesh.lmlayersArray[0] = NULL;
	mesh.stArray = texcoords;
	mesh.colorsArray[0] = colors;
	mesh.colorsArray[1] = NULL;
	mesh.sVectorsArray = NULL;

	RB_AddDynamicMesh( e, shader, fog, portalSurface, 0, &mesh, GL_TRIANGLES, 0.0f, 0.0f );
}

static bool R_AddSpriteToDrawList( const entity_t *e ) {
	float dist;

	if( e->flags & RF_NOSHADOW ) {
		if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
			return false;
		}
	}

	if( e->radius <= 0 || e->customShader == NULL || e->scale <= 0 ) {
		return false;
	}

	dist =
		( e->origin[0] - rn.refdef.vieworg[0] ) * rn.viewAxis[AXIS_FORWARD + 0] +
		( e->origin[1] - rn.refdef.vieworg[1] ) * rn.viewAxis[AXIS_FORWARD + 1] +
		( e->origin[2] - rn.refdef.vieworg[2] ) * rn.viewAxis[AXIS_FORWARD + 2];
	if( dist <= 0 ) {
		return false; // cull it because we don't want to sort unneeded things

	}
	if( !R_AddSurfToDrawList( rn.meshlist, e, R_FogForSphere( e->origin, e->radius ),
							  e->customShader, dist, 0, NULL, &spriteDrawSurf ) ) {
		return false;
	}

	return true;
}

static drawSurfaceType_t nullDrawSurf = ST_NULLMODEL;

mesh_vbo_t *R_InitNullModelVBO( void ) {
	float scale = 15;
	vec4_t xyz[6] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
	vec4_t normals[6] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
	byte_vec4_t colors[6];
	vec2_t texcoords[6] = { {0,0}, {0,1}, {0,0}, {0,1}, {0,0}, {0,1} };
	elem_t elems[6] = { 0, 1, 2, 3, 4, 5 };
	mesh_t mesh;
	vattribmask_t vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT | VATTRIB_COLOR0_BIT;
	mesh_vbo_t *vbo;

	vbo = R_CreateMeshVBO( &rf, 6, 6, 0, vattribs, VBO_TAG_NONE, vattribs );
	if( !vbo ) {
		return NULL;
	}

	VectorSet( xyz[0], 0, 0, 0 );
	VectorSet( xyz[1], scale, 0, 0 );
	Vector4Set( colors[0], 255, 0, 0, 127 );
	Vector4Set( colors[1], 255, 0, 0, 127 );

	VectorSet( xyz[2], 0, 0, 0 );
	VectorSet( xyz[3], 0, scale, 0 );
	Vector4Set( colors[2], 0, 255, 0, 127 );
	Vector4Set( colors[3], 0, 255, 0, 127 );

	VectorSet( xyz[4], 0, 0, 0 );
	VectorSet( xyz[5], 0, 0, scale );
	Vector4Set( colors[4], 0, 0, 255, 127 );
	Vector4Set( colors[5], 0, 0, 255, 127 );

	memset( &mesh, 0, sizeof( mesh ) );
	mesh.numVerts = 6;
	mesh.xyzArray = xyz;
	mesh.normalsArray = normals;
	mesh.stArray = texcoords;
	mesh.colorsArray[0] = colors;
	mesh.numElems = 6;
	mesh.elems = elems;

	R_UploadVBOVertexData( vbo, 0, vattribs, &mesh );
	R_UploadVBOElemData( vbo, 0, 0, &mesh );

	return vbo;
}

void R_DrawNullSurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, unsigned int shadowBits, drawSurfaceType_t *drawSurf ) {
	assert( rsh.nullVBO != NULL );
	if( !rsh.nullVBO ) {
		return;
	}

	RB_BindVBO( rsh.nullVBO->index, GL_LINES );

	RB_DrawElements( 0, 6, 0, 6 );
}

static bool R_AddNullSurfToDrawList( const entity_t *e ) {
	if( !R_AddSurfToDrawList( rn.meshlist, e, R_FogForSphere( e->origin, 0.1f ),
							  rsh.whiteShader, 0, 0, NULL, &nullDrawSurf ) ) {
		return false;
	}

	return true;
}

static vec4_t pic_xyz[4] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
static vec4_t pic_normals[4] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
static vec2_t pic_st[4];
static byte_vec4_t pic_colors[4];
static elem_t pic_elems[6] = { 0, 1, 2, 0, 2, 3 };
static mesh_t pic_mesh = { 4, 6, pic_elems, pic_xyz, pic_normals, NULL, pic_st, { 0, 0, 0, 0 }, { 0 }, { pic_colors, pic_colors, pic_colors, pic_colors }, NULL, NULL };

/*
* R_Set2DMode
*
* Note that this sets the viewport to size of the active framebuffer.
*/
void R_Set2DMode( bool enable ) {
	int width, height;

	width = rf.frameBufferWidth;
	height = rf.frameBufferHeight;

	if( rf.in2D == true && enable == true && width == rf.width2D && height == rf.height2D ) {
		return;
	} else if( rf.in2D == false && enable == false ) {
		return;
	}

	rf.in2D = enable;

	if( enable ) {
		rf.width2D = width;
		rf.height2D = height;

		Matrix4_OrthogonalProjection( 0, width, height, 0, -99999, 99999, rn.projectionMatrix );
		Matrix4_Copy( mat4x4_identity, rn.modelviewMatrix );
		Matrix4_Copy( rn.projectionMatrix, rn.cameraProjectionMatrix );

		// set 2D virtual screen size
		RB_Scissor( 0, 0, width, height );
		RB_Viewport( 0, 0, width, height );

		RB_LoadProjectionMatrix( rn.projectionMatrix );
		RB_LoadCameraMatrix( mat4x4_identity );
		RB_LoadObjectMatrix( mat4x4_identity );

		RB_SetShaderStateMask( ~0, GLSTATE_NO_DEPTH_TEST );

		RB_SetRenderFlags( 0 );
	} else {
		// render previously batched 2D geometry, if any
		RB_FlushDynamicMeshes();

		RB_SetShaderStateMask( ~0, 0 );
	}
}

void R_DrawRotatedStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2, float angle,
							  const vec4_t color, const shader_t *shader ) {
	int bcolor;

	if( !shader ) {
		return;
	}

	// lower-left
	Vector2Set( pic_xyz[0], x, y );
	Vector2Set( pic_st[0], s1, t1 );
	Vector4Set( pic_colors[0],
				bound( 0, ( int )( color[0] * 255.0f ), 255 ),
				bound( 0, ( int )( color[1] * 255.0f ), 255 ),
				bound( 0, ( int )( color[2] * 255.0f ), 255 ),
				bound( 0, ( int )( color[3] * 255.0f ), 255 ) );
	bcolor = *(int *)pic_colors[0];

	// lower-right
	Vector2Set( pic_xyz[1], x + w, y );
	Vector2Set( pic_st[1], s2, t1 );
	*(int *)pic_colors[1] = bcolor;

	// upper-right
	Vector2Set( pic_xyz[2], x + w, y + h );
	Vector2Set( pic_st[2], s2, t2 );
	*(int *)pic_colors[2] = bcolor;

	// upper-left
	Vector2Set( pic_xyz[3], x, y + h );
	Vector2Set( pic_st[3], s1, t2 );
	*(int *)pic_colors[3] = bcolor;

	// rotated image
	angle = anglemod( angle );
	if( angle ) {
		int j;
		float sint, cost;

		angle = DEG2RAD( angle );
		sint = sin( angle );
		cost = cos( angle );

		for( j = 0; j < 4; j++ ) {
			t1 = pic_st[j][0];
			t2 = pic_st[j][1];
			pic_st[j][0] = cost * ( t1 - 0.5f ) - sint * ( t2 - 0.5f ) + 0.5f;
			pic_st[j][1] = cost * ( t2 - 0.5f ) + sint * ( t1 - 0.5f ) + 0.5f;
		}
	}

	RB_AddDynamicMesh( NULL, shader, NULL, NULL, 0, &pic_mesh, GL_TRIANGLES, 0.0f, 0.0f );
}

void R_DrawStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
					   const vec4_t color, const shader_t *shader ) {
	R_DrawRotatedStretchPic( x, y, w, h, s1, t1, s2, t2, 0, color, shader );
}

void R_DrawExternalTextureOverlay( GLuint externalTexNum ) {
	Texture *texture = TextureCache::instance()->wrapUITextureHandle( externalTexNum );

	R_DrawStretchQuick( 0, 0, rf.width2D, rf.height2D, 0.0f, 1.0f, 1.0f, 0.0f, colorWhite,
		GLSL_PROGRAM_TYPE_NONE, texture, GLSTATE_SRCBLEND_SRC_ALPHA | GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA );
}

static const wsw::HashedStringView kBuiltinImage( "$builtinimage" );

void R_DrawStretchQuick( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
						 const vec4_t color, int program_type, Texture *image, int blendMask ) {
	static shaderpass_t p;
	static shader_t s;

	s.vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT;
	s.sort = SHADER_SORT_NEAREST;
	s.numpasses = 1;
	s.name = kBuiltinImage;
	s.passes = &p;

	p.rgbgen.type = RGB_GEN_CONST;
	VectorCopy( color, p.rgbgen.args );
	p.alphagen.type = ALPHA_GEN_CONST;
	p.alphagen.args[0] = color[3];
	p.tcgen = TC_GEN_BASE;
	p.images[0] = image;
	p.flags = blendMask;
	p.program_type = program_type;

	R_DrawRotatedStretchPic( x, y, w, h, s1, t1, s2, t2, 0, color, &s );

	RB_FlushDynamicMeshes();
}

void R_BindFrameBufferObject( int object ) {
	const int width = glConfig.width;
	const int height = glConfig.height;

	rf.frameBufferWidth = width;
	rf.frameBufferHeight = height;

	RB_BindFrameBufferObject();

	RB_Viewport( rn.viewport[0], rn.viewport[1], rn.viewport[2], rn.viewport[3] );
	RB_Scissor( rn.scissor[0], rn.scissor[1], rn.scissor[2], rn.scissor[3] );
}

static void R_PolyBlend( void ) {
	if( !r_polyblend->integer ) {
		return;
	}
	if( rsc.refdef.blend[3] < 0.01f ) {
		return;
	}

	R_Set2DMode( true );
	R_DrawStretchPic( 0, 0, rf.frameBufferWidth, rf.frameBufferHeight, 0, 0, 1, 1, rsc.refdef.blend, rsh.whiteShader );
	RB_FlushDynamicMeshes();
}

static void R_ApplyBrightness( void ) {
	float c;
	vec4_t color;

	c = r_brightness->value;
	if( c < 0.005 ) {
		return;
	} else if( c > 1.0 ) {
		c = 1.0;
	}

	color[0] = color[1] = color[2] = c, color[3] = 1;

	R_Set2DMode( true );
	auto *const whiteTexture = TextureCache::instance()->whiteTexture();
	R_DrawStretchQuick( 0, 0, rf.frameBufferWidth, rf.frameBufferHeight, 0, 0, 1, 1,
						color, GLSL_PROGRAM_TYPE_NONE, whiteTexture, GLSTATE_SRCBLEND_ONE | GLSTATE_DSTBLEND_ONE );
}

mesh_vbo_t *R_InitPostProcessingVBO( void ) {
	vec4_t xyz[4] = { {0,0,0,1}, {1,0,0,1}, {1,1,0,1}, {0,1,0,1} };
	vec2_t texcoords[4] = { {0,1}, {1,1}, {1,0}, {0,0} };
	elem_t elems[6] = { 0, 1, 2, 0, 2, 3 };
	mesh_t mesh;
	vattribmask_t vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT;
	mesh_vbo_t *vbo;

	vbo = R_CreateMeshVBO( &rf, 4, 6, 0, vattribs, VBO_TAG_NONE, vattribs );
	if( !vbo ) {
		return NULL;
	}

	memset( &mesh, 0, sizeof( mesh ) );
	mesh.numVerts = 4;
	mesh.xyzArray = xyz;
	mesh.stArray = texcoords;
	mesh.numElems = 6;
	mesh.elems = elems;

	R_UploadVBOVertexData( vbo, 0, vattribs, &mesh );
	R_UploadVBOElemData( vbo, 0, 0, &mesh );

	return vbo;
}

float R_DefaultFarClip( void ) {
	float farclip_dist;

	if( rn.refdef.rdflags & RDF_NOWORLDMODEL ) {
		farclip_dist = 1024;
	} else if( rsh.worldModel && rsh.worldBrushModel->globalfog ) {
		farclip_dist = rsh.worldBrushModel->globalfog->shader->fog_dist;
	} else {
		farclip_dist = Z_NEAR;
	}

	return std::max( Z_NEAR, farclip_dist ) + Z_BIAS;
}

static float R_SetVisFarClip( void ) {
	int i;
	float dist;
	vec3_t tmp;
	float farclip_dist;

	if( !rsh.worldModel || ( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		return rn.visFarClip;
	}

	rn.visFarClip = 0;

	farclip_dist = 0;
	for( i = 0; i < 8; i++ ) {
		tmp[0] = ( ( i & 1 ) ? rn.visMins[0] : rn.visMaxs[0] );
		tmp[1] = ( ( i & 2 ) ? rn.visMins[1] : rn.visMaxs[1] );
		tmp[2] = ( ( i & 4 ) ? rn.visMins[2] : rn.visMaxs[2] );

		dist = DistanceSquared( tmp, rn.viewOrigin );
		farclip_dist = std::max( farclip_dist, dist );
	}

	rn.visFarClip = sqrt( farclip_dist );
	return rn.visFarClip;
}

static void R_SetFarClip( void ) {
	float farclip;

	if( rn.refdef.rdflags & RDF_NOWORLDMODEL ) {
		rn.farClip = R_DefaultFarClip();
		return;
	}

	farclip = R_SetVisFarClip();

	if( rsh.worldBrushModel->globalfog ) {
		float fogdist = rsh.worldBrushModel->globalfog->shader->fog_dist;
		if( farclip > fogdist ) {
			farclip = fogdist;
		} else {
			rn.clipFlags &= ~16;
		}
	}

	rn.farClip = std::max( Z_NEAR, farclip ) + Z_BIAS;
}

static void R_SetupFrame( void ) {
	int viewcluster;
	int viewarea;

	// build the transformation matrix for the given view angles
	VectorCopy( rn.refdef.vieworg, rn.viewOrigin );
	Matrix3_Copy( rn.refdef.viewaxis, rn.viewAxis );

	rn.lod_dist_scale_for_fov = tan( rn.refdef.fov_x * ( M_PI / 180 ) * 0.5f );

	// current viewcluster
	if( !( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		mleaf_t *leaf;

		VectorCopy( rsh.worldModel->mins, rn.visMins );
		VectorCopy( rsh.worldModel->maxs, rn.visMaxs );

		leaf = Mod_PointInLeaf( rn.pvsOrigin, rsh.worldModel );
		viewcluster = leaf->cluster;
		viewarea = leaf->area;

		if( rf.worldModelSequence != rsh.worldModelSequence ) {
			rf.frameCount = 0;
			rf.worldModelSequence = rsh.worldModelSequence;

			if( !rf.numWorldSurfVis ) {
				rf.worldSurfVis = (unsigned char *)Q_malloc( rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
				rf.worldSurfFullVis = (unsigned char *)Q_malloc( rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
			} else if( rf.numWorldSurfVis < rsh.worldBrushModel->numsurfaces ) {
				rf.worldSurfVis = (unsigned char *)Q_realloc( (void *)rf.worldSurfVis, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
				rf.worldSurfFullVis = (unsigned char *)Q_realloc( (void *)rf.worldSurfFullVis, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
			}
			rf.numWorldSurfVis = rsh.worldBrushModel->numsurfaces;

			if( !rf.numWorldLeafVis ) {
				rf.worldLeafVis = (unsigned char *)Q_malloc( rsh.worldBrushModel->numvisleafs * sizeof( *rf.worldLeafVis ) );
			} else if( rf.numWorldLeafVis < rsh.worldBrushModel->numvisleafs ) {
				rf.worldLeafVis = (unsigned char *)Q_realloc( (void *)rf.worldLeafVis, rsh.worldBrushModel->numvisleafs * sizeof( *rf.worldLeafVis ) );
			}
			rf.numWorldLeafVis = rsh.worldBrushModel->numvisleafs;

			if( !rf.numWorldDrawSurfVis ) {
				rf.worldDrawSurfVis = (unsigned char *)Q_malloc( rsh.worldBrushModel->numDrawSurfaces * sizeof( *rf.worldDrawSurfVis ) );
			} else if( rf.numWorldDrawSurfVis < rsh.worldBrushModel->numDrawSurfaces ) {
				rf.worldDrawSurfVis = (unsigned char *)Q_realloc( (void *)rf.worldDrawSurfVis, rsh.worldBrushModel->numDrawSurfaces * sizeof( *rf.worldDrawSurfVis ) );
			}
			rf.numWorldDrawSurfVis = rsh.worldBrushModel->numDrawSurfaces;
		}
	} else {
		viewcluster = -1;
		viewarea = -1;
	}

	rf.viewcluster = viewcluster;
	rf.viewarea = viewarea;

	rf.frameCount++;
}

static void R_SetupViewMatrices( void ) {
	refdef_t *rd = &rn.refdef;

	Matrix4_Modelview( rd->vieworg, rd->viewaxis, rn.cameraMatrix );

	if( rd->rdflags & RDF_USEORTHO ) {
		Matrix4_OrthogonalProjection( -rd->ortho_x, rd->ortho_x, -rd->ortho_y, rd->ortho_y,
									  -rn.farClip, rn.farClip, rn.projectionMatrix );
	} else {
		Matrix4_PerspectiveProjection( rd->fov_x, rd->fov_y, Z_NEAR, rn.farClip, rn.projectionMatrix );
	}

	if( rd->rdflags & RDF_FLIPPED ) {
		rn.projectionMatrix[0] = -rn.projectionMatrix[0];
		rn.renderFlags |= RF_FLIPFRONTFACE;
	}

	Matrix4_Multiply( rn.projectionMatrix, rn.cameraMatrix, rn.cameraProjectionMatrix );
}

static void R_Clear( int bitMask ) {
	int fbo;
	int bits;
	vec4_t envColor;
	bool clearColor = false;
	bool rgbShadow = ( rn.renderFlags & ( RF_SHADOWMAPVIEW | RF_SHADOWMAPVIEW_RGB ) ) == ( RF_SHADOWMAPVIEW | RF_SHADOWMAPVIEW_RGB );
	bool depthPortal = ( rn.renderFlags & ( RF_MIRRORVIEW | RF_PORTALVIEW ) ) != 0 && ( rn.renderFlags & RF_PORTAL_CAPTURE ) == 0;

	if( rgbShadow ) {
		clearColor = true;
		Vector4Set( envColor, 1, 1, 1, 1 );
	} else if( rn.refdef.rdflags & RDF_NOWORLDMODEL ) {
		clearColor = rn.renderTarget != 0;
		Vector4Set( envColor, 1, 1, 1, 0 );
	} else {
		clearColor = !rn.numDepthPortalSurfaces || R_FASTSKY();
		if( rsh.worldBrushModel && rsh.worldBrushModel->globalfog && rsh.worldBrushModel->globalfog->shader ) {
			Vector4Scale( rsh.worldBrushModel->globalfog->shader->fog_color, 1.0 / 255.0, envColor );
		} else {
			Vector4Scale( mapConfig.environmentColor, 1.0 / 255.0, envColor );
		}
	}

	bits = 0;
	if( !depthPortal ) {
		bits |= GL_DEPTH_BUFFER_BIT;
	}
	if( clearColor ) {
		bits |= GL_COLOR_BUFFER_BIT;
	}

	bits &= bitMask;

	RB_Clear( bits, envColor[0], envColor[1], envColor[2], envColor[3] );
}

static void R_SetupGL( void ) {
	RB_Scissor( rn.scissor[0], rn.scissor[1], rn.scissor[2], rn.scissor[3] );
	RB_Viewport( rn.viewport[0], rn.viewport[1], rn.viewport[2], rn.viewport[3] );

	if( rn.renderFlags & RF_CLIPPLANE ) {
		cplane_t *p = &rn.clipPlane;
		Matrix4_ObliqueNearClipping( p->normal, -p->dist, rn.cameraMatrix, rn.projectionMatrix );
	}

	RB_SetZClip( Z_NEAR, rn.farClip );

	RB_SetCamera( rn.viewOrigin, rn.viewAxis );

	RB_SetLightParams( rn.refdef.minLight, ( rn.refdef.rdflags & RDF_NOWORLDMODEL ) != 0, rn.hdrExposure );

	RB_SetRenderFlags( rn.renderFlags );

	RB_LoadProjectionMatrix( rn.projectionMatrix );

	RB_LoadCameraMatrix( rn.cameraMatrix );

	RB_LoadObjectMatrix( mat4x4_identity );

	if( rn.renderFlags & RF_FLIPFRONTFACE ) {
		RB_FlipFrontFace();
	}

	if( ( rn.renderFlags & RF_SHADOWMAPVIEW ) ) {
		RB_SetShaderStateMask( ~0, GLSTATE_NO_COLORWRITE );
	}
}

static void R_EndGL( void ) {
	if( ( rn.renderFlags & RF_SHADOWMAPVIEW ) ) {
		RB_SetShaderStateMask( ~0, 0 );
	}

	if( rn.renderFlags & RF_FLIPFRONTFACE ) {
		RB_FlipFrontFace();
	}
}

static void R_DrawEntities( void ) {
	unsigned int i;
	entity_t *e;

	if( rn.renderFlags & RF_ENVVIEW ) {
		for( i = 0; i < rsc.numBmodelEntities; i++ ) {
			e = rsc.bmodelEntities[i];
			if( !r_lerpmodels->integer ) {
				e->backlerp = 0;
			}
			e->outlineHeight = rsc.worldent->outlineHeight;
			Vector4Copy( rsc.worldent->outlineRGBA, e->outlineColor );
			R_AddBrushModelToDrawList( e );
		}
		return;
	}

	for( i = rsc.numLocalEntities; i < rsc.numEntities; i++ ) {
		e = R_NUM2ENT( i );

		if( !r_lerpmodels->integer ) {
			e->backlerp = 0;
		}

		switch( e->rtype ) {
			case RT_MODEL:
				if( !e->model ) {
					R_AddNullSurfToDrawList( e );
					continue;
				}

				switch( e->model->type ) {
					case mod_alias:
						R_AddAliasModelToDrawList( e );
						break;
					case mod_skeletal:
						R_AddSkeletalModelToDrawList( e );
						break;
					case mod_brush:
						e->outlineHeight = rsc.worldent->outlineHeight;
						Vector4Copy( rsc.worldent->outlineRGBA, e->outlineColor );
						R_AddBrushModelToDrawList( e );
					default:
						break;
				}
				break;
			case RT_SPRITE:
				R_AddSpriteToDrawList( e );
				break;
			default:
				break;
		}
	}
}

static void R_BindRefInstFBO( void ) {
	int fbo = rn.renderTarget;
	R_BindFrameBufferObject( fbo );
}

void R_RenderView( const refdef_t *fd ) {
	int msec = 0;
	bool shadowMap = rn.renderFlags & RF_SHADOWMAPVIEW ? true : false;

	rn.refdef = *fd;

	// load view matrices with default far clip value
	R_SetupViewMatrices();

	rn.fog_eye = NULL;
	rn.hdrExposure = 1;

	rn.dlightBits = 0;

	rn.numPortalSurfaces = 0;
	rn.numDepthPortalSurfaces = 0;
	rn.skyportalSurface = NULL;

	ClearBounds( rn.visMins, rn.visMaxs );

	if( r_novis->integer ) {
		rn.renderFlags |= RF_NOVIS;
	}

	if( r_lightmap->integer ) {
		rn.renderFlags |= RF_LIGHTMAP;
	}

	if( r_drawflat->integer ) {
		rn.renderFlags |= RF_DRAWFLAT;
	}

	R_ClearDrawList( rn.meshlist );

	R_ClearDrawList( rn.portalmasklist );

	if( !rsh.worldModel && !( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}

	R_SetupFrame();

	R_SetupFrustum( &rn.refdef, rn.farClip, rn.frustum );

	// we know the initial farclip at this point after determining visible world leafs
	// R_DrawEntities can make adjustments as well

	if( !shadowMap ) {
		if( !( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
			R_DrawWorld();

			rn.fog_eye = R_FogForSphere( rn.viewOrigin, 0.5 );
			rn.hdrExposure = 1.0f;
		}

		wsw::ref::Scene::Instance()->DrawCoronae();

		if( r_speeds->integer ) {
			msec = Sys_Milliseconds();
		}
		R_DrawPolys();
		if( r_speeds->integer ) {
			rf.stats.t_add_polys += ( Sys_Milliseconds() - msec );
		}
	}

	if( r_speeds->integer ) {
		msec = Sys_Milliseconds();
	}
	R_DrawEntities();
	if( r_speeds->integer ) {
		rf.stats.t_add_entities += ( Sys_Milliseconds() - msec );
	}

	if( !shadowMap ) {
		// now set  the real far clip value and reload view matrices
		R_SetFarClip();

		R_SetupViewMatrices();

		// render to depth textures, mark shadowed entities and surfaces
		// TODO
	}

	R_SortDrawList( rn.meshlist );

	R_BindRefInstFBO();

	R_SetupGL();

	R_DrawPortals();

	if( r_portalonly->integer && !( rn.renderFlags & ( RF_MIRRORVIEW | RF_PORTALVIEW ) ) ) {
		return;
	}

	R_Clear( ~0 );

	if( r_speeds->integer ) {
		msec = Sys_Milliseconds();
	}
	R_DrawSurfaces( rn.meshlist );
	if( r_speeds->integer ) {
		rf.stats.t_draw_meshes += ( Sys_Milliseconds() - msec );
	}

	if( r_speeds->integer ) {
		R_GetVBOSliceCounts( rn.meshlist, &rf.stats.c_slices_verts, &rf.stats.c_slices_elems );
	}

	if( r_showtris->integer ) {
		R_DrawOutlinedSurfaces( rn.meshlist );
	}

	R_TransformForWorld();

	R_EndGL();
}

#define REFINST_STACK_SIZE  64
static refinst_t riStack[REFINST_STACK_SIZE];
static unsigned int riStackSize;

void R_ClearRefInstStack( void ) {
	riStackSize = 0;
}

bool R_PushRefInst( void ) {
	if( riStackSize == REFINST_STACK_SIZE ) {
		return false;
	}
	riStack[riStackSize++] = rn;
	R_EndGL();
	return true;
}

void R_PopRefInst( void ) {
	if( !riStackSize ) {
		return;
	}

	rn = riStack[--riStackSize];
	R_BindRefInstFBO();

	R_SetupGL();
}

void R_Finish() {
	qglFinish();
}

void R_Flush( void ) {
	qglFlush();
}

void R_DeferDataSync( void ) {
	if( rsh.registrationOpen ) {
		return;
	}

	rf.dataSync = true;
	qglFlush();
	RB_FlushTextureCache();
}

void R_DataSync( void ) {
	if( rf.dataSync ) {
		rf.dataSync = false;
	}
}

int R_SetSwapInterval( int swapInterval, int oldSwapInterval ) {
	if( swapInterval != oldSwapInterval ) {
		GLimp_SetSwapInterval( swapInterval );
	}
	return swapInterval;
}

void R_SetGamma( float gamma ) {
	int i, v;
	double invGamma, div;
	unsigned short gammaRamp[3 * GAMMARAMP_STRIDE];

	if( !glConfig.hwGamma ) {
		return;
	}

	invGamma = 1.0 / bound( 0.5, gamma, 3.0 );
	div = (double)( 1 << 0 ) / ( glConfig.gammaRampSize - 0.5 );

	for( i = 0; i < glConfig.gammaRampSize; i++ ) {
		v = ( int )( 65535.0 * pow( ( (double)i + 0.5 ) * div, invGamma ) + 0.5 );
		gammaRamp[i] = gammaRamp[i + GAMMARAMP_STRIDE] = gammaRamp[i + 2 * GAMMARAMP_STRIDE] = ( ( unsigned short )bound( 0, v, 65535 ) );
	}

	GLimp_SetGammaRamp( GAMMARAMP_STRIDE, glConfig.gammaRampSize, gammaRamp );
}

void R_SetWallFloorColors( const vec3_t wallColor, const vec3_t floorColor ) {
	int i;
	for( i = 0; i < 3; i++ ) {
		rsh.wallColor[i] = bound( 0, floor( wallColor[i] ) / 255.0, 1.0 );
		rsh.floorColor[i] = bound( 0, floor( floorColor[i] ) / 255.0, 1.0 );
	}
}

void R_SetDrawBuffer( const char *drawbuffer ) {
	Q_strncpyz( rf.drawBuffer, drawbuffer, sizeof( rf.drawBuffer ) );
	rf.newDrawBuffer = true;
}

const char *R_WriteSpeedsMessage( char *out, size_t size ) {
	char backend_msg[1024];

	if( !out || !size ) {
		return out;
	}

	out[0] = '\0';
	if( r_speeds->integer && !( rn.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		switch( r_speeds->integer ) {
			case 1:
				RB_StatsMessage( backend_msg, sizeof( backend_msg ) );

				Q_snprintfz( out, size,
							 "%u fps\n"
							 "%4u wpoly %4u leafs %4u surfs\n"
							 "%5u sverts %5u stris\n"
							 "%s",
							 (int)(1000.0 / rf.frameTime.average),
							 rf.stats.c_brush_polys, rf.stats.c_world_leafs, rf.stats.c_world_draw_surfs,
							 rf.stats.c_slices_verts, rf.stats.c_slices_elems / 3,
							 backend_msg
							 );
				break;
			case 2:
			case 3:
				Q_snprintfz( out, size,
							 "cull nodes\\surfs: %5u\\%5u\n"
							 "node: %5u\n"
							 "polys\\ents: %5u\\%5u  draw: %5u\n",
							 rf.stats.t_cull_world_nodes, rf.stats.t_cull_world_surfs, 
							 rf.stats.t_world_node,
							 rf.stats.t_add_polys, rf.stats.t_add_entities, rf.stats.t_draw_meshes
							 );
				break;
			case 4:
			case 5:
				if( rf.debugSurface ) {
					int numVerts = 0, numTris = 0;
					msurface_t *debugSurface = rf.debugSurface;
					drawSurfaceBSP_t *drawSurf = rf.debugSurface ? &rsh.worldBrushModel->drawSurfaces[debugSurface->drawSurf - 1] : NULL;

					assert( debugSurface->shader->name.isZeroTerminated() );
					Q_snprintfz( out, size,
								 "%s type:%i sort:%i",
								 debugSurface->shader->name.data(), debugSurface->facetype, debugSurface->shader->sort );

					Q_strncatz( out, "\n", size );

					if( r_speeds->integer == 5 && drawSurf->vbo ) {
						numVerts = drawSurf->vbo->numVerts;
						numTris = drawSurf->vbo->numElems / 3;
					} else {
						numVerts = debugSurface->mesh.numVerts;
						numTris = debugSurface->mesh.numElems;
					}

					if( numVerts ) {
						Q_snprintfz( out + strlen( out ), size - strlen( out ),
									 "verts: %5i tris: %5i", numVerts, numTris );
					}

					Q_strncatz( out, "\n", size );

					if( debugSurface->fog && debugSurface->fog->shader
						&& debugSurface->fog->shader != debugSurface->shader ) {
						assert( debugSurface->fog->shader->name.isZeroTerminated() );
						Q_strncatz( out, debugSurface->fog->shader->name.data(), size );
					}
				}
				break;
			case 6:
				Q_snprintfz( out, size,
							 "%.1f %.1f %.1f",
							 rn.refdef.vieworg[0], rn.refdef.vieworg[1], rn.refdef.vieworg[2]
							 );
				break;
			default:
				Q_snprintfz( out, size,
							 "%u fps",
							 (int)(1000.0 / rf.frameTime.average)
							 );
				break;
		}
	}

	out[size - 1] = '\0';
	return out;
}

void R_RenderDebugSurface( const refdef_t *fd ) {
	rtrace_t tr;
	vec3_t forward;
	vec3_t start, end;
	msurface_t *debugSurf = NULL;

	if( fd->rdflags & RDF_NOWORLDMODEL ) {
		return;
	}

	if( r_speeds->integer == 4 || r_speeds->integer == 5 ) {
		msurface_t *surf = NULL;

		VectorCopy( &fd->viewaxis[AXIS_FORWARD], forward );
		VectorCopy( fd->vieworg, start );
		VectorMA( start, 4096, forward, end );

		surf = R_TraceLine( &tr, start, end, 0 );
		if( surf && surf->drawSurf && !r_showtris->integer ) {
			drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + surf->drawSurf - 1;

			R_ClearDrawList( rn.meshlist );

			R_ClearDrawList( rn.portalmasklist );

			if( R_AddSurfToDrawList( rn.meshlist, R_NUM2ENT( tr.ent ), NULL, surf->shader, 0, 0, NULL, drawSurf ) ) {
				if( rn.refdef.rdflags & RDF_FLIPPED ) {
					RB_FlipFrontFace();
				}

				if( r_speeds->integer == 5 ) {
					// VBO debug mode
					R_AddDrawListVBOSlice( rn.meshlist, drawSurf - rsh.worldBrushModel->drawSurfaces,
								   drawSurf->numVerts, drawSurf->numElems,
								   0, 0 );
				} else {
					// classic mode (showtris for individual surface)
					R_AddDrawListVBOSlice( rn.meshlist, drawSurf - rsh.worldBrushModel->drawSurfaces,
								   surf->mesh.numVerts, surf->mesh.numElems,
								   surf->firstDrawSurfVert, surf->firstDrawSurfElem );
				}

				R_DrawOutlinedSurfaces( rn.meshlist );

				if( rn.refdef.rdflags & RDF_FLIPPED ) {
					RB_FlipFrontFace();
				}

				debugSurf = surf;
			}
		}
	}

	rf.debugSurface = debugSurf;
}

void R_BeginFrame( bool forceClear, int swapInterval ) {
	int64_t time = Sys_Milliseconds();

	GLimp_BeginFrame();

	RB_BeginFrame();

	qglDrawBuffer( GL_BACK );

	// draw buffer stuff
	if( rf.newDrawBuffer ) {
		rf.newDrawBuffer = false;

		if( Q_stricmp( rf.drawBuffer, "GL_FRONT" ) == 0 ) {
			qglDrawBuffer( GL_FRONT );
		} else {
			qglDrawBuffer( GL_BACK );
		}
	}

	if( forceClear ) {
		RB_Clear( GL_COLOR_BUFFER_BIT, 0, 0, 0, 1 );
	}

	// set swap interval (vertical synchronization)
	rf.swapInterval = R_SetSwapInterval( swapInterval, rf.swapInterval );

	memset( &rf.stats, 0, sizeof( rf.stats ) );

	// update fps meter
	rf.frameTime.count++;
	rf.frameTime.time = time;
	if( rf.frameTime.time - rf.frameTime.oldTime >= 50 ) {
		rf.frameTime.average = time - rf.frameTime.oldTime;
		rf.frameTime.average = ((float)rf.frameTime.average / ( rf.frameTime.count - rf.frameTime.oldCount )) + 0.5f;
		rf.frameTime.oldTime = time;
		rf.frameTime.oldCount = rf.frameTime.count;
	}

	R_Set2DMode( true );
}

void R_EndFrame( void ) {
	// render previously batched 2D geometry, if any
	RB_FlushDynamicMeshes();

	R_PolyBlend();

	R_ApplyBrightness();

	// reset the 2D state so that the mode will be
	// properly set back again in R_BeginFrame
	R_Set2DMode( false );

	RB_EndFrame();

	GLimp_EndFrame();

	//assert( qglGetError() == GL_NO_ERROR );
}

void R_NormToLatLong( const vec_t *normal, uint8_t latlong[2] ) {
	float flatlong[2];

	NormToLatLong( normal, flatlong );
	latlong[0] = (int)( flatlong[0] * 255.0 / M_TWOPI ) & 255;
	latlong[1] = (int)( flatlong[1] * 255.0 / M_TWOPI ) & 255;
}

void R_LatLongToNorm4( const uint8_t latlong[2], vec4_t out ) {
	static float * const sinTable = rsh.sinTableByte;
	float sin_a, sin_b, cos_a, cos_b;

	cos_a = sinTable[( latlong[0] + 64 ) & 255];
	sin_a = sinTable[latlong[0]];
	cos_b = sinTable[( latlong[1] + 64 ) & 255];
	sin_b = sinTable[latlong[1]];

	Vector4Set( out, cos_b * sin_a, sin_b * sin_a, cos_a, 0 );
}

void R_LatLongToNorm( const uint8_t latlong[2], vec3_t out ) {
	vec4_t t;
	R_LatLongToNorm4( latlong, t );
	VectorCopy( t, out );
}

int R_LoadFile_( const char *path, int flags, void **buffer, const char *filename, int fileline ) {
	uint8_t *buf;
	unsigned int len;
	int fhandle;

	buf = NULL; // quiet compiler warning

	// look for it in the filesystem or pack files
	len = FS_FOpenFile( path, &fhandle, FS_READ | flags );

	if( !fhandle ) {
		if( buffer ) {
			*buffer = NULL;
		}
		return -1;
	}

	if( !buffer ) {
		FS_FCloseFile( fhandle );
		return len;
	}

	buf = ( uint8_t *)Q_malloc( len + 1 );
	buf[len] = 0;
	*buffer = buf;

	FS_Read( buf, len, fhandle );
	FS_FCloseFile( fhandle );

	return len;
}

void R_FreeFile_( void *buffer, const char *filename, int fileline ) {
	Q_free( buffer );
}

static struct tm *R_Localtime( const time_t time, struct tm* _tm ) {
#ifdef _WIN32
	struct tm* __tm = localtime( &time );
	*_tm = *__tm;
#else
	localtime_r( &time, _tm );
#endif
	return _tm;
}

void R_TakeScreenShot( const char *path, const char *name, const char *fmtString, int x, int y, int w, int h, bool silent ) {
	const char *extension;
	size_t path_size = strlen( path ) + 1;
	char *checkname = NULL;
	size_t checkname_size = 0;
	int quality;

	if( r_screenshot_jpeg->integer ) {
		extension = ".jpg";
		quality = r_screenshot_jpeg_quality->integer;
	} else {
		extension = ".tga";
		quality = 100;
	}

	if( name && name[0] && Q_stricmp( name, "*" ) ) {
		if( !COM_ValidateRelativeFilename( name ) ) {
			Com_Printf( "Invalid filename\n" );
			return;
		}

		checkname_size = ( path_size - 1 ) + strlen( name ) + strlen( extension ) + 1;
		checkname = (char *)alloca( checkname_size );
		Q_snprintfz( checkname, checkname_size, "%s%s", path, name );
		COM_DefaultExtension( checkname, extension, checkname_size );
	}

	//
	// find a file name to save it to
	//
	if( !checkname ) {
		const int maxFiles = 100000;
		static int lastIndex = 0;
		bool addIndex = false;
		char timestampString[MAX_QPATH];
		static char lastFmtString[MAX_QPATH];
		struct tm newtime;

		R_Localtime( time( NULL ), &newtime );
		strftime( timestampString, sizeof( timestampString ), fmtString, &newtime );

		checkname_size = ( path_size - 1 ) + strlen( timestampString ) + 5 + 1 + strlen( extension );
		checkname = (char *)alloca( checkname_size );

		// if the string format is a constant or file already exists then iterate
		if( !*fmtString || !strcmp( timestampString, fmtString ) ) {
			addIndex = true;

			// force a rescan in case some vars have changed..
			if( strcmp( lastFmtString, fmtString ) ) {
				lastIndex = 0;
				Q_strncpyz( lastFmtString, fmtString, sizeof( lastFmtString ) );
				r_screenshot_fmtstr->modified = false;
			}
			if( r_screenshot_jpeg->modified ) {
				lastIndex = 0;
				r_screenshot_jpeg->modified = false;
			}
		} else {
			Q_snprintfz( checkname, checkname_size, "%s%s%s", path, timestampString, extension );
			if( FS_FOpenAbsoluteFile( checkname, NULL, FS_READ ) != -1 ) {
				lastIndex = 0;
				addIndex = true;
			}
		}

		for( ; addIndex && lastIndex < maxFiles; lastIndex++ ) {
			Q_snprintfz( checkname, checkname_size, "%s%s%05i%s", path, timestampString, lastIndex, extension );
			if( FS_FOpenAbsoluteFile( checkname, NULL, FS_READ ) == -1 ) {
				break; // file doesn't exist
			}
		}

		if( lastIndex == maxFiles ) {
			Com_Printf( "Couldn't create a file\n" );
			return;
		}

		lastIndex++;
	}

	R_ScreenShot( checkname, x, y, w, h, quality, silent );
}

void R_ScreenShot_f( void ) {
	int i;
	const char *name;
	size_t path_size;
	char *path;
	char timestamp_str[MAX_QPATH];
	struct tm newtime;

	R_Localtime( time( NULL ), &newtime );

	name = Cmd_Argv( 1 );

	path_size = strlen( FS_WriteDirectory() ) + 1 /* '/' */ + strlen( "/screenshots/" ) + 1;
	path = (char *)alloca( path_size );
	Q_snprintfz( path, path_size, "%s/screenshots/", FS_WriteDirectory() );

	// validate timestamp string
	for( i = 0; i < 2; i++ ) {
		strftime( timestamp_str, sizeof( timestamp_str ), r_screenshot_fmtstr->string, &newtime );
		if( !COM_ValidateRelativeFilename( timestamp_str ) ) {
			Cvar_ForceSet( r_screenshot_fmtstr->name, r_screenshot_fmtstr->dvalue );
		} else {
			break;
		}
	}

	// hm... shouldn't really happen, but check anyway
	if( i == 2 ) {
		Cvar_ForceSet( r_screenshot_fmtstr->name, glConfig.screenshotPrefix );
	}

	RF_ScreenShot( path, name, r_screenshot_fmtstr->string,
				   Cmd_Argc() >= 3 && !Q_stricmp( Cmd_Argv( 2 ), "silent" ) ? true : false );
}

#define WORLDSURF_DIST 1024.0f                  // hack the draw order for world surfaces

static vec3_t modelOrg;                         // relative to view point

bool R_SurfPotentiallyVisible( const msurface_t *surf ) {
	const shader_t *shader = surf->shader;
	// Exclude old sky surfaces from rendering for now
	if( surf->flags & ( SURF_NODRAW | SURF_SKY ) ) {
		return false;
	}
	if( !surf->mesh.numVerts ) {
		return false;
	}
	if( !shader ) {
		return false;
	}
	return true;
}

bool R_SurfPotentiallyShadowed( const msurface_t *surf ) {
	if( surf->flags & ( SURF_SKY | SURF_NODLIGHT | SURF_NODRAW ) ) {
		return false;
	}
	if( ( surf->shader->sort >= SHADER_SORT_OPAQUE ) && ( surf->shader->sort <= SHADER_SORT_ALPHATEST ) ) {
		return true;
	}
	return false;
}

bool R_SurfPotentiallyLit( const msurface_t *surf ) {
	const shader_t *shader;

	if( surf->flags & ( SURF_SKY | SURF_NODLIGHT | SURF_NODRAW ) ) {
		return false;
	}
	shader = surf->shader;
	if( !shader->numpasses ) {
		return false;
	}
	return ( surf->mesh.numVerts != 0 /* && (surf->facetype != FACETYPE_TRISURF)*/ );
}

bool R_CullSurface( const entity_t *e, const msurface_t *surf, unsigned int clipflags ) {
	return ( clipflags && R_CullBox( surf->mins, surf->maxs, clipflags ) );
}

static unsigned int R_SurfaceDlightBits( const msurface_t *surf, unsigned int checkDlightBits ) {
	float dist;
	unsigned int surfDlightBits = 0;

	if( !R_SurfPotentiallyLit( surf ) ) {
		return 0;
	}

	// TODO: This is totally wrong! Lights should mark affected surfaces in their radius on their own!

	const auto *scene = wsw::ref::Scene::Instance();
	const wsw::ref::Scene::LightNumType *rangeBegin, *rangeEnd;
	scene->GetDrawnProgramLightNums( &rangeBegin, &rangeEnd );
	// Caution: bits correspond to indices in range now!
	uint32_t mask = 1;
	for( const auto *iter = rangeBegin; iter < rangeEnd; ++iter, mask <<= 1 ) {
		// TODO: This condition seems to be pointless
		if( checkDlightBits & mask ) {
			const auto *__restrict lt = scene->ProgramLightForNum( *iter );
			// TODO: Avoid doing this and keep separate lists of planar and other surfaces
			switch( surf->facetype ) {
				case FACETYPE_PLANAR:
					dist = DotProduct( lt->center, surf->plane ) - surf->plane[3];
					if( dist > -lt->radius && dist < lt->radius ) {
						surfDlightBits |= mask;
					}
					break;
				case FACETYPE_PATCH:
				case FACETYPE_TRISURF:
				case FACETYPE_FOLIAGE:
					if( BoundsAndSphereIntersect( surf->mins, surf->maxs, lt->center, lt->radius ) ) {
						surfDlightBits |= mask;
					}
					break;
			}
			checkDlightBits &= ~mask;
			if( !checkDlightBits ) {
				break;
			}
		}
	}

	return surfDlightBits;
}

void R_DrawBSPSurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, unsigned int entShadowBits, drawSurfaceBSP_t *drawSurf ) {
	const vboSlice_t *slice;
	const vboSlice_t *shadowSlice;
	static const vboSlice_t nullSlice = { 0 };
	int firstVert, firstElem;
	int numVerts, numElems;
	unsigned dlightBits;

	slice = R_GetDrawListVBOSlice( rn.meshlist, drawSurf - rsh.worldBrushModel->drawSurfaces );
	shadowSlice = R_GetDrawListVBOSlice( rn.meshlist, rsh.worldBrushModel->numDrawSurfaces + ( drawSurf - rsh.worldBrushModel->drawSurfaces ) );
	if( !shadowSlice ) {
		shadowSlice = &nullSlice;
	}

	assert( slice != NULL );
	if( !slice ) {
		return;
	}

	// shadowBits are shared for all rendering instances (normal view, portals, etc)
	dlightBits = drawSurf->dlightBits;

	// if either shadow slice is empty or shadowBits is 0, then we must pass the surface unshadowed

	numVerts = slice->numVerts;
	numElems = slice->numElems;
	firstVert = drawSurf->firstVboVert + slice->firstVert;
	firstElem = drawSurf->firstVboElem + slice->firstElem;

	if( !numVerts ) {
		return;
	}

	RB_BindVBO( drawSurf->vbo->index, GL_TRIANGLES );

	RB_SetDlightBits( dlightBits );

	RB_SetLightstyle( drawSurf->superLightStyle );

	if( drawSurf->numInstances ) {
		RB_DrawElementsInstanced( firstVert, numVerts, firstElem, numElems,
								  drawSurf->numInstances, drawSurf->instances );
	} else {
		RB_DrawElements( firstVert, numVerts, firstElem, numElems );
	}
}

static void R_AddSurfaceVBOSlice( drawList_t *list, drawSurfaceBSP_t *drawSurf, const msurface_t *surf, int offset ) {
	R_AddDrawListVBOSlice( list, offset + drawSurf - rsh.worldBrushModel->drawSurfaces,
						   surf->mesh.numVerts, surf->mesh.numElems,
						   surf->firstDrawSurfVert, surf->firstDrawSurfElem );
}

static bool R_AddSurfaceToDrawList( const entity_t *e, drawSurfaceBSP_t *drawSurf ) {
	const shader_t *shader = drawSurf->shader;
	const mfog_t *fog = drawSurf->fog;
	portalSurface_t *portalSurface = NULL;
	unsigned drawOrder = 0;
	unsigned sliceIndex = drawSurf - rsh.worldBrushModel->drawSurfaces;

	if( drawSurf->visFrame == rf.frameCount ) {
		return true;
	}

	const bool portal = ( shader->flags & SHADER_PORTAL ) != 0;
	if( portal ) {
		portalSurface = R_AddPortalSurface( e, shader, drawSurf );
	}

	drawOrder = R_PackOpaqueOrder( fog, shader, drawSurf->numLightmaps, false );

	drawSurf->dlightBits = 0;
	drawSurf->visFrame = rf.frameCount;
	drawSurf->listSurf = R_AddSurfToDrawList( rn.meshlist, e, fog, shader, WORLDSURF_DIST, drawOrder, portalSurface, drawSurf );
	if( !drawSurf->listSurf ) {
		return false;
	}

	if( portalSurface && !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) ) ) {
		R_AddSurfToDrawList( rn.portalmasklist, e, NULL, NULL, 0, 0, NULL, drawSurf );
	}

	R_AddDrawListVBOSlice( rn.meshlist, sliceIndex, 0, 0, 0, 0 );
	R_AddDrawListVBOSlice( rn.meshlist, sliceIndex + rsh.worldBrushModel->numDrawSurfaces, 0, 0, 0, 0 );

	rf.stats.c_world_draw_surfs++;
	return true;
}

static bool R_ClipSpecialWorldSurf( drawSurfaceBSP_t *drawSurf, const msurface_t *surf, const vec3_t origin, float *pdist ) {
	portalSurface_t *portalSurface = NULL;
	const shader_t *shader = drawSurf->shader;

	const bool portal = ( shader->flags & SHADER_PORTAL ) != 0;
	if( portal ) {
		portalSurface = R_GetDrawListSurfPortal( drawSurf->listSurf );
	}

	if( portalSurface != NULL ) {
		vec3_t centre;
		float dist = 0;

		if( origin ) {
			VectorCopy( origin, centre );
		} else {
			VectorAdd( surf->mins, surf->maxs, centre );
			VectorScale( centre, 0.5, centre );
		}
		dist = Distance( rn.refdef.vieworg, centre );

		// draw portals in front-to-back order
		dist = 1024 - dist / 100.0f;
		if( dist < 1 ) {
			dist = 1;
		}

		R_UpdatePortalSurface( portalSurface, &surf->mesh, surf->mins, surf->maxs, shader, drawSurf );

		*pdist = dist;
	}

	return true;
}

/*
* R_UpdateSurfaceInDrawList
*
* Walk the list of visible world surfaces and prepare the final VBO slice and draw order bits.
* For sky surfaces, skybox clipping is also performed.
*/
static void R_UpdateSurfaceInDrawList( drawSurfaceBSP_t *drawSurf, unsigned int dlightBits, const vec3_t origin ) {
	unsigned i, end;
	float dist = 0;
	bool special;
	msurface_t *surf;
	unsigned dlightFrame;
	unsigned curDlightBits;
	msurface_t *firstVisSurf, *lastVisSurf;

	if( !drawSurf->listSurf ) {
		return;
	}

	firstVisSurf = lastVisSurf = NULL;

	curDlightBits = dlightFrame = 0;

	end = drawSurf->firstWorldSurface + drawSurf->numWorldSurfaces;
	surf = rsh.worldBrushModel->surfaces + drawSurf->firstWorldSurface;

	special = ( drawSurf->shader->flags & (SHADER_PORTAL) ) != 0;

	for( i = drawSurf->firstWorldSurface; i < end; i++ ) {
		if( rf.worldSurfVis[i] ) {
			float sdist = 0;
			unsigned int checkDlightBits = dlightBits & ~curDlightBits;

			if( special && !R_ClipSpecialWorldSurf( drawSurf, surf, origin, &sdist ) ) {
				// clipped away
				continue;
			}

			if( sdist > sdist )
				dist = sdist;

			if( checkDlightBits )
				checkDlightBits = R_SurfaceDlightBits( surf, checkDlightBits );

			// dynamic lights that affect the surface
			if( checkDlightBits ) {
				// ignore dlights that have already been marked as affectors
				if( dlightFrame == rsc.frameCount ) {
					curDlightBits |= checkDlightBits;
				} else {
					dlightFrame = rsc.frameCount;
					curDlightBits = checkDlightBits;
				}
			}

			// surfaces are sorted by their firstDrawVert index so to cut the final slice
			// we only need to note the first and the last surface
			if( firstVisSurf == NULL )
				firstVisSurf = surf;
			lastVisSurf = surf;
		}
		surf++;
	}

	if( dlightFrame == rsc.frameCount ) {
		drawSurf->dlightBits = curDlightBits;
		drawSurf->dlightFrame = dlightFrame;
	}

	// prepare the slice
	if( firstVisSurf ) {
		bool dlight = dlightFrame == rsc.frameCount;

		R_AddSurfaceVBOSlice( rn.meshlist, drawSurf, firstVisSurf, 0 );

		if( lastVisSurf != firstVisSurf )
			R_AddSurfaceVBOSlice( rn.meshlist, drawSurf, lastVisSurf, 0 );

		// update the distance sorting key if it's a portal surface or a normal dlit surface
		if( dist != 0 || dlight ) {
			int drawOrder = R_PackOpaqueOrder( drawSurf->fog, drawSurf->shader, drawSurf->numLightmaps, dlight );
			if( dist == 0 )
				dist = WORLDSURF_DIST;
			R_UpdateDrawSurfDistKey( drawSurf->listSurf, 0, drawSurf->shader, dist, drawOrder );
		}
	}
}

float R_BrushModelBBox( const entity_t *e, vec3_t mins, vec3_t maxs, bool *rotated ) {
	int i;
	const model_t   *model = e->model;

	if( !Matrix3_Compare( e->axis, axis_identity ) ) {
		if( rotated ) {
			*rotated = true;
		}
		for( i = 0; i < 3; i++ ) {
			mins[i] = e->origin[i] - model->radius * e->scale;
			maxs[i] = e->origin[i] + model->radius * e->scale;
		}
		return model->radius * e->scale;
	} else {
		if( rotated ) {
			*rotated = false;
		}
		VectorMA( e->origin, e->scale, model->mins, mins );
		VectorMA( e->origin, e->scale, model->maxs, maxs );
		return RadiusFromBounds( mins, maxs );
	}
}

#define R_TransformPointToModelSpace( e,rotate,in,out ) \
	VectorSubtract( in, ( e )->origin, out ); \
	if( rotated ) { \
		vec3_t temp; \
		VectorCopy( out, temp ); \
		Matrix3_TransformVector( ( e )->axis, temp, out ); \
	}

bool R_AddBrushModelToDrawList( const entity_t *e ) {
	unsigned int i;
	vec3_t origin;
	vec3_t bmins, bmaxs;
	bool rotated;
	model_t *model = e->model;
	mbrushmodel_t *bmodel = ( mbrushmodel_t * )model->extradata;
	mfog_t *fog;
	float radius;
	unsigned dlightBits;

	if( bmodel->numModelDrawSurfaces == 0 ) {
		return false;
	}

	radius = R_BrushModelBBox( e, bmins, bmaxs, &rotated );

	if( R_CullModelEntity( e, bmins, bmaxs, radius, rotated, false ) ) {
		return false;
	}

	VectorAdd( e->model->mins, e->model->maxs, origin );
	VectorMA( e->origin, 0.5, origin, origin );

	// TODO: Unused?
	fog = R_FogForBounds( bmins, bmaxs );

	R_TransformPointToModelSpace( e, rotated, rn.refdef.vieworg, modelOrg );

	const auto *scene = wsw::ref::Scene::Instance();
	// TODO: Lights should mark models they affect on their own!

	// check dynamic lights that matter in the instance against the model
	dlightBits = 0;

	const wsw::ref::Scene::LightNumType *rangeBegin, *rangeEnd;
	scene->GetDrawnProgramLightNums( &rangeBegin, &rangeEnd );
	unsigned mask = 1;
	for( const auto *iter = rangeBegin; iter < rangeEnd; ++iter, mask <<= 1 ) {
		const auto *light = scene->ProgramLightForNum( *iter );
		if( !BoundsAndSphereIntersect( bmins, bmaxs, light->center, light->radius ) ) {
			continue;
		}
		dlightBits |= mask;
	}

	dlightBits &= rn.dlightBits;

	for( i = 0; i < bmodel->numModelSurfaces; i++ ) {
		unsigned s = bmodel->firstModelSurface + i;
		msurface_t *surf = rsh.worldBrushModel->surfaces + s;

		if( !surf->drawSurf ) {
			continue;
		}
		if( R_CullSurface( e, surf, 0 ) ) {
			continue;
		}

		rf.worldSurfVis[s] = 1;
		rf.worldDrawSurfVis[surf->drawSurf - 1] = 1;
	}

	for( i = 0; i < bmodel->numModelDrawSurfaces; i++ ) {
		unsigned s = bmodel->firstModelDrawSurface + i;
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + s;

		if( rf.worldDrawSurfVis[s] ) {
			R_AddSurfaceToDrawList( e, drawSurf );

			R_UpdateSurfaceInDrawList( drawSurf, dlightBits, origin );
		}
	}

	return true;
}

static void R_PostCullVisLeaves( void ) {
	unsigned i, j;
	mleaf_t *leaf;

	for( i = 0; i < rsh.worldBrushModel->numvisleafs; i++ ) {
		if( !rf.worldLeafVis[i] ) {
			continue;
		}

		leaf = rsh.worldBrushModel->visleafs[i];

		// add leaf bounds to view bounds
		for( j = 0; j < 3; j++ ) {
			rn.visMins[j] = std::min( rn.visMins[j], leaf->mins[j] );
			rn.visMaxs[j] = std::max( rn.visMaxs[j], leaf->maxs[j] );
		}

		rf.stats.c_world_leafs++;
	}
}

static void R_CullVisLeaves( unsigned firstLeaf, unsigned numLeaves, unsigned clipFlags ) {
	unsigned i, j;
	mleaf_t *leaf;
	uint8_t *pvs;
	uint8_t *areabits;
	int arearowbytes, areabytes;
	bool novis;

	if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
		return;
	}

	novis = rn.renderFlags & RF_NOVIS || rf.viewcluster == -1 || !rsh.worldBrushModel->pvs;
	arearowbytes = ( ( rsh.worldBrushModel->numareas + 7 ) / 8 );
	areabytes = arearowbytes;
#ifdef AREAPORTALS_MATRIX
	areabytes *= rsh.worldBrushModel->numareas;
#endif

	pvs = Mod_ClusterPVS( rf.viewcluster, rsh.worldModel );
	if( rf.viewarea > -1 && rn.refdef.areabits )
#ifdef AREAPORTALS_MATRIX
	{ areabits = rn.refdef.areabits + rf.viewarea * arearowbytes;}
#else
		{ areabits = rn.refdef.areabits;}
#endif
	else {
		areabits = NULL;
	}

	for( i = 0; i < numLeaves; i++ ) {
		int clipped;
		unsigned bit, testFlags;
		cplane_t *clipplane;
		unsigned l = firstLeaf + i;

		leaf = rsh.worldBrushModel->visleafs[l];
		if( !novis ) {
			// check for door connected areas
			if( areabits ) {
				if( leaf->area < 0 || !( areabits[leaf->area >> 3] & ( 1 << ( leaf->area & 7 ) ) ) ) {
					continue; // not visible
				}
			}

			if( !( pvs[leaf->cluster >> 3] & ( 1 << ( leaf->cluster & 7 ) ) ) ) {
				continue; // not visible
			}
		}

		// track leaves, which are entirely inside the frustum
		clipped = 0;
		testFlags = clipFlags;
		for( j = sizeof( rn.frustum ) / sizeof( rn.frustum[0] ), bit = 1, clipplane = rn.frustum; j > 0; j--, bit <<= 1, clipplane++ ) {
			if( testFlags & bit ) {
				clipped = BoxOnPlaneSide( leaf->mins, leaf->maxs, clipplane );
				if( clipped == 2 ) {
					break;
				} else if( clipped == 1 ) {
					testFlags &= ~bit; // node is entirely on screen
				}
			}
		}

		if( clipped == 2 ) {
			continue; // fully clipped
		}

		if( testFlags == 0 ) {
			// fully visible
			for( j = 0; j < leaf->numVisSurfaces; j++ ) {
				assert( leaf->visSurfaces[j] < rf.numWorldSurfVis );
				rf.worldSurfFullVis[leaf->visSurfaces[j]] = 1;
			}
		} else {
			// partly visible
			for( j = 0; j < leaf->numVisSurfaces; j++ ) {
				assert( leaf->visSurfaces[j] < rf.numWorldSurfVis );
				rf.worldSurfVis[leaf->visSurfaces[j]] = 1;
			}
		}

		rf.worldLeafVis[l] = 1;
	}
}

static void R_CullVisSurfaces( unsigned firstSurf, unsigned numSurfs, unsigned clipFlags ) {
	unsigned i;
	unsigned end;
	msurface_t *surf;

	end = firstSurf + numSurfs;
	surf = rsh.worldBrushModel->surfaces + firstSurf;

	for( i = firstSurf; i < end; i++ ) {
		if( rf.worldSurfVis[i] ) {
			// the surface is at partly visible in at least one leaf, frustum cull it
			if( R_CullSurface( rsc.worldent, surf, clipFlags ) ) {
				rf.worldSurfVis[i] = 0;
			}
			rf.worldSurfFullVis[i] = 0;
		}
		else {
			if( rf.worldSurfFullVis[i] ) {
				// a fully visible surface, mark as visible
				rf.worldSurfVis[i] = 1;
			}
		}

		if( rf.worldSurfVis[i] ) {
			if( !surf->drawSurf )
				rf.worldSurfVis[i] = 0;
			else
				rf.worldDrawSurfVis[surf->drawSurf - 1] = 1;
		}

		surf++;
	}
}

static void R_AddVisSurfaces( unsigned dlightBits, unsigned shadowBits ) {

	unsigned i;

	for( i = 0; i < rsh.worldBrushModel->numModelDrawSurfaces; i++ ) {
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + i;

		if( !rf.worldDrawSurfVis[i] ) {
			continue;
		}

		R_AddSurfaceToDrawList( rsc.worldent, drawSurf );
	}
}

static void R_AddWorldDrawSurfaces( unsigned firstDrawSurf, unsigned numDrawSurfs ) {
	unsigned i;

	for( i = 0; i < numDrawSurfs; i++ ) {
		unsigned s = firstDrawSurf + i;
		drawSurfaceBSP_t *drawSurf = rsh.worldBrushModel->drawSurfaces + s;

		if( !rf.worldDrawSurfVis[s] ) {
			continue;
		}

		R_UpdateSurfaceInDrawList( drawSurf, rn.dlightBits, NULL );
	}
}

void R_DrawWorld( void ) {
	unsigned int i;
	int clipFlags;
	int64_t msec = 0, msec2 = 0;
	unsigned int dlightBits;
	unsigned int shadowBits;
	bool worldOutlines;
	bool speeds = r_speeds->integer != 0;

	assert( rf.numWorldSurfVis >= rsh.worldBrushModel->numsurfaces );
	assert( rf.numWorldLeafVis >= rsh.worldBrushModel->numvisleafs );

	if( !r_drawworld->integer ) {
		return;
	}
	if( !rsh.worldModel ) {
		return;
	}
	if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
		return;
	}

	VectorCopy( rn.refdef.vieworg, modelOrg );

	worldOutlines = mapConfig.forceWorldOutlines || ( rn.refdef.rdflags & RDF_WORLDOUTLINES );

	if( worldOutlines && ( rf.viewcluster != -1 ) && r_outlines_scale->value > 0 ) {
		rsc.worldent->outlineHeight = std::max( 0.0f, r_outlines_world->value );
	} else {
		rsc.worldent->outlineHeight = 0;
	}
	Vector4Copy( mapConfig.outlineColor, rsc.worldent->outlineColor );

	clipFlags = rn.clipFlags;
	dlightBits = 0;
	shadowBits = 0;

	if( r_nocull->integer ) {
		clipFlags = 0;
	}

	// cull dynamic lights
	rn.dlightBits = wsw::ref::Scene::Instance()->CullLights( clipFlags );

	// BEGIN t_world_node
	if( speeds ) {
		msec = Sys_Milliseconds();
	}

	if( rsh.worldBrushModel->numvisleafs > rsh.worldBrushModel->numsurfaces ) {
		memset( (void *)rf.worldSurfVis, 1, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
		memset( (void *)rf.worldSurfFullVis, 0, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
		memset( (void *)rf.worldLeafVis, 1, rsh.worldBrushModel->numvisleafs * sizeof( *rf.worldLeafVis ) );
		memset( (void *)rf.worldDrawSurfVis, 0, rsh.worldBrushModel->numDrawSurfaces * sizeof( *rf.worldDrawSurfVis ) );
	} else {
		memset( (void *)rf.worldSurfVis, 0, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
		memset( (void *)rf.worldSurfFullVis, 0, rsh.worldBrushModel->numsurfaces * sizeof( *rf.worldSurfVis ) );
		memset( (void *)rf.worldLeafVis, 0, rsh.worldBrushModel->numvisleafs * sizeof( *rf.worldLeafVis ) );
		memset( (void *)rf.worldDrawSurfVis, 0, rsh.worldBrushModel->numDrawSurfaces * sizeof( *rf.worldDrawSurfVis ) );

		if( r_speeds->integer ) {
			msec2 = Sys_Milliseconds();
		}

		//
		// cull leafs
		//
		R_CullVisLeaves( 0, rsh.worldBrushModel->numvisleafs, clipFlags );

		if( speeds ) {
			rf.stats.t_cull_world_nodes += Sys_Milliseconds() - msec2;
		}
	}

	//
	// cull surfaces and do some background work on computed vis leafs
	//
	if( speeds ) {
		msec2 = Sys_Milliseconds();
	}

	R_CullVisSurfaces( 0, rsh.worldBrushModel->numModelSurfaces, clipFlags );

	R_PostCullVisLeaves();

	if( speeds ) {
		rf.stats.t_cull_world_surfs += Sys_Milliseconds() - msec2;
	}

	//
	// add visible surfaces to draw list
	//
	if( speeds ) {
		msec2 = Sys_Milliseconds();
	}
	R_AddVisSurfaces( dlightBits, shadowBits );

	R_AddWorldDrawSurfaces( 0, rsh.worldBrushModel->numModelDrawSurfaces );

	if( speeds ) {
		for( i = 0; i < rsh.worldBrushModel->numsurfaces; i++ ) {
			if( rf.worldSurfVis[i] ) {
				rf.stats.c_brush_polys++;
			}
		}
	}

	// END t_world_node
	if( speeds ) {
		rf.stats.t_world_node += Sys_Milliseconds() - msec;
	}
}

drawList_t r_worldlist;
drawList_t r_shadowlist;
drawList_t r_portalmasklist;
drawList_t r_portallist, r_skyportallist;

void R_InitDrawList( drawList_t *list ) {
	memset( list, 0, sizeof( *list ) );
}

void R_InitDrawLists( void ) {
	R_InitDrawList( &r_worldlist );
	R_InitDrawList( &r_portalmasklist );
	R_InitDrawList( &r_portallist );
	R_InitDrawList( &r_skyportallist );
	R_InitDrawList( &r_shadowlist );
}

void R_ClearDrawList( drawList_t *list ) {
	if( !list ) {
		return;
	}

	// clear counters
	list->numDrawSurfs = 0;

	// clear VBO slices
	if( list->vboSlices ) {
		memset( list->vboSlices, 0, sizeof( *list->vboSlices ) * list->maxVboSlices );
	}
}

static void R_ReserveDrawSurfaces( drawList_t *list, int minMeshes ) {
	int oldSize, newSize;
	sortedDrawSurf_t *newDs;
	sortedDrawSurf_t *ds = list->drawSurfs;
	int maxMeshes = list->maxDrawSurfs;

	oldSize = maxMeshes;
	newSize = std::max( minMeshes, oldSize * 2 );

	newDs = (sortedDrawSurf_t *)Q_malloc( newSize * sizeof( sortedDrawSurf_t ) );
	if( ds ) {
		memcpy( newDs, ds, oldSize * sizeof( sortedDrawSurf_t ) );
		Q_free( ds );
	}

	list->drawSurfs = newDs;
	list->maxDrawSurfs = newSize;
}

static int R_PackDistKey( int renderFx, const shader_t *shader, float dist, unsigned order ) {
	int shaderSort;

	shaderSort = shader->sort;

	if( renderFx & RF_WEAPONMODEL ) {
		bool depthWrite = ( shader->flags & SHADER_DEPTHWRITE ) ? true : false;

		if( renderFx & RF_NOCOLORWRITE ) {
			// depth-pass for alpha-blended weapon:
			// write to depth but do not write to color
			if( !depthWrite ) {
				return 0;
			}
			// reorder the mesh to be drawn after everything else
			// but before the blend-pass for the weapon
			shaderSort = SHADER_SORT_WEAPON;
		} else if( renderFx & RF_ALPHAHACK ) {
			// blend-pass for the weapon:
			// meshes that do not write to depth, are rendered as additives,
			// meshes that were previously added as SHADER_SORT_WEAPON (see above)
			// are now added to the very end of the list
			shaderSort = depthWrite ? SHADER_SORT_WEAPON2 : SHADER_SORT_ADDITIVE;
		}
	} else if( renderFx & RF_ALPHAHACK ) {
		// force shader sort to additive
		shaderSort = SHADER_SORT_ADDITIVE;
	}

	return ( shaderSort << 26 ) | ( std::max( 0x400 - (int)dist, 0 ) << 15 ) | ( order & 0x7FFF );
}

static unsigned int R_PackSortKey( unsigned int shaderNum, int fogNum,
								   int portalNum, unsigned int entNum ) {
	return ( shaderNum & 0x7FF ) << 21 | ( entNum & 0x7FF ) << 10 |
		   ( ( ( portalNum + 1 ) & 0x1F ) << 5 ) | ( (unsigned int)( fogNum + 1 ) & 0x1F );
}

static void R_UnpackSortKey( unsigned int sortKey, unsigned int *shaderNum, int *fogNum,
							 int *portalNum, unsigned int *entNum ) {
	*shaderNum = ( sortKey >> 21 ) & 0x7FF;
	*entNum = ( sortKey >> 10 ) & 0x7FF;
	*portalNum = (signed int)( ( sortKey >> 5 ) & 0x1F ) - 1;
	*fogNum = (signed int)( sortKey & 0x1F ) - 1;
}

unsigned R_PackOpaqueOrder( const mfog_t *fog, const shader_t *shader, int numLightmaps, bool dlight ) {
	int order = 0;

	// shader order
	if( shader != NULL ) {
		order = R_PackShaderOrder( shader );
	}
	// group by dlight
	if( dlight ) {
		order |= 0x40;
	}
	// group by dlight
	if( fog != NULL ) {
		order |= 0x80;
	}
	// group by lightmaps
	order |= ( (MAX_LIGHTMAPS - numLightmaps) << 10 );

	return order;
}

void *R_AddSurfToDrawList( drawList_t *list, const entity_t *e, const mfog_t *fog, const shader_t *shader,
						   float dist, unsigned int order, const portalSurface_t *portalSurf, void *drawSurf ) {
	int distKey;
	sortedDrawSurf_t *sds;

	if( !list || !shader ) {
		return NULL;
	}
	if( ( rn.renderFlags & RF_SHADOWMAPVIEW ) && Shader_ReadDepth( shader ) ) {
		return NULL;
	}
	if( !rsh.worldBrushModel ) {
		fog = NULL;
	}

	distKey = R_PackDistKey( e->renderfx, shader, dist, order );
	if( !distKey ) {
		return NULL;
	}

	// reallocate if numDrawSurfs
	if( list->numDrawSurfs >= list->maxDrawSurfs ) {
		int minMeshes = MIN_RENDER_MESHES;
		if( rsh.worldBrushModel ) {
			minMeshes += rsh.worldBrushModel->numDrawSurfaces;
		}
		R_ReserveDrawSurfaces( list, minMeshes );
	}

	sds = &list->drawSurfs[list->numDrawSurfs++];
	sds->drawSurf = ( drawSurfaceType_t * )drawSurf;
	sds->sortKey = R_PackSortKey( shader->id, fog ? fog - rsh.worldBrushModel->fogs : -1,
								  portalSurf ? portalSurf - rn.portalSurfaces : -1, R_ENT2NUM( e ) );
	sds->distKey = distKey;

	return sds;
}

void R_UpdateDrawSurfDistKey( void *psds, int renderFx, const shader_t *shader, float dist, unsigned order ) {
	auto *sds = (sortedDrawSurf_t *)psds;
	sds->distKey = R_PackDistKey( renderFx, shader, dist, order );
}

portalSurface_t *R_GetDrawListSurfPortal( void *psds ) {
	auto *sds = (sortedDrawSurf_t *)psds;
	unsigned int sortKey = sds->sortKey;
	unsigned int shaderNum;
	unsigned int entNum;
	int portalNum, fogNum;

	R_UnpackSortKey( sortKey, &shaderNum, &fogNum, &portalNum, &entNum );

	return portalNum >= 0 ? rn.portalSurfaces + portalNum : NULL;
}

static int R_DrawSurfCompare( const sortedDrawSurf_t *sbs1, const sortedDrawSurf_t *sbs2 ) {
	if( sbs1->distKey > sbs2->distKey ) {
		return 1;
	}
	if( sbs2->distKey > sbs1->distKey ) {
		return -1;
	}

	if( sbs1->sortKey > sbs2->sortKey ) {
		return 1;
	}
	if( sbs2->sortKey > sbs1->sortKey ) {
		return -1;
	}

	return 0;
}

void R_SortDrawList( drawList_t *list ) {
	if( r_draworder->integer ) {
		return;
	}
	qsort( list->drawSurfs, list->numDrawSurfs, sizeof( sortedDrawSurf_t ),
		   ( int ( * )( const void *, const void * ) )R_DrawSurfCompare );
}

static void R_ReserveVBOSlices( drawList_t *list, unsigned int minSlices ) {
	unsigned int oldSize, newSize;
	vboSlice_t *slices, *newSlices;

	oldSize = list->maxVboSlices;
	newSize = std::max( minSlices, oldSize * 2 );

	slices = list->vboSlices;
	newSlices = (vboSlice_t *)Q_malloc( newSize * sizeof( vboSlice_t ) );
	if( slices ) {
		memcpy( newSlices, slices, oldSize * sizeof( vboSlice_t ) );
		Q_free( slices );
	}

	list->vboSlices = newSlices;
	list->maxVboSlices = newSize;
}

void R_AddDrawListVBOSlice( drawList_t *list, unsigned int index, unsigned int numVerts, unsigned int numElems,
							unsigned int firstVert, unsigned int firstElem ) {
	vboSlice_t *slice;

	if( index >= list->maxVboSlices ) {
		unsigned int minSlices = index + 1;
		if( rsh.worldBrushModel ) {
			minSlices = std::max( rsh.worldBrushModel->numDrawSurfaces, minSlices );
		}
		R_ReserveVBOSlices( list, minSlices );
	}

	slice = &list->vboSlices[index];
	if( !slice->numVerts ) {
		// initialize the slice
		slice->firstVert = firstVert;
		slice->firstElem = firstElem;
		slice->numVerts = numVerts;
		slice->numElems = numElems;
	} else {
		if( firstVert < slice->firstVert ) {
			// prepend
			slice->numVerts = slice->numVerts + slice->firstVert - firstVert;
			slice->numElems = slice->numElems + slice->firstElem - firstElem;

			slice->firstVert = firstVert;
			slice->firstElem = firstElem;
		} else {
			// append
			slice->numVerts = std::max( slice->numVerts, numVerts + firstVert - slice->firstVert );
			slice->numElems = std::max( slice->numElems, numElems + firstElem - slice->firstElem );
		}
	}
}

vboSlice_t *R_GetDrawListVBOSlice( drawList_t *list, unsigned int index ) {
	if( index >= list->maxVboSlices ) {
		return NULL;
	}
	return &list->vboSlices[index];
}

void R_GetVBOSliceCounts( drawList_t *list, unsigned *numSliceVerts, unsigned *numSliceElems ) {
	unsigned i;

	for( i = 0; i < list->maxVboSlices; i++ ) {
		*numSliceVerts += list->vboSlices[i].numVerts;

		*numSliceElems += list->vboSlices[i].numElems;
	}
}

static const drawSurf_cb r_drawSurfCb[ST_MAX_TYPES] =
	{
		/* ST_NONE */
		NULL,
		/* ST_BSP */
		( drawSurf_cb ) & R_DrawBSPSurf,
		/* ST_ALIAS */
		( drawSurf_cb ) & R_DrawAliasSurf,
		/* ST_SKELETAL */
		( drawSurf_cb ) & R_DrawSkeletalSurf,
		/* ST_SPRITE */
		NULL,
		/* ST_POLY */
		NULL,
		/* ST_CORONA */
		NULL,
		/* ST_NULLMODEL */
		( drawSurf_cb ) & R_DrawNullSurf,
	};

static const batchDrawSurf_cb r_batchDrawSurfCb[ST_MAX_TYPES] =
	{
		/* ST_NONE */
		NULL,
		/* ST_BSP */
		NULL,
		/* ST_ALIAS */
		NULL,
		/* ST_SKELETAL */
		NULL,
		/* ST_SPRITE */
		( batchDrawSurf_cb ) & R_BatchSpriteSurf,
		/* ST_POLY */
		( batchDrawSurf_cb ) & R_BatchPolySurf,
		/* ST_CORONA */
		( batchDrawSurf_cb ) & R_BatchCoronaSurf,
		/* ST_NULLMODEL */
		NULL,
	};

/*
* R_DrawSurfaces
*/
static void _R_DrawSurfaces( drawList_t *list ) {
	unsigned int i;
	unsigned int sortKey;
	unsigned int shaderNum = 0, prevShaderNum = MAX_SHADERS;
	unsigned int entNum = 0, prevEntNum = MAX_REF_ENTITIES;
	int portalNum = -1, prevPortalNum = -100500;
	int fogNum = -1, prevFogNum = -100500;
	sortedDrawSurf_t *sds;
	int drawSurfType;
	bool batchDrawSurf = false, prevBatchDrawSurf = false;
	const shader_t *shader;
	const entity_t *entity;
	const mfog_t *fog;
	const portalSurface_t *portalSurface;
	float depthmin = 0.0f, depthmax = 0.0f;
	bool depthHack = false, cullHack = false;
	bool infiniteProj = false, prevInfiniteProj = false;
	bool depthWrite = false;
	bool depthCopied = false;
	bool batchFlushed = true, batchOpaque = false;
	int entityFX = 0, prevEntityFX = -1;
	mat4_t projectionMatrix;

	if( !list->numDrawSurfs ) {
		return;
	}

	for( i = 0; i < list->numDrawSurfs; i++ ) {
		sds = list->drawSurfs + i;
		sortKey = sds->sortKey;
		drawSurfType = *(int *)sds->drawSurf;

		assert( drawSurfType > ST_NONE && drawSurfType < ST_MAX_TYPES );

		batchDrawSurf = ( r_batchDrawSurfCb[drawSurfType] ? true : false );

		// decode draw surface properties
		R_UnpackSortKey( sortKey, &shaderNum, &fogNum, &portalNum, &entNum );

		shader = MaterialCache::instance()->getMaterialById( shaderNum );
		entity = R_NUM2ENT( entNum );
		fog = fogNum >= 0 ? rsh.worldBrushModel->fogs + fogNum : NULL;
		portalSurface = portalNum >= 0 ? rn.portalSurfaces + portalNum : NULL;
		entityFX = entity->renderfx;
		depthWrite = shader->flags & SHADER_DEPTHWRITE ? true : false;

		// see if we need to reset mesh properties in the backend
		if( !prevBatchDrawSurf || shaderNum != prevShaderNum || fogNum != prevFogNum ||
			portalNum != prevPortalNum ||
			( entNum != prevEntNum && !( shader->flags & SHADER_ENTITY_MERGABLE ) ) ||
			entityFX != prevEntityFX ) {

			if( prevBatchDrawSurf && !batchDrawSurf ) {
				RB_FlushDynamicMeshes();
				batchFlushed = true;
			}

			// hack the depth range to prevent view model from poking into walls
			if( entity->flags & RF_WEAPONMODEL ) {
				if( !depthHack ) {
					RB_FlushDynamicMeshes();
					batchFlushed = true;
					depthHack = true;
					RB_GetDepthRange( &depthmin, &depthmax );
					RB_DepthRange( depthmin, depthmin + 0.3 * ( depthmax - depthmin ) );
				}
			} else {
				if( depthHack ) {
					RB_FlushDynamicMeshes();
					batchFlushed = true;
					depthHack = false;
					RB_DepthRange( depthmin, depthmax );
				}
			}

			if( entNum != prevEntNum ) {
				// backface culling for left-handed weapons
				bool oldCullHack = cullHack;
				cullHack = ( ( entity->flags & RF_CULLHACK ) ? true : false );
				if( cullHack != oldCullHack ) {
					RB_FlushDynamicMeshes();
					batchFlushed = true;
					RB_FlipFrontFace();
				}
			}

			// sky and things that don't use depth test use infinite projection matrix
			// to not pollute the farclip
			infiniteProj = entity->renderfx & RF_NODEPTHTEST ? true : false;
			if( infiniteProj != prevInfiniteProj ) {
				RB_FlushDynamicMeshes();
				batchFlushed = true;
				if( infiniteProj ) {
					Matrix4_Copy( rn.projectionMatrix, projectionMatrix );
					Matrix4_PerspectiveProjectionToInfinity( Z_NEAR, projectionMatrix, glConfig.depthEpsilon );
					RB_LoadProjectionMatrix( projectionMatrix );
				} else {
					RB_LoadProjectionMatrix( rn.projectionMatrix );
				}
			}

			if( batchFlushed ) {
				batchOpaque = false;
			}

			if( !depthWrite && !depthCopied && Shader_ReadDepth( shader ) ) {
				// ignore portals because oblique frustum messes up the depth values
				if( ( rn.renderFlags & RF_SOFT_PARTICLES ) && !( rn.renderFlags & RF_CLIPPLANE ) ) {
				}

				depthCopied = true;
			}

			if( batchDrawSurf ) {
				// don't transform batched surfaces
				if( !prevBatchDrawSurf ) {
					RB_LoadObjectMatrix( mat4x4_identity );
				}
			} else {
				if( ( entNum != prevEntNum ) || prevBatchDrawSurf ) {
					if( shader->flags & SHADER_AUTOSPRITE ) {
						R_TranslateForEntity( entity );
					} else {
						R_TransformForEntity( entity );
					}
				}
			}

			if( !batchDrawSurf ) {
				assert( r_drawSurfCb[drawSurfType] );

				RB_BindShader( entity, shader, fog );
				RB_SetPortalSurface( portalSurface );

				r_drawSurfCb[drawSurfType]( entity, shader, fog, portalSurface, 0, sds->drawSurf );
			}

			prevShaderNum = shaderNum;
			prevEntNum = entNum;
			prevFogNum = fogNum;
			prevBatchDrawSurf = batchDrawSurf;
			prevPortalNum = portalNum;
			prevInfiniteProj = infiniteProj;
			prevEntityFX = entityFX;
		}

		if( batchDrawSurf ) {
			r_batchDrawSurfCb[drawSurfType]( entity, shader, fog, portalSurface, 0, sds->drawSurf );
			batchFlushed = false;
			if( depthWrite ) {
				batchOpaque = true;
			}
		}
	}

	if( batchDrawSurf ) {
		RB_FlushDynamicMeshes();
	}
	if( depthHack ) {
		RB_DepthRange( depthmin, depthmax );
	}
	if( cullHack ) {
		RB_FlipFrontFace();
	}

	RB_BindFrameBufferObject();
}

void R_DrawSurfaces( drawList_t *list ) {
	bool triOutlines;

	triOutlines = RB_EnableTriangleOutlines( false );
	if( !triOutlines ) {
		// do not recurse into normal mode when rendering triangle outlines
		_R_DrawSurfaces( list );
	}
	RB_EnableTriangleOutlines( triOutlines );
}

void R_DrawOutlinedSurfaces( drawList_t *list ) {
	bool triOutlines;

	if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
		return;
	}

	// properly store and restore the state, as the
	// R_DrawOutlinedSurfaces calls can be nested
	triOutlines = RB_EnableTriangleOutlines( true );
	_R_DrawSurfaces( list );
	RB_EnableTriangleOutlines( triOutlines );
}

void R_CopyOffsetElements( const elem_t *inelems, int numElems, int vertsOffset, elem_t *outelems ) {
	int i;

	for( i = 0; i < numElems; i++, inelems++, outelems++ ) {
		*outelems = vertsOffset + *inelems;
	}
}

void R_CopyOffsetTriangles( const elem_t *inelems, int numElems, int vertsOffset, elem_t *outelems ) {
	int i;
	int numTris = numElems / 3;

	for( i = 0; i < numTris; i++, inelems += 3, outelems += 3 ) {
		outelems[0] = vertsOffset + inelems[0];
		outelems[1] = vertsOffset + inelems[1];
		outelems[2] = vertsOffset + inelems[2];
	}
}

void R_BuildTrifanElements( int vertsOffset, int numVerts, elem_t *elems ) {
	int i;

	for( i = 2; i < numVerts; i++, elems += 3 ) {
		elems[0] = vertsOffset;
		elems[1] = vertsOffset + i - 1;
		elems[2] = vertsOffset + i;
	}
}

void R_BuildTangentVectors( int numVertexes, vec4_t *xyzArray, vec4_t *normalsArray,
							vec2_t *stArray, int numTris, elem_t *elems, vec4_t *sVectorsArray ) {
	int i, j;
	float d, *v[3], *tc[3];
	vec_t *s, *t, *n;
	vec3_t stvec[3], cross;
	vec3_t stackTVectorsArray[128];
	vec3_t *tVectorsArray;

	if( numVertexes > sizeof( stackTVectorsArray ) / sizeof( stackTVectorsArray[0] ) ) {
		tVectorsArray = (vec3_t *)Q_malloc( sizeof( vec3_t ) * numVertexes );
	} else {
		tVectorsArray = stackTVectorsArray;
	}

	// assuming arrays have already been allocated
	// this also does some nice precaching
	memset( sVectorsArray, 0, numVertexes * sizeof( *sVectorsArray ) );
	memset( tVectorsArray, 0, numVertexes * sizeof( *tVectorsArray ) );

	for( i = 0; i < numTris; i++, elems += 3 ) {
		for( j = 0; j < 3; j++ ) {
			v[j] = ( float * )( xyzArray + elems[j] );
			tc[j] = ( float * )( stArray + elems[j] );
		}

		// calculate two mostly perpendicular edge directions
		VectorSubtract( v[1], v[0], stvec[0] );
		VectorSubtract( v[2], v[0], stvec[1] );

		// we have two edge directions, we can calculate the normal then
		CrossProduct( stvec[1], stvec[0], cross );

		for( j = 0; j < 3; j++ ) {
			stvec[0][j] = ( ( tc[1][1] - tc[0][1] ) * ( v[2][j] - v[0][j] ) - ( tc[2][1] - tc[0][1] ) * ( v[1][j] - v[0][j] ) );
			stvec[1][j] = ( ( tc[1][0] - tc[0][0] ) * ( v[2][j] - v[0][j] ) - ( tc[2][0] - tc[0][0] ) * ( v[1][j] - v[0][j] ) );
		}

		// inverse tangent vectors if their cross product goes in the opposite
		// direction to triangle normal
		CrossProduct( stvec[1], stvec[0], stvec[2] );
		if( DotProduct( stvec[2], cross ) < 0 ) {
			VectorInverse( stvec[0] );
			VectorInverse( stvec[1] );
		}

		for( j = 0; j < 3; j++ ) {
			VectorAdd( sVectorsArray[elems[j]], stvec[0], sVectorsArray[elems[j]] );
			VectorAdd( tVectorsArray[elems[j]], stvec[1], tVectorsArray[elems[j]] );
		}
	}

	// normalize
	for( i = 0, s = *sVectorsArray, t = *tVectorsArray, n = *normalsArray; i < numVertexes; i++, s += 4, t += 3, n += 4 ) {
		// keep s\t vectors perpendicular
		d = -DotProduct( s, n );
		VectorMA( s, d, n, s );
		VectorNormalize( s );

		d = -DotProduct( t, n );
		VectorMA( t, d, n, t );

		// store polarity of t-vector in the 4-th coordinate of s-vector
		CrossProduct( n, s, cross );
		if( DotProduct( cross, t ) < 0 ) {
			s[3] = -1;
		} else {
			s[3] = 1;
		}
	}

	if( tVectorsArray != stackTVectorsArray ) {
		Q_free( tVectorsArray );
	}
}

void R_ClearScene( void ) {
	rsc.numLocalEntities = 0;
	rsc.numPolys = 0;

	rsc.worldent = R_NUM2ENT( rsc.numLocalEntities );
	rsc.worldent->scale = 1.0f;
	rsc.worldent->model = rsh.worldModel;
	rsc.worldent->rtype = RT_MODEL;
	Matrix3_Identity( rsc.worldent->axis );
	rsc.numLocalEntities++;

	rsc.polyent = R_NUM2ENT( rsc.numLocalEntities );
	rsc.polyent->scale = 1.0f;
	rsc.polyent->model = NULL;
	rsc.polyent->rtype = RT_MODEL;
	Matrix3_Identity( rsc.polyent->axis );
	rsc.numLocalEntities++;

	rsc.skyent = R_NUM2ENT( rsc.numLocalEntities );
	*rsc.skyent = *rsc.worldent;
	rsc.numLocalEntities++;

	rsc.numEntities = rsc.numLocalEntities;

	rsc.numBmodelEntities = 0;

	rsc.frameCount++;

	R_ClearSkeletalCache();

	wsw::ref::Scene::Instance()->Clear();
}

void R_AddEntityToScene( const entity_t *ent ) {
	if( !r_drawentities->integer ) {
		return;
	}

	if( ( ( rsc.numEntities - rsc.numLocalEntities ) < MAX_ENTITIES ) && ent ) {
		int eNum = rsc.numEntities;
		entity_t *de = R_NUM2ENT( eNum );

		*de = *ent;
		if( r_outlines_scale->value <= 0 ) {
			de->outlineHeight = 0;
		}

		if( de->rtype == RT_MODEL ) {
			if( de->model && de->model->type == mod_brush ) {
				rsc.bmodelEntities[rsc.numBmodelEntities++] = de;
			}
			if( !( de->renderfx & RF_NOSHADOW ) ) {
				// TODO
			}
		} else if( de->rtype == RT_SPRITE ) {
			// simplifies further checks
			de->model = NULL;
		}

		if( de->renderfx & RF_ALPHAHACK ) {
			if( de->shaderRGBA[3] == 255 ) {
				de->renderfx &= ~RF_ALPHAHACK;
			}
		}

		rsc.numEntities++;

		// add invisible fake entity for depth write
		if( ( de->renderfx & ( RF_WEAPONMODEL | RF_ALPHAHACK ) ) == ( RF_WEAPONMODEL | RF_ALPHAHACK ) ) {
			entity_t tent = *ent;
			tent.renderfx &= ~RF_ALPHAHACK;
			tent.renderfx |= RF_NOCOLORWRITE | RF_NOSHADOW;
			R_AddEntityToScene( &tent );
		}
	}
}

void R_AddPolyToScene( const poly_t *poly ) {
	assert( sizeof( *poly->elems ) == sizeof( elem_t ) );

	if( ( rsc.numPolys < MAX_POLYS ) && poly && poly->numverts ) {
		drawSurfacePoly_t *dp = &rsc.polys[rsc.numPolys];

		assert( poly->shader != NULL );
		if( !poly->shader ) {
			return;
		}

		dp->type = ST_POLY;
		dp->shader = poly->shader;
		dp->numVerts = std::min( poly->numverts, MAX_POLY_VERTS );
		dp->xyzArray = poly->verts;
		dp->normalsArray = poly->normals;
		dp->stArray = poly->stcoords;
		dp->colorsArray = poly->colors;
		dp->numElems = poly->numelems;
		dp->elems = ( elem_t * )poly->elems;
		dp->fogNum = poly->fognum;

		// if fogNum is unset, we need to find the volume for polygon bounds
		if( !dp->fogNum ) {
			int i;
			mfog_t *fog;
			vec3_t dpmins, dpmaxs;

			ClearBounds( dpmins, dpmaxs );

			for( i = 0; i < dp->numVerts; i++ ) {
				AddPointToBounds( dp->xyzArray[i], dpmins, dpmaxs );
			}

			fog = R_FogForBounds( dpmins, dpmaxs );
			dp->fogNum = ( fog ? fog - rsh.worldBrushModel->fogs + 1 : -1 );
		}

		rsc.numPolys++;
	}
}

void R_AddLightStyleToScene( int style, float r, float g, float b ) {
	lightstyle_t *ls;

	if( style < 0 || style >= MAX_LIGHTSTYLES ) {
		Com_Error( ERR_DROP, "R_AddLightStyleToScene: bad light style %i", style );
		return;
	}

	ls = &rsc.lightStyles[style];
	ls->rgb[0] = std::max( 0.0f, r );
	ls->rgb[1] = std::max( 0.0f, g );
	ls->rgb[2] = std::max( 0.0f, b );
}

void R_RenderScene( const refdef_t *fd ) {
	int l;
	int ppFrontBuffer = 0;
	Texture *ppSource;
	shader_t *cc;
	Texture *bloomTex[NUM_BLOOM_LODS];

	if( r_norefresh->integer ) {
		return;
	}

	R_Set2DMode( false );

	RB_SetTime( fd->time );

	if( !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		rsc.refdef = *fd;
	}

	rn.refdef = *fd;
	if( !rn.refdef.minLight ) {
		rn.refdef.minLight = 0.1f;
	}

	fd = &rn.refdef;

	rn.renderFlags = RF_NONE;

	rn.farClip = R_DefaultFarClip();
	rn.clipFlags = 15;
	if( rsh.worldModel && !( fd->rdflags & RDF_NOWORLDMODEL ) && rsh.worldBrushModel->globalfog ) {
		rn.clipFlags |= 16;
	}
	rn.meshlist = &r_worldlist;
	rn.portalmasklist = &r_portalmasklist;
	rn.dlightBits = 0;

	rn.renderTarget = 0;
	rn.multisampleDepthResolved = false;

	cc = rn.refdef.colorCorrection;
	if( !( cc && cc->numpasses > 0 && cc->passes[0].images[0] ) ) {
		cc = NULL;
	}

	// clip new scissor region to the one currently set
	Vector4Set( rn.scissor, fd->scissor_x, fd->scissor_y, fd->scissor_width, fd->scissor_height );
	Vector4Set( rn.viewport, fd->x, fd->y, fd->width, fd->height );
	VectorCopy( fd->vieworg, rn.pvsOrigin );
	VectorCopy( fd->vieworg, rn.lodOrigin );

	R_BindFrameBufferObject( 0 );

	R_RenderView( fd );

	R_RenderDebugSurface( fd );

	R_BindFrameBufferObject( 0 );

	R_Set2DMode( true );

	if( !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		R_WriteSpeedsMessage( rf.speedsMsg, sizeof( rf.speedsMsg ) );
	}
}

void R_BatchPolySurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, unsigned int shadowBits, drawSurfacePoly_t *poly ) {
	mesh_t mesh;

	mesh.elems = poly->elems;
	mesh.numElems = poly->numElems;
	mesh.numVerts = poly->numVerts;
	mesh.xyzArray = poly->xyzArray;
	mesh.normalsArray = poly->normalsArray;
	mesh.lmstArray[0] = NULL;
	mesh.lmlayersArray[0] = NULL;
	mesh.stArray = poly->stArray;
	mesh.colorsArray[0] = poly->colorsArray;
	mesh.colorsArray[1] = NULL;
	mesh.sVectorsArray = NULL;

	RB_AddDynamicMesh( e, shader, fog, portalSurface, shadowBits, &mesh, GL_TRIANGLES, 0.0f, 0.0f );
}

void R_DrawPolys( void ) {
	unsigned int i;
	drawSurfacePoly_t *p;
	mfog_t *fog;

	if( rn.renderFlags & RF_ENVVIEW ) {
		return;
	}

	for( i = 0; i < rsc.numPolys; i++ ) {
		p = rsc.polys + i;
		if( p->fogNum <= 0 || (unsigned)p->fogNum > rsh.worldBrushModel->numfogs ) {
			fog = NULL;
		} else {
			fog = rsh.worldBrushModel->fogs + p->fogNum - 1;
		}

		if( !R_AddSurfToDrawList( rn.meshlist, rsc.polyent, fog, p->shader, 0, i, NULL, p ) ) {
			continue;
		}
	}
}

bool R_SurfPotentiallyFragmented( const msurface_t *surf ) {
	if( surf->flags & ( SURF_NOMARKS | SURF_NOIMPACT | SURF_NODRAW ) ) {
		return false;
	}
	return ( ( surf->facetype == FACETYPE_PLANAR )
			 || ( surf->facetype == FACETYPE_PATCH )
		/* || (surf->facetype == FACETYPE_TRISURF)*/ );
}

void R_BatchCoronaSurf( const entity_t *e, const shader_t *shader,
						const mfog_t *fog, const portalSurface_t *portalSurface, unsigned int shadowBits, drawSurfaceType_t *drawSurf ) {
	int i;
	vec3_t origin, point;
	vec3_t v_left, v_up;

	auto *const light = wsw::ref::Scene::Instance()->LightForCoronaSurf( drawSurf );

	float radius = light->radius;
	elem_t elems[6] = { 0, 1, 2, 0, 2, 3 };
	vec4_t xyz[4] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
	vec4_t normals[4] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
	byte_vec4_t colors[4];
	vec2_t texcoords[4] = { {0, 1}, {0, 0}, {1,0}, {1,1} };
	mesh_t mesh;

	VectorCopy( light->center, origin );

	VectorCopy( &rn.viewAxis[AXIS_RIGHT], v_left );
	VectorCopy( &rn.viewAxis[AXIS_UP], v_up );

	if( rn.renderFlags & ( RF_MIRRORVIEW | RF_FLIPFRONTFACE ) ) {
		VectorInverse( v_left );
	}

	VectorMA( origin, -radius, v_up, point );
	VectorMA( point, radius, v_left, xyz[0] );
	VectorMA( point, -radius, v_left, xyz[3] );

	VectorMA( origin, radius, v_up, point );
	VectorMA( point, radius, v_left, xyz[1] );
	VectorMA( point, -radius, v_left, xyz[2] );

	Vector4Set( colors[0],
				bound( 0, light->color[0] * 96, 255 ),
				bound( 0, light->color[1] * 96, 255 ),
				bound( 0, light->color[2] * 96, 255 ),
				255 );
	for( i = 1; i < 4; i++ )
		Vector4Copy( colors[0], colors[i] );

	memset( &mesh, 0, sizeof( mesh ) );
	mesh.numElems = 6;
	mesh.elems = elems;
	mesh.numVerts = 4;
	mesh.xyzArray = xyz;
	mesh.normalsArray = normals;
	mesh.stArray = texcoords;
	mesh.colorsArray[0] = colors;

	RB_AddDynamicMesh( e, shader, fog, portalSurface, 0, &mesh, GL_TRIANGLES, 0.0f, 0.0f );
}

void R_LightForOrigin( const vec3_t origin, vec3_t dir, vec4_t ambient, vec4_t diffuse, float radius, bool noWorldLight ) {
	int i, j;
	int k, s;
	int vi[3], elem[4];
	float t[8];
	vec3_t vf, vf2, tdir;
	vec3_t ambientLocal, diffuseLocal;
	vec_t *gridSize, *gridMins;
	int *gridBounds;
	mgridlight_t lightarray[8];
	lightstyle_t *lightStyles = rsc.lightStyles;

	VectorSet( ambientLocal, 0, 0, 0 );
	VectorSet( diffuseLocal, 0, 0, 0 );

	if( noWorldLight ) {
		VectorSet( dir, 0.0f, 0.0f, 0.0f );
		goto dynamic;
	}
	if( !rsh.worldModel || !rsh.worldBrushModel->lightgrid || !rsh.worldBrushModel->numlightgridelems ) {
		VectorSet( dir, 0.1f, 0.2f, 0.7f );
		goto dynamic;
	}

	gridSize = rsh.worldBrushModel->gridSize;
	gridMins = rsh.worldBrushModel->gridMins;
	gridBounds = rsh.worldBrushModel->gridBounds;

	for( i = 0; i < 3; i++ ) {
		vf[i] = ( origin[i] - gridMins[i] ) / gridSize[i];
		vi[i] = (int)vf[i];
		vf[i] = vf[i] - floor( vf[i] );
		vf2[i] = 1.0f - vf[i];
	}

	elem[0] = vi[2] * gridBounds[3] + vi[1] * gridBounds[0] + vi[0];
	elem[1] = elem[0] + gridBounds[0];
	elem[2] = elem[0] + gridBounds[3];
	elem[3] = elem[2] + gridBounds[0];

	for( i = 0; i < 4; i++ ) {
		lightarray[i * 2 + 0] = rsh.worldBrushModel->lightgrid[rsh.worldBrushModel->lightarray[bound( 0, elem[i] + 0,
																									  (int)rsh.worldBrushModel->numlightarrayelems - 1 )]];
		lightarray[i * 2 + 1] = rsh.worldBrushModel->lightgrid[rsh.worldBrushModel->lightarray[bound( 1, elem[i] + 1,
																									  (int)rsh.worldBrushModel->numlightarrayelems - 1 )]];
	}

	t[0] = vf2[0] * vf2[1] * vf2[2];
	t[1] = vf[0] * vf2[1] * vf2[2];
	t[2] = vf2[0] * vf[1] * vf2[2];
	t[3] = vf[0] * vf[1] * vf2[2];
	t[4] = vf2[0] * vf2[1] * vf[2];
	t[5] = vf[0] * vf2[1] * vf[2];
	t[6] = vf2[0] * vf[1] * vf[2];
	t[7] = vf[0] * vf[1] * vf[2];

	VectorClear( dir );

	for( i = 0; i < 4; i++ ) {
		R_LatLongToNorm( lightarray[i * 2].direction, tdir );
		VectorScale( tdir, t[i * 2], tdir );
		for( k = 0; k < MAX_LIGHTMAPS && ( s = lightarray[i * 2].styles[k] ) != 255; k++ ) {
			dir[0] += lightStyles[s].rgb[0] * tdir[0];
			dir[1] += lightStyles[s].rgb[1] * tdir[1];
			dir[2] += lightStyles[s].rgb[2] * tdir[2];
		}

		R_LatLongToNorm( lightarray[i * 2 + 1].direction, tdir );
		VectorScale( tdir, t[i * 2 + 1], tdir );
		for( k = 0; k < MAX_LIGHTMAPS && ( s = lightarray[i * 2 + 1].styles[k] ) != 255; k++ ) {
			dir[0] += lightStyles[s].rgb[0] * tdir[0];
			dir[1] += lightStyles[s].rgb[1] * tdir[1];
			dir[2] += lightStyles[s].rgb[2] * tdir[2];
		}
	}

	for( j = 0; j < 3; j++ ) {
		if( ambient ) {
			for( i = 0; i < 4; i++ ) {
				for( k = 0; k < MAX_LIGHTMAPS; k++ ) {
					if( ( s = lightarray[i * 2].styles[k] ) != 255 ) {
						ambientLocal[j] += t[i * 2] * lightarray[i * 2].ambient[k][j] * lightStyles[s].rgb[j];
					}
					if( ( s = lightarray[i * 2 + 1].styles[k] ) != 255 ) {
						ambientLocal[j] += t[i * 2 + 1] * lightarray[i * 2 + 1].ambient[k][j] * lightStyles[s].rgb[j];
					}
				}
			}
		}
		if( diffuse || radius ) {
			for( i = 0; i < 4; i++ ) {
				for( k = 0; k < MAX_LIGHTMAPS; k++ ) {
					if( ( s = lightarray[i * 2].styles[k] ) != 255 ) {
						diffuseLocal[j] += t[i * 2] * lightarray[i * 2].diffuse[k][j] * lightStyles[s].rgb[j];
					}
					if( ( s = lightarray[i * 2 + 1].styles[k] ) != 255 ) {
						diffuseLocal[j] += t[i * 2 + 1] * lightarray[i * 2 + 1].diffuse[k][j] * lightStyles[s].rgb[j];
					}
				}
			}
		}
	}

	// convert to grayscale
	if( r_lighting_grayscale->integer ) {
		vec_t grey;

		if( ambient ) {
			grey = ColorGrayscale( ambientLocal );
			ambientLocal[0] = ambientLocal[1] = ambientLocal[2] = bound( 0, grey, 255 );
		}

		if( diffuse || radius ) {
			grey = ColorGrayscale( diffuseLocal );
			diffuseLocal[0] = diffuseLocal[1] = diffuseLocal[2] = bound( 0, grey, 255 );
		}
	}

	dynamic:
	// add dynamic lights
	if( radius ) {
		wsw::ref::Scene::Instance()->DynLightDirForOrigin( origin, radius, dir, diffuseLocal, ambientLocal );
	}

	VectorNormalizeFast( dir );

	if( r_fullbright->integer ) {
		VectorSet( ambientLocal, 1, 1, 1 );
		VectorSet( diffuseLocal, 1, 1, 1 );
	} else {
		float scale = ( 1 << mapConfig.overbrightBits ) / 255.0f;

		for( i = 0; i < 3; i++ )
			ambientLocal[i] = ambientLocal[i] * scale * bound( 0.0f, r_lighting_ambientscale->value, 1.0f );
		ColorNormalize( ambientLocal, ambientLocal );

		for( i = 0; i < 3; i++ )
			diffuseLocal[i] = diffuseLocal[i] * scale * bound( 0.0f, r_lighting_directedscale->value, 1.0f );
		ColorNormalize( diffuseLocal, diffuseLocal );
	}

	if( ambient ) {
		VectorCopy( ambientLocal, ambient );
		ambient[3] = 1.0f;
	}

	if( diffuse ) {
		VectorCopy( diffuseLocal, diffuse );
		diffuse[3] = 1.0f;
	}
}

#define MAX_LIGHTMAP_IMAGES     1024

static uint8_t *r_lightmapBuffer;
static int r_lightmapBufferSize;
static Texture *r_lightmapTextures[MAX_LIGHTMAP_IMAGES];
static int r_numUploadedLightmaps;
static int r_maxLightmapBlockSize;

static void R_BuildLightmap( int w, int h, bool deluxe, const uint8_t *data, uint8_t *dest, int blockWidth, int samples ) {
	int x, y;
	uint8_t *rgba;

	if( !data || ( r_fullbright->integer && !deluxe ) ) {
		int val = deluxe ? 127 : 255;
		for( y = 0; y < h; y++ )
			memset( dest + y * blockWidth, val, w * samples * sizeof( *dest ) );
		return;
	}

	if( deluxe || !r_lighting_grayscale->integer ) { // samples == LIGHTMAP_BYTES in this case
		int wB = w * LIGHTMAP_BYTES;
		for( y = 0, rgba = dest; y < h; y++, data += wB, rgba += blockWidth )
			memcpy( rgba, data, wB );
		return;
	}

	if( r_lighting_grayscale->integer ) {
		for( y = 0; y < h; y++ ) {
			for( x = 0, rgba = dest + y * blockWidth; x < w; x++, data += LIGHTMAP_BYTES, rgba += samples ) {
				rgba[0] = bound( 0, ColorGrayscale( data ), 255 );
				if( samples > 1 ) {
					rgba[1] = rgba[0];
					rgba[2] = rgba[0];
				}
			}
		}
	} else {
		for( y = 0; y < h; y++ ) {
			for( x = 0, rgba = dest + y * blockWidth; x < w; x++, data += LIGHTMAP_BYTES, rgba += samples ) {
				rgba[0] = data[0];
				if( samples > 1 ) {
					rgba[1] = data[1];
					rgba[2] = data[2];
				}
			}
		}
	}
}

static int R_UploadLightmap( const char *name, uint8_t *data, int w, int h, int samples, bool deluxe ) {
	if( !name || !data ) {
		return r_numUploadedLightmaps;
	}
	if( r_numUploadedLightmaps == MAX_LIGHTMAP_IMAGES ) {
		// not sure what I'm supposed to do here.. an unrealistic scenario
		Com_Printf( S_COLOR_YELLOW "Warning: r_numUploadedLightmaps == MAX_LIGHTMAP_IMAGES\n" );
		return 0;
	}

	r_lightmapTextures[r_numUploadedLightmaps] = TextureCache::instance()
		->getUnderlyingFactory()->createLightmap( w, h, samples, data );

	return r_numUploadedLightmaps++;
}

static int R_PackLightmaps( int num, int w, int h, int dataSize, int stride, int samples, bool deluxe,
							const char *name, const uint8_t *data, mlightmapRect_t *rects ) {
	int i, x, y, root;
	uint8_t *block;
	int lightmapNum;
	int rectX, rectY, rectW, rectH, rectSize;
	int maxX, maxY, max, xStride;
	double tw, th, tx, ty;
	mlightmapRect_t *rect;

	maxX = r_maxLightmapBlockSize / w;
	maxY = r_maxLightmapBlockSize / h;
	max = std::min( maxX, maxY );

	Com_DPrintf( "Packing %i lightmap(s) -> ", num );

	if( !max || num == 1 || !mapConfig.lightmapsPacking ) {
		// process as it is
		R_BuildLightmap( w, h, deluxe, data, r_lightmapBuffer, w * samples, samples );

		lightmapNum = R_UploadLightmap( name, r_lightmapBuffer, w, h, samples, deluxe );
		if( rects ) {
			rects[0].texNum = lightmapNum;
			rects[0].texLayer = 0;

			// this is not a real texture matrix, but who cares?
			rects[0].texMatrix[0][0] = 1; rects[0].texMatrix[0][1] = 0;
			rects[0].texMatrix[1][0] = 1; rects[0].texMatrix[1][1] = 0;
		}

		Com_DPrintf( "\n" );

		return 1;
	}

	// find the nearest square block size
	root = ( int )sqrt( (float)num );
	if( root > max ) {
		root = max;
	}

	// keep row size a power of two
	for( i = 1; i < root; i <<= 1 ) ;
	if( i > root ) {
		i >>= 1;
	}
	root = i;

	num -= root * root;
	rectX = rectY = root;

	if( maxY > maxX ) {
		for(; ( num >= root ) && ( rectY < maxY ); rectY++, num -= root ) {
		}

		//if( !glConfig.ext.texture_non_power_of_two )
		{
			// sample down if not a power of two
			for( y = 1; y < rectY; y <<= 1 ) ;
			if( y > rectY ) {
				y >>= 1;
			}
			rectY = y;
		}
	} else {
		for(; ( num >= root ) && ( rectX < maxX ); rectX++, num -= root ) {
		}

		//if( !glConfig.ext.texture_non_power_of_two )
		{
			// sample down if not a power of two
			for( x = 1; x < rectX; x <<= 1 ) ;
			if( x > rectX ) {
				x >>= 1;
			}
			rectX = x;
		}
	}

	tw = 1.0 / (double)rectX;
	th = 1.0 / (double)rectY;

	xStride = w * samples;
	rectW = rectX * w;
	rectH = rectY * h;
	rectSize = rectW * rectH * samples * sizeof( *r_lightmapBuffer );
	if( rectSize > r_lightmapBufferSize ) {
		if( r_lightmapBuffer ) {
			Q_free( r_lightmapBuffer );
		}
		r_lightmapBuffer = (uint8_t *)Q_malloc( rectSize );
		memset( r_lightmapBuffer, 255, rectSize );
		r_lightmapBufferSize = rectSize;
	}

	Com_DPrintf( "%ix%i : %ix%i\n", rectX, rectY, rectW, rectH );

	block = r_lightmapBuffer;
	for( y = 0, ty = 0.0, num = 0, rect = rects; y < rectY; y++, ty += th, block += rectX * xStride * h ) {
		for( x = 0, tx = 0.0; x < rectX; x++, tx += tw, num++, data += dataSize * stride ) {
			R_BuildLightmap( w, h,
							 mapConfig.deluxeMappingEnabled && ( num & 1 ) ? true : false,
							 data, block + x * xStride, rectX * xStride, samples );

			// this is not a real texture matrix, but who cares?
			if( rects ) {
				rect->texMatrix[0][0] = tw; rect->texMatrix[0][1] = tx;
				rect->texMatrix[1][0] = th; rect->texMatrix[1][1] = ty;
				rect += stride;
			}
		}
	}

	lightmapNum = R_UploadLightmap( name, r_lightmapBuffer, rectW, rectH, samples, deluxe );
	if( rects ) {
		for( i = 0, rect = rects; i < num; i++, rect += stride ) {
			rect->texNum = lightmapNum;
			rect->texLayer = 0;
		}
	}

	if( rectW > mapConfig.maxLightmapSize ) {
		mapConfig.maxLightmapSize = rectW;
	}
	if( rectH > mapConfig.maxLightmapSize ) {
		mapConfig.maxLightmapSize = rectH;
	}

	return num;
}

void R_BuildLightmaps( model_t *mod, int numLightmaps, int w, int h, const uint8_t *data, mlightmapRect_t *rects ) {
	int i, j, p;
	int numBlocks = numLightmaps;
	int samples;
	int layerWidth, size;
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = ( ( mbrushmodel_t * )mod->extradata );

	samples = ( ( r_lighting_grayscale->integer && !mapConfig.deluxeMappingEnabled ) ? 1 : LIGHTMAP_BYTES );

	layerWidth = w * ( 1 + ( int )mapConfig.deluxeMappingEnabled );

	mapConfig.maxLightmapSize = 0;
	mapConfig.lightmapArrays = false && mapConfig.lightmapsPacking
							   && glConfig.ext.texture_array
							   && ( glConfig.maxVertexAttribs > VATTRIB_LMLAYERS0123 )
							   && ( glConfig.maxVaryingFloats >= ( 9 * 4 ) ) // 9th varying is required by material shaders
							   && ( layerWidth <= glConfig.maxTextureSize ) && ( h <= glConfig.maxTextureSize );

	if( mapConfig.lightmapArrays ) {
		mapConfig.maxLightmapSize = layerWidth;

		size = layerWidth * h;
	} else {
		if( !mapConfig.lightmapsPacking ) {
			size = std::max( w, h );
		} else {
			for( size = 1; ( size < r_lighting_maxlmblocksize->integer )
						   && ( size < glConfig.maxTextureSize ); size <<= 1 ) ;
		}

		if( mapConfig.deluxeMappingEnabled && ( ( size == w ) || ( size == h ) ) ) {
			Com_Printf( S_COLOR_YELLOW "Lightmap blocks larger than %ix%i aren't supported"
						", deluxemaps will be disabled\n", size, size );
			mapConfig.deluxeMappingEnabled = false;
		}

		r_maxLightmapBlockSize = size;

		size = w * h;
	}

	r_lightmapBufferSize = size * samples;
	r_lightmapBuffer = (uint8_t *)Q_malloc( r_lightmapBufferSize );
	r_numUploadedLightmaps = 0;

	if( mapConfig.lightmapArrays ) {
		int numLayers = std::min( glConfig.maxTextureLayers, 256 ); // layer index is a uint8_t
		int layer = 0;
		int lightmapNum = 0;
		Texture *image = NULL;
		mlightmapRect_t *rect = rects;
		int blockSize = w * h * LIGHTMAP_BYTES;
		float texScale = 1.0f;

		if( mapConfig.deluxeMaps ) {
			numLightmaps /= 2;
		}

		if( mapConfig.deluxeMappingEnabled ) {
			texScale = 0.5f;
		}

		auto *const textureFactory = TextureCache::instance()->getUnderlyingFactory();
		for( i = 0; i < numLightmaps; i++ ) {
			if( !layer ) {
				if( r_numUploadedLightmaps == MAX_LIGHTMAP_IMAGES ) {
					// not sure what I'm supposed to do here.. an unrealistic scenario
					Com_Printf( S_COLOR_YELLOW "Warning: r_numUploadedLightmaps == MAX_LIGHTMAP_IMAGES\n" );
					break;
				}
				lightmapNum = r_numUploadedLightmaps++;
				unsigned numArrayLayers = ( ( i + numLayers ) <= numLightmaps ) ? numLayers : numLightmaps % numLayers;
				image = textureFactory->createLightmapArray( layerWidth, h, numArrayLayers, samples );
				r_lightmapTextures[lightmapNum] = image;
			}

			R_BuildLightmap( w, h, false, data, r_lightmapBuffer, layerWidth * samples, samples );
			data += blockSize;

			rect->texNum = lightmapNum;
			rect->texLayer = layer;
			// this is not a real texture matrix, but who cares?
			rect->texMatrix[0][0] = texScale; rect->texMatrix[0][1] = 0.0f;
			rect->texMatrix[1][0] = 1.0f; rect->texMatrix[1][1] = 0.0f;
			++rect;

			if( mapConfig.deluxeMappingEnabled ) {
				R_BuildLightmap( w, h, true, data, r_lightmapBuffer + w * samples, layerWidth * samples, samples );
			}

			if( mapConfig.deluxeMaps ) {
				data += blockSize;
				++rect;
			}

			textureFactory->replaceLightmapLayer( image, layer, r_lightmapBuffer );

			++layer;
			if( layer == numLayers ) {
				layer = 0;
			}
		}
	} else {
		int stride = 1;
		int dataRowSize = size * LIGHTMAP_BYTES;

		if( mapConfig.deluxeMaps && !mapConfig.deluxeMappingEnabled ) {
			stride = 2;
			numLightmaps /= 2;
		}

		for( i = 0, j = 0; i < numBlocks; i += p * stride, j += p ) {
			p = R_PackLightmaps( numLightmaps - j, w, h, dataRowSize, stride, samples,
								 false, "*lm", data + j * dataRowSize * stride, &rects[i] );

		}
	}

	if( r_lightmapBuffer ) {
		Q_free( r_lightmapBuffer );
	}

	loadbmodel->lightmapImages = (Texture **)Q_malloc( sizeof( *loadbmodel->lightmapImages ) * r_numUploadedLightmaps );
	memcpy( loadbmodel->lightmapImages, r_lightmapTextures,
			sizeof( *loadbmodel->lightmapImages ) * r_numUploadedLightmaps );
	loadbmodel->numLightmapImages = r_numUploadedLightmaps;

	Com_DPrintf( "Packed %i lightmap blocks into %i texture(s)\n", numBlocks, r_numUploadedLightmaps );
}

void R_TouchLightmapImages( model_t *mod ) {
	unsigned int i;
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = ( ( mbrushmodel_t * )mod->extradata );

	auto *const textureCache = TextureCache::instance();
	for( i = 0; i < loadbmodel->numLightmapImages; i++ ) {
		// TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		// TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		// TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		// TODO!!!!!!!!!!!!!!!!!!!!!!!! is the cache supposed to care of lightmaps?
		textureCache->touchTexture( loadbmodel->lightmapImages[i], IMAGE_TAG_GENERIC );
	}
}

void R_InitLightStyles( model_t *mod ) {
	int i;
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = ( ( mbrushmodel_t * )mod->extradata );
	loadbmodel->superLightStyles = (superLightStyle_t *)Q_malloc( sizeof( *loadbmodel->superLightStyles ) * MAX_LIGHTSTYLES );
	loadbmodel->numSuperLightStyles = 0;

	for( i = 0; i < MAX_LIGHTSTYLES; i++ ) {
		rsc.lightStyles[i].rgb[0] = 1;
		rsc.lightStyles[i].rgb[1] = 1;
		rsc.lightStyles[i].rgb[2] = 1;
	}
}

superLightStyle_t *R_AddSuperLightStyle( model_t *mod, const int *lightmaps,
										 const uint8_t *lightmapStyles, const uint8_t *vertexStyles, mlightmapRect_t **lmRects ) {
	unsigned int i, j;
	superLightStyle_t *sls;
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = ( ( mbrushmodel_t * )mod->extradata );

	for( i = 0, sls = loadbmodel->superLightStyles; i < loadbmodel->numSuperLightStyles; i++, sls++ ) {
		for( j = 0; j < MAX_LIGHTMAPS; j++ )
			if( sls->lightmapNum[j] != lightmaps[j] ||
				sls->lightmapStyles[j] != lightmapStyles[j] ||
				sls->vertexStyles[j] != vertexStyles[j] ) {
				break;
			}
		if( j == MAX_LIGHTMAPS ) {
			return sls;
		}
	}

	if( loadbmodel->numSuperLightStyles == MAX_SUPER_STYLES ) {
		Com_Error( ERR_DROP, "R_AddSuperLightStyle: r_numSuperLightStyles == MAX_SUPER_STYLES" );
	}
	loadbmodel->numSuperLightStyles++;

	sls->vattribs = 0;
	for( j = 0; j < MAX_LIGHTMAPS; j++ ) {
		sls->lightmapNum[j] = lightmaps[j];
		sls->lightmapStyles[j] = lightmapStyles[j];
		sls->vertexStyles[j] = vertexStyles[j];

		if( lmRects && lmRects[j] && ( lightmaps[j] != -1 ) ) {
			sls->stOffset[j][0] = lmRects[j]->texMatrix[0][0];
			sls->stOffset[j][1] = lmRects[j]->texMatrix[1][0];
		} else {
			sls->stOffset[j][0] = 0;
			sls->stOffset[j][0] = 0;
		}

		if( lightmapStyles[j] != 255 ) {
			sls->vattribs |= ( VATTRIB_LMCOORDS0_BIT << j );
			if( mapConfig.lightmapArrays && !( j & 3 ) ) {
				sls->vattribs |= VATTRIB_LMLAYERS0123_BIT << ( j >> 2 );
			}
		}
	}

	return sls;
}

static int R_SuperLightStylesCmp( superLightStyle_t *sls1, superLightStyle_t *sls2 ) {
	int i;

	for( i = 0; i < MAX_LIGHTMAPS; i++ ) { // compare lightmaps
		if( sls2->lightmapNum[i] > sls1->lightmapNum[i] ) {
			return 1;
		} else if( sls1->lightmapNum[i] > sls2->lightmapNum[i] ) {
			return -1;
		}
	}

	for( i = 0; i < MAX_LIGHTMAPS; i++ ) { // compare lightmap styles
		if( sls2->lightmapStyles[i] > sls1->lightmapStyles[i] ) {
			return 1;
		} else if( sls1->lightmapStyles[i] > sls2->lightmapStyles[i] ) {
			return -1;
		}
	}

	for( i = 0; i < MAX_LIGHTMAPS; i++ ) { // compare vertex styles
		if( sls2->vertexStyles[i] > sls1->vertexStyles[i] ) {
			return 1;
		} else if( sls1->vertexStyles[i] > sls2->vertexStyles[i] ) {
			return -1;
		}
	}

	return 0; // equal
}

void R_SortSuperLightStyles( model_t *mod ) {
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = ( ( mbrushmodel_t * )mod->extradata );
	qsort( loadbmodel->superLightStyles, loadbmodel->numSuperLightStyles,
		   sizeof( superLightStyle_t ), ( int ( * )( const void *, const void * ) )R_SuperLightStylesCmp );
}

static void R_DrawSkyportal( const entity_t *e, skyportal_t *skyportal );

portalSurface_t *R_AddPortalSurface( const entity_t *ent, const shader_t *shader, void *drawSurf ) {
	portalSurface_t *portalSurface;
	bool depthPortal;

	if( !shader ) {
		return NULL;
	}

	depthPortal = !( shader->flags & ( SHADER_PORTAL_CAPTURE | SHADER_PORTAL_CAPTURE2 ) );
	if( R_FASTSKY() && depthPortal ) {
		return NULL;
	}

	if( rn.numPortalSurfaces == MAX_PORTAL_SURFACES ) {
		// not enough space
		return NULL;
	}

	portalSurface = &rn.portalSurfaces[rn.numPortalSurfaces++];
	memset( portalSurface, 0, sizeof( portalSurface_t ) );
	portalSurface->entity = ent;
	portalSurface->shader = shader;
	portalSurface->skyPortal = NULL;
	ClearBounds( portalSurface->mins, portalSurface->maxs );
	memset( portalSurface->texures, 0, sizeof( portalSurface->texures ) );

	if( depthPortal ) {
		rn.numDepthPortalSurfaces++;
	}

	return portalSurface;
}

void R_UpdatePortalSurface( portalSurface_t *portalSurface, const mesh_t *mesh,
							const vec3_t mins, const vec3_t maxs, const shader_t *shader, void *drawSurf ) {
	unsigned int i;
	float dist;
	cplane_t plane, untransformed_plane;
	vec3_t v[3];
	const entity_t *ent;

	if( !mesh || !portalSurface ) {
		return;
	}

	ent = portalSurface->entity;

	for( i = 0; i < 3; i++ ) {
		VectorCopy( mesh->xyzArray[mesh->elems[i]], v[i] );
	}

	PlaneFromPoints( v, &untransformed_plane );
	untransformed_plane.dist += DotProduct( ent->origin, untransformed_plane.normal );
	untransformed_plane.dist += 1; // nudge along the normal a bit
	CategorizePlane( &untransformed_plane );

	if( shader->flags & SHADER_AUTOSPRITE ) {
		vec3_t centre;

		// autosprites are quads, facing the viewer
		if( mesh->numVerts < 4 ) {
			return;
		}

		// compute centre as average of 4 vertices
		VectorCopy( mesh->xyzArray[mesh->elems[3]], centre );
		for( i = 0; i < 3; i++ )
			VectorAdd( centre, v[i], centre );
		VectorMA( ent->origin, 0.25, centre, centre );

		VectorNegate( &rn.viewAxis[AXIS_FORWARD], plane.normal );
		plane.dist = DotProduct( plane.normal, centre );
		CategorizePlane( &plane );
	} else {
		vec3_t temp;
		mat3_t entity_rotation;

		// regular surfaces
		if( !Matrix3_Compare( ent->axis, axis_identity ) ) {
			Matrix3_Transpose( ent->axis, entity_rotation );

			for( i = 0; i < 3; i++ ) {
				VectorCopy( v[i], temp );
				Matrix3_TransformVector( entity_rotation, temp, v[i] );
				VectorMA( ent->origin, ent->scale, v[i], v[i] );
			}

			PlaneFromPoints( v, &plane );
			CategorizePlane( &plane );
		} else {
			plane = untransformed_plane;
		}
	}

	if( ( dist = PlaneDiff( rn.viewOrigin, &plane ) ) <= BACKFACE_EPSILON ) {
		// behind the portal plane
		if( !( shader->flags & SHADER_PORTAL_CAPTURE2 ) ) {
			return;
		}

		// we need to render the backplane view
	}

	// check if portal view is opaque due to alphagen portal
	if( shader->portalDistance && dist > shader->portalDistance ) {
		return;
	}

	portalSurface->plane = plane;
	portalSurface->untransformed_plane = untransformed_plane;

	AddPointToBounds( mins, portalSurface->mins, portalSurface->maxs );
	AddPointToBounds( maxs, portalSurface->mins, portalSurface->maxs );
	VectorAdd( portalSurface->mins, portalSurface->maxs, portalSurface->centre );
	VectorScale( portalSurface->centre, 0.5, portalSurface->centre );
}

/*
* R_DrawPortalSurface
*
* Renders the portal view and captures the results from framebuffer if
* we need to do a $portalmap stage. Note that for $portalmaps we must
* use a different viewport.
*/
static void R_DrawPortalSurface( portalSurface_t *portalSurface ) {
	unsigned int i;
	int x, y, w, h;
	float dist, d, best_d;
	vec3_t viewerOrigin;
	vec3_t origin;
	mat3_t axis;
	entity_t *ent, *best;
	cplane_t *portal_plane = &portalSurface->plane, *untransformed_plane = &portalSurface->untransformed_plane;
	const shader_t *shader = portalSurface->shader;
	vec_t *portal_centre = portalSurface->centre;
	bool mirror, refraction = false;
	Texture *captureTexture;
	int captureTextureId = -1;
	int prevRenderFlags = 0;
	bool prevFlipped;
	bool doReflection, doRefraction;
	Texture *portalTexures[2] = { NULL, NULL };

	doReflection = doRefraction = true;
	if( shader->flags & SHADER_PORTAL_CAPTURE ) {
		shaderpass_t *pass;

		captureTexture = NULL;
		captureTextureId = 0;

		for( i = 0, pass = shader->passes; i < shader->numpasses; i++, pass++ ) {
			if( pass->program_type == GLSL_PROGRAM_TYPE_DISTORTION ) {
				if( ( pass->alphagen.type == ALPHA_GEN_CONST && pass->alphagen.args[0] == 1 ) ) {
					doRefraction = false;
				} else if( ( pass->alphagen.type == ALPHA_GEN_CONST && pass->alphagen.args[0] == 0 ) ) {
					doReflection = false;
				}
				break;
			}
		}
	} else {
		captureTexture = NULL;
		captureTextureId = -1;
	}

	x = y = 0;
	w = rn.refdef.width;
	h = rn.refdef.height;

	dist = PlaneDiff( rn.viewOrigin, portal_plane );
	if( dist <= BACKFACE_EPSILON || !doReflection ) {
		if( !( shader->flags & SHADER_PORTAL_CAPTURE2 ) || !doRefraction ) {
			return;
		}

		// even if we're behind the portal, we still need to capture
		// the second portal image for refraction
		refraction = true;
		captureTexture = NULL;
		captureTextureId = 1;
		if( dist < 0 ) {
			VectorInverse( portal_plane->normal );
			portal_plane->dist = -portal_plane->dist;
		}
	}

	mirror = true; // default to mirror view
	// it is stupid IMO that mirrors require a RT_PORTALSURFACE entity

	best = NULL;
	best_d = 100000000;
	for( i = rsc.numLocalEntities; i < rsc.numEntities; i++ ) {
		ent = R_NUM2ENT( i );
		if( ent->rtype != RT_PORTALSURFACE ) {
			continue;
		}

		d = PlaneDiff( ent->origin, untransformed_plane );
		if( ( d >= -64 ) && ( d <= 64 ) ) {
			d = Distance( ent->origin, portal_centre );
			if( d < best_d ) {
				best = ent;
				best_d = d;
			}
		}
	}

	if( best == NULL ) {
		if( captureTextureId < 0 ) {
			// still do a push&pop because to ensure the clean state
			if( R_PushRefInst() ) {
				R_PopRefInst();
			}
			return;
		}
	} else {
		if( !VectorCompare( best->origin, best->origin2 ) ) { // portal
			mirror = false;
		}
		best->rtype = NUM_RTYPES;
	}

	prevRenderFlags = rn.renderFlags;
	prevFlipped = ( rn.refdef.rdflags & RDF_FLIPPED ) != 0;
	if( !R_PushRefInst() ) {
		return;
	}

	VectorCopy( rn.viewOrigin, viewerOrigin );
	if( prevFlipped ) {
		VectorInverse( &rn.viewAxis[AXIS_RIGHT] );
	}

	setup_and_render:

	if( refraction ) {
		VectorInverse( portal_plane->normal );
		portal_plane->dist = -portal_plane->dist;
		CategorizePlane( portal_plane );
		VectorCopy( rn.viewOrigin, origin );
		Matrix3_Copy( rn.refdef.viewaxis, axis );
		VectorCopy( viewerOrigin, rn.pvsOrigin );

		rn.renderFlags |= RF_PORTALVIEW;
		if( prevFlipped ) {
			rn.renderFlags |= RF_FLIPFRONTFACE;
		}
	} else if( mirror ) {
		VectorReflect( rn.viewOrigin, portal_plane->normal, portal_plane->dist, origin );

		VectorReflect( &rn.viewAxis[AXIS_FORWARD], portal_plane->normal, 0, &axis[AXIS_FORWARD] );
		VectorReflect( &rn.viewAxis[AXIS_RIGHT], portal_plane->normal, 0, &axis[AXIS_RIGHT] );
		VectorReflect( &rn.viewAxis[AXIS_UP], portal_plane->normal, 0, &axis[AXIS_UP] );

		Matrix3_Normalize( axis );

		VectorCopy( viewerOrigin, rn.pvsOrigin );

		rn.renderFlags = ( prevRenderFlags ^ RF_FLIPFRONTFACE ) | RF_MIRRORVIEW;
	} else {
		vec3_t tvec;
		mat3_t A, B, C, rot;

		// build world-to-portal rotation matrix
		VectorNegate( portal_plane->normal, tvec );
		NormalVectorToAxis( tvec, A );

		// build portal_dest-to-world rotation matrix
		ByteToDir( best->frame, tvec );
		NormalVectorToAxis( tvec, B );
		Matrix3_Transpose( B, C );

		// multiply to get world-to-world rotation matrix
		Matrix3_Multiply( C, A, rot );

		// translate view origin
		VectorSubtract( rn.viewOrigin, best->origin, tvec );
		Matrix3_TransformVector( rot, tvec, origin );
		VectorAdd( origin, best->origin2, origin );

		Matrix3_Transpose( A, B );
		Matrix3_Multiply( rn.viewAxis, B, rot );
		Matrix3_Multiply( best->axis, rot, B );
		Matrix3_Transpose( C, A );
		Matrix3_Multiply( B, A, axis );

		// set up portal_plane
		VectorCopy( &axis[AXIS_FORWARD], portal_plane->normal );
		portal_plane->dist = DotProduct( best->origin2, portal_plane->normal );
		CategorizePlane( portal_plane );

		// for portals, vis data is taken from portal origin, not
		// view origin, because the view point moves around and
		// might fly into (or behind) a wall
		VectorCopy( best->origin2, rn.pvsOrigin );
		VectorCopy( best->origin2, rn.lodOrigin );

		rn.renderFlags |= RF_PORTALVIEW;

		// ignore entities, if asked politely
		if( best->renderfx & RF_NOPORTALENTS ) {
			rn.renderFlags |= RF_ENVVIEW;
		}
		if( prevFlipped ) {
			rn.renderFlags |= RF_FLIPFRONTFACE;
		}
	}

	rn.refdef.rdflags &= ~( RDF_UNDERWATER | RDF_CROSSINGWATER | RDF_FLIPPED );

	rn.meshlist = &r_portallist;
	rn.portalmasklist = NULL;

	rn.renderFlags |= RF_CLIPPLANE;
	rn.renderFlags &= ~RF_SOFT_PARTICLES;
	rn.clipPlane = *portal_plane;

	rn.farClip = R_DefaultFarClip();

	rn.clipFlags |= ( 1 << 5 );
	rn.frustum[5] = *portal_plane;
	CategorizePlane( &rn.frustum[5] );

	// if we want to render to a texture, initialize texture
	// but do not try to render to it more than once
	if( captureTextureId >= 0 ) {
		//int texFlags = shader->flags & SHADER_NO_TEX_FILTERING ? IT_NOFILTERING : 0;

		// TODO:
		//captureTexture = R_GetPortalTexture( rsc.refdef.width, rsc.refdef.height, texFlags,
		//									 rsc.frameCount );
		portalTexures[captureTextureId] = captureTexture;

		if( !captureTexture ) {
			// couldn't register a slot for this plane
			goto done;
		}

		x = y = 0;
		w = captureTexture->width;
		h = captureTexture->height;
		rn.refdef.width = w;
		rn.refdef.height = h;
		rn.refdef.x = 0;
		rn.refdef.y = 0;
		// TODO.... rn.renderTarget = captureTexture->fbo;
		rn.renderFlags |= RF_PORTAL_CAPTURE;
		Vector4Set( rn.viewport, rn.refdef.x + x, rn.refdef.y + y, w, h );
		Vector4Set( rn.scissor, rn.refdef.x + x, rn.refdef.y + y, w, h );
	} else {
		rn.renderFlags &= ~RF_PORTAL_CAPTURE;
	}

	VectorCopy( origin, rn.refdef.vieworg );
	Matrix3_Copy( axis, rn.refdef.viewaxis );

	R_RenderView( &rn.refdef );

	if( doRefraction && !refraction && ( shader->flags & SHADER_PORTAL_CAPTURE2 ) ) {
		rn.renderFlags = prevRenderFlags;
		refraction = true;
		captureTexture = NULL;
		captureTextureId = 1;
		goto setup_and_render;
	}

	done:
	portalSurface->texures[0] = portalTexures[0];
	portalSurface->texures[1] = portalTexures[1];

	R_PopRefInst();
}

/*
* R_DrawPortalsDepthMask
*
* Renders portal or sky surfaces from the BSP tree to depth buffer. Each rendered pixel
* receives the depth value of 1.0, everything else is cleared to 0.0.
*
* The depth buffer is then preserved for portal render stage to minimize overdraw.
*/
static void R_DrawPortalsDepthMask( void ) {
	float depthmin, depthmax;

	if( !rn.portalmasklist || !rn.portalmasklist->numDrawSurfs ) {
		return;
	}

	RB_GetDepthRange( &depthmin, &depthmax );

	RB_ClearDepth( depthmin );
	RB_Clear( GL_DEPTH_BUFFER_BIT, 0, 0, 0, 0 );
	RB_SetShaderStateMask( ~0, GLSTATE_DEPTHWRITE | GLSTATE_DEPTHFUNC_GT | GLSTATE_NO_COLORWRITE );
	RB_DepthRange( depthmax, depthmax );

	R_DrawSurfaces( rn.portalmasklist );

	RB_DepthRange( depthmin, depthmax );
	RB_ClearDepth( depthmax );
	RB_SetShaderStateMask( ~0, 0 );
}

void R_DrawPortals( void ) {
	unsigned int i;

	if( rf.viewcluster == -1 ) {
		return;
	}

	if( !( rn.renderFlags & ( RF_MIRRORVIEW | RF_PORTALVIEW | RF_SHADOWMAPVIEW ) ) ) {
		R_DrawPortalsDepthMask();

		// render skyportal
		if( rn.skyportalSurface ) {
			portalSurface_t *ps = rn.skyportalSurface;
			R_DrawSkyportal( ps->entity, ps->skyPortal );
		}

		// render regular portals
		for( i = 0; i < rn.numPortalSurfaces; i++ ) {
			portalSurface_t ps = rn.portalSurfaces[i];
			if( !ps.skyPortal ) {
				R_DrawPortalSurface( &ps );
				rn.portalSurfaces[i] = ps;
			}
		}
	}
}

portalSurface_t *R_AddSkyportalSurface( const entity_t *ent, const shader_t *shader, void *drawSurf ) {
	portalSurface_t *portalSurface;

	if( rn.skyportalSurface ) {
		portalSurface = rn.skyportalSurface;
	} else if( rn.numPortalSurfaces == MAX_PORTAL_SURFACES ) {
		// not enough space
		return NULL;
	} else {
		portalSurface = &rn.portalSurfaces[rn.numPortalSurfaces++];
		memset( portalSurface, 0, sizeof( *portalSurface ) );
		rn.skyportalSurface = portalSurface;
		rn.numDepthPortalSurfaces++;
	}

	R_AddSurfToDrawList( rn.portalmasklist, ent, NULL, NULL, 0, 0, NULL, drawSurf );

	portalSurface->entity = ent;
	portalSurface->shader = shader;
	portalSurface->skyPortal = &rn.refdef.skyportal;
	return rn.skyportalSurface;
}

static void R_DrawSkyportal( const entity_t *e, skyportal_t *skyportal ) {
	if( !R_PushRefInst() ) {
		return;
	}

	rn.renderFlags = ( rn.renderFlags | RF_PORTALVIEW );
	//rn.renderFlags &= ~RF_SOFT_PARTICLES;
	VectorCopy( skyportal->vieworg, rn.pvsOrigin );

	rn.farClip = R_DefaultFarClip();

	rn.clipFlags = 15;
	rn.meshlist = &r_skyportallist;
	rn.portalmasklist = NULL;
	//Vector4Set( rn.scissor, rn.refdef.x + x, rn.refdef.y + y, w, h );

	if( skyportal->noEnts ) {
		rn.renderFlags |= RF_ENVVIEW;
	}

	if( skyportal->scale ) {
		vec3_t centre, diff;

		VectorAdd( rsh.worldModel->mins, rsh.worldModel->maxs, centre );
		VectorScale( centre, 0.5f, centre );
		VectorSubtract( centre, rn.viewOrigin, diff );
		VectorMA( skyportal->vieworg, -skyportal->scale, diff, rn.refdef.vieworg );
	} else {
		VectorCopy( skyportal->vieworg, rn.refdef.vieworg );
	}

	// FIXME
	if( !VectorCompare( skyportal->viewanglesOffset, vec3_origin ) ) {
		vec3_t angles;
		mat3_t axis;

		Matrix3_Copy( rn.refdef.viewaxis, axis );
		VectorInverse( &axis[AXIS_RIGHT] );
		Matrix3_ToAngles( axis, angles );

		VectorAdd( angles, skyportal->viewanglesOffset, angles );
		AnglesToAxis( angles, axis );
		Matrix3_Copy( axis, rn.refdef.viewaxis );
	}

	rn.refdef.rdflags &= ~( RDF_UNDERWATER | RDF_CROSSINGWATER | RDF_SKYPORTALINVIEW );
	if( skyportal->fov ) {
		rn.refdef.fov_x = skyportal->fov;
		rn.refdef.fov_y = CalcFov( rn.refdef.fov_x, rn.refdef.width, rn.refdef.height );
		AdjustFov( &rn.refdef.fov_x, &rn.refdef.fov_y, glConfig.width, glConfig.height, false );
	}

	R_RenderView( &rn.refdef );

	// restore modelview and projection matrices, scissoring, etc for the main view
	R_PopRefInst();
}

void RF_AppActivate( bool active, bool minimize, bool destroy ) {
	R_Flush();
	GLimp_AppActivate( active, minimize, destroy );
}

void RF_Shutdown( bool verbose ) {
	if( TextureCache *instance = TextureCache::maybeInstance() ) {
		instance->releaseRenderTargetAttachments();
	}

	RB_Shutdown();

	R_Shutdown_( verbose );
}

static void RF_CheckCvars( void ) {
	// update gamma
	if( r_gamma->modified ) {
		r_gamma->modified = false;
		R_SetGamma( r_gamma->value );
	}

	if( r_texturefilter->modified || r_anisolevel->modified ) {
		r_texturefilter->modified = false;
		r_anisolevel->modified = false;
		TextureCache::instance()->applyFilter( wsw::StringView( r_texturefilter->string ), r_anisolevel->integer );
	}

	if( r_wallcolor->modified || r_floorcolor->modified ) {
		vec3_t wallColor, floorColor;

		sscanf( r_wallcolor->string,  "%3f %3f %3f", &wallColor[0], &wallColor[1], &wallColor[2] );
		sscanf( r_floorcolor->string, "%3f %3f %3f", &floorColor[0], &floorColor[1], &floorColor[2] );

		r_wallcolor->modified = r_floorcolor->modified = false;

		R_SetWallFloorColors( wallColor, floorColor );
	}

	// keep r_outlines_cutoff value in sane bounds to prevent wallhacking
	if( r_outlines_scale->modified ) {
		if( r_outlines_scale->value < 0 ) {
			Cvar_ForceSet( r_outlines_scale->name, "0" );
		} else if( r_outlines_scale->value > 3 ) {
			Cvar_ForceSet( r_outlines_scale->name, "3" );
		}
		r_outlines_scale->modified = false;
	}
}

void RF_BeginFrame( bool forceClear, bool forceVsync, bool uncappedFPS ) {
	int swapInterval;

	RF_CheckCvars();

	R_DataSync();

	swapInterval = r_swapinterval->integer || forceVsync ? 1 : 0;
	clamp_low( swapInterval, r_swapinterval_min->integer );

	R_BeginFrame( forceClear, swapInterval );
}

void RF_EndFrame( void ) {
	R_EndFrame();
}

void RF_BeginRegistration( void ) {
	// sync to the backend thread to ensure it's not using old assets for drawing
	R_BeginRegistration_();

	R_DeferDataSync();
	R_DataSync();

	RB_BeginRegistration();
}

void RF_EndRegistration( void ) {
	R_EndRegistration_();

	R_DeferDataSync();
	R_DataSync();

	RB_EndRegistration();
}

void R_AddLightToScene( const vec3_t org, float programIntensity, float coronaIntensity, float r, float g, float b ) {
	if( !r_dynamiclight->integer ) {
		return;
	}

	// Do a sanity check before submitting the command

	if( !( ( (bool)programIntensity | (bool)coronaIntensity ) ) ) {
		return;
	}

	if( !( (bool)r | (bool)g | (bool)b ) ) {
		return;
	}

	wsw::ref::Scene::Instance()->AddLight( org, programIntensity, coronaIntensity, r, g, b );
}

void RF_ScreenShot( const char *path, const char *name, const char *fmtstring, bool silent ) {
	R_TakeScreenShot( path, name, fmtstring, 0, 0, glConfig.width, glConfig.height, silent );
}

const char *RF_GetSpeedsMessage( char *out, size_t size ) {
	Q_strncpyz( out, rf.speedsMsg, size );
	return out;
}

int RF_GetAverageFrametime( void ) {
	return rf.frameTime.average;
}

void RF_TransformVectorToScreen( const refdef_t *rd, const vec3_t in, vec2_t out ) {
	mat4_t p, m;
	vec4_t temp, temp2;

	if( !rd || !in || !out ) {
		return;
	}

	temp[0] = in[0];
	temp[1] = in[1];
	temp[2] = in[2];
	temp[3] = 1.0f;

	if( rd->rdflags & RDF_USEORTHO ) {
		Matrix4_OrthogonalProjection( rd->ortho_x, rd->ortho_x, rd->ortho_y, rd->ortho_y,
									  -4096.0f, 4096.0f, p );
	} else {
		Matrix4_InfinitePerspectiveProjection( rd->fov_x, rd->fov_y, Z_NEAR, p, glConfig.depthEpsilon );
	}

	if( rd->rdflags & RDF_FLIPPED ) {
		p[0] = -p[0];
	}

	Matrix4_Modelview( rd->vieworg, rd->viewaxis, m );

	Matrix4_Multiply_Vector( m, temp, temp2 );
	Matrix4_Multiply_Vector( p, temp2, temp );

	if( !temp[3] ) {
		return;
	}

	out[0] = rd->x + ( temp[0] / temp[3] + 1.0f ) * rd->width * 0.5f;
	out[1] = glConfig.height - ( rd->y + ( temp[1] / temp[3] + 1.0f ) * rd->height * 0.5f );
}

bool RF_LerpTag( orientation_t *orient, const model_t *mod, int oldframe, int frame, float lerpfrac, const char *name ) {
	if( !orient ) {
		return false;
	}

	VectorClear( orient->origin );
	Matrix3_Identity( orient->axis );

	if( !name ) {
		return false;
	}

	if( mod->type == mod_skeletal ) {
		return R_SkeletalModelLerpTag( orient, (const mskmodel_t *)mod->extradata, oldframe, frame, lerpfrac, name );
	}
	if( mod->type == mod_alias ) {
		return R_AliasModelLerpTag( orient, (const maliasmodel_t *)mod->extradata, oldframe, frame, lerpfrac, name );
	}

	return false;
}

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
cvar_t *r_texturefilter;
cvar_t *r_anisolevel;
cvar_t *r_texturecompression;
cvar_t *r_picmip;
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
	if( isExtensionSupported( "GL_EXT_texture_array" ) ) {
		glConfig.ext.texture_array = true;
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

	glConfig.maxObjectLabelLen = 0;
	if( qglObjectLabel ) {
		static_assert( sizeof( int ) == sizeof( glConfig.maxObjectLabelLen ) );
		qglGetIntegerv( GL_MAX_LABEL_LENGTH, (int *)&glConfig.maxObjectLabelLen );
	}

	Cvar_Get( "r_anisolevel_max", "0", CVAR_READONLY );
	Cvar_ForceSet( "r_anisolevel_max", va_r( tmp, sizeof( tmp ), "%i", glConfig.maxTextureFilterAnisotropic ) );

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
	r_texturefilter = Cvar_Get( "r_texturefilter", "trilinear", CVAR_ARCHIVE );
	r_anisolevel = Cvar_Get( "r_anisolevel", "4", CVAR_ARCHIVE );
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
	r_wallcolor = Cvar_Get( "r_wallcolor", "192 128 192", CVAR_ARCHIVE );
	r_floorcolor = Cvar_Get( "r_floorcolor", "136 89 178", CVAR_ARCHIVE );

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
	Com_Printf( "texturefilter: %s\n", r_texturefilter->string );
	Com_Printf( "anisotropic filtering: %i\n", r_anisolevel->integer );
	Com_Printf( "vertical sync: %s\n", ( r_swapinterval->integer || r_swapinterval_min->integer ) ? "enabled" : "disabled" );

	R_PrintGLExtensionsInfo();
}

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

	memset( &rsh, 0, sizeof( rsh ) );
	memset( &rf, 0, sizeof( rf ) );

	rsh.registrationSequence = 1;
	rsh.registrationOpen = false;

	rsh.worldModelSequence = 1;

	for( i = 0; i < 256; i++ )
		rsh.sinTableByte[i] = sin( (float)i / 255.0 * M_TWOPI );

	rf.frameTime.average = 1;
	rf.swapInterval = -1;

	R_InitDrawLists();

	if( !R_RegisterGLExtensions() ) {
		QGL_Shutdown();
		return rserr_unknown;
	}

	R_SetSwapInterval( 0, -1 );

	R_FillStartupBackgroundColor( COLOR_R( glConfig.startupColor ) / 255.0f,
								  COLOR_G( glConfig.startupColor ) / 255.0f, COLOR_B( glConfig.startupColor ) / 255.0f );

	if( r_verbose ) {
		R_PrintInfo();
	}

	// load and compile GLSL programs
	RP_Init();

	R_InitVBO();

	TextureCache::init();

	TextureCache::instance()->applyFilter( wsw::StringView( r_texturefilter->string ), r_anisolevel->integer );

	MaterialCache::init();

	R_InitModels();

	wsw::ref::Scene::Init();

	R_ClearScene();

	R_InitVolatileAssets();

	R_ClearRefInstStack();

	glerr = qglGetError();
	if( glerr != GL_NO_ERROR ) {
		Com_Printf( "glGetError() = 0x%x\n", glerr );
	}

	return rserr_ok;
}

rserr_t R_TrySettingMode( int x, int y, int width, int height, int displayFrequency, const VidModeOptions &options ) {
	// If the fullscreen flag is the single difference, choose the lightweight path
	if( glConfig.width == width && glConfig.height == height ) {
		if( glConfig.fullScreen != options.fullscreen ) {
			return GLimp_SetFullscreenMode( displayFrequency, options.fullscreen );
		}
	}

	if( TextureCache *instance = TextureCache::maybeInstance() ) {
		instance->releaseRenderTargetAttachments();
	}

	RB_Shutdown();

	rserr_t err = GLimp_SetMode( x, y, width, height, displayFrequency, options );
	if( err != rserr_ok ) {
		Com_Printf( "Could not GLimp_SetMode()\n" );
	} else if( r_postinit ) {
		err = R_PostInit();
		r_postinit = false;
	}

	if( err != rserr_ok ) {
		return err;
	}

	RB_Init();

	TextureCache::instance()->createRenderTargetAttachments();

	R_BindFrameBufferObject( 0 );

	return rserr_ok;
}

static void R_InitVolatileAssets( void ) {
	// init volatile data
	R_InitSkeletalCache();
	R_InitCustomColors();

	wsw::ref::Scene::Instance()->InitVolatileAssets();

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

static void R_DestroyVolatileAssets( void ) {
	wsw::ref::Scene::Instance()->DestroyVolatileAssets();

	// kill volatile data
	R_ShutdownCustomColors();
	R_ShutdownSkeletalCache();
}

void R_BeginRegistration_( void ) {
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

void R_EndRegistration_( void ) {
	if( rsh.registrationOpen == false ) {
		return;
	}

	rsh.registrationOpen = false;

	R_FreeUnusedModels();
	R_FreeUnusedVBOs();

	MaterialCache::instance()->freeUnusedObjects();

	TextureCache::instance()->freeAllUnusedTextures();

	R_DeferDataSync();

	R_DataSync();
}

void R_Shutdown_( bool verbose ) {
	Cmd_RemoveCommand( "screenshot" );

	// free shaders, models, etc.

	R_DestroyVolatileAssets();

	wsw::ref::Scene::Shutdown();

	R_ShutdownModels();

	R_ShutdownVBO();

	MaterialCache::shutdown();

	TextureCache::shutdown();

	// destroy compiled GLSL programs
	RP_Shutdown();

	// restore original gamma
	if( glConfig.hwGamma ) {
		GLimp_SetGammaRamp( GAMMARAMP_STRIDE, glConfig.gammaRampSize, glConfig.originalGammaRamp );
	}

	// shut down OS specific OpenGL stuff like contexts, etc.
	GLimp_Shutdown();

	// shutdown our QGL subsystem
	QGL_Shutdown();
}