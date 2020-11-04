#include "qcommon.h"
#include "cm_local.h"
#include "cm_trace.h"

#ifdef CM_USE_SSE

/**
 * Create this object in scopes that match boundaries
 * of transition between regular SSE2 and VEX-encoded binary code.
 * Compiling this file using MSVC requires AVX support and the code
 * is VEX-encoded contrary to the rest of the codebase.
 * This fence inserts instructions that help to avoid transition penalties.
 */
struct VexEncodingFence {
#ifdef _MSC_VER
	VexEncodingFence() {
		_mm256_zeroupper();
	}
	~VexEncodingFence() {
		_mm256_zeroupper();
	}
#endif
};

static inline bool CM_BoundsIntersect_SSE42( __m128 traceAbsmins, __m128 traceAbsmaxs,
											 __m128 shapeMins, __m128 shapeMaxs ) {
	__m128 cmp1 = _mm_cmpge_ps( shapeMins, traceAbsmaxs );
	__m128 cmp2 = _mm_cmpge_ps( traceAbsmins, shapeMaxs );
	__m128 orCmp = _mm_or_ps( cmp1, cmp2 );

	return _mm_movemask_epi8( _mm_cmpeq_epi32( _mm_castps_si128( orCmp ), _mm_setzero_si128() ) ) == 0xFFFF;
}

static inline bool CM_BoundsIntersect_SSE42( __m128 traceAbsmins, __m128 traceAbsmaxs,
											 const vec4_t shapeMins, const vec4_t shapeMaxs ) {
	// This version relies on fast unaligned loads, that's why it requires SSE4.
	__m128 xmmShapeMins = _mm_loadu_ps( shapeMins );
	__m128 xmmShapeMaxs = _mm_loadu_ps( shapeMaxs );

	return CM_BoundsIntersect_SSE42( traceAbsmins, traceAbsmaxs, xmmShapeMins, xmmShapeMaxs );
}

static inline bool CM_MightCollide_SSE42( const vec_bounds_t shapeMins,
										  const vec_bounds_t shapeMaxs,
										  const CMTraceContext *tlc ) {
	return CM_BoundsIntersect_SSE42( tlc->xmmAbsmins, tlc->xmmAbsmaxs, shapeMins, shapeMaxs );
}

static inline bool CM_MightCollideInLeaf_SSE42( const vec_bounds_t shapeMins,
												const vec_bounds_t shapeMaxs,
												const vec_bounds_t shapeCenter,
												float shapeRadius,
												const CMTraceContext *tlc ) {
	if( !CM_MightCollide_SSE42( shapeMins, shapeMaxs, tlc ) ) {
		return false;
	}

	// TODO: Vectorize this part. This task is not completed for various reasons.

	vec3_t centerToStart;
	vec3_t proj, perp;

	VectorSubtract( tlc->start, shapeCenter, centerToStart );
	float projMagnitude = DotProduct( centerToStart, tlc->traceDir );
	VectorScale( tlc->traceDir, projMagnitude, proj );
	VectorSubtract( centerToStart, proj, perp );
	float distanceThreshold = shapeRadius + tlc->boxRadius;
	return VectorLengthSquared( perp ) <= distanceThreshold * distanceThreshold;
}

void CMSse42TraceComputer::ClipBoxToLeaf( CMTraceContext *tlc, const cbrush_t *brushes,
										  int numbrushes, const cface_t *markfaces, int nummarkfaces ) {
	volatile VexEncodingFence fence;
	(void)fence;

	// Save the exact address to avoid pointer chasing in loops
	const float *fraction = &tlc->trace->fraction;

	// trace line against all brushes
	for( int i = 0; i < numbrushes; i++ ) {
		const auto *__restrict b = &brushes[i];
		if( !( b->contents & tlc->contents ) ) {
			continue;
		}
		if( !CM_MightCollideInLeaf_SSE42( b->mins, b->maxs, b->center, b->radius, tlc ) ) {
			continue;
		}
		// Specify the "overridden" method explicitly
		CMSse42TraceComputer::ClipBoxToBrush( tlc, b );
		if( !*fraction ) {
			return;
		}
	}

	// trace line against all patches
	for( int i = 0; i < nummarkfaces; i++ ) {
		const auto *__restrict patch = &markfaces[i];
		if( !( patch->contents & tlc->contents ) ) {
			continue;
		}
		if( !CM_MightCollideInLeaf_SSE42( patch->mins, patch->maxs, patch->center, patch->radius, tlc ) ) {
			continue;
		}
		for( int j = 0; j < patch->numfacets; j++ ) {
			const auto *__restrict facet = &patch->facets[j];
			if( !CM_MightCollideInLeaf_SSE42( facet->mins, facet->maxs, facet->center, facet->radius, tlc ) ) {
				continue;
			}
			// Specify the "overridden" method explicitly
			CMSse42TraceComputer::ClipBoxToBrush( tlc, facet );
			if( !*fraction ) {
				return;
			}
		}
	}
}

