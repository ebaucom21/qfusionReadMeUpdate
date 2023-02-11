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

// cg_lents.c -- client side temporary entities

#include "cg_local.h"
#include "../client/snd_public.h"

#define MAX_LOCAL_ENTITIES  512

typedef enum
{
	LE_FREE,
	LE_NO_FADE,
	LE_RGB_FADE,
	LE_ALPHA_FADE,
	LE_SCALE_ALPHA_FADE,
	LE_INVERSESCALE_ALPHA_FADE,

	LE_PUFF_SCALE,
	LE_PUFF_SHRINK
} letype_t;

typedef struct lentity_s
{
	struct lentity_s *prev, *next;

	letype_t type;

	entity_t ent;
	vec4_t color;

	unsigned int start;

	float light;
	vec3_t lightcolor;
	vec3_t lightOrigin;

	vec3_t velocity;
	vec3_t avelocity;
	vec3_t angles;
	vec3_t accel;

	int bounce;     //is activator and bounceability value at once

	int frames;

	cgs_skeleton_t *skel;
	bonepose_t *static_boneposes;
} lentity_t;

lentity_t cg_localents[MAX_LOCAL_ENTITIES];
lentity_t cg_localents_headnode, *cg_free_lents;

void CG_ClearLocalEntities( void ) {
	int i;

	memset( cg_localents, 0, sizeof( cg_localents ) );

	// link local entities
	cg_free_lents = cg_localents;
	cg_localents_headnode.prev = &cg_localents_headnode;
	cg_localents_headnode.next = &cg_localents_headnode;
	for( i = 0; i < MAX_LOCAL_ENTITIES - 1; i++ )
		cg_localents[i].next = &cg_localents[i + 1];
}

static lentity_t *CG_AllocLocalEntity( letype_t type, float r, float g, float b, float a ) {
	lentity_t *le;

	if( cg_free_lents ) { // take a free decal if possible
		le = cg_free_lents;
		cg_free_lents = le->next;
	} else {              // grab the oldest one otherwise
		le = cg_localents_headnode.prev;
		le->prev->next = le->next;
		le->next->prev = le->prev;
	}

	memset( le, 0, sizeof( *le ) );
	le->type = type;
	le->start = cg.time;
	le->color[0] = r;
	le->color[1] = g;
	le->color[2] = b;
	le->color[3] = a;

	switch( le->type ) {
		case LE_NO_FADE:
			break;
		case LE_RGB_FADE:
			le->ent.shaderRGBA[3] = ( uint8_t )( 255 * a );
			break;
		case LE_SCALE_ALPHA_FADE:
		case LE_INVERSESCALE_ALPHA_FADE:
		case LE_PUFF_SHRINK:
			le->ent.shaderRGBA[0] = ( uint8_t )( 255 * r );
			le->ent.shaderRGBA[1] = ( uint8_t )( 255 * g );
			le->ent.shaderRGBA[2] = ( uint8_t )( 255 * b );
			le->ent.shaderRGBA[3] = ( uint8_t )( 255 * a );
			break;
		case LE_PUFF_SCALE:
			le->ent.shaderRGBA[0] = ( uint8_t )( 255 * r );
			le->ent.shaderRGBA[1] = ( uint8_t )( 255 * g );
			le->ent.shaderRGBA[2] = ( uint8_t )( 255 * b );
			le->ent.shaderRGBA[3] = ( uint8_t )( 255 * a );
			break;
		case LE_ALPHA_FADE:
			le->ent.shaderRGBA[0] = ( uint8_t )( 255 * r );
			le->ent.shaderRGBA[1] = ( uint8_t )( 255 * g );
			le->ent.shaderRGBA[2] = ( uint8_t )( 255 * b );
			break;
		default:
			break;
	}

	// put the decal at the start of the list
	le->prev = &cg_localents_headnode;
	le->next = cg_localents_headnode.next;
	le->next->prev = le;
	le->prev->next = le;

	return le;
}

static void CG_FreeLocalEntity( lentity_t *le ) {
	if( le->static_boneposes ) {
		Q_free(   le->static_boneposes );
		le->static_boneposes = NULL;
	}

	// remove from linked active list
	le->prev->next = le->next;
	le->next->prev = le->prev;

	// insert into linked free list
	le->next = cg_free_lents;
	cg_free_lents = le;
}

