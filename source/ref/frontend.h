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

#include <memory>
#include <span>

struct alignas( 32 )Frustum {
	float planeX[8];
	float planeY[8];
	float planeZ[8];
	float planeD[8];
	uint32_t xBlendMasks[8];
	uint32_t yBlendMasks[8];
	uint32_t zBlendMasks[8];

	void setPlaneComponentsAtIndex( unsigned index, const float *n, float d );

	// Call after setting up 5th+ planes
	void fillComponentTails( unsigned numPlanesSoFar );

	void setupFor4Planes( const float *viewOrigin, const mat3_t viewAxis, float fovX, float fovY );

	// TODO: This is just for exploratory purposes, culling should use bulk operations

	// Returns an on-zero value if the bounds are completely outside the frustum
	[[nodiscard]]
	auto computeBinaryResultFor4Planes( const vec4_t mins, const vec4_t maxs ) const -> int;
	[[nodiscard]]
	auto computeBinaryResultFor8Planes( const vec4_t mins, const vec4_t maxs ) const -> int;

	// Returns a pair of values.
	// A non-zero first value indicates that the bounds are completely outside the frustum.
	// Otherwise, a non-zero second value indicates that the bounds are partially outside the frustum.
	[[nodiscard]]
	auto computeTristateResultFor4Planes( const vec4_t mins, const vec4_t maxs ) const -> std::pair<int, int>;
	[[nodiscard]]
	auto computeTristateResultFor8Planes( const vec4_t mins, const vec4_t maxs ) const -> std::pair<int, int>;
};

namespace wsw::ref {

class Frontend {
private:
	refinst_t m_state;
	// TODO: Put in the state
	Frustum m_frustum;

	wsw::StaticVector<DrawSceneRequest, 1> m_drawSceneRequestHolder;

	struct DebugLine {
		float p1[3];
		float p2[3];
		int color;
	};
	wsw::Vector<DebugLine> m_debugLines;

	wsw::Vector<sortedDrawSurf_t> m_meshDrawList;

	template <typename T>
	struct BufferHolder {
		std::unique_ptr<T[]> data;
		unsigned capacity { 0 };

		void reserve( size_t newSize ) {
			if( newSize > capacity ) [[unlikely]] {
				data = std::make_unique<T[]>( newSize );
				capacity = newSize;
			}
		}
	};

	BufferHolder<unsigned> m_visibleLeavesBuffer;
	BufferHolder<bool> m_surfVisibilityTable;
	BufferHolder<unsigned> m_visibleOccluderSurfacesBuffer;

	const msurface_t *m_bestOccludersBuffer[8];
	Frustum m_occluderFrusta[8];

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

	void addDebugLine( const float *p1, const float *p2, int color = COLOR_RGB( 255, 255, 255 ) );

	void submitDebugStuffToBackend( Scene *scene );

	void showOccluderSurface( const msurface_t *surface );

	[[nodiscard]]
	auto collectVisibleWorldLeaves() -> std::span<const unsigned>;
	[[nodiscard]]
	auto collectVisibleOccluders( std::span<const unsigned> visibleLeaves ) -> std::span<const unsigned>;
	[[nodiscard]]
	auto selectBestOccluders( std::span<const unsigned> visibleOccluders ) -> std::span<const msurface_t *>;
	[[nodiscard]]
	auto buildFrustaOfOccluders( std::span<const msurface_t *> bestOccluders ) -> std::span<const Frustum>;

	void cullSurfacesInVisLeavesByOccluders( std::span<const unsigned> indicesOfVisibleLeaves,
											 std::span<const Frustum> occluderFrusta );

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