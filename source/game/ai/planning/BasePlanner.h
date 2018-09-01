#ifndef QFUSION_AI_BASE_BRAIN_H
#define QFUSION_AI_BASE_BRAIN_H

#include "../ai_local.h"
#include "GoalEntities.h"
#include "../ai_frame_aware_updatable.h"
#include "../static_vector.h"
#include "../navigation/AasRouteCache.h"
#include "../ai_base_ai.h"
#include "WorldState.h"

class AiBaseGoal
{
	friend class Ai;
	friend class BasePlanner;

	static inline void Register( Ai *ai, AiBaseGoal *goal );

protected:
	edict_t *const self;
	const char *name;
	const unsigned updatePeriod;
	int debugColor { 0 };
	float weight { 0.0f };

public:
	// Don't pass self as a constructor argument (self->ai ptr might not been set yet)
	inline AiBaseGoal( Ai *ai, const char *name_, unsigned updatePeriod_ )
		: self( ai->self ), name( name_ ), updatePeriod( updatePeriod_ ) {
		Register( ai, this );
	}

	virtual ~AiBaseGoal() = default;

	virtual void UpdateWeight( const WorldState &worldState ) = 0;
	virtual void GetDesiredWorldState( WorldState *worldState ) = 0;
	virtual struct PlannerNode *GetWorldStateTransitions( const WorldState &worldState ) = 0;

	virtual void OnPlanBuildingStarted() {}
	virtual void OnPlanBuildingCompleted( const class AiBaseActionRecord *planHead ) {}

	inline bool IsRelevant() const { return weight > 0; }

	// More important goals are first after sorting goals array
	inline bool operator<( const AiBaseGoal &that ) const {
		return this->weight > that.weight;
	}

	inline int DebugColor() const { return debugColor; }

	inline const char *Name() const { return name; }
	inline unsigned UpdatePeriod() const { return updatePeriod; }
};

class alignas ( sizeof( void * ) )PoolBase {
	friend class PoolItem;

	char *basePtr;
	const char *tag;
	const uint16_t linksOffset;
	const uint16_t alignedChunkSize;

	static constexpr auto FREE_LIST = 0;
	static constexpr auto USED_LIST = 1;

	// The head of the free list initially points at the first element.
	// The head of the used list is not initially defined (is "null").
	int16_t listFirst[2] { 0, -1 };

#ifdef _DEBUG
	inline const char *ListName( int index ) {
		switch( index ) {
			case FREE_LIST: return "FREE";
			case USED_LIST: return "USED";
			default: AI_FailWith( "PoolBase::ListName()", "Illegal index %d\n", index );
		}
	}
#endif

	static inline uint16_t LinksOffset( uint16_t itemSize ) {
		uint16_t remainder = itemSize % alignof( uint16_t );
		if( !remainder ) {
			return itemSize;
		}
		return itemSize + alignof( uint16_t ) - remainder;
	}

	static inline uint16_t AlignedChunkSize( uint16_t itemSize ) {
		uint16_t totalSize = LinksOffset( itemSize ) + sizeof( ItemLinks );
		uint16_t remainder = totalSize % alignof( void * );
		if( !remainder ) {
			return totalSize;
		}
		return totalSize + alignof( void * ) - remainder;
	}

	inline class PoolItem &ItemAt( int16_t index ) {
		char *mem = ( basePtr + alignedChunkSize * index );
		assert( !( (uintptr_t)mem % sizeof( void * ) ) );
		return *(PoolItem *)mem;
	}
	inline int16_t IndexOf( const class PoolItem *item ) const {
		return (int16_t)( ( (const char *)item - basePtr ) / alignedChunkSize );
	}

	inline void Link( int16_t itemIndex, int16_t listIndex );
	inline void Unlink( int16_t itemIndex, int16_t listIndex );

protected:
	void *Alloc();
	void Free( class PoolItem *poolItem );

#ifndef _MSC_VER
	inline void Debug( const char *format, ... ) const __attribute__( ( format( printf, 2, 3 ) ) )
#else
	inline void Debug( _Printf_format_string_ const char *format, ... ) const
#endif
	{
		va_list va;
		va_start( va, format );
		AI_Debugv( tag, format, va );
		va_end( va );
	}

