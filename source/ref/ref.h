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

#ifndef WSW_ac367414_9635_4452_b2fc_9477f5254db5_H
#define WSW_ac367414_9635_4452_b2fc_9477f5254db5_H

#include "../qcommon/wswstaticvector.h"
#include "../gameshared/q_math.h"

#include <optional>

// FIXME: move these to r_local.h?
#define MAX_DLIGHTS             32
#define MAX_ENTITIES            2048
#define MAX_POLY_VERTS          3000
#define MAX_POLYS               2048

// entity_state_t->renderfx flags
#define RF_MINLIGHT             0x1       // always have some light (viewmodel)
#define RF_FULLBRIGHT           0x2       // always draw full intensity
#define RF_FRAMELERP            0x4
#define RF_NOSHADOW             0x8

#define RF_VIEWERMODEL          0x10     // don't draw through eyes, only mirrors
#define RF_WEAPONMODEL          0x20     // only draw through eyes and depth hack
#define RF_CULLHACK             0x40
#define RF_FORCENOLOD           0x80
#define RF_NOPORTALENTS         0x100
#define RF_ALPHAHACK            0x200   // force alpha blending on opaque passes, read alpha from entity
#define RF_GREYSCALE            0x400
#define RF_NODEPTHTEST          0x800
#define RF_NOCOLORWRITE         0x1000

// refdef flags
#define RDF_UNDERWATER          0x1     // warp the screen as apropriate
#define RDF_NOWORLDMODEL        0x2     // used for player configuration screen
#define RDF_OLDAREABITS         0x4     // forces R_MarkLeaves if not set
#define RDF_PORTALINVIEW        0x8     // cull entities using vis too because pvs\areabits are merged serverside
#define RDF_SKYPORTALINVIEW     0x10    // draw skyportal instead of regular sky
#define RDF_FLIPPED             0x20
#define RDF_WORLDOUTLINES       0x40    // draw cell outlines for world surfaces
#define RDF_CROSSINGWATER       0x80    // potentially crossing water surface
#define RDF_USEORTHO            0x100   // use orthographic projection
#define RDF_BLURRED             0x200

// skm flags
#define SKM_ATTACHMENT_BONE     1

typedef struct orientation_s {
	mat3_t axis;
	vec3_t origin;
} orientation_t;

typedef struct bonepose_s {
	dualquat_t dualquat;
} bonepose_t;

typedef struct fragment_s {
	int firstvert;
	int numverts;                       // can't exceed MAX_POLY_VERTS
	int fognum;                         // -1 - no fog
	                                    //  0 - determine fog in R_AddPolyToScene
	                                    // >0 - valid fog volume number returned by R_GetClippedFragments
	vec3_t normal;
} fragment_t;

typedef struct poly_s {
	int numverts;
	vec4_t *verts;
	vec4_t *normals;
	vec2_t *stcoords;
	byte_vec4_t *colors;
	int numelems;
	unsigned short *elems;
	struct shader_s *shader;
	int fognum;
} poly_t;

typedef struct {
	float rgb[3];                       // 0.0 - 2.0
} lightstyle_t;

typedef struct {
	float fov;
	float scale;
	vec3_t vieworg;
	vec3_t viewanglesOffset;
	bool noEnts;
} skyportal_t;

typedef enum {
	RT_MODEL,
	RT_SPRITE,
	RT_PORTALSURFACE,

	NUM_RTYPES
} refEntityType_t;

