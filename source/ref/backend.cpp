/*
Copyright (C) 2002-2011 Victor Luchits

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
#include "program.h"
#include "backendlocal.h"
#include "../qcommon/memspecbuilder.h"

// Smaller buffer for 2D polygons. Also a workaround for some instances of a hardly explainable bug on Adreno
// that caused dynamic draws to slow everything down in some cases when normals are used with dynamic VBOs.
#define COMPACT_STREAM_VATTRIBS ( VATTRIB_POSITION_BIT | VATTRIB_COLOR0_BIT | VATTRIB_TEXCOORDS_BIT )
static elem_t dynamicStreamElems[RB_VBO_NUM_STREAMS][MAX_STREAM_VBO_ELEMENTS];

rbackend_t rb;

static void RB_SetGLDefaults();
static void RB_RegisterStreamVBOs();
static void RB_SelectTextureUnit( int tmu );

void RB_Init() {
	memset( &rb, 0, sizeof( rb ) );

	// set default OpenGL state
	RB_SetGLDefaults();
	rb.gl.scissor[2] = glConfig.width;
	rb.gl.scissor[3] = glConfig.height;

	// initialize shading
	RB_InitShading();

	// create VBO's we're going to use for streamed data
	RB_RegisterStreamVBOs();
}

void RB_Shutdown() {
}

void RB_BeginRegistration() {
	RB_RegisterStreamVBOs();
	RB_BindVBO( 0, 0 );

	// unbind all texture targets on all TMUs
	for( int i = MAX_TEXTURE_UNITS - 1; i >= 0; i-- ) {
		RB_SelectTextureUnit( i );

		qglBindTexture( GL_TEXTURE_CUBE_MAP, 0 );
		if( glConfig.ext.texture_array ) {
			qglBindTexture( GL_TEXTURE_2D_ARRAY_EXT, 0 );
		}
		qglBindTexture( GL_TEXTURE_3D, 0 );
		qglBindTexture( GL_TEXTURE_2D, 0 );
	}

	RB_FlushTextureCache();
}

void RB_EndRegistration() {
	RB_BindVBO( 0, 0 );
}

void RB_SetTime( int64_t time ) {
	rb.time = time;
	rb.nullEnt.shaderTime = Sys_Milliseconds();
}

void RB_BeginFrame() {
	Vector4Set( rb.nullEnt.shaderRGBA, 1, 1, 1, 1 );
	rb.nullEnt.scale = 1;
	VectorClear( rb.nullEnt.origin );
	Matrix3_Identity( rb.nullEnt.axis );

	// start fresh each frame
	RB_SetShaderStateMask( ~0, 0 );
	RB_BindVBO( 0, 0 );
	RB_FlushTextureCache();
}

void RB_EndFrame() {
}

static void RB_SetGLDefaults( void ) {
	if( glConfig.stencilBits ) {
		qglStencilMask( ( GLuint ) ~0 );
		qglStencilFunc( GL_EQUAL, 128, 0xFF );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );
	}

	qglDisable( GL_CULL_FACE );
	qglFrontFace( GL_CCW );
	qglDisable( GL_BLEND );
	qglDepthFunc( GL_LEQUAL );
	qglDepthMask( GL_FALSE );
	qglDisable( GL_POLYGON_OFFSET_FILL );
	qglPolygonOffset( -1.0f, 0.0f ); // units will be handled by RB_DepthOffset
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	qglEnable( GL_DEPTH_TEST );
	qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	qglFrontFace( GL_CCW );
	qglEnable( GL_SCISSOR_TEST );
}

static void RB_SelectTextureUnit( int tmu ) {
	if( tmu != rb.gl.currentTMU ) {
		rb.gl.currentTMU = tmu;
		qglActiveTexture( tmu + GL_TEXTURE0 );
	}
}

void RB_FlushTextureCache( void ) {
	rb.gl.flushTextures = true;
}

void RB_BindImage( int tmu, const Texture *tex ) {
	assert( tex != NULL );
	assert( tex->texnum != 0 );

	if( rb.gl.flushTextures ) {
		rb.gl.flushTextures = false;
		memset( rb.gl.currentTextures, 0, sizeof( rb.gl.currentTextures ) );
	}

	const GLuint texnum = tex->texnum;
	if( rb.gl.currentTextures[tmu] != texnum ) {
		rb.gl.currentTextures[tmu] = texnum;
		RB_SelectTextureUnit( tmu );
		qglBindTexture( tex->target, tex->texnum );
	}
}

void RB_DepthRange( float depthmin, float depthmax ) {
	Q_clamp( depthmin, 0.0f, 1.0f );
	Q_clamp( depthmax, 0.0f, 1.0f );
	rb.gl.depthmin = depthmin;
	rb.gl.depthmax = depthmax;
	// depthmin == depthmax is a special case when a specific depth value is going to be written
	if( ( depthmin != depthmax ) && !rb.gl.depthoffset ) {
		depthmin += 4.0f / 65535.0f;
	}
	qglDepthRange( depthmin, depthmax );
}

void RB_GetDepthRange( float* depthmin, float *depthmax ) {
	*depthmin = rb.gl.depthmin;
	*depthmax = rb.gl.depthmax;
}

void RB_DepthOffset( bool enable ) {
	float depthmin = rb.gl.depthmin;
	float depthmax = rb.gl.depthmax;
	rb.gl.depthoffset = enable;
	if( depthmin != depthmax ) {
		if( !enable ) {
			depthmin += 4.0f / 65535.0f;
		}
		qglDepthRange( depthmin, depthmax );
	}
}

void RB_ClearDepth( float depth ) {
	qglClearDepth( depth );
}

void RB_LoadCameraMatrix( const mat4_t m ) {
	Matrix4_Copy( m, rb.cameraMatrix );
}

void RB_LoadObjectMatrix( const mat4_t m ) {
	Matrix4_Copy( m, rb.objectMatrix );
	Matrix4_MultiplyFast( rb.cameraMatrix, m, rb.modelviewMatrix );
	Matrix4_Multiply( rb.projectionMatrix, rb.modelviewMatrix, rb.modelviewProjectionMatrix );
}

void RB_LoadProjectionMatrix( const mat4_t m ) {
	Matrix4_Copy( m, rb.projectionMatrix );
	Matrix4_Multiply( m, rb.modelviewMatrix, rb.modelviewProjectionMatrix );
}

void RB_Cull( int cull ) {
	if( rb.gl.faceCull != cull ) {
		if( cull ) {
			if( !rb.gl.faceCull ) {
				qglEnable( GL_CULL_FACE );
			}
			qglCullFace( cull );
			rb.gl.faceCull = cull;
		} else {
			qglDisable( GL_CULL_FACE );
			rb.gl.faceCull = 0;
		}
	}
}

void RB_SetState( int state ) {
	const int diff = rb.gl.state ^ state;
	if( !diff ) {
		return;
	}

	if( diff & GLSTATE_BLEND_MASK ) {
		if( state & GLSTATE_BLEND_MASK ) {
			int blendsrc, blenddst;

			switch( state & GLSTATE_SRCBLEND_MASK ) {
				case GLSTATE_SRCBLEND_ZERO:
					blendsrc = GL_ZERO;
					break;
				case GLSTATE_SRCBLEND_DST_COLOR:
					blendsrc = GL_DST_COLOR;
					break;
				case GLSTATE_SRCBLEND_ONE_MINUS_DST_COLOR:
					blendsrc = GL_ONE_MINUS_DST_COLOR;
					break;
				case GLSTATE_SRCBLEND_SRC_ALPHA:
					blendsrc = GL_SRC_ALPHA;
					break;
				case GLSTATE_SRCBLEND_ONE_MINUS_SRC_ALPHA:
					blendsrc = GL_ONE_MINUS_SRC_ALPHA;
					break;
				case GLSTATE_SRCBLEND_DST_ALPHA:
					blendsrc = GL_DST_ALPHA;
					break;
				case GLSTATE_SRCBLEND_ONE_MINUS_DST_ALPHA:
					blendsrc = GL_ONE_MINUS_DST_ALPHA;
					break;
				default:
				case GLSTATE_SRCBLEND_ONE:
					blendsrc = GL_ONE;
					break;
			}

			switch( state & GLSTATE_DSTBLEND_MASK ) {
				case GLSTATE_DSTBLEND_ONE:
					blenddst = GL_ONE;
					break;
				case GLSTATE_DSTBLEND_SRC_COLOR:
					blenddst = GL_SRC_COLOR;
					break;
				case GLSTATE_DSTBLEND_ONE_MINUS_SRC_COLOR:
					blenddst = GL_ONE_MINUS_SRC_COLOR;
					break;
				case GLSTATE_DSTBLEND_SRC_ALPHA:
					blenddst = GL_SRC_ALPHA;
					break;
				case GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA:
					blenddst = GL_ONE_MINUS_SRC_ALPHA;
					break;
				case GLSTATE_DSTBLEND_DST_ALPHA:
					blenddst = GL_DST_ALPHA;
					break;
				case GLSTATE_DSTBLEND_ONE_MINUS_DST_ALPHA:
					blenddst = GL_ONE_MINUS_DST_ALPHA;
					break;
				default:
				case GLSTATE_DSTBLEND_ZERO:
					blenddst = GL_ZERO;
					break;
			}

			if( !( rb.gl.state & GLSTATE_BLEND_MASK ) ) {
				qglEnable( GL_BLEND );
			}

			qglBlendFuncSeparate( blendsrc, blenddst, GL_ONE, GL_ONE );
		} else {
			qglDisable( GL_BLEND );
		}
	}

	if( diff & ( GLSTATE_NO_COLORWRITE | GLSTATE_ALPHAWRITE ) ) {
		if( state & GLSTATE_NO_COLORWRITE ) {
			qglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
		} else {
			qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, ( state & GLSTATE_ALPHAWRITE ) ? GL_TRUE : GL_FALSE );
		}
	}

	if( diff & ( GLSTATE_DEPTHFUNC_EQ | GLSTATE_DEPTHFUNC_GT ) ) {
		if( state & GLSTATE_DEPTHFUNC_EQ ) {
			qglDepthFunc( GL_EQUAL );
		} else if( state & GLSTATE_DEPTHFUNC_GT ) {
			qglDepthFunc( GL_GREATER );
		} else {
			qglDepthFunc( GL_LEQUAL );
		}
	}

	if( diff & GLSTATE_DEPTHWRITE ) {
		if( state & GLSTATE_DEPTHWRITE ) {
			qglDepthMask( GL_TRUE );
		} else {
			qglDepthMask( GL_FALSE );
		}
	}

	if( diff & GLSTATE_NO_DEPTH_TEST ) {
		if( state & GLSTATE_NO_DEPTH_TEST ) {
			qglDisable( GL_DEPTH_TEST );
		} else {
			qglEnable( GL_DEPTH_TEST );
		}
	}

	if( diff & GLSTATE_OFFSET_FILL ) {
		if( state & GLSTATE_OFFSET_FILL ) {
			qglEnable( GL_POLYGON_OFFSET_FILL );
			RB_DepthOffset( true );
		} else {
			qglDisable( GL_POLYGON_OFFSET_FILL );
			RB_DepthOffset( false );
		}
	}

	if( diff & GLSTATE_STENCIL_TEST ) {
		if( glConfig.stencilBits ) {
			if( state & GLSTATE_STENCIL_TEST ) {
				qglEnable( GL_STENCIL_TEST );
			} else {
				qglDisable( GL_STENCIL_TEST );
			}
		}
	}

	if( diff & GLSTATE_ALPHATEST ) {
		if( glConfig.ext.multisample ) {
			if( state & GLSTATE_ALPHATEST ) {
				qglEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
			} else {
				qglDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
			}
		}
	}

	rb.gl.state = state;
}

void RB_FrontFace( bool front ) {
	qglFrontFace( front ? GL_CW : GL_CCW );
	rb.gl.frontFace = front;
}

void RB_FlipFrontFace( void ) {
	RB_FrontFace( !rb.gl.frontFace );
}

void RB_BindArrayBuffer( int buffer ) {
	if( buffer != rb.gl.currentArrayVBO ) {
		qglBindBuffer( GL_ARRAY_BUFFER, buffer );
		rb.gl.currentArrayVBO = buffer;
		rb.gl.lastVAttribs = 0;
	}
}

void RB_BindElementArrayBuffer( int buffer ) {
	if( buffer != rb.gl.currentElemArrayVBO ) {
		qglBindBuffer( GL_ELEMENT_ARRAY_BUFFER, buffer );
		rb.gl.currentElemArrayVBO = buffer;
	}
}

static void RB_EnableVertexAttrib( int index, bool enable ) {
	const unsigned bit = 1 << index;
	const unsigned diff = ( rb.gl.vertexAttribEnabled & bit ) ^ ( enable ? bit : 0 );
	if( diff ) {
		if( enable ) {
			rb.gl.vertexAttribEnabled |= bit;
			qglEnableVertexAttribArray( index );
		} else {
			rb.gl.vertexAttribEnabled &= ~bit;
			qglDisableVertexAttribArray( index );
		}
	}
}

void RB_Scissor( int x, int y, int w, int h ) {
	if( ( rb.gl.scissor[0] == x ) && ( rb.gl.scissor[1] == y ) &&
		( rb.gl.scissor[2] == w ) && ( rb.gl.scissor[3] == h ) ) {
		return;
	}

	rb.gl.scissor[0] = x;
	rb.gl.scissor[1] = y;
	rb.gl.scissor[2] = w;
	rb.gl.scissor[3] = h;
	rb.gl.scissorChanged = true;
}

void RB_GetScissor( int *x, int *y, int *w, int *h ) {
	if( x ) {
		*x = rb.gl.scissor[0];
	}
	if( y ) {
		*y = rb.gl.scissor[1];
	}
	if( w ) {
		*w = rb.gl.scissor[2];
	}
	if( h ) {
		*h = rb.gl.scissor[3];
	}
}

void RB_ApplyScissor( void ) {
	int h = rb.gl.scissor[3];
	if( rb.gl.scissorChanged ) {
		rb.gl.scissorChanged = false;
		qglScissor( rb.gl.scissor[0], rb.gl.fbHeight - h - rb.gl.scissor[1], rb.gl.scissor[2], h );
	}
}

void RB_Viewport( int x, int y, int w, int h ) {
	rb.gl.viewport[0] = x;
	rb.gl.viewport[1] = y;
	rb.gl.viewport[2] = w;
	rb.gl.viewport[3] = h;
	qglViewport( x, rb.gl.fbHeight - h - y, w, h );
}

void RB_Clear( int bits, float r, float g, float b, float a ) {
	int state = rb.gl.state;

	if( bits & GL_DEPTH_BUFFER_BIT ) {
		state |= GLSTATE_DEPTHWRITE;
	}

	if( bits & GL_STENCIL_BUFFER_BIT ) {
		qglClearStencil( 128 );
	}

	if( bits & GL_COLOR_BUFFER_BIT ) {
		state = ( state & ~GLSTATE_NO_COLORWRITE ) | GLSTATE_ALPHAWRITE;
		qglClearColor( r, g, b, a );
	}

	RB_SetState( state );

	RB_ApplyScissor();

	qglClear( bits );

	RB_DepthRange( 0.0f, 1.0f );
}

void RB_BindFrameBufferObject() {
	const int width = glConfig.width;
	const int height = glConfig.height;

	if( rb.gl.fbHeight != height ) {
		rb.gl.scissorChanged = true;
	}

	rb.gl.fbWidth = width;
	rb.gl.fbHeight = height;
}

/*
* RB_RegisterStreamVBOs
*
* Allocate/keep alive dynamic vertex buffers object
* we'll steam the dynamic geometry into
*/
void RB_RegisterStreamVBOs() {
	vattribmask_t vattribs[RB_VBO_NUM_STREAMS] = {
		VATTRIBS_MASK &~VATTRIB_INSTANCES_BITS,
		COMPACT_STREAM_VATTRIBS
	};

	// allocate stream VBO's
	for( int i = 0; i < RB_VBO_NUM_STREAMS; i++ ) {
		rbDynamicStream_t *stream = &rb.dynamicStreams[i];
		if( stream->vbo ) {
			R_TouchMeshVBO( stream->vbo );
		} else {
			stream->vbo = R_CreateMeshVBO( &rb,
										   MAX_STREAM_VBO_VERTS, MAX_STREAM_VBO_ELEMENTS, 0,
										   vattribs[i], VBO_TAG_STREAM, 0 );
			stream->vertexData = (uint8_t *)Q_malloc( MAX_STREAM_VBO_VERTS * stream->vbo->vertexSize );
		}
	}
}

