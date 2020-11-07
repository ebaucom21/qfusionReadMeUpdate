#ifndef QFUSION_SND_PROPAGATION_H
#define QFUSION_SND_PROPAGATION_H

#include "snd_local.h"
#include "snd_cached_computation.h"

#include <limits>

class GraphLike {
	friend class CachedLeafsGraph;
	friend class CachedGraphReader;
	friend class CachedGraphWriter;
protected:
	/**
	 * Should be released using TaggedAllocator::FreeUsingMetadata()
	 * (descendants can use custom allocators that are put in metadata).
	 */
	float *distanceTable { nullptr };
	/**
	 * Should be released using TaggedAllocator::FreeUsingMetadata()
	 * (descendants can use custom allocators that are put in metadata).
	 */
	int *adjacencyListsData { nullptr };
	/**
	 * Assumed to be allocated within {@code adjacencyListsData} at its end.
	 */
	int *adjacencyListsOffsets { nullptr };

	int numLeafs;
	explicit GraphLike( int numLeafs_ ): numLeafs( numLeafs_ ) {}
public:
	/**
	 * @note is put in the corresponding source to avoid exposing {@code TaggedAllocator}.
	 * Gets called in only in the source anyway.
	 */
	virtual ~GraphLike();

	int NumLeafs() const { return numLeafs; }

	float EdgeDistance( int leaf1, int leaf2 ) const {
		assert( distanceTable );
		assert( leaf1 > 0 && leaf1 < numLeafs );
		assert( leaf2 > 0 && leaf2 < numLeafs );
		return distanceTable[leaf1 * numLeafs + leaf2];
	}

	const int *AdjacencyList( int leafNum ) const {
		assert( adjacencyListsData && adjacencyListsOffsets );
		assert( leafNum > 0 && leafNum < numLeafs );
		return adjacencyListsData + adjacencyListsOffsets[leafNum];
	}
};

class CachedLeafsGraph: public CachedComputation, public GraphLike {
	friend class PropagationTable;
	friend class CachedGraphReader;
	friend class CachedGraphWriter;
	template <typename> friend class SingletonHolder;
	friend class PropagationGraphBuilder;

	friend float *ReuseGlobalDistanceTable( int numLeafs );

	/**
	 * This is a temporary data useful for {@code PropagationGraphBuilder<?,?>}
	 * While it currently serves no purpose for the {@code CachedLeafsGraph} itself,
	 * having/saving it is mandatory to be able to use {@code GraphBuilder::TryUsingGlobalGraph()}.
	 * Should be released by calling {@code TaggedAllocator::FreeUsingMetadata()}
	 * (an ownership over this chunk of memory can be transferred via builder).
	 * @note an actual allocator of this memory chunk must use reference counting as we must support sharing.
	 */
	uint8_t *dirsTable { nullptr };

	int leafListsDataSize { -1 };

	void ResetExistingState() override;
	bool TryReadFromFile( int fsFlags ) override;
	bool ComputeNewState( bool fastAndCoarse ) override;
	void ProvideDummyData() override;
	bool SaveToCache() override;

	CachedLeafsGraph()
		: CachedComputation( "CachedLeafsGraph", ".graph", "CachedLeafsGraph@v1337" )
		, GraphLike( -1 ) {}

	~CachedLeafsGraph() override;
public:
	/**
	 * Exposed for {@code GraphBuilder<?,?>::TryUsingGlobalGraph()} (a template can't be a friend).
	 * @note the size is specified in integer elements and not in bytes.
	 */
	int LeafListsDataSize() const { return leafListsDataSize; }
	/**
	 * Exposed for {@code GraphBuilder<?,?>::TryUsingGlobalGraph()} (a template can't be a friend).
	 */
	const float *DistanceTable() const { return distanceTable; }
	/**
	 * A helper that resolves ambiguous calls of {@code NumLeafs()} of both base classes.
	 */
	int NumLeafs() const { return ( (GraphLike *)this)->NumLeafs(); }

	static CachedLeafsGraph *Instance();
	static void Init();
	static void Shutdown();
};

class PropagationTable: public CachedComputation {
	friend class PropagationIOHelper;
	friend class PropagationTableReader;
	friend class PropagationTableWriter;
	friend class PropagationTableBuilder;
	friend class PropagationBuilderTask;
	friend class CoarsePropagationTask;
	friend class FinePropagationTask;
	friend class CachedLeafsGraph;
	template <typename> friend class SingletonHolder;