	// These links follow an item in-memory, not precede it.
	// This is to avoid wasting bytes on aligning an item after these links.
	// (An item is required to be 8-byte aligned on 64-bit systems
	// while the links alignment is less restrictive).
	struct alignas( 2 )ItemLinks {
		int16_t links[2];
		int16_t &Prev() { return links[0]; }
		int16_t &Next() { return links[1]; }
	};

	inline ItemLinks &ItemLinksAt( int16_t index ) {
		char *mem = ( basePtr + alignedChunkSize * index + linksOffset );
		assert( !( (uintptr_t)mem % alignof( ItemLinks ) ) );
		return *(ItemLinks *)mem;
	}
public:
	PoolBase( char *basePtr_, const char *tag_, uint16_t itemSize, uint16_t itemsCount );

	void Clear();
};

class alignas ( sizeof( void * ) )PoolItem {
	friend class PoolBase;
	PoolBase *pool;
public:
	explicit PoolItem( PoolBase * pool_ ) : pool( pool_ ) {}
	virtual ~PoolItem() = default;

	inline void DeleteSelf() {
		this->~PoolItem();
		pool->Free( this );
	}
};

template<class Item, unsigned N>
class alignas ( sizeof( void * ) )Pool : public PoolBase {
	// We have to introduce these intermediates instead of variables since we are limited to C++11 (not 14) standard.

	static constexpr unsigned OffsetRemainder() {
		return sizeof( Item ) % alignof( ItemLinks );
	}

	static constexpr unsigned ItemLinksOffset() {
		return OffsetRemainder() ? sizeof( Item ) + alignof( ItemLinks ) - OffsetRemainder() : sizeof( Item );
	}

	static constexpr unsigned TotalSize() {
		return ItemLinksOffset() + sizeof( ItemLinks );
	}

	static constexpr unsigned ChunkSizeRemainder() {
		return TotalSize() % sizeof( void * );
	}

	static constexpr unsigned ChunkSize() {
		return ChunkSizeRemainder() ? TotalSize() + sizeof( void * ) - ChunkSizeRemainder() : TotalSize();
	}

	alignas( alignof( void * ) ) char buffer[N * ChunkSize()];

public:
	Pool( const char *tag_ ) : PoolBase( buffer, tag_, sizeof( Item ), (uint16_t)N ) {
		static_assert( N <= std::numeric_limits<int16_t>::max(), "Links can't handle more than 2^15 elements in pool" );
	}

	inline Item *New() {
		if( void *mem = Alloc() ) {
			return new(mem) Item( this );
		}
		return nullptr;
	}

	template <typename Arg1>
	inline Item *New( Arg1 arg1 ) {
		if( void *mem = Alloc() ) {
			return new(mem) Item( this, arg1 );
		}
		return nullptr;
	}

	template <typename Arg1, typename Arg2>
	inline Item *New( Arg1 arg1, Arg2 arg2 ) {
		if( void *mem = Alloc() ) {
			return new(mem) Item( this, arg1, arg2 );
		}
		return nullptr;
	};

	template <typename Arg1, typename Arg2, typename Arg3>
	inline Item *New( Arg1 arg1, Arg2 arg2, Arg3 arg3 ) {
		if( void *mem = Alloc() ) {
			return new(mem) Item( this, arg1, arg2, arg3 );
		}
		return nullptr;
	};

	template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
	inline Item *New( Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4 ) {
		if( void *mem = Alloc() ) {
			return new(mem) Item( this, arg1, arg2, arg3, arg4 );
		}
		return nullptr;
	};

	template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
	inline Item *New( Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5 ) {
		if( void *mem = Alloc() ) {
			return new(mem) Item( this, arg1, arg2, arg3, arg4, arg5 );
		}
		return nullptr;
	};
};

class AiBaseActionRecord : public PoolItem {
	friend class AiBaseAction;

protected:
	edict_t *const self;
	const char *name;

#ifndef _MSC_VER
	inline void Debug( const char *format, ... ) const __attribute__( ( format( printf, 2, 3 ) ) )
#else
	inline void Debug( _Printf_format_string_ const char *format, ... ) const
#endif
	{
		va_list va;
		va_start( va, format );
		AI_Debugv( name, format, va );
		va_end( va );
	}

public:
	AiBaseActionRecord *nextInPlan { nullptr };