void RB_BindVBO( int id, int primitive ) {
	rb.primitive = primitive;

	mesh_vbo_t *vbo;
	if( id < RB_VBO_NONE ) {
		vbo = rb.dynamicStreams[-id - 1].vbo;
	} else if( id == RB_VBO_NONE ) {
		vbo = NULL;
	} else {
		vbo = R_GetVBOByIndex( id );
	}

	rb.currentVBOId = id;
	rb.currentVBO = vbo;
	if( !vbo ) {
		RB_BindArrayBuffer( 0 );
		RB_BindElementArrayBuffer( 0 );
		return;
	}

	RB_BindArrayBuffer( vbo->vertexId );
	RB_BindElementArrayBuffer( vbo->elemId );
}

void RB_AddDynamicMesh( const entity_t *entity, const shader_t *shader,
						const struct mfog_s *fog, const struct portalSurface_s *portalSurface, unsigned shadowBits,
						const struct mesh_s *mesh, int primitive, float x_offset, float y_offset ) {

	// can't (and shouldn't because that would break batching) merge strip draw calls
	// (consider simply disabling merge later in this case if models with tristrips are added in the future, but that's slow)
	assert( ( primitive == GL_TRIANGLES ) || ( primitive == GL_LINES ) );

	bool trifan = false;
	int numVerts = mesh->numVerts, numElems = mesh->numElems;
	if( !numElems ) {
		numElems = ( wsw::max( numVerts, 2 ) - 2 ) * 3;
		trifan = true;
	}

	if( !numVerts || !numElems || ( numVerts > MAX_STREAM_VBO_VERTS ) || ( numElems > MAX_STREAM_VBO_ELEMENTS ) ) {
		return;
	}

	rbDynamicDraw_t *prev = nullptr;
	if( rb.numDynamicDraws ) {
		prev = &rb.dynamicDraws[rb.numDynamicDraws - 1];
	}

	int scissor[4];
	RB_GetScissor( &scissor[0], &scissor[1], &scissor[2], &scissor[3] );

	int streamId = RB_VBO_NONE;
	bool merge = false;
	if( prev ) {
		int prevRenderFX = 0, renderFX = 0;
		if( prev->entity ) {
			prevRenderFX = prev->entity->renderfx;
		}
		if( entity ) {
			renderFX = entity->renderfx;
		}
		if( ( ( shader->flags & SHADER_ENTITY_MERGABLE ) || ( prev->entity == entity ) ) && ( prevRenderFX == renderFX ) &&
			( prev->shader == shader ) && ( prev->fog == fog ) && ( prev->portalSurface == portalSurface ) &&
			( ( prev->shadowBits && shadowBits ) || ( !prev->shadowBits && !shadowBits ) ) ) {
			// don't rebind the shader to get the VBO in this case
			streamId = prev->streamId;
			if( ( prev->shadowBits == shadowBits ) && ( prev->primitive == primitive ) &&
				( prev->offset[0] == x_offset ) && ( prev->offset[1] == y_offset ) &&
				!memcmp( prev->scissor, scissor, sizeof( scissor ) ) ) {
				merge = true;
			}
		}
	}

	vattribmask_t vattribs;
	if( streamId == RB_VBO_NONE ) {
		RB_BindShader( entity, shader, fog );
		vattribs = rb.currentVAttribs;
		streamId = ( ( vattribs & ~COMPACT_STREAM_VATTRIBS ) ? RB_VBO_STREAM : RB_VBO_STREAM_COMPACT );
	} else {
		vattribs = prev->vattribs;
	}

	rbDynamicStream_t *const stream = &rb.dynamicStreams[-streamId - 1];

	if( ( !merge && ( ( rb.numDynamicDraws + 1 ) > MAX_DYNAMIC_DRAWS ) ) ||
		( ( stream->drawElements.firstVert + stream->drawElements.numVerts + numVerts ) > MAX_STREAM_VBO_VERTS ) ||
		( ( stream->drawElements.firstElem + stream->drawElements.numElems + numElems ) > MAX_STREAM_VBO_ELEMENTS ) ) {
		// wrap if overflows
		RB_FlushDynamicMeshes();

		stream->drawElements.firstVert = 0;
		stream->drawElements.numVerts = 0;
		stream->drawElements.firstElem = 0;
		stream->drawElements.numElems = 0;

		merge = false;
	}

	rbDynamicDraw_t *draw;
	if( merge ) {
		// merge continuous draw calls
		draw = prev;
		draw->drawElements.numVerts += numVerts;
		draw->drawElements.numElems += numElems;
	} else {
		draw = &rb.dynamicDraws[rb.numDynamicDraws++];
		draw->entity = entity;
		draw->shader = shader;
		draw->fog = fog;
		draw->portalSurface = portalSurface;
		draw->shadowBits = shadowBits;
		draw->vattribs = vattribs;
		draw->streamId = streamId;
		draw->primitive = primitive;
		draw->offset[0] = x_offset;
		draw->offset[1] = y_offset;
		memcpy( draw->scissor, scissor, sizeof( scissor ) );
		draw->drawElements.firstVert = stream->drawElements.firstVert + stream->drawElements.numVerts;
		draw->drawElements.numVerts = numVerts;
		draw->drawElements.firstElem = stream->drawElements.firstElem + stream->drawElements.numElems;
		draw->drawElements.numElems = numElems;
		draw->drawElements.numInstances = 0;
	}

	const int destVertOffset = stream->drawElements.firstVert + stream->drawElements.numVerts;
	R_FillVBOVertexDataBuffer( stream->vbo, vattribs, mesh,
							   stream->vertexData + destVertOffset * stream->vbo->vertexSize );

	elem_t *destElems = dynamicStreamElems[-streamId - 1] + stream->drawElements.firstElem + stream->drawElements.numElems;
	if( trifan ) {
		R_BuildTrifanElements( destVertOffset, numElems, destElems );
	} else {
		if( primitive == GL_TRIANGLES ) {
			R_CopyOffsetTriangles( mesh->elems, numElems, destVertOffset, destElems );
		} else {
			R_CopyOffsetElements( mesh->elems, numElems, destVertOffset, destElems );
		}
	}

	stream->drawElements.numVerts += numVerts;
	stream->drawElements.numElems += numElems;
}

