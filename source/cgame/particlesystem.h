#ifndef WSW_d66e64a4_3cf7_44a9_a3f3_cc95b2beaa6a_H
#define WSW_d66e64a4_3cf7_44a9_a3f3_cc95b2beaa6a_H

struct CMShapeList;

template <typename> class SingletonHolder;

#include "../qcommon/freelistallocator.h"
#include "../qcommon/randomgenerator.h"
#include "../ref/ref.h"

#include <span>

struct alignas( 16 ) BaseParticle {
	float origin[4];
	float oldOrigin[4];
	float velocity[4];
	float accel[4];
	int64_t timeoutAt;
	unsigned bouncesLeft;
};

struct UniformFlockFiller {
	const float *origin;
	const float *offset = vec3_origin;
	const float gravity { 600 };
	const unsigned bounceCount { 3 };
	const float minSpeed { 300 };
	const float maxSpeed { 300 };
	const float minPercentage { 0.0f };
	const float maxPercentage { 1.0f };
	const unsigned minTimeout { 300u };
	const unsigned maxTimeout { 700u };

	[[nodiscard]]
	auto fill( BaseParticle *__restrict, unsigned maxParticles,
			   wsw::RandomGenerator *__restrict, int64_t currTime ) __restrict -> std::pair<int64_t, unsigned>;
};

struct ConeFlockFiller {
	const float *origin;
	const float *offset = vec3_origin;
	const float *dir = &axis_identity[AXIS_UP];
	const float gravity { 600 };
	const float angle { 75 };
	const unsigned bounceCount { 3 };
	const float minSpeed { 300 };
	const float maxSpeed { 300 };
	const float minPercentage { 0.0f };
	const float maxPercentage { 1.0f };
	const unsigned minTimeout { 300u };
	const unsigned maxTimeout { 700u };

	[[nodiscard]]
	auto fill( BaseParticle *__restrict, unsigned maxParticles,
			   wsw::RandomGenerator *__restrict, int64_t currTime ) __restrict -> std::pair<int64_t, unsigned>;
};

class ParticleSystem {
	template<typename> friend class SingletonHolder;

	struct alignas( 16 ) Flock {
		BaseParticle *particles;
		int64_t timeoutAt;
		unsigned numParticlesLeft;
		unsigned binIndex;
		CMShapeList *shapeList;
		// TODO: Make links work with "m_"
		Flock *prev { nullptr }, *next { nullptr };
		float color[4];

		void simulate( int64_t currTime, float deltaSeconds );
	};

	// TODO: Just align manually in the combined memory chunk
	static_assert( sizeof( Flock ) % 16 == 0 );

	struct FlocksBin {
		Flock *head { nullptr };
		wsw::HeapBasedFreelistAllocator allocator;
		const unsigned maxParticlesPerFlock;

		FlocksBin( unsigned maxParticlesPerFlock, unsigned maxFlocks )
			: allocator( sizeof( Flock ) + sizeof( BaseParticle ) * maxParticlesPerFlock, maxFlocks )
			, maxParticlesPerFlock( maxParticlesPerFlock ) {}
	};

	static constexpr unsigned kMaxSmallFlocks  = 128;
	static constexpr unsigned kMaxMediumFlocks = 64;
	static constexpr unsigned kMaxLargeFlocks  = 16;

	static constexpr unsigned kMaxSmallFlockSize  = 8;
	static constexpr unsigned kMaxMediumFlockSize = 48;
	static constexpr unsigned kMaxLargeFlockSize  = 128;

	wsw::StaticVector<CMShapeList *, kMaxSmallFlocks + kMaxMediumFlocks + kMaxLargeFlocks> m_freeShapeLists;

	wsw::StaticVector<FlocksBin, 3> m_bins;
	int64_t m_lastTime { 0 };

	wsw::RandomGenerator m_rng;

	void releaseFlock( Flock *flock );

	[[nodiscard]]
	auto createFlock( unsigned binIndex, int64_t currTime ) -> Flock *;

	[[nodiscard]]
	static auto cgTimeFixme() -> int64_t;
public:
	ParticleSystem();
	~ParticleSystem();

	template <typename Filler>
	void addSmallParticleFlock( const float *baseColor, Filler &&filler ) {
		const int64_t currTime = cgTimeFixme();
		Flock *flock = createFlock( 0, currTime );
		auto [timeoutAt, numParticles] = filler.fill( flock->particles, kMaxSmallFlockSize, &m_rng, currTime );
		Vector4Copy( baseColor, flock->color );
		flock->timeoutAt = timeoutAt;
		flock->numParticlesLeft = numParticles;
	}

	template <typename Filler>
	void addMediumParticleFlock( const float *baseColor, Filler &&filler ) {
		const int64_t currTime = cgTimeFixme();
		Flock *flock = createFlock( 1, currTime );
		auto [timeoutAt, numParticles] = filler.fill( flock->particles, kMaxMediumFlockSize, &m_rng, currTime );
		Vector4Copy( baseColor, flock->color );
		flock->timeoutAt = timeoutAt;
		flock->numParticlesLeft = numParticles;
	}

	template <typename Filler>
	void addLargeParticleFlock( const float *baseColor, Filler &&filler ) {
		const int64_t currTime = cgTimeFixme();
		Flock *flock = createFlock( 2, currTime );
		auto [timeoutAt, numParticles] = filler.fill( flock->particles, kMaxLargeFlockSize, &m_rng, currTime );
		Vector4Copy( baseColor, flock->color );
		flock->timeoutAt = timeoutAt;
		flock->numParticlesLeft = numParticles;
	}

	void runFrame( int64_t currTime, DrawSceneRequest *drawSceneRequest );
};

#endif