	struct alignas( 1 )PropagationProps {
		/**
		 * An index for {@code ByteToDir()} if is within {@code [0, MAXVERTEXNORMALS)} range.
		 */
		uint8_t maybeDirByte;
		/**
		 * An rough exponential encoding of an indirect path
		 */
		uint8_t distanceByte;

		bool HasDirectPath() const {
			return maybeDirByte == std::numeric_limits<uint8_t>::max();
		}

		bool HasIndirectPath() const {
			return maybeDirByte < std::numeric_limits<uint8_t>::max() - 1;
		}

		void SetHasDirectPath() {
			maybeDirByte = std::numeric_limits<uint8_t>::max();
		}

		void MarkAsFailed() {
			maybeDirByte = std::numeric_limits<uint8_t>::max() - 1;
		}

		inline void SetIndirectPath( const vec3_t dir, float distance );

		/**
		 * Implemented in the source as some things related to implementation should not be exposed right now
		 */
		inline void SetDir( const vec3_t dir );

		void GetDir( vec3_t dir ) const {
			assert( HasIndirectPath() );
			ByteToDir( maybeDirByte, dir );
		}

		void SetDistance( float distance ) {
			assert( distance > 0 );
			auto u = (unsigned)distance;
			// Limit the stored distance to 2^16 - 1
			clamp_high( u, ( 1u << 16u ) - 1 );
			// Store the distance using 256 units granularity
			u >>= 8;
			assert( u >= 0 && u < 256 );
			// Make sure that we do not lose the property of distance being positive.
			// Otherwise validation fails (while computations were perfect up to this).
			clamp_low( u, 1u );
			distanceByte = (uint8_t)u;
		}

		float GetDistance() const {
			return distanceByte * 256.0f;
		}
	};

	static_assert( alignof( PropagationProps ) == 1, "" );
	static_assert( sizeof( PropagationProps ) == 2, "" );

	PropagationProps *table { nullptr };

	const PropagationProps &GetProps( int fromLeafNum, int toLeafNum ) const {
		assert( table );
		const auto numLeafs = NumLeafs();
		assert( numLeafs );
		assert( fromLeafNum > 0 && fromLeafNum < numLeafs );
		assert( toLeafNum > 0 && toLeafNum < numLeafs );
		return table[numLeafs * fromLeafNum + toLeafNum];
	}

	void Clear() {
		FreeIfNeeded( &table );
	}

	void ResetExistingState() override {
		Clear();
	}

	bool TryReadFromFile( int fsFlags ) override;
	bool ComputeNewState( bool fastAndCoarse ) override;
	void ProvideDummyData() override;
	bool SaveToCache() override;
public:
	PropagationTable(): CachedComputation( "PropagationTable", ".table", "PropagationTable@v1337" ) {}

	~PropagationTable() override {
		Clear();
	}

	bool IsValid() const { return table != nullptr; }

	/**
	 * Returns true if a direct (ray-like) path between these leaves exists.
	 * @note true results of {@code HasDirectPath()} and {@code HasIndirectPath} are mutually exclusive.
	 */
	bool HasDirectPath( int fromLeafNum, int toLeafNum ) const {
		return fromLeafNum == toLeafNum || GetProps( fromLeafNum, toLeafNum ).HasDirectPath();
	}

	/**
	 * Returns true if an indirect (maze-like) path between these leaves exists.
	 * @note true results of {@code HasDirectPath()} and {@code HasIndirectPath} are mutually exclusive.
	 */
	bool HasIndirectPath( int fromLeafNum, int toLeafNum ) const {
		return fromLeafNum != toLeafNum && GetProps( fromLeafNum, toLeafNum ).HasIndirectPath();
	}

	/**
	 * Returns propagation properties of an indirect (maze) path between these leaves.
	 * @param fromLeafNum a number of leaf where a real sound emitter origin is assumed to be located.
	 * @param toLeafNum a number of leaf where a listener origin is assumed to be located.
	 * @param dir an average direction of sound waves emitted by the source and ingoing to the listener leaf.
	 * @param distance a coarse estimation of distance that is covered by sound waves during propagation.
	 * @return true if an indirect path between given leaves exists (and there were propagation properties).
	 */
	bool GetIndirectPathProps( int fromLeafNum, int toLeafNum, vec3_t dir, float *distance ) const {
		if( fromLeafNum == toLeafNum ) {
			return false;
		}
		const auto &props = GetProps( fromLeafNum, toLeafNum );
		if( !props.HasIndirectPath() ) {
			return false;
		}
		props.GetDir( dir );
		*distance = props.GetDistance();
		return true;
	}

	static PropagationTable *Instance();
	static void Init();
	static void Shutdown();
};

#endif