__attribute__ ((noinline)) int BuildSimdBrushsideData( const cbrushside_t *sides, int numSides, uint8_t *buffer );

// TODO: This was just to get the stuff working, rewrite it
__attribute__ ((noinline)) int BuildSimdBrushsideData( const cbrushside_t *sides, int numSides, uint8_t *buffer ) {
	if( numSides >= 256 ) {
		printf( "Too many brushsides %d\n", numSides );
		abort();
	}

	int numVectorGroups = numSides / 4;
	int tail = 0;
	if( const auto rem = numSides % 4 ) {
		numVectorGroups += 1;
		tail = 4 - rem;
	}

	const int floatDataStride = numVectorGroups * 4 * sizeof( float );
	const int intDataStride = numVectorGroups * 4 * sizeof( float );
	const int shortDataStride = numVectorGroups * 4 * sizeof( short );

	auto *const xPtr = (float *)( buffer );
	auto *const yPtr = (float *)( buffer + floatDataStride );
	auto *const zPtr = (float *)( buffer + 2 * floatDataStride );
	auto *const blendsXPtr = (uint32_t *)( buffer + 3 * floatDataStride );
	auto *const blendsYPtr = (uint32_t *)( buffer + 4 * floatDataStride );
	auto *const blendsZPtr = (uint32_t *)( buffer + 5 * floatDataStride );
	auto *const dPtr = (float *)( buffer + 6 * floatDataStride );

	auto *const sideNumsPtr = (int *)( buffer + 7 * floatDataStride );
	auto *const shaderRefsPtr = (int *)( buffer + 7 * floatDataStride + intDataStride );
	auto *const surfFlagsPtr = (int *)( buffer + 7 * floatDataStride + 2 * intDataStride );
	auto *const planeTypesPtr = (short *)( buffer + 7 * floatDataStride + 3 * intDataStride );
	auto *const signBitsPtr = (short *)( buffer + 7 * floatDataStride + 3 * intDataStride + shortDataStride );

	const uint32_t blendsForSign[2] { 0, ~( (uint32_t)0 ) };
	for( int i = 0; i < numSides; ++i ) {
		const cbrushside_t *side = sides + i;
		float nx = xPtr[i] = side->plane.normal[0];
		float ny = yPtr[i] = side->plane.normal[1];
		float nz = zPtr[i] = side->plane.normal[2];
		dPtr[i] = side->plane.dist;

		blendsXPtr[i] = blendsForSign[nx < 0];
		blendsYPtr[i] = blendsForSign[ny < 0];
		blendsZPtr[i] = blendsForSign[nz < 0];

		sideNumsPtr[i] = i;
		shaderRefsPtr[i] = side->shaderNum;
		surfFlagsPtr[i] = side->surfFlags;
		planeTypesPtr[i] = side->plane.type;
		signBitsPtr[i] = side->plane.signbits;
	}

	assert( numSides );
	const int lastSideNum = numSides - 1;
	for( int i = numSides; i < numSides + tail; ++i ) {
		xPtr[i] = xPtr[lastSideNum];
		yPtr[i] = yPtr[lastSideNum];
		zPtr[i] = zPtr[lastSideNum];
		dPtr[i] = dPtr[lastSideNum];

		blendsXPtr[i] = blendsXPtr[lastSideNum];
		blendsYPtr[i] = blendsYPtr[lastSideNum];
		blendsZPtr[i] = blendsZPtr[lastSideNum];

		sideNumsPtr[i] = sideNumsPtr[lastSideNum];
		shaderRefsPtr[i] = shaderRefsPtr[lastSideNum];
		surfFlagsPtr[i] = surfFlagsPtr[lastSideNum];
		planeTypesPtr[i] = planeTypesPtr[lastSideNum];
		signBitsPtr[i] = signBitsPtr[lastSideNum];
	}

	return numVectorGroups;
}