void RB_FlushDynamicMeshes() {
	const int numDraws = rb.numDynamicDraws;
	if( !numDraws ) {
		return;
	}

	for( int i = 0; i < RB_VBO_NUM_STREAMS; i++ ) {
		rbDynamicStream_t *const stream = &rb.dynamicStreams[i];

		// R_UploadVBO* are going to rebind buffer arrays for upload
		// so update our local VBO state cache by calling RB_BindVBO
		RB_BindVBO( -i - 1, GL_TRIANGLES ); // dummy value for primitive here

		// because of firstVert, upload elems first
		if( stream->drawElements.numElems ) {
			mesh_t elemMesh;
			memset( &elemMesh, 0, sizeof( elemMesh ) );
			elemMesh.elems = dynamicStreamElems[i] + stream->drawElements.firstElem;
			elemMesh.numElems = stream->drawElements.numElems;
			R_UploadVBOElemData( stream->vbo, 0, stream->drawElements.firstElem, &elemMesh );
			stream->drawElements.firstElem += stream->drawElements.numElems;
			stream->drawElements.numElems = 0;
		}

		if( stream->drawElements.numVerts ) {
			R_UploadVBOVertexRawData( stream->vbo, stream->drawElements.firstVert, stream->drawElements.numVerts,
									  stream->vertexData + stream->drawElements.firstVert * stream->vbo->vertexSize );
			stream->drawElements.firstVert += stream->drawElements.numVerts;
			stream->drawElements.numVerts = 0;
		}
	}

	int sx, sy, sw, sh;
	RB_GetScissor( &sx, &sy, &sw, &sh );

	mat4_t m;
	Matrix4_Copy( rb.objectMatrix, m );
	const float transx = m[12];
	const float transy = m[13];

	float offsetx = 0.0f, offsety = 0.0f;
	for( int i = 0; i < numDraws; i++ ) {
		rbDynamicDraw_t *draw = rb.dynamicDraws + i;
		RB_BindShader( draw->entity, draw->shader, draw->fog );
		RB_BindVBO( draw->streamId, draw->primitive );
		RB_SetPortalSurface( draw->portalSurface );
		RB_Scissor( draw->scissor[0], draw->scissor[1], draw->scissor[2], draw->scissor[3] );

		// translate the mesh in 2D
		if( ( offsetx != draw->offset[0] ) || ( offsety != draw->offset[1] ) ) {
			offsetx = draw->offset[0];
			offsety = draw->offset[1];
			m[12] = transx + offsetx;
			m[13] = transy + offsety;
			RB_LoadObjectMatrix( m );
		}

		const auto &drawElements = draw->drawElements;
		RB_DrawElements( nullptr, drawElements.firstVert, drawElements.numVerts, drawElements.firstElem, drawElements.numElems );
	}

	rb.numDynamicDraws = 0;

	RB_Scissor( sx, sy, sw, sh );

	// restore the original translation in the object matrix if it has been changed
	if( offsetx || offsety ) {
		m[12] = transx;
		m[13] = transy;
		RB_LoadObjectMatrix( m );
	}
}

static void RB_EnableVertexAttribs( void ) {
	const vattribmask_t vattribs = rb.currentVAttribs;
	const mesh_vbo_t *vbo = rb.currentVBO;
	const vattribmask_t hfa = vbo->halfFloatAttribs;

	assert( vattribs & VATTRIB_POSITION_BIT );

	if( ( vattribs == rb.gl.lastVAttribs ) && ( hfa == rb.gl.lastHalfFloatVAttribs ) ) {
		return;
	}

	rb.gl.lastVAttribs = vattribs;
	rb.gl.lastHalfFloatVAttribs = hfa;

	// xyz position
	RB_EnableVertexAttrib( VATTRIB_POSITION, true );
	qglVertexAttribPointer( VATTRIB_POSITION, 4, FLOAT_VATTRIB_GL_TYPE( VATTRIB_POSITION_BIT, hfa ),
							   GL_FALSE, vbo->vertexSize, ( const GLvoid * )0 );

	// normal
	if( vattribs & VATTRIB_NORMAL_BIT ) {
		RB_EnableVertexAttrib( VATTRIB_NORMAL, true );
		qglVertexAttribPointer( VATTRIB_NORMAL, 4, FLOAT_VATTRIB_GL_TYPE( VATTRIB_NORMAL_BIT, hfa ),
								   GL_FALSE, vbo->vertexSize, ( const GLvoid * )vbo->normalsOffset );
	} else {
		RB_EnableVertexAttrib( VATTRIB_NORMAL, false );
	}

	// s-vector
	if( vattribs & VATTRIB_SVECTOR_BIT ) {
		RB_EnableVertexAttrib( VATTRIB_SVECTOR, true );
		qglVertexAttribPointer( VATTRIB_SVECTOR, 4, FLOAT_VATTRIB_GL_TYPE( VATTRIB_SVECTOR_BIT, hfa ),
								   GL_FALSE, vbo->vertexSize, ( const GLvoid * )vbo->sVectorsOffset );
	} else {
		RB_EnableVertexAttrib( VATTRIB_SVECTOR, false );
	}

	// color
	if( vattribs & VATTRIB_COLOR0_BIT ) {
		RB_EnableVertexAttrib( VATTRIB_COLOR0, true );
		qglVertexAttribPointer( VATTRIB_COLOR0, 4, GL_UNSIGNED_BYTE,
								   GL_TRUE, vbo->vertexSize, (const GLvoid * )vbo->colorsOffset[0] );
	} else {
		RB_EnableVertexAttrib( VATTRIB_COLOR0, false );
	}

	// texture coordinates
	if( vattribs & VATTRIB_TEXCOORDS_BIT ) {
		RB_EnableVertexAttrib( VATTRIB_TEXCOORDS, true );
		qglVertexAttribPointer( VATTRIB_TEXCOORDS, 2, FLOAT_VATTRIB_GL_TYPE( VATTRIB_TEXCOORDS_BIT, hfa ),
								   GL_FALSE, vbo->vertexSize, ( const GLvoid * )vbo->stOffset );
	} else {
		RB_EnableVertexAttrib( VATTRIB_TEXCOORDS, false );
	}

	if( ( vattribs & VATTRIB_AUTOSPRITE_BIT ) == VATTRIB_AUTOSPRITE_BIT ) {
		// submit sprite point
		RB_EnableVertexAttrib( VATTRIB_SPRITEPOINT, true );
		qglVertexAttribPointer( VATTRIB_SPRITEPOINT, 4, FLOAT_VATTRIB_GL_TYPE( VATTRIB_AUTOSPRITE_BIT, hfa ),
								   GL_FALSE, vbo->vertexSize, ( const GLvoid * )vbo->spritePointsOffset );
	} else {
		RB_EnableVertexAttrib( VATTRIB_SPRITEPOINT, false );
	}

	// bones (skeletal models)
	if( ( vattribs & VATTRIB_BONES_BITS ) == VATTRIB_BONES_BITS ) {
		// submit indices
		RB_EnableVertexAttrib( VATTRIB_BONESINDICES, true );
		qglVertexAttribPointer( VATTRIB_BONESINDICES, 4, GL_UNSIGNED_BYTE,
								   GL_FALSE, vbo->vertexSize, ( const GLvoid * )vbo->bonesIndicesOffset );

		// submit weights
		RB_EnableVertexAttrib( VATTRIB_BONESWEIGHTS, true );
		qglVertexAttribPointer( VATTRIB_BONESWEIGHTS, 4, GL_UNSIGNED_BYTE,
								   GL_TRUE, vbo->vertexSize, ( const GLvoid * )vbo->bonesWeightsOffset );
	} else {
		// lightmap texture coordinates - aliasing bones, so not disabling bones
		int lmattr = VATTRIB_LMCOORDS01;
		int lmattrbit = VATTRIB_LMCOORDS0_BIT;

		for( int i = 0; i < ( MAX_LIGHTMAPS + 1 ) / 2; i++ ) {
			if( vattribs & lmattrbit ) {
				RB_EnableVertexAttrib( lmattr, true );
				qglVertexAttribPointer( lmattr, vbo->lmstSize[i],
										   FLOAT_VATTRIB_GL_TYPE( VATTRIB_LMCOORDS0_BIT, hfa ),
										   GL_FALSE, vbo->vertexSize, ( const GLvoid * )vbo->lmstOffset[i] );
			} else {
				RB_EnableVertexAttrib( lmattr, false );
			}

			lmattr++;
			lmattrbit <<= 2;
		}

		// lightmap array texture layers
		lmattr = VATTRIB_LMLAYERS0123;

		for( int i = 0; i < ( MAX_LIGHTMAPS + 3 ) / 4; i++ ) {
			if( vattribs & ( VATTRIB_LMLAYERS0123_BIT << i ) ) {
				RB_EnableVertexAttrib( lmattr, true );
				qglVertexAttribPointer( lmattr, 4, GL_UNSIGNED_BYTE,
										   GL_FALSE, vbo->vertexSize, ( const GLvoid * )vbo->lmlayersOffset[i] );
			} else {
				RB_EnableVertexAttrib( lmattr, false );
			}

			lmattr++;
		}
	}

	if( ( vattribs & VATTRIB_INSTANCES_BITS ) == VATTRIB_INSTANCES_BITS ) {
		RB_EnableVertexAttrib( VATTRIB_INSTANCE_QUAT, true );
		qglVertexAttribPointer( VATTRIB_INSTANCE_QUAT, 4, GL_FLOAT, GL_FALSE, 8 * sizeof( vec_t ),
								   ( const GLvoid * )vbo->instancesOffset );
		qglVertexAttribDivisor( VATTRIB_INSTANCE_QUAT, 1 );

		RB_EnableVertexAttrib( VATTRIB_INSTANCE_XYZS, true );
		qglVertexAttribPointer( VATTRIB_INSTANCE_XYZS, 4, GL_FLOAT, GL_FALSE, 8 * sizeof( vec_t ),
								   ( const GLvoid * )( vbo->instancesOffset + sizeof( vec_t ) * 4 ) );
		qglVertexAttribDivisor( VATTRIB_INSTANCE_XYZS, 1 );
	} else {
		RB_EnableVertexAttrib( VATTRIB_INSTANCE_QUAT, false );
		RB_EnableVertexAttrib( VATTRIB_INSTANCE_XYZS, false );
	}
}

