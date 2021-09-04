/*
Copyright (C) 2016 Victor Luchits

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

#ifndef R_FRONTEND_H
#define R_FRONTEND_H

#include "local.h"

rserr_t R_TrySettingMode( int x, int y, int width, int height, int displayFrequency, VidModeFlags flags );

void RF_AppActivate( bool active, bool minimize, bool destroy );
void RF_Shutdown( bool verbose );

void RF_BeginRegistration();
void RF_EndRegistration();

void RF_BeginFrame( bool forceClear, bool forceVsync, bool uncappedFPS );
void RF_EndFrame();

void RF_RegisterWorldModel( const char *model );
void RF_ClearScene();
void RF_AddEntityToScene( const entity_t *ent );
void RF_AddLightToScene( const vec3_t org, float programIntensity, float coronaIntensity, float r, float g, float b );
void RF_AddPolyToScene( const poly_t *poly );
void RF_AddLightStyleToScene( int style, float r, float g, float b );
void RF_RenderScene( const refdef_t *fd );

void RF_DrawStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
						const vec4_t color, const shader_t *shader );

void RF_ScreenShot( const char *path, const char *name, const char *fmtstring, bool silent );
const char *RF_GetSpeedsMessage( char *out, size_t size );

int RF_GetAverageFrametime();

void RF_TransformVectorToScreen( const refdef_t *rd, const vec3_t in, vec2_t out );
bool RF_LerpTag( orientation_t *orient, const model_t *mod, int oldframe, int frame, float lerpfrac, const char *name );
void RF_Finish( void );

#endif // R_FRONTEND_H