static void printv( const char *tag, __m128 v ) {
	alignas( 16 ) float data[4];
	_mm_store_ps( data, v );
	printf( "%32s: %f %f %f %f\n", tag, data[0], data[1], data[2], data[3] );
}

static void printx( const char *tag, __m128 v ) {
	alignas( 16 ) uint32_t data[4];
	_mm_store_ps( (float *)data, v );
	printf( "%32s: %08x %08x %08x %08x\n", tag, data[0], data[1], data[2], data[3] );
}

void CMSse42TraceComputer::ClipBoxToBrush( CMTraceContext *tlc, const cbrush_t *brush ) {
	if( !brush->numsides ) {
		return;
	}

	// This is just for exploratory programming.
	// The data must be converted to SIMD-friendly format upon the map loading.
	// Actually, this is performed for world brushes/facets but in a non-optimal fashion.

	alignas( 16 ) uint8_t tmpBuffer[1024];

	const uint8_t *buffer;
	int numVectorGroups;
	if( brush->simd ) {
		numVectorGroups = brush->numSimdGroups;
		buffer = brush->simddata;
	} else {
		numVectorGroups = BuildSimdBrushsideData( brush->brushsides, brush->numsides, tmpBuffer );
		buffer = tmpBuffer;
	}

	// Could use _mm_broadcast_ss() if AVX is the actual target
	__m128 xmmStartMinsX = _mm_set1_ps( tlc->startmins[0] );
	__m128 xmmStartMinsY = _mm_set1_ps( tlc->startmins[1] );
	__m128 xmmStartMinsZ = _mm_set1_ps( tlc->startmins[2] );

	__m128 xmmStartMaxsX = _mm_set1_ps( tlc->startmaxs[0] );
	__m128 xmmStartMaxsY = _mm_set1_ps( tlc->startmaxs[1] );
	__m128 xmmStartMaxsZ = _mm_set1_ps( tlc->startmaxs[2] );

	__m128 xmmEndMinsX = _mm_set1_ps( tlc->endmins[0] );
	__m128 xmmEndMinsY = _mm_set1_ps( tlc->endmins[1] );
	__m128 xmmEndMinsZ = _mm_set1_ps( tlc->endmins[2] );

	__m128 xmmEndMaxsX = _mm_set1_ps( tlc->endmaxs[0] );
	__m128 xmmEndMaxsY = _mm_set1_ps( tlc->endmaxs[1] );
	__m128 xmmEndMaxsZ = _mm_set1_ps( tlc->endmaxs[2] );

	__m128 xmmCurrEnterFracs = _mm_set1_ps( -1.0f );
	__m128 xmmCurrLeaveFracs = _mm_set1_ps( +1.0f );

	const __m128 xmmZero = _mm_setzero_ps();
	const __m128 xmmFFFs = _mm_castsi128_ps( _mm_set1_epi32( ~0 ) );
	const __m128 xmmEps = _mm_set1_ps( DIST_EPSILON );

	int getout = 0;
	int startout = 0;

	const int strideInElems = 4 * numVectorGroups;
	const auto *const __restrict sidenums = (int *)( buffer + 7 * strideInElems * sizeof( float ) );
	const auto *const __restrict cursorPtr = (float *)( buffer );

	// Make it negative so we get a segfault on a misuse
	int leadSideNum = std::numeric_limits<int>::min();
	for( int i = 0; i < numVectorGroups; ++i ) {
		// http://cbloomrants.blogspot.com/2010/07/07-21-10-x86.html
		// "... on x86 you should write code to take use of complex addressing,
		// you can have fewer data dependencies if you just set up one base variable
		// and then do lots of referencing off it.
		const float *__restrict p = cursorPtr + 4 * i;
		const __m128 xmmX = _mm_loadu_ps( p );
		const __m128 xmmY = _mm_loadu_ps( p + strideInElems );
		const __m128 xmmZ = _mm_loadu_ps( p + 2 * strideInElems );
		const __m128 xmmD = _mm_loadu_ps( p + 6 * strideInElems );

		const __m128 xmmBlendsX = _mm_loadu_ps( p + 3 * strideInElems );
		const __m128 xmmBlendsY = _mm_loadu_ps( p + 4 * strideInElems );
		const __m128 xmmBlendsZ = _mm_loadu_ps( p + 5 * strideInElems );

		const __m128 xmmStartBoundsX = _mm_blendv_ps( xmmStartMinsX, xmmStartMaxsX, xmmBlendsX );
		const __m128 xmmStartBoundsY = _mm_blendv_ps( xmmStartMinsY, xmmStartMaxsY, xmmBlendsY );
		const __m128 xmmStartBoundsZ = _mm_blendv_ps( xmmStartMinsZ, xmmStartMaxsZ, xmmBlendsZ );

		const __m128 xmmStartXMuls = _mm_mul_ps( xmmStartBoundsX, xmmX );
		const __m128 xmmStartYMuls = _mm_mul_ps( xmmStartBoundsY, xmmY );
		const __m128 xmmStartZMuls = _mm_mul_ps( xmmStartBoundsZ, xmmZ );

		const __m128 xmmStartDots = _mm_add_ps( _mm_add_ps( xmmStartXMuls, xmmStartYMuls ), xmmStartZMuls );

		const __m128 xmmEndBoundsX = _mm_blendv_ps( xmmEndMinsX, xmmEndMaxsX, xmmBlendsX );
		const __m128 xmmEndBoundsY = _mm_blendv_ps( xmmEndMinsY, xmmEndMaxsY, xmmBlendsY );
		const __m128 xmmEndBoundsZ = _mm_blendv_ps( xmmEndMinsZ, xmmEndMaxsZ, xmmBlendsZ );

		const __m128 xmmEndXMuls = _mm_mul_ps( xmmEndBoundsX, xmmX );
		const __m128 xmmEndYMuls = _mm_mul_ps( xmmEndBoundsY, xmmY );
		const __m128 xmmEndZMuls = _mm_mul_ps( xmmEndBoundsZ, xmmZ );

		const __m128 xmmEndDots = _mm_add_ps( _mm_add_ps( xmmEndXMuls, xmmEndYMuls ), xmmEndZMuls );

		const __m128 xmmDist1 = _mm_sub_ps( xmmStartDots, xmmD );
		const __m128 xmmDist2 = _mm_sub_ps( xmmEndDots, xmmD );

		const __m128 xmmDist1Gt0 = _mm_cmpgt_ps( xmmDist1, xmmZero );

		// "if completely in front of face, no intersection"
		// Check whether d1 > 0 && d2 >= d1 holds for any vector component
		if( _mm_movemask_ps( _mm_and_ps( xmmDist1Gt0, _mm_cmpge_ps( xmmDist2, xmmDist1 ) ) ) ) {
			return;
		}

		const __m128 xmmDist2Gt0 = _mm_cmpgt_ps( xmmDist2, xmmZero );
		const __m128 xmmDist1Le0 = _mm_xor_ps( xmmDist1Gt0, xmmFFFs );
		const __m128 xmmDist2Le0 = _mm_xor_ps( xmmDist2Gt0, xmmFFFs );

		// "if( d2 > 0 ) getout = true"
		// Check whether d2 > 0 holds for any vector component
		getout |= _mm_movemask_ps( xmmDist2Gt0 );
		// "if( d1 > 0 ) startout = true"
		// Check whether d1 > 0 holds for any vector component
		startout |= _mm_movemask_ps( xmmDist1Gt0 );

		// "if( d1 <= 0 && d2 <= 0 ) continue"
		const __m128 xmmSkipMask = _mm_and_ps( xmmDist1Le0, xmmDist2Le0 );
		// Check for early exit
		if( _mm_movemask_ps( xmmSkipMask ) == 0xF ) {
			continue;
		}

		// We could use _mm_andnot_ps with the skip mask but its slower (wtf?)
		const __m128 xmmTestMask = _mm_xor_ps( xmmSkipMask, xmmFFFs );
		const __m128 xmmF = _mm_sub_ps( xmmDist1, xmmDist2 );

		// TODO: Is it that safe? Use a precise division maybe?
		const __m128 xmmInvDelta = _mm_rcp_ps( xmmF );
		const __m128 xmmEnterFracs = _mm_mul_ps( _mm_sub_ps( xmmDist1, xmmEps ), xmmInvDelta );
		const __m128 xmmFNegMask = _mm_and_ps( xmmTestMask, _mm_cmplt_ps( xmmF, xmmZero ) );
		const __m128 xmmLeaveFracs = _mm_mul_ps( _mm_add_ps( xmmDist1, xmmEps ), xmmInvDelta );
		const __m128 xmmFPosMask = _mm_and_ps( xmmTestMask, _mm_cmpgt_ps( xmmF, xmmZero ) );

		const __m128 xmmEnterFracsToCmp = _mm_blendv_ps( xmmCurrEnterFracs, xmmEnterFracs, xmmFPosMask );
		const __m128 xmmMaxEnterFracs = _mm_max_ps( xmmCurrEnterFracs, xmmEnterFracsToCmp );
		const __m128 xmmLeaveFracsToCmp = _mm_blendv_ps( xmmCurrLeaveFracs, xmmLeaveFracs, xmmFNegMask );
		const __m128 xmmMinLeaveFracs = _mm_min_ps( xmmCurrLeaveFracs, xmmLeaveFracsToCmp );

		// Compute a horizontal max of the enter fracs vector and a horizontal min of the leave fracs vector
		// https://stackoverflow.com/a/35270026

		__m128 xmmTmpShuf1 = _mm_movehdup_ps( xmmMaxEnterFracs );        // broadcast elements 3,1 to 2,0
		__m128 xmmTmpRes1 = _mm_max_ps( xmmMaxEnterFracs, xmmTmpShuf1 );
		xmmTmpShuf1 = _mm_movehl_ps( xmmTmpShuf1, xmmTmpRes1 ); // high half -> low half
		xmmTmpRes1 = _mm_max_ss( xmmTmpRes1, xmmTmpShuf1 );

		// We have to find an index of the new max enter frac (if any) to store the lead side num
		const __m128 xmmAllMaxEnterFracs = _mm_shuffle_ps( xmmTmpRes1, xmmTmpRes1, _MM_SHUFFLE( 0, 0, 0, 0 ) );

		const __m128 xmmMatchBestIndexCmp = _mm_cmpeq_ps( xmmMaxEnterFracs, xmmAllMaxEnterFracs );
		const __m128 xmmMatchLessCurrCmp = _mm_cmpgt_ps( xmmMaxEnterFracs, xmmCurrEnterFracs );
		const __m128 xmmIndexMask = _mm_and_ps( xmmMatchBestIndexCmp, xmmMatchLessCurrCmp );
		const int indexMask = _mm_movemask_ps( xmmIndexMask );

		// This could have been branchless, e.g we could unload to memory
		// and address the current result by -1 but the branchless TZCNT
		// is not omnipresent and also memory ops could suck.
		if( indexMask ) {
			leadSideNum = sidenums[( 4 * i ) + __builtin_ctz( indexMask )];
			xmmCurrEnterFracs = xmmAllMaxEnterFracs;
		}

		// Save min leave fracs for the next iteration
		// Doing this after the branch is actually faster as it probably could use a misprediction latency window
		__m128 xmmTmpShuf2 = _mm_movehdup_ps( xmmMinLeaveFracs );        // broadcast elements 3,1 to 2,0
		__m128 xmmTmpRes2 = _mm_min_ps( xmmMinLeaveFracs, xmmTmpShuf2 );
		xmmTmpShuf2 = _mm_movehl_ps( xmmTmpShuf2, xmmTmpRes2 ); // high half -> low half
		xmmTmpRes2 = _mm_min_ss( xmmTmpRes2, xmmTmpShuf2 );
		xmmCurrLeaveFracs = _mm_shuffle_ps( xmmTmpRes2, xmmTmpRes2, _MM_SHUFFLE( 0, 0, 0, 0 ) );
	}

	if( !startout ) {
		// original point was inside brush
		tlc->trace->startsolid = true;
		tlc->trace->contents = brush->contents;
		if( !getout ) {
			tlc->trace->allsolid = true;
			tlc->trace->fraction = 0;
		}
		return;
	}

	float enterfrac = _mm_cvtss_f32( xmmCurrEnterFracs );
	const float leavefrac = _mm_cvtss_f32( xmmCurrLeaveFracs );
	if( enterfrac - ( 1.0f / 1024.0f ) <= leavefrac ) {
		if( enterfrac > -1 && enterfrac < tlc->trace->fraction ) {
			// TODO: Rewrite these magic offsets
			const int floatStrideInBytes = strideInElems * sizeof( float );
			const int intStrideInBytes = strideInElems * sizeof( float );
			const int shortStrideInBytes = strideInElems * sizeof( short );
			const auto *const shadernums = (int *)( buffer + 7 * floatStrideInBytes + intStrideInBytes );
			const auto *const surfflags = (int *)( buffer + 7 * floatStrideInBytes + 2 * intStrideInBytes );
			const auto *const sidetype = (short *)( buffer + 7 * floatStrideInBytes + 3 * intStrideInBytes );
			const auto *const sidebits = (short *)( buffer + 7 * floatStrideInBytes + 3 * intStrideInBytes + shortStrideInBytes );
			const auto *const sidex = (float *)( buffer );
			const auto *const sidey = (float *)( buffer + floatStrideInBytes );
			const auto *const sidez = (float *)( buffer + 2 * floatStrideInBytes );
			const auto *const sided = (float *)( buffer + 6 * floatStrideInBytes );
			if( enterfrac < 0 ) {
				enterfrac = 0;
			}
			tlc->trace->fraction = enterfrac;
			cplane_s *destPlane = &tlc->trace->plane;
			assert( (unsigned)leadSideNum < (unsigned)brush->numsides );
			destPlane->normal[0] = sidex[leadSideNum];
			destPlane->normal[1] = sidey[leadSideNum];
			destPlane->normal[2] = sidez[leadSideNum];
			destPlane->dist = sided[leadSideNum];
			destPlane->signbits = sidebits[leadSideNum];
			destPlane->type = sidetype[leadSideNum];
			tlc->trace->surfFlags = surfflags[leadSideNum];
			tlc->trace->contents = brush->contents;
			tlc->trace->shaderNum = shadernums[leadSideNum];
		}
	}
}