typedef struct entity_s {
	refEntityType_t rtype;
	unsigned number;

	union {
		int flags;
		int renderfx;
	};

	struct model_s *model;              // opaque type outside refresh

	/*
	** most recent data
	*/
	mat3_t axis;
	vec3_t origin, origin2;
	vec3_t lightingOrigin;
	int frame;
	bonepose_t *boneposes;              // pretransformed boneposes for current frame

	/*
	** previous data for lerping
	*/
	int oldframe;
	bonepose_t *oldboneposes;           // pretransformed boneposes for old frame
	float backlerp;                     // 0.0 = current, 1.0 = old

	/*
	** texturing
	*/
	struct Skin *customSkin;      // registered .skin file
	struct shader_s *customShader;      // NULL for inline skin

	/*
	** misc
	*/
	int64_t shaderTime;
	union {
		byte_vec4_t color;
		uint8_t shaderRGBA[4];
	};

	float scale;
	float radius;                       // used as RT_SPRITE's radius
	float rotation;

	float outlineHeight;
	union {
		byte_vec4_t outlineColor;
		uint8_t outlineRGBA[4];
	};
} entity_t;

typedef struct refdef_s {
	int x, y, width, height;            // viewport, in virtual screen coordinates
	int scissor_x, scissor_y, scissor_width, scissor_height;
	int ortho_x, ortho_y;
	float fov_x, fov_y;
	vec3_t vieworg;
	mat3_t viewaxis;
	float blend[4];                     // rgba 0-1 full screen blend
	int64_t time;                       // time is used for timing offsets
	int rdflags;                        // RDF_UNDERWATER, etc
	skyportal_t skyportal;
	uint8_t *areabits;                  // if not NULL, only areas with set bits will be drawn
	float weaponAlpha;
	float minLight;                     // minimum value of ambient lighting applied to RF_MINLIGHT entities
	struct shader_s *colorCorrection;   // post processing color correction lookup table to apply
} refdef_t;

struct alignas( 16 ) BasicParticle {
	float origin[4];
	float oldOrigin[4];
	float velocity[4];
	float accel[4];
};

namespace wsw::ref { class Frontend; }

class Scene {
	friend class wsw::ref::Frontend;
public:
	struct Poly {
		int type;
		int fogNum;
		int numElems;
		int numVerts;

		vec4_t *xyzArray;
		vec4_t *normalsArray;
		vec2_t *stArray;
		byte_vec4_t *colorsArray;
		uint16_t *elems;
		struct shader_s *shader;
	};

	struct DynamicLight {
		float origin[3];
		float programRadius;
		float coronaRadius;
		float color[3];
		bool hasProgramLight;
		bool hasCoronaLight;
	};
protected:
	Scene();

	wsw::StaticVector<DynamicLight, 1024> m_dynamicLights;

	entity_t *m_worldent;
	entity_t *m_polyent;

	wsw::StaticVector<entity_t *, MAX_ENTITIES + 48> m_entities;
	wsw::StaticVector<entity_t, 2> m_localEntities;
	// TODO: Use different subtypes so the storage is more compact
	wsw::StaticVector<entity_t, MAX_ENTITIES> m_nullModelEntities;
	wsw::StaticVector<entity_t, MAX_ENTITIES> m_aliasModelEntities;
	wsw::StaticVector<entity_t, MAX_ENTITIES> m_skeletalModelEntities;
	wsw::StaticVector<entity_t, MAX_ENTITIES> m_brushModelEntities;
	wsw::StaticVector<entity_t, MAX_ENTITIES> m_spriteEntities;
	wsw::StaticVector<Poly, MAX_POLYS> m_polys;
};

// TODO: Aggregate Scene as a member?
class DrawSceneRequest : public Scene {
	friend class wsw::ref::Frontend;

	// TODO: Get rid of "refdef_t"
	refdef_t m_refdef;
public:
	void addLight( const float *origin, float programRadius, float coronaRadius, float r, float g, float b );
	void addLight( const float *origin, float programRadius, float coronaRadius, const float *color ) {
		addLight( origin, programRadius, coronaRadius, color[0], color[1], color[2] );
	}
	void addEntity( const entity_t *ent );
	void addPoly( const poly_t *poly );

	explicit DrawSceneRequest( const refdef_t &refdef ) : m_refdef( refdef ) {}
};