void RB_DrawElementsReal( rbDrawElements_t *de ) {
	if( !( r_drawelements->integer || rb.currentEntity == &rb.nullEnt ) || !de ) {
		return;
	}

	RB_ApplyScissor();

	const int numVerts = de->numVerts;
	const int numElems = de->numElems;
	const int firstVert = de->firstVert;
	const int firstElem = de->firstElem;
	const int numInstances = de->numInstances;

	if( numInstances ) {
		// the instance data is contained in vertex attributes
		qglDrawElementsInstanced( rb.primitive, numElems, GL_UNSIGNED_SHORT,
								  (GLvoid *)( firstElem * sizeof( elem_t ) ), numInstances );
	} else {
		qglDrawRangeElements( rb.primitive, firstVert, firstVert + numVerts - 1, numElems,
							  GL_UNSIGNED_SHORT, (GLvoid *)( firstElem * sizeof( elem_t ) ) );
	}
}

static void RB_DrawElements_( const FrontendToBackendShared *fsh ) {
	if( rb.drawElements.numVerts && rb.drawElements.numElems ) [[likely]] {
		assert( rb.currentShader );

		RB_EnableVertexAttribs();

		if( rb.wireframe ) {
			RB_DrawWireframeElements( fsh );
		} else {
			RB_DrawShadedElements( fsh );
		}
	}
}

void RB_DrawElements( const FrontendToBackendShared *fsh, int firstVert, int numVerts, int firstElem, int numElems ) {
	rb.currentVAttribs &= ~VATTRIB_INSTANCES_BITS;

	rb.drawElements.numVerts = numVerts;
	rb.drawElements.numElems = numElems;
	rb.drawElements.firstVert = firstVert;
	rb.drawElements.firstElem = firstElem;
	rb.drawElements.numInstances = 0;

	RB_DrawElements_( fsh );
}

void RB_DrawElementsInstanced( const FrontendToBackendShared *fsh,
							   int firstVert, int numVerts, int firstElem, int numElems,
							   int numInstances, instancePoint_t *instances ) {
	if( numInstances ) {
		// currently not supporting dynamic instances
		// they will need a separate stream so they can be used with both static and dynamic geometry
		// (dynamic geometry will need changes to rbDynamicDraw_t)
		assert( rb.currentVBOId > RB_VBO_NONE );
		if( rb.currentVBOId > RB_VBO_NONE ) {
			rb.drawElements.numVerts = numVerts;
			rb.drawElements.numElems = numElems;
			rb.drawElements.firstVert = firstVert;
			rb.drawElements.firstElem = firstElem;
			rb.drawElements.numInstances = 0;

			if( rb.currentVBO->instancesOffset ) {
				// static VBO's must come with their own set of instance data
				rb.currentVAttribs |= VATTRIB_INSTANCES_BITS;
			}

			rb.drawElements.numInstances = numInstances;
			RB_DrawElements_( fsh );
		}
	}
}

void RB_SetCamera( const vec3_t cameraOrigin, const mat3_t cameraAxis ) {
	VectorCopy( cameraOrigin, rb.cameraOrigin );
	Matrix3_Copy( cameraAxis, rb.cameraAxis );
}

void RB_SetRenderFlags( int flags ) {
	rb.renderFlags = flags;
}

bool RB_EnableWireframe( bool enable ) {
	const bool oldVal = rb.wireframe;

	if( rb.wireframe != enable ) {
		rb.wireframe = enable;

		if( enable ) {
			RB_SetShaderStateMask( 0, GLSTATE_NO_DEPTH_TEST );
			qglPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		} else {
			RB_SetShaderStateMask( ~0, 0 );
			qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		}
	}

	return oldVal;
}

void R_SubmitAliasSurfToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, const drawSurfaceAlias_t *drawSurf ) {
	const maliasmesh_t *aliasmesh = drawSurf->mesh;

	RB_BindVBO( aliasmesh->vbo->index, GL_TRIANGLES );
	RB_DrawElements( fsh, 0, aliasmesh->numverts, 0, aliasmesh->numtris * 3 );
}

void R_SubmitSkeletalSurfToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, const drawSurfaceSkeletal_t *drawSurf ) {
	const model_t *mod = drawSurf->model;
	const mskmodel_t *skmodel = ( const mskmodel_t * )mod->extradata;
	const mskmesh_t *skmesh = drawSurf->mesh;
	skmcacheentry_s *cache = nullptr;
	dualquat_t *bonePoseRelativeDQ = nullptr;

	skmodel = ( ( mskmodel_t * )mod->extradata );
	if( skmodel->numbones && skmodel->numframes > 0 ) {
		cache = R_GetSkeletalCache( e->number, mod->lodnum );
	}

	if( cache ) {
		bonePoseRelativeDQ = R_GetSkeletalBones( cache );
	}

	if( !cache || R_SkeletalRenderAsFrame0( cache ) ) {
		// fastpath: render static frame 0 as is
		if( skmesh->vbo ) {
			RB_BindVBO( skmesh->vbo->index, GL_TRIANGLES );
			RB_DrawElements( fsh, 0, skmesh->numverts, 0, skmesh->numtris * 3 );
			return;
		}
	}

	if( bonePoseRelativeDQ && skmesh->vbo ) {
		// another fastpath: transform the initial pose on the GPU
		RB_BindVBO( skmesh->vbo->index, GL_TRIANGLES );
		RB_SetBonesData( skmodel->numbones, bonePoseRelativeDQ, skmesh->maxWeights );
		RB_DrawElements( fsh, 0, skmesh->numverts, 0, skmesh->numtris * 3 );
		return;
	}
}

void R_SubmitBSPSurfToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, const drawSurfaceBSP_t *drawSurf ) {
	// shadowBits are shared for all rendering instances (normal view, portals, etc)
	const unsigned dlightBits = drawSurf->dlightBits;

	const unsigned numVerts = drawSurf->numSpanVerts;
	assert( numVerts );
	const unsigned numElems = drawSurf->numSpanElems;
	const unsigned firstVert = drawSurf->firstVboVert + drawSurf->firstSpanVert;
	const unsigned firstElem = drawSurf->firstVboElem + drawSurf->firstSpanElem;

	RB_BindVBO( drawSurf->vbo->index, GL_TRIANGLES );

	RB_SetDlightBits( dlightBits );

	RB_SetLightstyle( drawSurf->superLightStyle );

	if( drawSurf->numInstances ) {
		RB_DrawElementsInstanced( fsh, firstVert, numVerts, firstElem, numElems, drawSurf->numInstances, drawSurf->instances );
	} else {
		RB_DrawElements( fsh, firstVert, numVerts, firstElem, numElems );
	}
}

void R_SubmitNullSurfToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, const void * ) {
	assert( rsh.nullVBO != NULL );

	RB_BindVBO( rsh.nullVBO->index, GL_LINES );
	RB_DrawElements( fsh, 0, 6, 0, 6 );
}

class MeshTesselationHelper {
public:
	vec4_t *m_tmpTessPositions { nullptr };
	vec4_t *m_tmpTessFloatColors { nullptr };
	int8_t *m_tessNeighboursCount { nullptr };
	vec4_t *m_tessPositions { nullptr };
	byte_vec4_t *m_tessByteColors { nullptr };
	uint16_t *m_neighbourLessVertices { nullptr };
	bool *m_isVertexNeighbourLess { nullptr };

	std::unique_ptr<uint8_t[]> m_allocationBuffer;
	unsigned m_lastNumNextLevelVertices { 0 };
	unsigned m_lastAllocationSize { 0 };

	template <bool SmoothColors>
	void exec( const ExternalMesh *mesh, const byte_vec4_t *overrideColors, unsigned numSimulatedVertices,
			   unsigned numNextLevelVertices, const uint16_t nextLevelNeighbours[][5] );
private:
	static constexpr unsigned kAlignment = 16;

	void setupBuffers( unsigned numSimulatedVertices, unsigned numNextLevelVertices );

	template <bool SmoothColors>
	void runPassOverOriginalVertices( const ExternalMesh *mesh, const byte_vec4_t *overrideColors,
									  unsigned numSimulatedVertices, const uint16_t nextLevelNeighbours[][5] );
	template <bool SmoothColors>
	[[nodiscard]]
	auto collectNeighbourLessVertices( unsigned numSimulatedVertices, unsigned numNextLevelVertices ) -> unsigned;
	template <bool SmoothColors>
	void processNeighbourLessVertices( unsigned numNeighbourLessVertices, const uint16_t nextLevelNeighbours[][5] );
	template <bool SmoothColors>
	void runSmoothVerticesPass( unsigned numNextLevelVertices, const uint16_t nextLevelNeighbours[][5] );
};

template <bool SmoothColors>
void MeshTesselationHelper::exec( const ExternalMesh *mesh, const byte_vec4_t *overrideColors,
								  unsigned numSimulatedVertices, unsigned numNextLevelVertices,
								  const uint16_t nextLevelNeighbours[][5] ) {
	setupBuffers( numSimulatedVertices, numNextLevelVertices );

	runPassOverOriginalVertices<SmoothColors>( mesh, overrideColors, numSimulatedVertices, nextLevelNeighbours );
	unsigned numNeighbourLessVertices = collectNeighbourLessVertices<SmoothColors>( numSimulatedVertices, numNextLevelVertices );
	processNeighbourLessVertices<SmoothColors>( numNeighbourLessVertices, nextLevelNeighbours );
	runSmoothVerticesPass<SmoothColors>( numNextLevelVertices, nextLevelNeighbours );
}