static lentity_t *CG_AllocModel( letype_t type, const vec3_t origin, const vec3_t angles, int frames,
								 float r, float g, float b, float a, float light, float lr, float lg, float lb, struct model_s *model, struct shader_s *shader ) {
	lentity_t *le;

	le = CG_AllocLocalEntity( type, r, g, b, a );
	le->frames = frames;
	le->light = light;
	le->lightcolor[0] = lr;
	le->lightcolor[1] = lg;
	le->lightcolor[2] = lb;

	le->ent.rtype = RT_MODEL;
	le->ent.renderfx = RF_NOSHADOW;
	le->ent.model = model;
	le->ent.customShader = shader;
	le->ent.shaderTime = cg.time;
	le->ent.scale = 1.0f;

	VectorCopy( angles, le->angles );
	AnglesToAxis( angles, le->ent.axis );
	VectorCopy( origin, le->ent.origin );
	VectorCopy( origin, le->lightOrigin );

	return le;
}

void CG_LaserGunImpact( const vec3_t pos, const vec3_t dir, float radius, const vec3_t laser_dir,
						const vec4_t color, DrawSceneRequest *drawSceneRequest ) {
	entity_t ent;
	vec3_t ndir;
	vec3_t angles;

	memset( &ent, 0, sizeof( ent ) );
	VectorCopy( pos, ent.origin );
	VectorMA( ent.origin, 2, dir, ent.origin );
	ent.renderfx = RF_FULLBRIGHT | RF_NOSHADOW;
	ent.scale = 1.45f;
	Vector4Set( ent.shaderRGBA, color[0] * 255, color[1] * 255, color[2] * 255, color[3] * 255 );
	ent.model = cgs.media.modLasergunWallExplo;
	VectorNegate( laser_dir, ndir );
	VecToAngles( ndir, angles );
	angles[2] = anglemod( -360.0f * cg.time * 0.001f );

	AnglesToAxis( angles, ent.axis );

	drawSceneRequest->addEntity( &ent );
}

void CG_PModel_SpawnTeleportEffect( centity_t *cent ) {
	int j;
	cgs_skeleton_t *skel;
	lentity_t *le;
	vec3_t teleportOrigin;
	vec3_t rgb;

	skel = CG_SkeletonForModel( cent->ent.model );
	if( !skel || !cent->ent.boneposes ) {
		return;
	}

	for( j = LOCALEFFECT_EV_PLAYER_TELEPORT_IN; j <= LOCALEFFECT_EV_PLAYER_TELEPORT_OUT; j++ ) {
		if( cent->localEffects[j] ) {
			cent->localEffects[j] = 0;

			VectorSet( rgb, 0.5, 0.5, 0.5 );
			if( j == LOCALEFFECT_EV_PLAYER_TELEPORT_OUT ) {
				VectorCopy( cent->teleportedFrom, teleportOrigin );
			} else {
				VectorCopy( cent->teleportedTo, teleportOrigin );
				if( ISVIEWERENTITY( cent->current.number ) ) {
					VectorSet( rgb, 0.1, 0.1, 0.1 );
				}
			}
			if( cg_raceGhosts->integer && !ISVIEWERENTITY( cent->current.number ) && GS_RaceGametype() ) {
				VectorScale( rgb, cg_raceGhostsAlpha->value, rgb );
			}

			// spawn a dummy model
			le = CG_AllocModel( LE_RGB_FADE, teleportOrigin, vec3_origin, 10,
								rgb[0], rgb[1], rgb[2], 1, 0, 0, 0, 0, cent->ent.model,
								cgs.media.shaderTeleportShellGfx );

			if( cent->skel ) {
				// use static bone pose, no animation
				le->skel = cent->skel;
				le->static_boneposes = ( bonepose_t * )Q_malloc( sizeof( bonepose_t ) * le->skel->numBones );
				memcpy( le->static_boneposes, cent->ent.boneposes, sizeof( bonepose_t ) * le->skel->numBones );
				le->ent.boneposes = le->static_boneposes;
				le->ent.oldboneposes = le->ent.boneposes;
			}

			le->ent.frame = cent->ent.frame;
			le->ent.oldframe = cent->ent.oldframe;
			le->ent.backlerp = 1.0f;
			Matrix3_Copy( cent->ent.axis, le->ent.axis );
		}
	}
}

