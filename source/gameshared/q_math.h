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

#ifndef GAME_QMATH_H
#define GAME_QMATH_H

#include "q_arch.h"

//==============================================================
//
//MATHLIB
//
//==============================================================

enum {
	PITCH = 0,      // up / down
	YAW = 1,        // left / right
	ROLL = 2        // fall over
};

enum {
	FORWARD = 0,
	RIGHT = 1,
	UP = 2
};

enum {
	AXIS_FORWARD = 0,
	AXIS_RIGHT = 3,
	AXIS_UP = 6
};

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];
typedef vec_t vec5_t[5];

typedef vec_t quat_t[4];

typedef vec_t mat3_t[9];

typedef vec_t dualquat_t[8];

typedef uint8_t byte_vec4_t[4];

// 0-2 are axial planes
#define PLANE_X     0
#define PLANE_Y     1
#define PLANE_Z     2
#define PLANE_NONAXIAL  3

// cplane_t structure
typedef struct cplane_s {
	vec3_t normal;
	float dist;
	short type;                 // for fast side tests
	short signbits;             // signx + (signy<<1) + (signz<<1)
} cplane_t;

constexpr const vec3_t vec3_origin { 0, 0, 0 };
constexpr const mat3_t axis_identity { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
constexpr const quat_t quat_identity { 0, 0, 0, 1 };

constexpr const vec4_t colorBlack    { 0, 0, 0, 1 };
constexpr const vec4_t colorRed      { 1, 0, 0, 1 };
constexpr const vec4_t colorGreen    { 0, 1, 0, 1 };
constexpr const vec4_t colorBlue     { 0, 0, 1, 1 };
constexpr const vec4_t colorYellow   { 1, 1, 0, 1 };
constexpr const vec4_t colorOrange   { 1, 0.5, 0, 1 };
constexpr const vec4_t colorMagenta  { 1, 0, 1, 1 };
constexpr const vec4_t colorCyan     { 0, 1, 1, 1 };
constexpr const vec4_t colorWhite    { 1, 1, 1, 1 };
constexpr const vec4_t colorLtGrey   { 0.75, 0.75, 0.75, 1 };
constexpr const vec4_t colorMdGrey   { 0.5, 0.5, 0.5, 1 };
constexpr const vec4_t colorDkGrey   { 0.25, 0.25, 0.25, 1 };

#define MAX_S_COLORS 10

extern const vec4_t color_table[MAX_S_COLORS];

#ifndef M_PI
#define M_PI       3.14159265358979323846   // matches value in gcc v2 math.h
#endif

#ifndef M_TWOPI
#define M_TWOPI    6.28318530717958647692
#endif

#define DEG2RAD( a ) ( a * M_PI ) / 180.0F
#define RAD2DEG( a ) ( a * 180.0F ) / M_PI


// returns b clamped to [a..c] range
//#define bound(a,b,c) (max((a), min((b), (c))))

#ifndef Q_max
#define Q_max( a, b ) ( ( a ) > ( b ) ? ( a ) : ( b ) )
#endif

#ifndef Q_min
#define Q_min( a, b ) ( ( a ) < ( b ) ? ( a ) : ( b ) )
#endif

#define bound( a, b, c ) ( ( a ) >= ( c ) ? ( a ) : ( b ) < ( a ) ? ( a ) : ( b ) > ( c ) ? ( c ) : ( b ) )

// clamps a (must be lvalue) to [b..c] range
#define Q_clamp( a, b, c ) ( ( b ) >= ( c ) ? ( a ) = ( b ) : ( a ) < ( b ) ? ( a ) = ( b ) : ( a ) > ( c ) ? ( a ) = ( c ) : ( a ) )

#define clamp_low( a, low ) ( ( a ) = ( a ) < ( low ) ? ( low ) : ( a ) )
#define clamp_high( a, high ) ( ( a ) = ( a ) > ( high ) ? ( high ) : ( a ) )

#define random()    ( ( rand() & 0x7fff ) / ( (float)0x7fff ) )  // 0..1
#define brandom( a, b )    ( ( a ) + random() * ( ( b ) - ( a ) ) )                // a..b
#define crandom()   brandom( -1, 1 )                           // -1..1

int Q_rand( int *seed );
#define Q_random( seed )      ( ( Q_rand( seed ) & 0x7fff ) / ( (float)0x7fff ) )    // 0..1
#define Q_brandom( seed, a, b ) ( ( a ) + Q_random( seed ) * ( ( b ) - ( a ) ) )                      // a..b
#define Q_crandom( seed )     Q_brandom( seed, -1, 1 )

#if ( defined ( __i386__ ) || defined ( __x86_64__ ) || defined( _M_IX86 ) || defined( _M_AMD64 ) || defined( _M_X64 ) )

static inline float Q_RSqrt( float number ) {
	assert( number >= 0 );
	return _mm_cvtss_f32( _mm_rsqrt_ss( _mm_set_ss( number ) ) );
}

static inline float Q_Rcp( float number ) {
	return _mm_cvtss_f32( _mm_rcp_ss( _mm_set_ss( number ) ) );
}

static inline float Q_Sqrt( float number ) {
	assert( number >= 0 );

	// jal : The expression a * rsqrt(b) is intended as a higher performance alternative to a / sqrt(b).
	// The two expressions are comparably accurate, but do not compute exactly the same value in every case.
	// For example, a * rsqrt(a*a + b*b) can be just slightly greater than 1, in rare cases.

	// We have to check for zero or else a NAN is produced (0 times infinity)

	// Force an eager computation of result for further branch-less selection.
	// Note that modern x86 handle infinities/NANs without penalties.
	// Supplying zeroes is rare anyway.
	float maybeSqrt = number * _mm_cvtss_f32( _mm_rsqrt_ss( _mm_set_ss( number ) ) );

	// There's unfortunately no CMOV for the actually used SSE2+ f.p. instruction set.
	// Here's a workaround that uses the integer CMOV.
	// As far as we know MSVC 2019 produces an expected code (while a 2-elements array approach generates a branch).

	// Copy the value bits to an integer
	int32_t maybeSqrtAsInt = *( (int32_t *)&maybeSqrt );
	// This should be a CMOV. The UCOMISS instruction sets the needed flags.
	// Note that in case when `number` bits are also converted to an integer the sign bit should be masked.
	int32_t resultAsInt = number ? maybeSqrtAsInt : 0;
	// Return the value bits as a float
	return *( (float *)( &resultAsInt ) );
}

#else

static inline float Q_RSqrt( float number ) {
	assert( number >= 0 );
	return 1.0f / sqrtf( number );
}

static inline float Q_Rcp( float number ) {
	return 1.0f / number;
}

static inline float Q_Sqrt( float number ) {
	assert( number >= 0 );
	return sqrtf( number );
}

#endif

int Q_log2( int val );

int Q_bitcount( int v );

#define DotProduct( x, y )     ( ( x )[0] * ( y )[0] + ( x )[1] * ( y )[1] + ( x )[2] * ( y )[2] )
#define CrossProduct( v1, v2, cross ) ( ( cross )[0] = ( v1 )[1] * ( v2 )[2] - ( v1 )[2] * ( v2 )[1], ( cross )[1] = ( v1 )[2] * ( v2 )[0] - ( v1 )[0] * ( v2 )[2], ( cross )[2] = ( v1 )[0] * ( v2 )[1] - ( v1 )[1] * ( v2 )[0] )

#define PlaneDiff( point, plane ) ( ( ( plane )->type < 3 ? ( point )[( plane )->type] : DotProduct( ( point ), ( plane )->normal ) ) - ( plane )->dist )

#define VectorSubtract( a, b, c )   ( ( c )[0] = ( a )[0] - ( b )[0], ( c )[1] = ( a )[1] - ( b )[1], ( c )[2] = ( a )[2] - ( b )[2] )
#define VectorAdd( a, b, c )        ( ( c )[0] = ( a )[0] + ( b )[0], ( c )[1] = ( a )[1] + ( b )[1], ( c )[2] = ( a )[2] + ( b )[2] )
#define VectorCopy( a, b )     ( ( b )[0] = ( a )[0], ( b )[1] = ( a )[1], ( b )[2] = ( a )[2] )
#define VectorClear( a )      ( ( a )[0] = ( a )[1] = ( a )[2] = 0 )
#define VectorNegate( a, b )       ( ( b )[0] = -( a )[0], ( b )[1] = -( a )[1], ( b )[2] = -( a )[2] )
#define VectorSet( v, x, y, z )   ( ( v )[0] = ( x ), ( v )[1] = ( y ), ( v )[2] = ( z ) )
#define VectorAvg( a, b, c )        ( ( c )[0] = ( ( a )[0] + ( b )[0] ) * 0.5f, ( c )[1] = ( ( a )[1] + ( b )[1] ) * 0.5f, ( c )[2] = ( ( a )[2] + ( b )[2] ) * 0.5f )
#define VectorMA( a, b, c, d )       ( ( d )[0] = ( a )[0] + ( b ) * ( c )[0], ( d )[1] = ( a )[1] + ( b ) * ( c )[1], ( d )[2] = ( a )[2] + ( b ) * ( c )[2] )
#define VectorCompare( v1, v2 )    ( ( v1 )[0] == ( v2 )[0] && ( v1 )[1] == ( v2 )[1] && ( v1 )[2] == ( v2 )[2] )
#define VectorLengthSquared( v )    ( DotProduct( ( v ), ( v ) ) )
#define VectorLength( v )     ( sqrt( VectorLengthSquared( v ) ) )
#define VectorInverse( v )    ( ( v )[0] = -( v )[0], ( v )[1] = -( v )[1], ( v )[2] = -( v )[2] )
#define VectorLerp( a, c, b, v )     ( ( v )[0] = ( a )[0] + ( c ) * ( ( b )[0] - ( a )[0] ), ( v )[1] = ( a )[1] + ( c ) * ( ( b )[1] - ( a )[1] ), ( v )[2] = ( a )[2] + ( c ) * ( ( b )[2] - ( a )[2] ) )
#define VectorScale( in, scale, out ) ( ( out )[0] = ( in )[0] * ( scale ), ( out )[1] = ( in )[1] * ( scale ), ( out )[2] = ( in )[2] * ( scale ) )

#define DistanceSquared( v1, v2 ) ( ( ( v1 )[0] - ( v2 )[0] ) * ( ( v1 )[0] - ( v2 )[0] ) + ( ( v1 )[1] - ( v2 )[1] ) * ( ( v1 )[1] - ( v2 )[1] ) + ( ( v1 )[2] - ( v2 )[2] ) * ( ( v1 )[2] - ( v2 )[2] ) )
#define Distance( v1, v2 ) ( sqrt( DistanceSquared( v1, v2 ) ) )

#define VectorLengthFast( v )     ( Q_Sqrt( DotProduct( ( v ), ( v ) ) ) )
#define DistanceFast( v1, v2 )     ( Q_Sqrt( DistanceSquared( v1, v2 ) ) )

#define Vector2Set( v, x, y )     ( ( v )[0] = ( x ), ( v )[1] = ( y ) )
#define Vector2Copy( a, b )    ( ( b )[0] = ( a )[0], ( b )[1] = ( a )[1] )
#define Vector2Avg( a, b, c )       ( ( c )[0] = ( ( ( a[0] ) + ( b[0] ) ) * 0.5f ), ( c )[1] = ( ( ( a[1] ) + ( b[1] ) ) * 0.5f ) )

#define Vector4Set( v, a, b, c, d )   ( ( v )[0] = ( a ), ( v )[1] = ( b ), ( v )[2] = ( c ), ( v )[3] = ( d ) )
#define Vector4Clear( a )     ( ( a )[0] = ( a )[1] = ( a )[2] = ( a )[3] = 0 )
#define Vector4Copy( a, b )    ( ( b )[0] = ( a )[0], ( b )[1] = ( a )[1], ( b )[2] = ( a )[2], ( b )[3] = ( a )[3] )
#define Vector4Scale( in, scale, out )      ( ( out )[0] = ( in )[0] * scale, ( out )[1] = ( in )[1] * scale, ( out )[2] = ( in )[2] * scale, ( out )[3] = ( in )[3] * scale )
#define Vector4Add( a, b, c )       ( ( c )[0] = ( ( ( ( a )[0] ) + ( ( b )[0] ) ) ), ( c )[1] = ( ( ( ( a )[1] ) + ( ( b )[1] ) ) ), ( c )[2] = ( ( ( ( a )[2] ) + ( ( b )[2] ) ) ), ( c )[3] = ( ( ( ( a )[3] ) + ( ( b )[3] ) ) ) )
#define Vector4Avg( a, b, c )       ( ( c )[0] = ( ( ( ( a )[0] ) + ( ( b )[0] ) ) * 0.5f ), ( c )[1] = ( ( ( ( a )[1] ) + ( ( b )[1] ) ) * 0.5f ), ( c )[2] = ( ( ( ( a )[2] ) + ( b )[2] ) ) * 0.5f ), ( c )[3] = ( ( ( ( a )[3] ) + ( ( b )[3] ) ) * 0.5f ) )
#define Vector4Negate( a, b )      ( ( b )[0] = -( a )[0], ( b )[1] = -( a )[1], ( b )[2] = -( a )[2], ( b )[3] = -( a )[3] )
#define Vector4Inverse( v )         ( ( v )[0] = -( v )[0], ( v )[1] = -( v )[1], ( v )[2] = -( v )[2], ( v )[3] = -( v )[3] )
#define DotProduct4( x, y )    ( ( x )[0] * ( y )[0] + ( x )[1] * ( y )[1] + ( x )[2] * ( y )[2] + ( x )[3] * ( y )[3] )

inline float VectorNormalize( float *v ) {
	const float squareLen = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	float len = 0.0f;

	// TODO: [[likely]]
	if( squareLen > 0 ) {
		len = std::sqrt( squareLen );
		float invLen = 1.0f / len;
		v[0] *= invLen;
		v[1] *= invLen;
		v[2] *= invLen;
	}

	return len;
}

inline float VectorNormalize2( const vec3_t v, vec3_t out ) {
	const float squareLen = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	float len = 0.0f;

	// TODO: [[likely]]
	if( squareLen > 0 ) {
		len = std::sqrt( squareLen );
		const float invLen = 1.0f / len;
		out[0] = v[0] * invLen;
		out[1] = v[1] * invLen;
		out[2] = v[2] * invLen;
	} else {
		VectorClear( out );
	}

	return len;
}

inline float Vector4Normalize( float *v ) {
	const float squareLen = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
	float len = 0.0f;

	// TODO: [[likely]]
	if( squareLen > 0.0f ) {
		len = std::sqrt( squareLen );
		const float invLen = 1.0f / len;
		v[0] *= invLen;
		v[1] *= invLen;
		v[2] *= invLen;
		v[3] *= invLen;
	}

	return len;
}

inline void VectorNormalizeFast( vec3_t v ) {
	const float invLen = Q_RSqrt( DotProduct( v, v ) );
	v[0] *= invLen;
	v[1] *= invLen;
	v[2] *= invLen;
}

void VectorReflect( const vec3_t v, const vec3_t n, const vec_t dist, vec3_t out );

static inline void ClearBounds( vec3_t mins, vec3_t maxs ) {
	mins[0] = mins[1] = mins[2] = 99999;
	maxs[0] = maxs[1] = maxs[2] = -99999;
}

static inline void AddPointToBounds( const vec3_t v, vec3_t mins, vec3_t maxs ) {
	// A sane compiler should produce a branchless code.
	// We should also use SIMD intrinsics manually if a code path is really hot.
	mins[0] = v[0] < mins[0] ? v[0] : mins[0];
	mins[1] = v[1] < mins[1] ? v[1] : mins[1];
	mins[2] = v[2] < mins[2] ? v[2] : mins[2];
	maxs[0] = v[0] > maxs[0] ? v[0] : maxs[0];
	maxs[1] = v[1] > maxs[1] ? v[1] : maxs[1];
	maxs[2] = v[2] > maxs[2] ? v[2] : maxs[2];
}

float RadiusFromBounds( const vec3_t mins, const vec3_t maxs );

static inline bool BoundsIntersect( const vec3_t mins1, const vec3_t maxs1, const vec3_t mins2, const vec3_t maxs2 ) {
	// Though collision code is likely to use its own BoundsIntersect() version
	// optimized even further, this code is fine too for the rest of the code base

#ifndef WSW_USE_SSE2
	return (bool)( mins1[0] <= maxs2[0] && mins1[1] <= maxs2[1] && mins1[2] <= maxs2[2] &&
				   maxs1[0] >= mins2[0] && maxs1[1] >= mins2[1] && maxs1[2] >= mins2[2] );

#else
	__m128 xmmMins1 = _mm_set_ps( mins1[0], mins1[1], mins1[2], 0 );
	__m128 xmmMaxs1 = _mm_set_ps( maxs1[0], maxs1[1], maxs1[2], 1 );
	__m128 xmmMins2 = _mm_set_ps( mins2[0], mins2[1], mins2[2], 0 );
	__m128 xmmMaxs2 = _mm_set_ps( maxs2[0], maxs2[1], maxs2[2], 1 );

	__m128 cmp1 = _mm_cmpge_ps( xmmMins1, xmmMaxs2 );
	__m128 cmp2 = _mm_cmpge_ps( xmmMins2, xmmMaxs1 );
	return !_mm_movemask_ps( _mm_or_ps( cmp1, cmp2 ) );
#endif
}

class alignas( 16 ) BoundsBuilder {
#ifdef WSW_USE_SSE2
	__m128 m_mins { _mm_setr_ps( +99999, +99999, +99999, 0 ) };
	__m128 m_maxs { _mm_setr_ps( -99999, -99999, -99999, 1 ) };
#else
	vec3_t m_mins { +99999, +99999, +99999 };
	vec3_t m_maxs { -99999, -99999, -99999 };
#endif

#ifdef _DEBUG
	bool m_touched { false };
#endif

	void markAsTouched() {
#ifdef _DEBUG
		m_touched = true;
#endif
	}

	void checkTouched() const {
#ifdef _DEBUG
		assert( m_touched );
#endif
	}
public:
	void addPoint( const float *__restrict p ) {
#ifdef WSW_USE_SSE2
		__m128 xmmP = _mm_setr_ps( p[0], p[1], p[2], 0 );
		m_mins = _mm_min_ps( xmmP, m_mins );
		m_maxs = _mm_max_ps( xmmP, m_maxs );
#else
		m_mins[0] = std::min( p[0], m_mins[0] );
		m_maxs[0] = std::max( p[0], m_maxs[0] );
		m_mins[1] = std::min( p[1], m_mins[1] );
		m_maxs[1] = std::max( p[1], m_maxs[1] );
		m_mins[2] = std::min( p[2], m_mins[2] );
		m_maxs[2] = std::max( p[2], m_maxs[2] );
#endif

		markAsTouched();
	}

#ifdef WSW_USE_SSE2
	void addPoint( __m128 p ) {
		m_mins = _mm_min_ps( p, m_mins );
		m_maxs = _mm_max_ps( p, m_maxs );

		markAsTouched();
	}
#endif

	void storeTo( float *__restrict mins, float *__restrict maxs ) const {
		checkTouched();

#ifdef WSW_USE_SSE2
		alignas( 16 ) vec4_t tmpMins, tmpMaxs;
		_mm_store_ps( tmpMins, m_mins );
		_mm_store_ps( tmpMaxs, m_maxs );
		VectorCopy( tmpMins, mins );
		VectorCopy( tmpMaxs, maxs );
#else
		VectorCopy( m_mins, mins );
		VectorCopy( m_maxs, maxs );
#endif
	}

	void storeToWithAddedEpsilon( float *__restrict mins, float *__restrict maxs, float epsilon = 1.0f ) const {
		checkTouched();

#ifdef WSW_USE_SSE2
		alignas( 16 ) vec4_t tmpMins, tmpMaxs;
		__m128 xmmEpsilon = _mm_setr_ps( epsilon, epsilon, epsilon, 0.0f );
		__m128 expandedMins = _mm_sub_ps( m_mins, xmmEpsilon );
		__m128 expandedMaxs = _mm_add_ps( m_maxs, xmmEpsilon );
		_mm_store_ps( tmpMins, expandedMins );
		_mm_store_ps( tmpMaxs, expandedMaxs );
		VectorCopy( tmpMins, mins );
		VectorCopy( tmpMaxs, maxs );
#else
		mins[0] = m_mins[0] - epsilon;
		maxs[0] = m_maxs[0] + epsilon;
		mins[1] = m_mins[1] - epsilon;
		maxs[1] = m_maxs[1] + epsilon;
		mins[2] = m_mins[2] - epsilon;
		maxs[2] = m_maxs[2] + epsilon;
#endif
	}

#ifdef WSW_USE_SSE2
	void storeTo( __m128 *__restrict mins, __m128 *__restrict maxs ) {
		checkTouched();
		*mins = m_mins;
		*maxs = m_maxs;
	}

	void storeToWithAddedEpsilon( __m128 *__restrict mins, __m128 *__restrict maxs, float epsilon = 1.0f ) {
		checkTouched();
		__m128 xmmEpsilon = _mm_setr_ps( epsilon, epsilon, epsilon, 0.0f );
		*mins = _mm_sub_ps( m_mins, xmmEpsilon );
		*maxs = _mm_add_ps( m_maxs, xmmEpsilon );
	}
#endif
};

bool BoundsAndSphereIntersect( const vec3_t mins, const vec3_t maxs, const vec3_t centre, float radius );

#define NUMVERTEXNORMALS    162
int DirToByte( const vec3_t dir );
int DirToByteFast( const vec3_t dir );
void ByteToDir( int b, vec3_t dir );
inline bool IsValidDirByte( int byte ) {
	return (unsigned)byte < (unsigned)NUMVERTEXNORMALS;
}

void NormToLatLong( const vec3_t normal, float latlong[2] );

void MakeNormalVectors( const vec3_t forward, vec3_t right, vec3_t up );

static inline void AngleVectors( const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up ) {
	const float deg2Rad = (float)( M_PI ) / 180.0f;

	const float yaw = deg2Rad * angles[YAW];
	const float sy = sinf( yaw );
	const float cy = cosf( yaw );

	const float pitch = deg2Rad * angles[PITCH];
	const float sp = sinf( pitch );
	const float cp = cosf( pitch );

	const float roll = deg2Rad * angles[ROLL];
	const float sr = sinf( roll );
	const float cr = cosf( roll );

	if( forward ) {
		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;
	}

	if( right ) {
		const float t = sr * sp;
		right[0] = ( -1 * t * cy + -1 * cr * -sy );
		right[1] = ( -1 * t * sy + -1 * cr * cy );
		right[2] = -1 * sr * cp;
	}

	if( up ) {
		const float t = cr * sp;
		up[0] = ( t * cy + -sr * -sy );
		up[1] = ( t * sy + -sr * cy );
		up[2] = cr * cp;
	}
}

int BoxOnPlaneSide( const vec3_t emins, const vec3_t emaxs, const struct cplane_s *plane );
float anglemod( float a );
float LerpAngle( float a1, float a2, const float frac );
float AngleSubtract( float a1, float a2 );
void AnglesSubtract( vec3_t v1, vec3_t v2, vec3_t v3 );
float AngleNormalize360( float angle );
float AngleNormalize180( float angle );
float AngleDelta( float angle1, float angle2 );
void VecToAngles( const vec3_t vec, vec3_t angles );
void AnglesToAxis( const vec3_t angles, mat3_t axis );
void NormalVectorToAxis( const vec3_t forward, mat3_t axis );
void BuildBoxPoints( vec3_t p[8], const vec3_t org, const vec3_t mins, const vec3_t maxs );

vec_t ColorNormalize( const vec_t *in, vec_t *out );

#define ColorGrayscale( c ) ( 0.299 * ( c )[0] + 0.587 * ( c )[1] + 0.114 * ( c )[2] )

float CalcFov( float fov_x, float width, float height );
void AdjustFov( float *fov_x, float *fov_y, float width, float height, bool lock_x );

#define Q_sign( x ) ( ( x ) < 0 ? -1 : ( ( x ) > 0 ? 1 : 0 ) )
#define Q_rint( x ) ( ( x ) < 0 ? ( (int)( ( x ) - 0.5f ) ) : ( (int)( ( x ) + 0.5f ) ) )

int SignbitsForPlane( const cplane_t *out );
int PlaneTypeForNormal( const vec3_t normal );
void CategorizePlane( cplane_t *plane );
void PlaneFromPoints( vec3_t verts[3], cplane_t *plane );

bool ComparePlanes( const vec3_t p1normal, vec_t p1dist, const vec3_t p2normal, vec_t p2dist );
void SnapVector( vec3_t normal );
void SnapPlane( vec3_t normal, vec_t *dist );

#define BOX_ON_PLANE_SIDE( emins, emaxs, p )  \
	( ( ( p )->type < 3 ) ?                       \
	  (                                       \
		  ( ( p )->dist <= ( emins )[( p )->type] ) ?  \
		  1                               \
		  :                                   \
		  (                                   \
			  ( ( p )->dist >= ( emaxs )[( p )->type] ) ? \
			  2                           \
			  :                               \
			  3                           \
		  )                                   \
	  )                                       \
	  :                                       \
	  BoxOnPlaneSide( ( emins ), ( emaxs ), ( p ) ) )

void ProjectPointOntoPlane( vec3_t dst, const vec3_t p, const vec3_t normal );
void PerpendicularVector( vec3_t dst, const vec3_t src );
void RotatePointAroundVector( vec3_t dst, const vec3_t dir, const vec3_t point, float degrees );
void ProjectPointOntoVector( const vec3_t point, const vec3_t vStart, const vec3_t vDir, vec3_t vProj );

float LinearMovementWithOvershoot( vec_t start, vec_t end, float duration, float freq, float decay, float t );

void Matrix3_Identity( mat3_t m );
void Matrix3_Copy( const mat3_t m1, mat3_t m2 );
bool Matrix3_Compare( const mat3_t m1, const mat3_t m2 );
void Matrix3_Multiply( const mat3_t m1, const mat3_t m2, mat3_t out );

inline void Matrix3_TransformVector( const mat3_t m, const vec3_t v, vec3_t out ) {
	out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
	out[1] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
	out[2] = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
}

void Matrix3_Transpose( const mat3_t in, mat3_t out );
void Matrix3_FromAngles( const vec3_t angles, mat3_t m );
void Matrix3_ToAngles( const mat3_t m, vec3_t angles );
void Matrix3_Rotate( const mat3_t in, vec_t angle, vec_t x, vec_t y, vec_t z, mat3_t out );
void Matrix3_FromPoints( const vec3_t v1, const vec3_t v2, const vec3_t v3, mat3_t m );
void Matrix3_Normalize( mat3_t m );

void Quat_Identity( quat_t q );
void Quat_Copy( const quat_t q1, quat_t q2 );
void Quat_Quat3( const vec3_t in, quat_t out );
bool Quat_Compare( const quat_t q1, const quat_t q2 );
void Quat_Conjugate( const quat_t q1, quat_t q2 );
vec_t Quat_DotProduct( const quat_t q1, const quat_t q2 );
vec_t Quat_Normalize( quat_t q );
vec_t Quat_Inverse( const quat_t q1, quat_t q2 );
void Quat_Multiply( const quat_t q1, const quat_t q2, quat_t out );
void Quat_Lerp( const quat_t q1, const quat_t q2, vec_t t, quat_t out );
void Quat_Vectors( const quat_t q, vec3_t f, vec3_t r, vec3_t u );
void Quat_ToMatrix3( const quat_t q, mat3_t m );
void Quat_FromMatrix3( const mat3_t m, quat_t q );
void Quat_TransformVector( const quat_t q, const vec3_t v, vec3_t out );
void Quat_ConcatTransforms( const quat_t q1, const vec3_t v1, const quat_t q2, const vec3_t v2, quat_t q, vec3_t v );

void DualQuat_Identity( dualquat_t dq );
void DualQuat_Copy( const dualquat_t in, dualquat_t out );
void DualQuat_FromAnglesAndVector( const vec3_t angles, const vec3_t v, dualquat_t out );
void DualQuat_FromMatrix3AndVector( const mat3_t m, const vec3_t v, dualquat_t out );
void DualQuat_FromQuatAndVector( const quat_t q, const vec3_t v, dualquat_t out );
void DualQuat_FromQuat3AndVector( const vec3_t q, const vec3_t v, dualquat_t out );
void DualQuat_GetVector( const dualquat_t dq, vec3_t v );
void DualQuat_ToQuatAndVector( const dualquat_t dq, quat_t q, vec3_t v );
void DualQuat_ToMatrix3AndVector( const dualquat_t dq, mat3_t m, vec3_t v );
void DualQuat_Invert( dualquat_t dq );
vec_t DualQuat_Normalize( dualquat_t dq );
void DualQuat_Multiply( const dualquat_t dq1, const dualquat_t dq2, dualquat_t out );
void DualQuat_Lerp( const dualquat_t dq1, const dualquat_t dq2, vec_t t, dualquat_t out );

vec_t LogisticCDF( vec_t x );
vec_t LogisticPDF( vec_t x );
vec_t NormalCDF( vec_t x );
vec_t NormalPDF( vec_t x );

#endif // GAME_QMATH_H