void MeshTesselationHelper::setupBuffers( unsigned numSimulatedVertices, unsigned numNextLevelVertices ) {
	if( m_lastNumNextLevelVertices != numNextLevelVertices ) {
		// Compute offsets from the base ptr
		wsw::MemSpecBuilder memSpecBuilder( wsw::MemSpecBuilder::initiallyEmpty() );

		const auto tmpTessPositionsSpec       = memSpecBuilder.addAligned<vec4_t>( numNextLevelVertices, kAlignment );
		const auto tmpTessFloatColorsSpec     = memSpecBuilder.addAligned<vec4_t>( numNextLevelVertices, kAlignment );
		const auto tessPositionsSpec          = memSpecBuilder.addAligned<vec4_t>( numNextLevelVertices, kAlignment );
		const auto tmpTessNeighboursCountSpec = memSpecBuilder.add<int8_t>( numNextLevelVertices );
		const auto tessByteColorsSpec         = memSpecBuilder.add<byte_vec4_t>( numNextLevelVertices );
		const auto neighbourLessVerticesSpec  = memSpecBuilder.add<uint16_t>( numNextLevelVertices );
		const auto isNextVertexNeighbourLess  = memSpecBuilder.add<bool>( numNextLevelVertices );

		if ( m_lastAllocationSize < memSpecBuilder.sizeSoFar() ) [[unlikely]] {
			m_lastAllocationSize = memSpecBuilder.sizeSoFar();
			m_allocationBuffer   = std::make_unique<uint8_t[]>( m_lastAllocationSize );
		}

		void *const basePtr     = m_allocationBuffer.get();
		m_tmpTessPositions      = tmpTessPositionsSpec.get( basePtr );
		m_tmpTessFloatColors    = tmpTessFloatColorsSpec.get( basePtr );
		m_tessNeighboursCount   = tmpTessNeighboursCountSpec.get( basePtr );
		m_tessPositions         = tessPositionsSpec.get( basePtr );
		m_tessByteColors        = tessByteColorsSpec.get( basePtr );
		m_neighbourLessVertices = neighbourLessVerticesSpec.get( basePtr );
		m_isVertexNeighbourLess = isNextVertexNeighbourLess.get( basePtr );

		m_lastNumNextLevelVertices = numNextLevelVertices;
	}

	assert( numSimulatedVertices && numNextLevelVertices && numSimulatedVertices < numNextLevelVertices );
	const unsigned numAddedVertices = numNextLevelVertices - numSimulatedVertices;

	std::memset( m_tessPositions, 0, sizeof( m_tessPositions[0] ) * numNextLevelVertices );
	std::memset( m_tessByteColors, 0, sizeof( m_tessByteColors[0] ) * numNextLevelVertices );
	std::memset( m_isVertexNeighbourLess, 0, sizeof( m_isVertexNeighbourLess[0] ) * numNextLevelVertices );

	std::memset( m_tmpTessPositions + numSimulatedVertices, 0, sizeof( m_tmpTessPositions[0] ) * numAddedVertices );
	std::memset( m_tmpTessFloatColors + numSimulatedVertices, 0, sizeof( m_tmpTessFloatColors[0] ) * numAddedVertices );
	std::memset( m_tessNeighboursCount + numSimulatedVertices, 0, sizeof( m_tessNeighboursCount[0] ) * numAddedVertices );
}

template <bool SmoothColors>
void MeshTesselationHelper::runPassOverOriginalVertices( const ExternalMesh *mesh,
														 const byte_vec4_t *overrideColors,
														 unsigned numSimulatedVertices,
														 const uint16_t nextLevelNeighbours[][5] ) {
	vec4_t *const __restrict tmpTessFloatColors  = std::assume_aligned<kAlignment>( m_tmpTessFloatColors );
	vec4_t *const __restrict tmpTessPositions    = std::assume_aligned<kAlignment>( m_tmpTessPositions );
	const byte_vec4_t *const __restrict colors   = overrideColors ? overrideColors : mesh->colors;
	byte_vec4_t *const __restrict tessByteColors = m_tessByteColors;
	int8_t *const __restrict tessNeighboursCount = m_tessNeighboursCount;

	// For each vertex in the original mesh
	for( unsigned vertexIndex = 0; vertexIndex < numSimulatedVertices; ++vertexIndex ) {
		const auto byteColor  = colors[vertexIndex];
		const float *position = mesh->positions[vertexIndex];

		if constexpr( SmoothColors ) {
			// Write the color to the accum buffer for smoothing it later
			Vector4Copy( byteColor, tmpTessFloatColors[vertexIndex] );
		} else {
			// Write the color directly to the resulting color buffer
			Vector4Copy( byteColor, tessByteColors[vertexIndex] );
		}

		// Copy for the further smooth pass
		Vector4Copy( position, tmpTessPositions[vertexIndex] );

		// For each neighbour of this vertex in the tesselated mesh
		for( const unsigned neighbourIndex: nextLevelNeighbours[vertexIndex] ) {
			if( neighbourIndex >= numSimulatedVertices ) {
				VectorAdd( tmpTessPositions[neighbourIndex], position, tmpTessPositions[neighbourIndex] );
				// Add integer values as is to the float accumulation buffer
				Vector4Add( tmpTessFloatColors[neighbourIndex], byteColor, tmpTessFloatColors[neighbourIndex] );
				tessNeighboursCount[neighbourIndex]++;
			}
		}
	}
}

template <bool SmoothColors>
auto MeshTesselationHelper::collectNeighbourLessVertices( unsigned numSimulatedVertices,
														  unsigned numNextLevelVertices ) -> unsigned {
	assert( numSimulatedVertices && numNextLevelVertices && numSimulatedVertices < numNextLevelVertices );

	vec4_t *const __restrict tmpTessPositions        = std::assume_aligned<kAlignment>( m_tmpTessPositions );
	vec4_t *const __restrict  tmpTessFloatColors     = std::assume_aligned<kAlignment>( m_tmpTessFloatColors );
	int8_t *const __restrict tessNeighboursCount     = m_tessNeighboursCount;
	uint16_t *const __restrict neighbourLessVertices = m_neighbourLessVertices;
	bool *const __restrict isVertexNeighbourLess     = m_isVertexNeighbourLess;
	byte_vec4_t *const __restrict tessByteColors     = m_tessByteColors;

	unsigned numNeighbourLessVertices = 0;
	for( unsigned vertexIndex = numSimulatedVertices; vertexIndex < numNextLevelVertices; ++vertexIndex ) {
		// Wtf? how do such vertices exist?
		if( !tessNeighboursCount[vertexIndex] ) [[unlikely]] {
			neighbourLessVertices[numNeighbourLessVertices++] = vertexIndex;
			isVertexNeighbourLess[vertexIndex] = true;
			continue;
		}

		const float scale = Q_Rcp( (float)tessNeighboursCount[vertexIndex] );
		VectorScale( tmpTessPositions[vertexIndex], scale, tmpTessPositions[vertexIndex] );
		tmpTessPositions[vertexIndex][3] = 1.0f;

		if constexpr( SmoothColors ) {
			// Just scale by the averaging multiplier
			Vector4Scale( tmpTessFloatColors[vertexIndex], scale, tmpTessFloatColors[vertexIndex] );
		} else {
			// Write the vertex color directly to the resulting color buffer
			Vector4Scale( tmpTessFloatColors[vertexIndex], scale, tessByteColors[vertexIndex] );
		}
	}

	return numNeighbourLessVertices;
}

template <bool SmoothColors>
void MeshTesselationHelper::processNeighbourLessVertices( unsigned numNeighbourLessVertices,
														  const uint16_t nextLevelNeighbours[][5] ) {
	vec4_t *const __restrict tmpTessFloatColors            = std::assume_aligned<kAlignment>( m_tmpTessFloatColors );
	const uint16_t *const __restrict neighbourLessVertices = m_neighbourLessVertices;
	const bool *const __restrict isVertexNeighbourLess     = m_isVertexNeighbourLess;
	byte_vec4_t *const __restrict tessByteColors           = m_tessByteColors;

	// Hack for neighbour-less vertices: apply a gathering pass
	// (the opposite to what we do for each vertex in the original mesh)
	for( unsigned i = 0; i < numNeighbourLessVertices; ++i ) {
		const unsigned vertexIndex = neighbourLessVertices[i];
		alignas( 16 ) vec4_t accumulatedColor { 0.0f, 0.0f, 0.0f, 0.0f };
		unsigned numAccumulatedColors = 0;
		for( unsigned neighbourIndex: nextLevelNeighbours[vertexIndex] ) {
			if( !isVertexNeighbourLess[neighbourIndex] ) [[likely]] {
				numAccumulatedColors++;
				if constexpr( SmoothColors ) {
					const float *const __restrict neighbourColor = tmpTessFloatColors[neighbourIndex];
					Vector4Add( neighbourColor, accumulatedColor, accumulatedColor );
				} else {
					const uint8_t *const __restrict neighbourColor = tessByteColors[neighbourIndex];
					Vector4Add( neighbourColor, accumulatedColor, accumulatedColor );
				}
			}
		}
		if( numAccumulatedColors ) [[likely]] {
			const float scale = Q_Rcp( (float)numAccumulatedColors );
			if constexpr( SmoothColors ) {
				Vector4Scale( accumulatedColor, scale, tmpTessFloatColors[vertexIndex] );
			} else {
				Vector4Scale( accumulatedColor, scale, tessByteColors[vertexIndex] );
			}
		}
	}
}