DrawSceneRequest *CreateDrawSceneRequest( const refdef_t &refdef );
void SubmitDrawSceneRequest( DrawSceneRequest *request );

class Texture;

void        R_DrawStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
							  const vec4_t color, const shader_s *shader );
void        R_DrawRotatedStretchPic( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
									 float angle, const vec4_t color, const shader_s *shader );
void        R_DrawStretchQuick( int x, int y, int w, int h, float s1, float t1, float s2, float t2,
								const vec4_t color, int program_type, Texture *image, int blendMask );

void        R_DrawExternalTextureOverlay( unsigned externalTexNum );

struct model_s;

void        R_ModelBounds( const model_s *model, vec3_t mins, vec3_t maxs );
void        R_ModelFrameBounds( const model_s *model, int frame, vec3_t mins, vec3_t maxs );
void        R_RegisterWorldModel( const char *model );
model_s *R_RegisterModel( const char *name );

int         R_SkeletalGetBoneInfo( const model_s *mod, int bonenum, char *name, size_t name_size, int *flags );
void        R_SkeletalGetBonePose( const model_s *mod, int bonenum, int frame, bonepose_t *bonepose );
int         R_SkeletalGetNumBones( const model_s *mod, int *numFrames );

shader_s    *R_RegisterShader( const char *name, int type );
shader_s    *R_RegisterPic( const char *name );
shader_s    *R_RegisterRawAlphaMask( const char *name, int width, int height, const uint8_t *data );
shader_s    *R_RegisterSkin( const char *name );
shader_s    *R_RegisterLinearPic( const char *name );

struct ImageOptions {
	std::optional<std::pair<unsigned, unsigned>> desiredSize;
	unsigned borderWidth { 0 };
	bool fitSizeForCrispness { false };
	bool useOutlineEffect { false };

	template <typename T>
	void setDesiredSize( T width, T height ) {
		assert( width > 0 && height > 0 && width < (T)( 1 << 16 ) && height < (T)( 1 << 16 ) );
		desiredSize = std::make_pair( (unsigned)width, (unsigned)height );
	}
};

shader_s *R_CreateExplicitlyManaged2DMaterial();
void R_ReleaseExplicitlyManaged2DMaterial( shader_s *material );
bool R_UpdateExplicitlyManaged2DMaterialImage( shader_s *material, const char *name, const ImageOptions &options );

[[nodiscard]]
auto R_GetShaderDimensions( const shader_s *shader ) -> std::optional<std::pair<unsigned, unsigned>>;

void        R_ReplaceRawSubPic( shader_s *shader, int x, int y, int width, int height, const uint8_t *data );

struct Skin;
Skin *R_RegisterSkinFile( const char *name );
shader_s *R_FindShaderForSkinFile( const Skin *skin, const char *meshname );

void RF_TransformVectorToScreen( const refdef_t *rd, const vec3_t in, vec2_t out );
bool RF_LerpTag( orientation_t *orient, const model_s *mod, int oldframe, int frame, float lerpfrac, const char *name );

void        R_SetCustomColor( int num, int r, int g, int b );

struct VidModeOptions {
	bool fullscreen { false };
	bool borderless { false };
};

rserr_t R_TrySettingMode( int x, int y, int width, int height, int displayFrequency, const VidModeOptions &options );

void RF_BeginRegistration();
void RF_EndRegistration();

void RF_AppActivate( bool active, bool minimize, bool destroy );
void RF_Shutdown( bool verbose );

void RF_BeginFrame( bool forceClear, bool forceVsync, bool uncappedFPS );
void RF_EndFrame();

void R_Set2DMode( bool );

const char *RF_GetSpeedsMessage( char *out, size_t size );

int RF_GetAverageFrametime();

void R_Finish();

rserr_t     R_Init( const char *applicationName, const char *screenshotPrefix, int startupColor,
					int iconResource, const int *iconXPM,
					void *hinstance, void *wndproc, void *parenthWnd,
					bool verbose );

#endif // __REF_H
