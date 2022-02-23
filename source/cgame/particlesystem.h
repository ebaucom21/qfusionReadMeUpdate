#ifndef WSW_d66e64a4_3cf7_44a9_a3f3_cc95b2beaa6a_H
#define WSW_d66e64a4_3cf7_44a9_a3f3_cc95b2beaa6a_H

struct CMShapeList;

template <typename> class SingletonHolder;

#include "../qcommon/freelistallocator.h"
#include "../qcommon/randomgenerator.h"
#include "../ref/ref.h"

#include <span>

// Mutability of fields makes adjusting parameters in a loop more convenient
struct UniformFlockFiller {
	float origin[3] { 1.0f / 0.0f, 1.0f / 0.0f, 1.0f / 0.0f };
	float offset[3] { 0.0f, 0.0f, 0.0f };
	float gravity { 600 };
	unsigned bounceCount { 3 };
	float minSpeed { 300 };
	float maxSpeed { 300 };
	float minPercentage { 0.0f };
	float maxPercentage { 1.0f };
	unsigned minTimeout { 300u };
	unsigned maxTimeout { 700u };

	[[nodiscard]]
	auto fill( Particle *__restrict, unsigned maxParticles,
			   wsw::RandomGenerator *__restrict, int64_t currTime ) __restrict -> std::pair<int64_t, unsigned>;
};

// Mutability of fields makes adjusting parameters in a loop more convenient
struct ConeFlockFiller {
	float origin[3];
	float offset[3] { 0.0f, 0.0f, 0.0f };
	float dir[3] { 0.0f, 0.0f, 1.0f };
	float gravity { 600 };
	float angle { 45 };
	unsigned bounceCount { 3 };
	float minSpeed { 300 };
	float maxSpeed { 300 };
	float minPercentage { 0.0f };
	float maxPercentage { 1.0f };
	unsigned minTimeout { 300u };
	unsigned maxTimeout { 700u };

	[[nodiscard]]
	auto fill( Particle *__restrict, unsigned maxParticles,
			   wsw::RandomGenerator *__restrict, int64_t currTime ) __restrict -> std::pair<int64_t, unsigned>;
};

struct alignas( 16 ) ParticleFlock {
	Particle::AppearanceRules appearanceRules;
	Particle *particles;
	int64_t timeoutAt;
	unsigned numParticlesLeft;
	unsigned binIndex;
	CMShapeList *shapeList;
	// TODO: Make links work with "m_"
	ParticleFlock *prev { nullptr }, *next { nullptr };
	float mins[4];
	float maxs[4];

	void simulate( int64_t currTime, float deltaSeconds );
	void simulateWithoutClipping( int64_t currTime, float deltaSeconds );
};

class ParticleSystem {
public:
	static constexpr unsigned kMaxClippedTrailFlockSize = 64;
	static constexpr unsigned kMaxNonClippedTrailFlockSize = 64;

	static constexpr unsigned kClippedTrailFlocksBin = 3;
	static constexpr unsigned kNonClippedTrailFlocksBin = 4;
private:
	template<typename> friend class SingletonHolder;

	// TODO: Just align manually in the combined memory chunk
	static_assert( sizeof( ParticleFlock ) % 16 == 0 );

	struct FlocksBin {
		ParticleFlock *head { nullptr };
		wsw::HeapBasedFreelistAllocator allocator;
		const unsigned maxParticlesPerFlock;
		bool needsShapeLists { true };

		FlocksBin( unsigned maxParticlesPerFlock, unsigned maxFlocks )
			: allocator( sizeof( ParticleFlock ) + sizeof( Particle ) * maxParticlesPerFlock, maxFlocks )
			, maxParticlesPerFlock( maxParticlesPerFlock ) {}
	};

	static constexpr unsigned kMaxSmallFlocks  = 128;
	static constexpr unsigned kMaxMediumFlocks = 64;
	static constexpr unsigned kMaxLargeFlocks  = 20;

	static constexpr unsigned kMaxClippedTrailFlocks    = 32;
	static constexpr unsigned kMaxNonClippedTrailFlocks = 16;

	static constexpr unsigned kMaxSmallFlockSize  = 8;
	static constexpr unsigned kMaxMediumFlockSize = 48;
	static constexpr unsigned kMaxLargeFlockSize  = 144;

	static constexpr unsigned kMaxNumberOfClippedFlocks = kMaxClippedTrailFlocks +
		kMaxSmallFlocks + kMaxMediumFlocks + kMaxLargeFlocks;

	wsw::StaticVector<CMShapeList *, kMaxNumberOfClippedFlocks> m_freeShapeLists;

	wsw::StaticVector<FlocksBin, 5> m_bins;
	int64_t m_lastTime { 0 };

	wsw::RandomGenerator m_rng;

	void unlinkAndFree( ParticleFlock *flock );

	[[nodiscard]]
	auto createFlock( unsigned binIndex, int64_t currTime ) -> ParticleFlock *;

	[[nodiscard]]
	static auto cgTimeFixme() -> int64_t;
public:
	ParticleSystem();
	~ParticleSystem();

	template <typename Filler>
	void addSmallParticleFlock( const Particle::AppearanceRules &appearanceRules, Filler &&filler ) {
		const int64_t currTime = cgTimeFixme();
		ParticleFlock *flock   = createFlock( 0, currTime );
		flock->appearanceRules = appearanceRules;
		const auto [timeoutAt, numParticles] = filler.fill( flock->particles, kMaxSmallFlockSize, &m_rng, currTime );
		flock->timeoutAt = timeoutAt;
		flock->numParticlesLeft = numParticles;
	}

	template <typename Filler>
	void addMediumParticleFlock( const Particle::AppearanceRules &appearanceRules, Filler &&filler ) {
		const int64_t currTime = cgTimeFixme();
		ParticleFlock *flock   = createFlock( 1, currTime );
		const auto [timeoutAt, numParticles] = filler.fill( flock->particles, kMaxMediumFlockSize, &m_rng, currTime );
		flock->timeoutAt        = timeoutAt;
		flock->numParticlesLeft = numParticles;
		flock->appearanceRules  = appearanceRules;
	}

	template <typename Filler>
	void addLargeParticleFlock( const Particle::AppearanceRules &appearanceRules, Filler &&filler ) {
		const int64_t currTime = cgTimeFixme();
		ParticleFlock *flock   = createFlock( 2, currTime );
		const auto [timeoutAt, numParticles] = filler.fill( flock->particles, kMaxLargeFlockSize, &m_rng, currTime );
		flock->timeoutAt        = timeoutAt;
		flock->numParticlesLeft = numParticles;
		flock->appearanceRules  = appearanceRules;
	}

	[[nodiscard]]
	auto createTrailFlock( const Particle::AppearanceRules &appearanceRules, unsigned binIndex ) -> ParticleFlock *;

	void destroyTrailFlock( ParticleFlock *flock ) { unlinkAndFree( flock ); }

	void runFrame( int64_t currTime, DrawSceneRequest *drawSceneRequest );
};

#endif