template <bool SmoothColors>
void MeshTesselationHelper::runSmoothVerticesPass( unsigned numNextLevelVertices,
												   const uint16_t nextLevelNeighbours[][5] ) {
	vec4_t *const __restrict tmpTessPositions    = std::assume_aligned<kAlignment>( m_tmpTessPositions );
	vec4_t *const __restrict tmpTessFloatColors  = std::assume_aligned<kAlignment>( m_tmpTessFloatColors );
	vec4_t *const __restrict tessPositions       = std::assume_aligned<kAlignment>( m_tessPositions );
	byte_vec4_t *const __restrict tessByteColors = m_tessByteColors;

	// Each icosphere vertex has 5 neighbours (we don't make a distinction between old and added ones for this pass)
	constexpr float icoAvgScale = 1.0f / 5.0f;
	constexpr float smoothFrac  = 0.5f;

	// Apply the smooth pass
	if constexpr( SmoothColors ) {
		for( unsigned vertexIndex = 0; vertexIndex < numNextLevelVertices; ++vertexIndex ) {
			alignas( 16 ) vec4_t sumOfNeighbourPositions { 0.0f, 0.0f, 0.0f, 0.0f };
			alignas( 16 ) vec4_t sumOfNeighbourColors { 0.0f, 0.0f, 0.0f, 0.0f };

			for( const unsigned neighbourIndex: nextLevelNeighbours[vertexIndex] ) {
				Vector4Add( tmpTessPositions[neighbourIndex], sumOfNeighbourPositions, sumOfNeighbourPositions );
				Vector4Add( tmpTessFloatColors[neighbourIndex], sumOfNeighbourColors, sumOfNeighbourColors );
			}

			Vector4Scale( sumOfNeighbourPositions, icoAvgScale, sumOfNeighbourPositions );

			// Write the average color
			Vector4Scale( sumOfNeighbourColors, icoAvgScale, tessByteColors[vertexIndex] );

			// Write combined positions
			Vector4Lerp( tmpTessPositions[vertexIndex], smoothFrac, sumOfNeighbourPositions, tessPositions[vertexIndex] );
		}
	} else {
		for( unsigned vertexIndex = 0; vertexIndex < numNextLevelVertices; ++vertexIndex ) {
			alignas( 16 ) vec4_t sumOfNeighbourPositions { 0.0f, 0.0f, 0.0f, 0.0f };

			for( const unsigned neighbourIndex: nextLevelNeighbours[vertexIndex] ) {
				Vector4Add( tmpTessPositions[neighbourIndex], sumOfNeighbourPositions, sumOfNeighbourPositions );
			}

			Vector4Scale( sumOfNeighbourPositions, icoAvgScale, sumOfNeighbourPositions );

			// Write combined positions
			Vector4Lerp( tmpTessPositions[vertexIndex], smoothFrac, sumOfNeighbourPositions, tessPositions[vertexIndex] );
		}
	}
}

static MeshTesselationHelper meshTesselationHelper;

[[nodiscard]]
static auto findLightsThatAffectBounds( const Scene::DynamicLight *lights, std::span<const uint16_t> lightIndicesSpan,
										const float *mins, const float *maxs,
										uint16_t *affectingLightIndices ) -> unsigned {
	assert( mins[3] == 0.0f && maxs[3] == 1.0f );

	const uint16_t *lightIndices = lightIndicesSpan.data();
	const auto numLights         = (unsigned)lightIndicesSpan.size();

	unsigned lightIndexNum = 0;
	unsigned numAffectingLights = 0;
	do {
		const uint16_t lightIndex = lightIndices[lightIndexNum];
		const Scene::DynamicLight *light = lights + lightIndex;

		// TODO: Use SIMD explicitly without these redundant loads/shuffles
		const bool overlaps = BoundsIntersect( light->mins, light->maxs, mins, maxs );

		affectingLightIndices[numAffectingLights] = lightIndex;
		numAffectingLights += overlaps;
	} while( ++lightIndexNum < numLights );

	return numAffectingLights;
}

void R_SubmitExternalMeshToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, const ExternalMesh *externalMesh ) {
	// Checking it this late is not a very nice things but otherwise
	// the API gets way too complicated due to some technical details.
	const float squareExtent = DistanceSquared( externalMesh->mins, externalMesh->maxs );
	if( squareExtent < 1.0f ) {
		return;
	}

	vec3_t center;
	VectorAvg( externalMesh->mins, externalMesh->maxs, center );
	const float squareDistance = DistanceSquared( fsh->viewOrigin, center );

	unsigned chosenLodIndex = 0;
	assert( externalMesh->numLods );
	// Otherwise, don't even bother checking lod conditions
	if( squareDistance > 64.0f * 64.0f ) [[likely]] {
		const float extent       = Q_Sqrt( squareExtent );
		const float rcpDistance  = Q_RSqrt( squareDistance );
		const float viewTangent  = extent * rcpDistance;
		const float tangentRatio = viewTangent * Q_Rcp( fsh->fovTangent );
		for( unsigned lodIndex = 0; lodIndex < externalMesh->numLods; ++lodIndex ) {
			if( externalMesh->lods[lodIndex].maxRatioOfViewTangentsToUse > tangentRatio ) {
				chosenLodIndex = lodIndex;
			}
		}
	}

	const ExternalMesh::LodProps *chosenLod = &externalMesh->lods[chosenLodIndex];

	RB_FlushDynamicMeshes();

	mesh_t mesh;
	memset( &mesh, 0, sizeof( mesh ) );

	byte_vec4_t *overrideColors = nullptr;

	if( externalMesh->applyVertexViewDotFade && chosenLod->neighbours ) {
		using Neighbours = const uint16_t (*)[5];
		Neighbours neighboursOfVertices;
		unsigned numVertices;

		// If tesselation is going to be performed, apply light to the base non-tesselated lod colors
		if( chosenLod->tesselate ) {
			assert( chosenLodIndex + 1 < externalMesh->numLods );
			const ExternalMesh::LodProps &nonTessLod = externalMesh->lods[chosenLodIndex + 1];
			neighboursOfVertices = (Neighbours)nonTessLod.neighbours;
			numVertices          = nonTessLod.numVertices;
		} else {
			neighboursOfVertices = (Neighbours)chosenLod->neighbours;
			numVertices = chosenLod->numVertices;
		}

		const vec4_t *const positions          = externalMesh->positions;
		const byte_vec4_t *const givenColors   = externalMesh->colors;
		const float *__restrict viewOrigin     = fsh->viewOrigin;

		overrideColors = (byte_vec4_t *)alloca( sizeof( byte_vec4_t ) * numVertices );

		unsigned vertexNum = 0;
		do {
			const uint16_t *const neighboursOfVertex = neighboursOfVertices[vertexNum];
			const float *const __restrict vertexOrigin = positions[vertexNum];
			vec3_t accumDir { 0.0f, 0.0f, 0.0f };
			unsigned neighbourIndex = 0;
			bool hasAddedDirs = false;
			do {
				const float *__restrict v1 = positions[vertexNum];
				const float *__restrict v2 = positions[neighboursOfVertex[neighbourIndex]];
				const float *__restrict v3 = positions[neighboursOfVertex[( neighbourIndex + 1 ) % 5]];
				vec3_t v1To2, v1To3, cross;
				VectorSubtract( v2, v1, v1To2 );
				VectorSubtract( v3, v1, v1To3 );
				CrossProduct( v1To2, v1To3, cross );
				if( const float squaredLength = VectorLengthSquared( cross ); squaredLength > 1.0f ) [[likely]] {
					const float rcpLength = Q_RSqrt( squaredLength );
					VectorMA( accumDir, rcpLength, cross, accumDir );
					hasAddedDirs = true;
				}
			} while( ++neighbourIndex < 5 );

			VectorCopy( givenColors[vertexNum], overrideColors[vertexNum] );
			if( hasAddedDirs ) [[likely]] {
				vec3_t viewDir;
				VectorSubtract( vertexOrigin, viewOrigin, viewDir );
				const float squareDistanceToVertex = VectorLengthSquared( viewDir );
				if( squareDistanceToVertex > 1.0f ) [[likely]] {
					vec3_t normal;
					VectorCopy( accumDir, normal );
					VectorNormalizeFast( normal );
					assert( std::fabs( VectorLengthFast( normal ) - 1.0f ) < 0.1f );
					const float rcpDistance = Q_RSqrt( squareDistanceToVertex );
					VectorScale( viewDir, rcpDistance, viewDir );
					assert( std::fabs( VectorLengthFast( viewDir ) - 1.0f ) < 0.1f );
					const float frac = std::fabs( DotProduct( viewDir, normal ) );
					const uint8_t givenByteAlpha = givenColors[vertexNum][3];
					const float modifiedAlpha = frac * (float)givenByteAlpha;
					overrideColors[vertexNum][3] = (uint8_t)( wsw::clamp( modifiedAlpha, 0.0f, 255.0f ) );
				} else {
					overrideColors[vertexNum][3] = 0;
				}
			} else {
				overrideColors[vertexNum][3] = 0;
			}
		} while( ++vertexNum < numVertices );
	}

	if( externalMesh->applyVertexDynLight && r_dynamiclight->integer && !fsh->allVisibleLightIndices.empty() ) {
		auto *const __restrict affectingLightIndices = (uint16_t *)alloca( sizeof( uint16_t ) *
															fsh->allVisibleLightIndices.size() );

		const unsigned numAffectingLights = findLightsThatAffectBounds( fsh->dynamicLights, fsh->allVisibleLightIndices,
																		externalMesh->mins, externalMesh->maxs,
																		affectingLightIndices );
		if( numAffectingLights > 0 ) {
			unsigned numVertices;
			// If tesselation is going to be performed, apply light to the base non-tesselated lod colors
			if( chosenLod->tesselate ) {
				assert( chosenLodIndex + 1 < externalMesh->numLods );
				numVertices = externalMesh->lods[chosenLodIndex + 1].numVertices;
			} else {
				numVertices = chosenLod->numVertices;
			}

			const byte_vec4_t *alphaSourceColors;
			if( overrideColors ) {
				alphaSourceColors = overrideColors;
			} else {
				overrideColors    = (byte_vec4_t *)alloca( sizeof( byte_vec4_t ) * numVertices );
				alphaSourceColors = externalMesh->colors;
			}

			assert( numVertices );
			unsigned vertexIndex = 0;
			do {
				const float *__restrict vertexOrigin  = externalMesh->positions[vertexIndex];
				auto *const __restrict givenColor     = externalMesh->colors[vertexIndex];
				auto *const __restrict resultingColor = overrideColors[vertexIndex];

				alignas( 16 ) vec4_t accumColor;
				VectorScale( givenColor, ( 1.0f / 255.0f ), accumColor );

				unsigned lightNum = 0;
				do {
					const Scene::DynamicLight *light = fsh->dynamicLights + affectingLightIndices[lightNum];
					const float squareLightToVertexDistance = DistanceSquared( light->origin, vertexOrigin );
					// May go outside [0.0, 1.0] as we test against the bounding box of the entire hull
					float impactStrength = 1.0f - Q_Sqrt( squareLightToVertexDistance ) * Q_Rcp( light->maxRadius );
					// Just clamp so the code stays branchless
					impactStrength = wsw::clamp( impactStrength, 0.0f, 1.0f );
					VectorMA( accumColor, impactStrength, light->color, accumColor );
				} while( ++lightNum < numAffectingLights );

				resultingColor[0] = (uint8_t)( 255.0f * wsw::clamp( accumColor[0], 0.0f, 1.0f ) );
				resultingColor[1] = (uint8_t)( 255.0f * wsw::clamp( accumColor[1], 0.0f, 1.0f ) );
				resultingColor[2] = (uint8_t)( 255.0f * wsw::clamp( accumColor[2], 0.0f, 1.0f ) );
				resultingColor[3] = alphaSourceColors[vertexIndex][3];
			} while( ++vertexIndex < numVertices );
		}
	}

	// HACK Perform an additional tesselation of some hulls.
	// CPU-side tesselation is the single option in the current codebase state.
	if( chosenLod->tesselate ) {
		auto *nextLevelNeighbours = (const uint16_t (*)[5])chosenLod->neighbours;
		assert( chosenLodIndex + 1 < externalMesh->numLods );
		const auto &nonTessLod = externalMesh->lods[chosenLodIndex + 1];
		assert( nonTessLod.numVertices < chosenLod->numVertices );
		assert( nonTessLod.numIndices < chosenLod->numIndices );
		const unsigned numSimulatedVertices = nonTessLod.numVertices;
		const unsigned numNextLevelVertices = chosenLod->numVertices;
		MeshTesselationHelper *const tesselationHelper = &::meshTesselationHelper;
		if( chosenLod->lerpNextLevelColors ) {
			tesselationHelper->exec<true>( externalMesh, overrideColors,
										   numSimulatedVertices, numNextLevelVertices, nextLevelNeighbours );
		} else {
			tesselationHelper->exec<false>( externalMesh, overrideColors,
											numSimulatedVertices, numNextLevelVertices, nextLevelNeighbours );
		}
		mesh.elems          = const_cast<uint16_t *>( chosenLod->indices );
		mesh.numElems       = chosenLod->numIndices;
		mesh.numVerts       = chosenLod->numVertices;
		mesh.xyzArray       = tesselationHelper->m_tessPositions;
		mesh.colorsArray[0] = tesselationHelper->m_tessByteColors;
	} else {
		mesh.elems          = const_cast<uint16_t *>( chosenLod->indices );
		mesh.numElems       = chosenLod->numIndices;
		mesh.numVerts       = chosenLod->numVertices;
		// Vertices are shared for all lods (except dynamically tesselated ones)
		mesh.xyzArray       = const_cast<vec4_t *>( externalMesh->positions );
		mesh.colorsArray[0] = overrideColors ? overrideColors : const_cast<byte_vec4_t *>( externalMesh->colors );
	}

	RB_AddDynamicMesh( e, shader, fog, portalSurface, 0, &mesh, GL_TRIANGLES, 0.0f, 0.0f );

	RB_FlushDynamicMeshes();
}

void R_SubmitSpriteSurfsToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader,
								   const mfog_t *fog, const portalSurface_t *portalSurface, std::span<const sortedDrawSurf_t> surfSpan ) {
	mesh_t mesh;

	for( size_t surfInSpan = 0; surfInSpan < surfSpan.size(); ++surfInSpan ) {

		vec3_t v_left, v_up;
		if( const float rotation = e->rotation; rotation != 0.0f ) {
			RotatePointAroundVector( v_left, &fsh->viewAxis[AXIS_FORWARD], &fsh->viewAxis[AXIS_RIGHT], rotation );
			CrossProduct( &fsh->viewAxis[AXIS_FORWARD], v_left, v_up );
		} else {
			VectorCopy( &fsh->viewAxis[AXIS_RIGHT], v_left );
			VectorCopy( &fsh->viewAxis[AXIS_UP], v_up );
		}

		if( fsh->renderFlags & (RF_MIRRORVIEW | RF_FLIPFRONTFACE ) ) {
			VectorInverse( v_left );
		}

		vec4_t xyz[4] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
		vec4_t normals[4] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };

		vec3_t point;
		const float radius = e->radius * e->scale;
		VectorMA( e->origin, -radius, v_up, point );
		VectorMA( point, radius, v_left, xyz[0] );
		VectorMA( point, -radius, v_left, xyz[3] );

		VectorMA( e->origin, radius, v_up, point );
		VectorMA( point, radius, v_left, xyz[1] );
		VectorMA( point, -radius, v_left, xyz[2] );

		byte_vec4_t colors[4];
		for( unsigned i = 0; i < 4; i++ ) {
			VectorNegate( &fsh->viewAxis[AXIS_FORWARD], normals[i] );
			Vector4Copy( e->color, colors[i] );
		}

		elem_t elems[6] = { 0, 1, 2, 0, 2, 3 };
		vec2_t texcoords[4] = { {0, 1}, {0, 0}, {1,0}, {1,1} };

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
}

void R_SubmitQuadPolysToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader, const mfog_t *fog,
								 const portalSurface_t *portalSurface, std::span<const sortedDrawSurf_t> surfSpan ) {
	constexpr const unsigned kMaxPlanes = 2;
	vec4_t positionsBuffer[kMaxPlanes * 4];
	byte_vec4_t colorsBuffer[kMaxPlanes * 4];
	vec2_t texcoordsBuffer[kMaxPlanes * 4];
	uint16_t indicesBuffer[kMaxPlanes * 6];

	for( const sortedDrawSurf_t &sds: surfSpan ) {
		const auto *__restrict p = (const QuadPoly *)sds.drawSurf;

		const float xmin = 0.0f;
		const float xmax = p->length;
		const float ymin = -0.5f * p->width;
		const float ymax = +0.5f * p->width;

		float stx = 1.0f, sty = 1.0f;
		if( p->tileLength > 0 && xmax > p->tileLength ) {
			stx = xmax * Q_Rcp( p->tileLength );
		}

		Vector2Set( texcoordsBuffer[0], 0.0f, 0.0f );
		Vector2Set( texcoordsBuffer[1], 0.0f, sty );
		Vector2Set( texcoordsBuffer[2], stx, sty );
		Vector2Set( texcoordsBuffer[3], stx, 0.0f );

		colorsBuffer[0][0] = ( uint8_t )( p->color[0] * 255 );
		colorsBuffer[0][1] = ( uint8_t )( p->color[1] * 255 );
		colorsBuffer[0][2] = ( uint8_t )( p->color[2] * 255 );
		colorsBuffer[0][3] = ( uint8_t )( p->color[3] * 255 );

		unsigned numVertices = 0, numIndices = 0;
		constexpr const float planeRollStep = 90.0f / (float)( kMaxPlanes - 1 );
		const unsigned numPlanes = ( p->flags & QuadPoly::XLike ) ? kMaxPlanes : 1;
		for( unsigned planeNum = 0; planeNum < numPlanes; ++planeNum ) {
			uint16_t *const indices   = indicesBuffer + numIndices;
			vec4_t *const positions   = positionsBuffer + numVertices;
			byte_vec4_t *const colors = colorsBuffer + numVertices;
			vec2_t *const texcoords   = texcoordsBuffer + numVertices;

			VectorSet( indices + 0, 0 + numVertices, 1 + numVertices, 2 + numVertices );
			VectorSet( indices + 3, 0 + numVertices, 2 + numVertices, 3 + numVertices );

			Vector4Set( positions[0], xmin, 0, ymin, 1 );
			Vector4Set( positions[1], xmin, 0, ymax, 1 );
			Vector4Set( positions[2], xmax, 0, ymax, 1 );
			Vector4Set( positions[3], xmax, 0, ymin, 1 );

			mat3_t axis, localAxis;
			vec3_t angles;
			VecToAngles( p->dir, angles );
			angles[ROLL] += planeRollStep * (float)planeNum;
			AnglesToAxis( angles, axis );

			Matrix3_Transpose( axis, localAxis );

			for( unsigned i = 0; i < 4; ++i ) {
				vec3_t perp;
				Matrix3_TransformVector( localAxis, positions[i], perp );
				VectorAdd( perp, p->from, positions[i] );
				// Set the proper vertex color
				Vector4Copy( colorsBuffer[0], colors[i] );
				// Copy texcoords of the first plane
				Vector2Copy( texcoordsBuffer[i], texcoords[i] );
			}

			numVertices += 4;
			numIndices += 6;
		}

		mesh_t mesh;
		memset( &mesh, 0, sizeof( mesh ) );

		mesh.elems          = indicesBuffer;
		mesh.numElems       = numIndices;
		mesh.numVerts       = numVertices;
		mesh.xyzArray       = positionsBuffer;
		mesh.stArray        = texcoordsBuffer;
		mesh.colorsArray[0] = colorsBuffer;

		RB_AddDynamicMesh( e, p->material, nullptr, nullptr, 0, &mesh, GL_TRIANGLES, 0.0f, 0.0f );
	}
}

void R_SubmitComplexPolysToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader,
									const mfog_t *fog, const portalSurface_t *portalSurface, std::span<const sortedDrawSurf_t> surfSpan ) {
	mesh_t mesh;

	for( const sortedDrawSurf_t &sds: surfSpan ) {
		const auto *__restrict poly = (const ComplexPoly *)sds.drawSurf;

		memset( &mesh, 0, sizeof( mesh ) );

		mesh.elems = poly->indices;
		mesh.numElems = poly->numIndices;
		mesh.numVerts = poly->numVertices;
		mesh.xyzArray = poly->positions;
		mesh.normalsArray = poly->normals;
		mesh.stArray = poly->texcoords;
		mesh.colorsArray[0] = poly->colors;

		RB_AddDynamicMesh( e, shader, fog, portalSurface, 0, &mesh, GL_TRIANGLES, 0.0f, 0.0f );
	}
}

[[nodiscard]]
static inline float calcSizeFracForLifetimeFrac( float lifetimeFrac, Particle::SizeBehaviour sizeBehaviour ) {
	assert( lifetimeFrac >= 0.0f && lifetimeFrac <= 1.0f );
	// Disallowed intentionally to avoid extra branching while testing the final particle dimensions for feasibility
	assert( sizeBehaviour != Particle::SizeNotChanging );

	float result;
	if( sizeBehaviour == Particle::Expanding ) {
		// Grow faster than the linear growth
		result = Q_Sqrt( lifetimeFrac );
	} else if( sizeBehaviour == Particle::Shrinking ) {
		// Shrink faster than the linear growth
		result = ( 1.0f - lifetimeFrac );
		result *= result;
	} else {
		assert( sizeBehaviour == Particle::ExpandingAndShrinking );
		if( lifetimeFrac < 0.5f ) {
			result = 2.0f * lifetimeFrac;
		} else {
			result = 2.0f * ( 1.0f - lifetimeFrac );
		}
	}
	assert( result >= 0.0f && result <= 1.0f );
	return result;
}

