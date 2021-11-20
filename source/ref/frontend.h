/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2002-2013 Victor Luchits

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

#ifndef WSW_63ccf348_3b16_4f9c_9a49_cd5849918618_H
#define WSW_63ccf348_3b16_4f9c_9a49_cd5849918618_H

namespace wsw::ref {

class Frontend {
private:
	refinst_t m_state;

	wsw::StaticVector<DrawSceneRequest, 1> m_drawSceneRequestHolder;

	wsw::Vector<sortedDrawSurf_t> m_meshDrawList;

	[[nodiscard]]
	auto getFogForBounds( const float *mins, const float *maxs ) -> mfog_t *;
	[[nodiscard]]
	auto getFogForSphere( const vec3_t centre, const float radius ) -> mfog_t *;
	[[nodiscard]]
	bool isPointCompletelyFogged( const mfog_t *fog, const float *origin, float radius );

	void bindFrameBuffer( int );

	[[nodiscard]]
	auto getDefaultFarClip() const -> float;

	void renderViewFromThisCamera( Scene *scene, const refdef_t *fd );

	[[nodiscard]]
	auto tryAddingPortalSurface( const entity_t *ent, const shader_t *shader, void *drawSurf ) -> portalSurface_t *;

	[[nodiscard]]
	auto tryUpdatingPortalSurfaceAndDistance( drawSurfaceBSP_t *drawSurf, const msurface_t *surf, const float *origin ) -> std::optional<float>;

	void updatePortalSurface( portalSurface_t *portalSurface, const mesh_t *mesh,
							  const float *mins, const float *maxs, const shader_t *shader, void *drawSurf );

	void collectVisiblePolys( Scene *scene );
	void collectVisibleWorldBrushes( Scene *scene );
	void collectVisibleEntities( Scene *scene );

	void setupViewMatrices();
	void clearActiveFrameBuffer();

	bool addSpriteToSortList( const entity_t *e );
	bool addAliasModelToSortList( const entity_t *e );
	bool addSkeletalModelToSortList( const entity_t *e );
	bool addBrushModelToSortList( const entity_t *e );
	bool addNullSurfToSortList( const entity_t *e );
	bool addBspSurfToSortList( const entity_t *e, drawSurfaceBSP_t *drawSurf, const float *maybeOrigin );

	void *addEntryToSortList( const entity_t *e, const mfog_t *fog, const shader_t *shader,
							  float dist, unsigned order, const portalSurface_t *portalSurf, void *drawSurf );

	void submitSortedSurfacesToBackend( Scene *scene );
public:
	static void init();
	static void shutdown();

	[[nodiscard]]
	static auto instance() -> Frontend *;

	void clearScene();

	[[nodiscard]]
	auto createDrawSceneRequest( const refdef_t &refdef ) -> DrawSceneRequest *;
	void submitDrawSceneRequest( DrawSceneRequest *request );

	void initVolatileAssets();

	void destroyVolatileAssets();

	void renderScene( Scene *scene, const refdef_t *rd );

	void set2DMode( bool enable );

	void dynLightDirForOrigin( const float *origin, float radius, vec3_t dir, vec3_t diffuseLocal, vec3_t ambientLocal );
};

}

#endif