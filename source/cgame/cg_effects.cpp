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

// cg_effects.c -- entity effects parsing and management

#include "cg_local.h"

#include "../qcommon/singletonholder.h"
#include "../client/client.h"
#include "../qcommon/links.h"

typedef struct
{
	int length;
	float value[3];
	float map[MAX_QPATH];
} cg_lightStyle_t;

cg_lightStyle_t cg_lightStyle[MAX_LIGHTSTYLES];

/*
* CG_ClearLightStyles
*/
void CG_ClearLightStyles( void ) {
	memset( cg_lightStyle, 0, sizeof( cg_lightStyle ) );
}

/*
* CG_RunLightStyles
*/
void CG_RunLightStyles( void ) {
	int i;
	float f;
	int ofs;
	cg_lightStyle_t *ls;

	f = cg.time / 100.0f;
	ofs = (int)floor( f );
	f = f - ofs;

	for( i = 0, ls = cg_lightStyle; i < MAX_LIGHTSTYLES; i++, ls++ ) {
		if( !ls->length ) {
			ls->value[0] = ls->value[1] = ls->value[2] = 1.0;
			continue;
		}
		if( ls->length == 1 ) {
			ls->value[0] = ls->value[1] = ls->value[2] = ls->map[0];
		} else {
			ls->value[0] = ls->value[1] = ls->value[2] = ( ls->map[ofs % ls->length] * f + ( 1 - f ) * ls->map[( ofs - 1 ) % ls->length] );
		}
	}
}

/*
* CG_SetLightStyle
*/
void CG_SetLightStyle( unsigned i, const wsw::StringView &s ) {
	const auto length = s.length();
	if( length >= MAX_QPATH ) {
		CG_Error( "CL_SetLightstyle length = %i", (int)length );
	}

	cg_lightStyle[i].length = length;
	for( unsigned k = 0; k < length; k++ )
		cg_lightStyle[i].map[k] = (float)( s[k] - 'a' ) / (float)( 'm' - 'a' );
}

void CG_AddLightStyles( DrawSceneRequest * ) {
	int i;
	cg_lightStyle_t *ls;

	//for( i = 0, ls = cg_lightStyle; i < MAX_LIGHTSTYLES; i++, ls++ )
		//R_AddLightStyleToScene( i, ls->value[0], ls->value[1], ls->value[2] );
}

/*
==============================================================

PARTICLE MANAGEMENT

==============================================================
*/

typedef struct particle_s
{
	float time;

	vec3_t org;
	vec3_t vel;
	vec3_t accel;
	vec3_t color;
	float alpha;
	float alphavel;
	float scale;
	bool fog;

	poly_t poly;
	vec4_t pVerts[4];
	vec2_t pStcoords[4];
	byte_vec4_t pColor[4];

	struct shader_s *shader;
} cparticle_t;

#define PARTICLE_GRAVITY    500

#define MAX_PARTICLES       2048

cparticle_t particles[MAX_PARTICLES];
int cg_numparticles;

/*
* CG_ClearParticles
*/
static void CG_ClearParticles( void ) {
	int i;
	cparticle_t *p;

	cg_numparticles = 0;
	memset( particles, 0, sizeof( cparticle_t ) * MAX_PARTICLES );

	for( i = 0, p = particles; i < MAX_PARTICLES; i++, p++ ) {
		p->pStcoords[0][0] = 0; p->pStcoords[0][1] = 1;
		p->pStcoords[1][0] = 0; p->pStcoords[1][1] = 0;
		p->pStcoords[2][0] = 1; p->pStcoords[2][1] = 0;
		p->pStcoords[3][0] = 1; p->pStcoords[3][1] = 1;
	}
}

#define CG_InitParticle( p, s, a, r, g, b, h ) \
	( \
		( p )->time = cg.time, \
		( p )->scale = ( s ), \
		( p )->alpha = ( a ), \
		( p )->color[0] = ( r ), \
		( p )->color[1] = ( g ), \
		( p )->color[2] = ( b ), \
		( p )->shader = ( h ), \
		( p )->fog = true \
	)

/*
* CG_ElectroWeakTrail
*/
void CG_ElectroWeakTrail( const vec3_t start, const vec3_t end, const vec4_t color ) {
	int j, count;
	vec3_t move, vec;
	float len;
	const float dec = 5;
	cparticle_t *p;
	vec4_t ucolor = { 1.0f, 1.0f, 1.0f, 0.8f };

	if( color ) {
		VectorCopy( color, ucolor );
	}

	// Always draw projectile EB trail. Otherwise it is almost impossible to see the projectile.
#if 0
	if( !cg_particles->integer ) {
		return;
	}
#endif

	VectorCopy( start, move );
	VectorSubtract( end, start, vec );
	len = VectorNormalize( vec );
	VectorScale( vec, dec, vec );

	count = (int)( len / dec ) + 1;
	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}

	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		//CG_InitParticle( p, 2.0f, 0.8f, 1.0f, 1.0f, 1.0f, NULL );
		CG_InitParticle( p, 2.0f, ucolor[3], ucolor[0], ucolor[1], ucolor[2], NULL );

		p->alphavel = -1.0 / ( 0.2 + random() * 0.1 );
		for( j = 0; j < 3; j++ ) {
			p->org[j] = move[j] + random();/* + crandom();*/
			p->vel[j] = crandom() * 2;
		}

		VectorClear( p->accel );
		VectorAdd( move, vec, move );
	}
}