void R_SubmitParticleSurfsToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader,
									 const mfog_t *fog, const portalSurface_t *portalSurface, std::span<const sortedDrawSurf_t> surfSpan ) {
	mesh_t mesh;

	elem_t elems[6] = { 0, 1, 2, 0, 2, 3 };
	vec4_t xyz[4] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
	vec4_t normals[4] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
	byte_vec4_t colors[4];
	vec2_t texcoords[4] = { {0, 1}, {0, 0}, {1,0}, {1,1} };

	const auto *const firstDrawSurf = (const ParticleDrawSurface *)surfSpan.front().drawSurf;
	const auto *const aggregate = fsh->particleAggregates + firstDrawSurf->aggregateIndex;
	// Less if the aggregate is visually split by some surfaces of other kinds
	assert( surfSpan.size() <= aggregate->numParticles );

	const Particle::AppearanceRules *const __restrict appearanceRules = &aggregate->appearanceRules;

	bool applyLight                 = false;
	unsigned numAffectingLights     = 0;
	uint16_t *affectingLightIndices = nullptr;
	if( appearanceRules->applyVertexDynLight && r_dynamiclight->integer && !fsh->allVisibleLightIndices.empty() ) {
		affectingLightIndices = (uint16_t *)alloca( sizeof( uint16_t ) * fsh->allVisibleLightIndices.size() );

		numAffectingLights = findLightsThatAffectBounds( fsh->dynamicLights, fsh->allVisibleLightIndices,
														 aggregate->mins, aggregate->maxs, affectingLightIndices );
		applyLight = numAffectingLights > 0;
	}

	for( const sortedDrawSurf_t &sds: surfSpan ) {
		const auto *drawSurf = (const ParticleDrawSurface *)sds.drawSurf;

		// Ensure that the aggregate is the same
		assert( fsh->particleAggregates + drawSurf->aggregateIndex == aggregate );

		assert( drawSurf->particleIndex < aggregate->numParticles );
		const Particle *const __restrict particle = aggregate->particles + drawSurf->particleIndex;

		assert( particle->lifetimeFrac >= 0.0f && particle->lifetimeFrac <= 1.0f );

		assert( appearanceRules->kind == Particle::Sprite || appearanceRules->kind == Particle::Spark );
		if( appearanceRules->kind == Particle::Sprite ) {
			assert( appearanceRules->radius >= 0.1f );

			float signedFrac = Particle::kByteParamNormalizer * (float)particle->instanceRadiusFraction;
			float radius     = wsw::max( 0.0f, appearanceRules->radius + signedFrac * appearanceRules->radiusSpread );

			if( appearanceRules->sizeBehaviour != Particle::SizeNotChanging ) {
				radius *= calcSizeFracForLifetimeFrac( particle->lifetimeFrac, appearanceRules->sizeBehaviour );
				if( radius < 0.1f ) {
					continue;
				}
			}

			vec3_t v_left, v_up;
			VectorCopy( &fsh->viewAxis[AXIS_RIGHT], v_left );
			VectorCopy( &fsh->viewAxis[AXIS_UP], v_up );

			if( fsh->renderFlags & ( RF_MIRRORVIEW | RF_FLIPFRONTFACE ) ) {
				VectorInverse( v_left );
			}

			vec3_t point;
			VectorMA( particle->origin, -radius, v_up, point );
			VectorMA( point, radius, v_left, xyz[0] );
			VectorMA( point, -radius, v_left, xyz[3] );

			VectorMA( particle->origin, radius, v_up, point );
			VectorMA( point, radius, v_left, xyz[1] );
			VectorMA( point, -radius, v_left, xyz[2] );
		} else {
			assert( appearanceRules->length >= 0.1f && appearanceRules->width >= 0.1f );

			float lengthSignedFrac = Particle::kByteParamNormalizer * (float)particle->instanceLengthFraction;
			float widthSignedFrac  = Particle::kByteParamNormalizer * (float)particle->instanceWidthFraction;

			float length = wsw::max( 0.0f, appearanceRules->length + lengthSignedFrac * appearanceRules->lengthSpread );
			float width  = wsw::max( 0.0f, appearanceRules->width + widthSignedFrac * appearanceRules->widthSpread );

			if( appearanceRules->sizeBehaviour != Particle::SizeNotChanging ) {
				const float sizeFrac = calcSizeFracForLifetimeFrac( particle->lifetimeFrac, appearanceRules->sizeBehaviour );
				length *= sizeFrac;
				width  *= sizeFrac;
				if( length < 0.1f || width < 0.1f ) {
					continue;
				}
			}

			mat3_t axis, localAxis;
			if( float squareSpeed = VectorLengthSquared( particle->velocity ); squareSpeed > 1.0f ) [[likely]] {
				if( float squareDist = DistanceSquared( particle->origin, fsh->viewOrigin ); squareDist > 1.0f ) [[likely]] {
					const float rcpSpeed = Q_RSqrt( squareSpeed );
					const float rcpDist  = Q_RSqrt( squareDist );
					VectorScale( particle->velocity, rcpSpeed, &axis[AXIS_FORWARD] );
					VectorSubtract( fsh->viewOrigin, particle->origin, &axis[AXIS_RIGHT] );
					VectorScale( &axis[AXIS_RIGHT], rcpDist, &axis[AXIS_RIGHT] );
					CrossProduct( &axis[AXIS_FORWARD], &axis[AXIS_RIGHT], &axis[AXIS_UP] );
				} else {
					continue;
				}
			} else {
				continue;
			}

			Matrix3_Transpose( axis, localAxis );

			const float xmin = 0;
			const float xmax = length;
			const float ymin = -0.5f * width;
			const float ymax = +0.5f * width;

			VectorSet( xyz[0], xmin, 0, ymin );
			VectorSet( xyz[1], xmin, 0, ymax );
			VectorSet( xyz[2], xmax, 0, ymax );
			VectorSet( xyz[3], xmax, 0, ymin );

			vec3_t tmp[4];
			Matrix3_TransformVector( localAxis, xyz[0], tmp[0] );
			VectorAdd( tmp[0], particle->origin, xyz[0] );
			Matrix3_TransformVector( localAxis, xyz[1], tmp[1] );
			VectorAdd( tmp[1], particle->origin, xyz[1] );
			Matrix3_TransformVector( localAxis, xyz[2], tmp[2] );
			VectorAdd( tmp[2], particle->origin, xyz[2] );
			Matrix3_TransformVector( localAxis, xyz[3], tmp[3] );
			VectorAdd( tmp[3], particle->origin, xyz[3] );
		}

		assert( particle->lifetimeFrac >= 0.0f && particle->lifetimeFrac < 1.0f );

		const float *const __restrict initialColor  = appearanceRules->initialColors[particle->instanceColorIndex];
		const float *const __restrict fadedInColor  = appearanceRules->fadedInColors[particle->instanceColorIndex];
		const float *const __restrict fadedOutColor = appearanceRules->fadedOutColors[particle->instanceColorIndex];

		vec4_t colorBuffer;
		if( particle->lifetimeFrac < appearanceRules->fadeInLifetimeFrac ) [[unlikely]] {
			// Fade in
			const float fadeInFrac = particle->lifetimeFrac * Q_Rcp( appearanceRules->fadeInLifetimeFrac );
			Vector4Lerp( initialColor, fadeInFrac, fadedInColor, colorBuffer );
		} else {
			const float startFadeOutAtLifetimeFrac = 1.0f - appearanceRules->fadeOutLifetimeFrac;
			if( particle->lifetimeFrac > startFadeOutAtLifetimeFrac ) [[unlikely]] {
				// Fade out
				float fadeOutFrac = particle->lifetimeFrac - startFadeOutAtLifetimeFrac;
				fadeOutFrac *= Q_Rcp( appearanceRules->fadeOutLifetimeFrac );
				Vector4Lerp( fadedInColor, fadeOutFrac, fadedOutColor, colorBuffer );
			} else {
				// Use the color of the "faded-in" state
				Vector4Copy( fadedInColor, colorBuffer );
			}
		}

		if( applyLight ) {
			alignas( 16 ) vec4_t addedLight { 0.0f, 0.0f, 0.0f, 1.0f };
			unsigned lightNum = 0;
			do {
				const Scene::DynamicLight *light = fsh->dynamicLights + affectingLightIndices[lightNum];
				const float squareDistance = DistanceSquared( light->origin, particle->origin );
				// May go outside [0.0, 1.0] as we test against the bounding box of the entire aggregate
				float impactStrength = 1.0f - Q_Sqrt( squareDistance ) * Q_Rcp( light->maxRadius );
				// Just clamp so the code stays branchless
				impactStrength = wsw::clamp( impactStrength, 0.0f, 1.0f );
				VectorMA( addedLight, impactStrength, light->color, addedLight );
			} while( ++lightNum < numAffectingLights );
			// The clipping due to LDR limitations sucks...
			// TODO: Pass as a floating-point attribute to a GPU program?
			colorBuffer[0] = wsw::min( 1.0f, colorBuffer[0] + addedLight[0] );
			colorBuffer[1] = wsw::min( 1.0f, colorBuffer[1] + addedLight[1] );
			colorBuffer[2] = wsw::min( 1.0f, colorBuffer[2] + addedLight[2] );
		}

		Vector4Set( colors[0],
					(uint8_t)( 255 * colorBuffer[0] ),
					(uint8_t)( 255 * colorBuffer[1] ),
					(uint8_t)( 255 * colorBuffer[2] ),
					(uint8_t)( 255 * colorBuffer[3] ) );

		Vector4Copy( colors[0], colors[1] );
		Vector4Copy( colors[0], colors[2] );
		Vector4Copy( colors[0], colors[3] );

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
}

void R_SubmitCoronaSurfsToBackend( const FrontendToBackendShared *fsh, const entity_t *e, const shader_t *shader,
								   const mfog_t *fog, const portalSurface_t *portalSurface, std::span<const sortedDrawSurf_t> surfSpan ) {
	mesh_t mesh;
	elem_t elems[6] = { 0, 1, 2, 0, 2, 3 };
	vec4_t xyz[4] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
	vec4_t normals[4] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
	byte_vec4_t colors[4];
	vec2_t texcoords[4] = { {0, 1}, {0, 0}, {1,0}, {1,1} };

	vec3_t v_left, v_up;
	VectorCopy( &fsh->viewAxis[AXIS_RIGHT], v_left );
	VectorCopy( &fsh->viewAxis[AXIS_UP], v_up );

	if( fsh->renderFlags & ( RF_MIRRORVIEW | RF_FLIPFRONTFACE ) ) {
		VectorInverse( v_left );
	}

	for( const sortedDrawSurf_t &sds: surfSpan ) {
		const auto *light = (const Scene::DynamicLight *)sds.drawSurf;

		assert( light && light->hasCoronaLight );

		const float radius = light->coronaRadius;

		vec3_t origin;
		VectorCopy( light->origin, origin );

		vec3_t point;
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

		Vector4Copy( colors[0], colors[1] );
		Vector4Copy( colors[0], colors[2] );
		Vector4Copy( colors[0], colors[3] );

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
}