void CMSse42TraceComputer::SetupCollideContext( CMTraceContext *tlc, trace_t *tr,
												const vec_t *start, const vec3_t end,
												const vec3_t mins, const vec3_t maxs, int brushmask ) {
	CMTraceComputer::SetupCollideContext( tlc, tr, start, end, mins, maxs, brushmask );
	// Put the fence after the super method call (that does not use VEX encoding)
	volatile VexEncodingFence fence;
	(void)fence;

	// Always set xmm trace bounds since it is used by all code paths, leaf-optimized and generic
	tlc->xmmAbsmins = _mm_setr_ps( tlc->absmins[0], tlc->absmins[1], tlc->absmins[2], 0 );
	tlc->xmmAbsmaxs = _mm_setr_ps( tlc->absmaxs[0], tlc->absmaxs[1], tlc->absmaxs[2], 1 );
}

void CMSse42TraceComputer::BuildShapeList( CMShapeList *list, const float *mins, const float *maxs, int clipMask ) {
	int leafNums[1024], topNode;
	// TODO: This can be optimized
	const int numLeaves = CM_BoxLeafnums( cms, mins, maxs, leafNums, 1024, &topNode );

	int numShapes = 0;
	const auto *leaves = cms->map_leafs;

	__m128 testedMins = _mm_setr_ps( mins[0], mins[1], mins[2], 0 );
	__m128 testedMaxs = _mm_setr_ps( maxs[0], maxs[1], maxs[2], 1 );

	auto *__restrict destShapes = list->shapes;
	for( int i = 0; i < numLeaves; ++i ) {
		const auto *__restrict leaf = &leaves[leafNums[i]];
		const auto *brushes = leaf->brushes;
		for( int j = 0; j < leaf->numbrushes; ++j ) {
			const auto *__restrict b = &brushes[j];
			if( !( b->contents & clipMask ) ) {
				continue;
			}
			if( !CM_BoundsIntersect_SSE42( testedMins, testedMaxs, b->mins, b->maxs ) ) {
				continue;
			}
			destShapes[numShapes++] = b;
		}

		const auto *faces = leaf->faces;
		for( int j = 0; j < leaf->numfaces; ++j ) {
			const auto *__restrict f = &faces[j];
			if( !( f->contents & clipMask ) ) {
				continue;
			}
			if( !CM_BoundsIntersect_SSE42( testedMins, testedMaxs, f->mins, f->maxs ) ) {
				continue;
			}
			for( int k = 0; k < f->numfacets; ++k ) {
				const auto *__restrict b = &f->facets[k];
				if( !CM_BoundsIntersect_SSE42( testedMins, testedMaxs, b->mins, b->maxs ) ) {
					continue;
				}
				destShapes[numShapes++] = b;
			}
		}
	}

	list->hasBounds = false;
	list->numShapes = numShapes;
}