	inline AiBaseActionRecord( PoolBase *pool_, edict_t *self_, const char *name_ )
		: PoolItem( pool_ ), self( self_ ), name( name_ ) {}

	virtual void Activate() {
		Debug( "About to activate\n" );
	};
	virtual void Deactivate() {
		Debug( "About to deactivate\n" );
	};

	const char *Name() const { return name; }

	enum Status {
		INVALID,
		VALID,
		COMPLETED
	};

	virtual Status CheckStatus( const WorldState &currWorldState ) const = 0;
};

struct PlannerNode : PoolItem {
	// World state after applying an action
	WorldState worldState;
	// An action record to apply
	AiBaseActionRecord *actionRecord { nullptr };
	// Used to reconstruct a plan
	PlannerNode *parent { nullptr };
	// Next in linked list of transitions for current node
	PlannerNode *nextTransition { nullptr };

	// Utilities for storing the node in a hash set
	PlannerNode *prevInHashBin { nullptr };
	PlannerNode *nextInHashBin { nullptr };

	// An A-star edge "distance"
	float transitionCost { std::numeric_limits<float>::max() };
	// An A-star node "G"
	float costSoFar { std::numeric_limits<float>::max() };
	// A priority queue parameter
	float heapCost { std::numeric_limits<float>::max() };
	// An utility for retrieval an actual index in heap array by a node value
	unsigned heapArrayIndex { std::numeric_limits<unsigned>::max() };

	// A hash of the associated world state (put here for optimal members alignment)
	uint32_t worldStateHash { 0 };

	PlannerNode( PoolBase *pool, edict_t *self )
		: PoolItem( pool ),	worldState( self ) {}

	~PlannerNode() override {
		if( actionRecord ) {
			actionRecord->DeleteSelf();
		}

#ifndef PUBLIC_BUILD
		// Prevent use-after-free.
		actionRecord = nullptr;
		parent = nullptr;
		nextTransition = nullptr;
		prevInHashBin = nullptr;
		nextInHashBin = nullptr;
#endif
	}
};

class AiBaseAction {
	friend class Ai;
	friend class BasePlanner;

	static inline void Register( Ai *ai, AiBaseAction *action );

protected:
	edict_t *self;
	const char *name;

#ifndef _MSC_VER
	inline void Debug( const char *format, ... ) const __attribute__( ( format( printf, 2, 3 ) ) )
#else
	inline void Debug( _Printf_format_string_ const char *format, ... ) const
#endif
	{
		va_list va;
		va_start( va, format );
		AI_Debugv( name, format, va );
		va_end( va );
	}

	class PlannerNodePtr {
		PlannerNode *node;
	public:
		PlannerNodePtr( const PlannerNodePtr &that ) = delete;
		PlannerNodePtr &operator=( const PlannerNodePtr &that ) = delete;

		explicit PlannerNodePtr( PlannerNode *node_ ) : node( node_ ) {}

		PlannerNodePtr( PlannerNodePtr &&that ) : node( that.node ) {
			that.node = nullptr;
		}

		PlannerNodePtr &operator=( PlannerNodePtr &&that ) {
			node = that.node;
			that.node = nullptr;
			return *this;
		}

		PlannerNode *ReleaseOwnership() {
			PlannerNode *result = node;
			// Clear node reference to avoid being deleted in the destructor
			node = nullptr;
			return result;
		}

		inline ~PlannerNodePtr();
		inline PlannerNode *PrepareActionResult();
		inline class WorldState &WorldState();
		inline float &Cost();
		operator bool() const { return node != nullptr; }
	};

	inline PlannerNodePtr NewNodeForRecord( AiBaseActionRecord *record );

public:
	// Don't pass self as a constructor argument (self->ai ptr might not been set yet)
	inline AiBaseAction( Ai *ai, const char *name_ )
		: self( ai->self ), name( name_ ) {
		Register( ai, this );
	}