/*
* CG_ElectroIonsTrail
*/
void CG_ElectroIonsTrail( const vec3_t start, const vec3_t end, const vec4_t color ) {
#define MAX_BOLT_IONS 48
	int i, count;
	vec3_t move, vec;
	float len;
	float dec2 = 24.0f;
	cparticle_t *p;

	if( !cg_particles->integer ) {
		return;
	}

	VectorSubtract( end, start, vec );
	len = VectorNormalize( vec );
	count = (int)( len / dec2 ) + 1;
	if( count > MAX_BOLT_IONS ) {
		count = MAX_BOLT_IONS;
		dec2 = len / count;
	}

	VectorScale( vec, dec2, vec );
	VectorCopy( start, move );

	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, 0.65f, color[3], color[0] + crandom() * 0.1, color[1] + crandom() * 0.1, color[2] + crandom() * 0.1, NULL );

		for( i = 0; i < 3; i++ ) {
			p->org[i] = move[i];
			p->vel[i] = crandom() * 4;
		}
		p->alphavel = -1.0 / ( 0.6 + random() * 0.6 );
		VectorClear( p->accel );
		VectorAdd( move, vec, move );
	}
}

void CG_ElectroIonsTrail2( const vec3_t start, const vec3_t end, const vec4_t color ) {
#define MAX_RING_IONS 96
	int count;
	vec3_t move, vec;
	float len;
	float dec2 = 8.0f;
	cparticle_t *p;

	if( !cg_particles->integer ) {
		return;
	}

	VectorSubtract( end, start, vec );
	len = VectorNormalize( vec );
	count = (int)( len / dec2 ) + 1;
	if( count > MAX_RING_IONS ) {
		count = MAX_RING_IONS;
		dec2 = len / count;
	}

	VectorScale( vec, dec2, vec );
	VectorCopy( start, move );

	if( cg_numparticles + count > MAX_PARTICLES ) {
		count = MAX_PARTICLES - cg_numparticles;
	}

	// Ring rail eb particles
	for( p = &particles[cg_numparticles], cg_numparticles += count; count > 0; count--, p++ ) {
		CG_InitParticle( p, 0.65f, color[3], color[0] + crandom() * 0.1, color[1] + crandom() * 0.1, color[2] + crandom() * 0.1, NULL );

		p->alphavel = -1.0 / ( 0.6 + random() * 0.6 );

		VectorCopy( move, p->org );
		VectorClear( p->accel );
		VectorClear( p->vel );
		VectorAdd( move, vec, move );
	}
}

void CG_AddParticles( DrawSceneRequest *drawSceneRequest ) {
	int i, j, k;
	float alpha;
	float time, time2;
	vec3_t org;
	vec3_t corner;
	byte_vec4_t color;
	int maxparticle, activeparticles;
	float alphaValues[MAX_PARTICLES];
	cparticle_t *p, *free_particles[MAX_PARTICLES];

	if( !cg_numparticles ) {
		return;
	}

	j = 0;
	maxparticle = -1;
	activeparticles = 0;

	for( i = 0, p = particles; i < cg_numparticles; i++, p++ ) {
		time = ( cg.time - p->time ) * 0.001f;
		alpha = alphaValues[i] = p->alpha + time * p->alphavel;

		if( alpha <= 0 ) { // faded out
			free_particles[j++] = p;
			continue;
		}

		maxparticle = i;
		activeparticles++;

		time2 = time * time * 0.5f;

		org[0] = p->org[0] + p->vel[0] * time + p->accel[0] * time2;
		org[1] = p->org[1] + p->vel[1] * time + p->accel[1] * time2;
		org[2] = p->org[2] + p->vel[2] * time + p->accel[2] * time2;

		color[0] = (uint8_t)( bound( 0, p->color[0], 1.0f ) * 255 );
		color[1] = (uint8_t)( bound( 0, p->color[1], 1.0f ) * 255 );
		color[2] = (uint8_t)( bound( 0, p->color[2], 1.0f ) * 255 );
		color[3] = (uint8_t)( bound( 0, alpha, 1.0f ) * 255 );

		corner[0] = org[0];
		corner[1] = org[1] - 0.5f * p->scale;
		corner[2] = org[2] - 0.5f * p->scale;

		Vector4Set( p->pVerts[0], corner[0], corner[1] + p->scale, corner[2] + p->scale, 1 );
		Vector4Set( p->pVerts[1], corner[0], corner[1], corner[2] + p->scale, 1 );
		Vector4Set( p->pVerts[2], corner[0], corner[1], corner[2], 1 );
		Vector4Set( p->pVerts[3], corner[0], corner[1] + p->scale, corner[2], 1 );
		for( k = 0; k < 4; k++ ) {
			Vector4Copy( color, p->pColor[k] );
		}

		p->poly.numverts = 4;
		p->poly.verts = p->pVerts;
		p->poly.stcoords = p->pStcoords;
		p->poly.colors = p->pColor;
		p->poly.fognum = p->fog ? 0 : -1;
		p->poly.shader = ( p->shader == NULL ) ? cgs.media.shaderParticle : p->shader;

		drawSceneRequest->addPoly( &p->poly );
	}

	i = 0;
	while( maxparticle >= activeparticles ) {
		*free_particles[i++] = particles[maxparticle--];

		while( maxparticle >= activeparticles ) {
			if( alphaValues[maxparticle] <= 0 ) {
				maxparticle--;
			} else {
				break;
			}
		}
	}

	cg_numparticles = activeparticles;
}

/*
* CG_ClearEffects
*/
void CG_ClearEffects( void ) {
	CG_ClearFragmentedDecals();
	CG_ClearParticles();
}