void CMSse42TraceComputer::ClipShapeList( CMShapeList *list, const CMShapeList *baseList, const float *mins, const float *maxs ) {
	const int numSrcShapes = baseList->numShapes;
	const auto *srcShapes = baseList->shapes;

	int numDestShapes = 0;
	auto *destShapes = list->shapes;

	__m128 testedMins = _mm_setr_ps( mins[0], mins[1], mins[2], 0 );
	__m128 testedMaxs = _mm_setr_ps( maxs[0], maxs[1], maxs[2], 1 );

	BoundsBuilder builder;
	for( int i = 0; i < numSrcShapes; ++i ) {
		const cbrush_t *__restrict b = srcShapes[i];
		__m128 shapeMins = _mm_loadu_ps( b->mins );
		__m128 shapeMaxs = _mm_loadu_ps( b->maxs );
		if( !CM_BoundsIntersect_SSE42( testedMins, testedMaxs, shapeMins, shapeMaxs ) ) {
			continue;
		}

		destShapes[numDestShapes++] = b;
		builder.addPoint( shapeMins );
		builder.addPoint( shapeMaxs );
	}

	list->numShapes = numDestShapes;
	list->hasBounds = numDestShapes > 0;
	if( list->hasBounds ) {
		builder.storeTo( list->mins, list->maxs );
	}
}

