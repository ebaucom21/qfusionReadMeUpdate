#include "boundsbuildertest.h"
#include "../../gameshared/q_math.h"

#include <QtGui/QVector3D>

#ifdef WSW_USE_SSE2
#undef WSW_USE_SSE2
#endif

static const vec3_t testedPoints1[] {
	{ -10, 3, 89 }, { -7, 7, -42 }, { 84, 95, 72 }, { -108, -4, 73 }
};

static const vec3_t expectedMins1 { -108, -4, -42 };
static const vec3_t expectedMaxs1 { 84, 95, 89 };

static const vec3_t testedPoints2[] {
	{ -32, 78, 67 }, { 84, 87, -34 }, { 85, 11, 86 }, { -5, -6, -111 }
};

static const vec3_t expectedMins2 { -32, -6, -111 };
static const vec3_t expectedMaxs2 { 85, 87, 86 };

[[nodiscard]]
auto toQV( const float *v ) -> QVector3D {
	return QVector3D( v[0], v[1], v[2] );
}

[[nodiscard]]
auto toQV( __m128 v ) -> QVector3D {
	alignas( 16 ) float tmp[4];
	_mm_store_ps( tmp, v );
	return QVector3D( v[0], v[1], v[2] );
}

[[nodiscard]]
auto toXmm( const float *v ) -> __m128 {
	return _mm_setr_ps( v[0], v[1], v[2], 0.5f );
}

[[nodiscard]]
auto toXmm( float x, float y, float z ) -> __m128 {
	return _mm_setr_ps( x, y, z, 0.5f );
}

void BoundsBuilderTest::test_generic() {
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints1 ) {
			builder.addPoint( p );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeTo( actualMins, actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( actualMins, actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) + QVector3D( 1, 1, 1 ) );
		}
	}
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints2 ) {
			builder.addPoint( p );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeTo( actualMins, actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( actualMins, actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) + QVector3D( 1, 1, 1 ) );
		}
	}
}

#define WSW_USE_SSE2
#define WSW_USE_SSE41

void BoundsBuilderTest::test_sse2() {
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints1 ) {
			builder.addPoint( p );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeTo( actualMins, actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( actualMins, actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) + QVector3D( 1, 1, 1 ) );
		}
	}
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints2 ) {
			builder.addPoint( p );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeTo( actualMins, actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( actualMins, actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) + QVector3D( 1, 1, 1 ) );
		}
	}
}

void BoundsBuilderTest::test_sse2Specific() {
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints1 ) {
			builder.addPoint( toXmm( p ) );
		}
		{
			__m128 actualMins, actualMaxs;
			builder.storeTo( &actualMins, &actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) );
		}
		{
			__m128 actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( &actualMins, &actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) + QVector3D( 1, 1, 1 ) );
		}
	}
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints2 ) {
			builder.addPoint( p );
		}
		{
			__m128 actualMins, actualMaxs;
			builder.storeTo( &actualMins, &actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) );
		}
		{
			__m128 actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( &actualMins, &actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) + QVector3D( 1, 1, 1 ) );
		}
	}
}