void CG_AddLocalEntities( DrawSceneRequest *drawSceneRequest ) {
#define FADEINFRAMES 2
	int f;
	lentity_t *le, *next, *hnode;
	entity_t *ent;
	float scale, frac, fade, time, scaleIn, fadeIn;
	float backlerp;

	time = (float)cg.frameTime * 0.001f;
	backlerp = 1.0f - cg.lerpfrac;

	hnode = &cg_localents_headnode;
	for( le = hnode->next; le != hnode; le = next ) {
		next = le->next;

		frac = ( cg.time - le->start ) * 0.01f;
		f = ( int )floor( frac );
		clamp_low( f, 0 );

		// it's time to DIE
		if( f >= le->frames - 1 ) {
			le->type = LE_FREE;
			CG_FreeLocalEntity( le );
			continue;
		}

		if( le->frames > 1 ) {
			scale = 1.0f - frac / ( le->frames - 1 );
			scale = bound( 0.0f, scale, 1.0f );
			fade = scale * 255.0f;

			// quick fade in, if time enough
			if( le->frames > FADEINFRAMES * 2 ) {
				scaleIn = frac / (float)FADEINFRAMES;
				Q_clamp( scaleIn, 0.0f, 1.0f );
				fadeIn = scaleIn * 255.0f;
			} else {
				fadeIn = 255.0f;
			}
		} else {
			scale = 1.0f;
			fade = 255.0f;
			fadeIn = 255.0f;
		}

		ent = &le->ent;

		if( le->type == LE_PUFF_SCALE ) {
			if( le->frames - f < 4 ) {
				ent->scale = 1.0f - 1.0f * ( frac - abs( 4 - le->frames ) ) / 4;
			}
		}
		if( le->type == LE_PUFF_SHRINK ) {
			if( frac < 3 ) {
				ent->scale = 1.0f - 0.2f * frac / 4;
			} else {
				ent->scale = 0.8 - 0.8 * ( frac - 3 ) / 3;
				VectorScale( le->velocity, 0.85f, le->velocity );
			}
		}

		switch( le->type ) {
			case LE_NO_FADE:
				break;
			case LE_RGB_FADE:
				fade = wsw::min( fade, fadeIn );
				ent->shaderRGBA[0] = ( uint8_t )( fade * le->color[0] );
				ent->shaderRGBA[1] = ( uint8_t )( fade * le->color[1] );
				ent->shaderRGBA[2] = ( uint8_t )( fade * le->color[2] );
				break;
			case LE_SCALE_ALPHA_FADE:
				fade = wsw::min( fade, fadeIn );
				ent->scale = 1.0f + 1.0f / scale;
				ent->scale = wsw::min( ent->scale, 5.0f );
				ent->shaderRGBA[3] = ( uint8_t )( fade * le->color[3] );
				break;
			case LE_INVERSESCALE_ALPHA_FADE:
				fade = wsw::min( fade, fadeIn );
				ent->scale = scale + 0.1f;
				Q_clamp( ent->scale, 0.1f, 1.0f );
				ent->shaderRGBA[3] = ( uint8_t )( fade * le->color[3] );
				break;
			case LE_ALPHA_FADE:
				fade = wsw::min( fade, fadeIn );
				ent->shaderRGBA[3] = ( uint8_t )( fade * le->color[3] );
				break;
			default:
				break;
		}

		ent->backlerp = backlerp;

		if( le->avelocity[0] || le->avelocity[1] || le->avelocity[2] ) {
			VectorMA( le->angles, time, le->avelocity, le->angles );
			AnglesToAxis( le->angles, le->ent.axis );
		}

		VectorCopy( ent->origin, ent->origin2 );
		VectorMA( ent->origin, time, le->velocity, ent->origin );

		VectorCopy( ent->origin, ent->lightingOrigin );
		VectorMA( le->velocity, time, le->accel, le->velocity );

		CG_AddEntityToScene( ent, drawSceneRequest );
	}
}

void CG_FreeLocalEntities( void ) {
	lentity_t *le, *next, *hnode;

	hnode = &cg_localents_headnode;
	for( le = hnode->next; le != hnode; le = next ) {
		next = le->next;

		le->type = LE_FREE;
		CG_FreeLocalEntity( le );
	}

	CG_ClearLocalEntities();
}