	virtual ~AiBaseAction() = default;

	const char *Name() const { return name; }

	virtual PlannerNode *TryApply( const WorldState &worldState ) = 0;
};

class BasePlanner : public AiFrameAwareUpdatable {
	friend class Ai;
	friend class AiManager;
	friend class AiBaseTeam;
	friend class AiBaseGoal;
	friend class AiBaseAction;
	friend class AiBaseActionRecord;

public:
	static constexpr unsigned MAX_GOALS = 12;
	static constexpr unsigned MAX_ACTIONS = 36;

protected:
	edict_t *const self;

	AiBaseActionRecord *planHead { nullptr };
	AiBaseGoal *activeGoal { nullptr };
	int64_t nextActiveGoalUpdateAt { 0 };

	StaticVector<AiBaseGoal *, MAX_GOALS> goals;
	StaticVector<AiBaseAction *, MAX_ACTIONS> actions;

	static constexpr unsigned MAX_PLANNER_NODES = 384;
	Pool<PlannerNode, MAX_PLANNER_NODES> plannerNodesPool { "PlannerNodesPool" };

	explicit BasePlanner( edict_t *self_ ): self( self_ ) {}

	virtual void PrepareCurrWorldState( WorldState *worldState ) = 0;

	virtual bool ShouldSkipPlanning() const = 0;

	bool UpdateGoalAndPlan( const WorldState &currWorldState );

	bool FindNewGoalAndPlan( const WorldState &currWorldState );

	// Allowed to be overridden in a subclass for class-specific optimization purposes
	virtual AiBaseActionRecord *BuildPlan( AiBaseGoal *goal, const WorldState &startWorldState );

	AiBaseActionRecord *ReconstructPlan( PlannerNode *lastNode ) const;

	void SetGoalAndPlan( AiBaseGoal *goal_, AiBaseActionRecord *planHead_ );

	void Think() override;

	virtual void BeforePlanning() {}
	virtual void AfterPlanning() {}
public:
	inline bool HasPlan() const { return planHead != nullptr; }

	void ClearGoalAndPlan();

	void DeletePlan( AiBaseActionRecord *head );
};

inline void AiBaseGoal::Register( Ai *ai, AiBaseGoal *goal ) {
	assert( ai );
	assert( ai->basePlanner );
	ai->basePlanner->goals.push_back( goal );
}

inline void AiBaseAction::Register( Ai *ai, AiBaseAction *action ) {
	assert( ai );
	assert( ai->basePlanner );
	ai->basePlanner->actions.push_back( action );
}

inline AiBaseAction::PlannerNodePtr::~PlannerNodePtr() {
	if( this->node ) {
		this->node->DeleteSelf();
	}
}

inline PlannerNode *AiBaseAction::PlannerNodePtr::PrepareActionResult() {
	PlannerNode *result = this->node;
	this->node = nullptr;

#ifndef PUBLIC_BUILD
	if( !result->worldState.IsCopiedFromOtherWorldState() ) {
		AI_FailWith( "PlannerNodePtr::PrepareActionResult()", "World state has not been copied from parent one" );
	}
#endif

	// Compute modified world state hash
	// This computation have been put here to avoid error-prone copy-pasting.
	// Another approach is to use lazy hash code computation but it adds branching on each hash code access
	result->worldStateHash = result->worldState.Hash();
	return result;
}

inline WorldState &AiBaseAction::PlannerNodePtr::WorldState() {
	return node->worldState;
}

inline float &AiBaseAction::PlannerNodePtr::Cost() {
	return node->transitionCost;
}

inline AiBaseAction::PlannerNodePtr AiBaseAction::NewNodeForRecord( AiBaseActionRecord *record ) {
	if( !record ) {
		Debug( "Can't allocate an action record\n" );
		return PlannerNodePtr( nullptr );
	}

	PlannerNode *node = self->ai->aiRef->basePlanner->plannerNodesPool.New( self );
	if( !node ) {
		Debug( "Can't allocate a planner node\n" );
		record->DeleteSelf();
		return PlannerNodePtr( nullptr );
	}

	node->actionRecord = record;
	return PlannerNodePtr( node );
}

#endif