void BoundsBuilderTest::test_addingByMask() {
	{
		BoundsBuilder builder1, builder2;
		static_assert( std::size( testedPoints1 ) == std::size( testedPoints2 ) );
		static_assert( std::size( testedPoints1 ) >= 2 );

		for( unsigned i = 0; i < std::size( testedPoints1 ); ++i ) {
			__m128 mask;
			if( i % 2 ) {
				mask = _mm_castsi128_ps( _mm_set1_epi32( ~0 ) );
			} else {
				mask = _mm_setzero_ps();
				builder1.addPoint( testedPoints1[i] );
				builder1.addPoint( testedPoints2[i] );
			}
			builder2.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[i] ), toXmm( testedPoints2[i ]) );
		}

		vec3_t naiveMins, naiveMaxs;
		builder1.storeTo( naiveMins, naiveMaxs );
		vec3_t branchlessMins, branchlessMaxs;
		builder2.storeTo( branchlessMins, branchlessMaxs );
		QCOMPARE( toQV( naiveMins ), toQV( branchlessMins ) );
		QCOMPARE( toQV( naiveMaxs ), toQV( branchlessMaxs ) );
	}

	{
		BoundsBuilder builder1;
		static_assert( std::size( testedPoints1 ) >= 4 );
		static_assert( std::size( testedPoints2 ) >= 4 );

		// These statements should have no effects on resulting bounds

		// 1 of 4 dwords is set

		__m128 mask = _mm_castsi128_ps( _mm_setr_epi32( ~0, 0, 0, 0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[0] ), toXmm( testedPoints2[0] ) );

		mask = _mm_castsi128_ps( _mm_setr_epi32( 0, ~0, 0, 0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[1] ), toXmm( testedPoints2[1] ) );

		mask = _mm_castsi128_ps( _mm_setr_epi32( 0, 0, ~0, 0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[2] ), toXmm( testedPoints2[2] ) );

		mask = _mm_castsi128_ps( _mm_setr_epi32( 0, 0, 0, ~0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[3] ), toXmm( testedPoints2[3] ) );

		// 2 of 4 dwords are set

		mask = _mm_castsi128_ps( _mm_setr_epi32( ~0, ~0, 0, 0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[0] ), toXmm( testedPoints2[0] ) );

		mask = _mm_castsi128_ps( _mm_setr_epi32( 0, ~0, ~0, 0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[1] ), toXmm( testedPoints2[1] ) );

		mask = _mm_castsi128_ps( _mm_setr_epi32( 0, 0, ~0, ~0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[2] ), toXmm( testedPoints2[2] ) );

		mask = _mm_castsi128_ps( _mm_setr_epi32( ~0, ~0, 0, 0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[3] ), toXmm( testedPoints2[3] ) );

		// TODO: Test all combinations (only adjacent ~0~0 were tested)

		// 3 of 4 dwords are set

		mask = _mm_castsi128_ps( _mm_setr_epi32( 0, ~0, ~0, ~0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[1] ), toXmm( testedPoints2[0] ) );

		mask = _mm_castsi128_ps( _mm_setr_epi32( ~0, 0, ~0, ~0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[1] ), toXmm( testedPoints2[1] ) );

		mask = _mm_castsi128_ps( _mm_setr_epi32( ~0, ~0, 0, ~0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[2] ), toXmm( testedPoints2[2] ) );

		mask = _mm_castsi128_ps( _mm_setr_epi32( ~0, ~0, ~0, 0 ) );
		builder1.addPointsIfNoCmpMaskDWordSet( mask, toXmm( testedPoints1[3] ), toXmm( testedPoints2[3] ) );

		// We've already tested all-bits-set in the first subcase

		BoundsBuilder builder2;
		for( const float *p: testedPoints1 ) {
			builder2.addPoint( p );
		}
		for( const float *p: testedPoints2 ) {
			builder2.addPoint( p );
		}

		float regularMins[3], regularMaxs[3];
		builder2.storeTo( regularMins, regularMaxs );

		// Ensure that these bounds are out of previously supplied point's bounds
		float outOfBoundsMins[3], outOfBoundsMaxs[3];
		for( int i = 0; i < 3; ++i ) {
			outOfBoundsMins[i] = regularMaxs[i] + 5.0f;
			outOfBoundsMaxs[i] = regularMaxs[i] + 6.0f;
		}

		// This should pass
		builder1.addPointsIfNoCmpMaskDWordSet( _mm_setzero_ps(), toXmm( outOfBoundsMins ), toXmm( outOfBoundsMaxs ) );

		float resultMins[3], resultMaxs[3];
		builder1.storeTo( resultMins, resultMaxs );

		QCOMPARE( toQV( resultMins ), toQV( outOfBoundsMins ) );
		QCOMPARE( toQV( resultMaxs ), toQV( outOfBoundsMaxs ) );
	}
}