void CMSse42TraceComputer::ClipToShapeList( const CMShapeList *list, trace_t *tr,
					  const float *start, const float *end,
					  const float *mins, const float *maxs, int clipMask ) {
	alignas( 16 ) CMTraceContext tlc;
	SetupCollideContext( &tlc, tr, start, end, mins, maxs, clipMask );

	if( list->hasBounds ) {
		if( !BoundsIntersect( list->mins, list->maxs, tlc.absmins, tlc.absmaxs ) ) {
			assert( tr->fraction == 1.0f );
			VectorCopy( end, tr->endpos );
			return;
		}
	}

	// Make sure the virtual call address gets resolved here
	auto clipFn = &CMSse42TraceComputer::ClipBoxToBrush;
	if( !VectorCompare( start, end ) ) {
		SetupClipContext( &tlc );
	} else {
		clipFn = &CMTraceComputer::TestBoxInBrush;
	}

	const int numShapes = list->numShapes;
	const auto *__restrict shapes = list->shapes;
	float *const __restrict fraction = &tlc.trace->fraction;
	for( int i = 0; i < numShapes; ++i ) {
		const cbrush_t *__restrict b = shapes[i];
		if( !CM_BoundsIntersect_SSE42( tlc.xmmAbsmins, tlc.xmmAbsmaxs, b->mins, b->maxs ) ) {
			continue;
		}
		( this->*clipFn )( &tlc, b );
		if( !*fraction ) {
			break;
		}
	}

	if( tr->fraction == 1.0f ) {
		VectorCopy( end, tr->endpos );
	} else {
		VectorLerp( start, tr->fraction, end, tr->endpos );
#ifdef TRACE_NOAXIAL
		if( PlaneTypeForNormal( tr->plane.normal ) == PLANE_NONAXIAL ) {
			VectorMA( tr->endpos, TRACE_NOAXIAL_SAFETY_OFFSET, tr->plane.normal, tr->endpos );
		}
#endif
	}
}

#endif