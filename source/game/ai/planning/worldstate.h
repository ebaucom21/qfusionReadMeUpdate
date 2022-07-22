#ifndef WSW_b6521d59_fa45_4a78_bc5b_bd1261aec3a7_H
#define WSW_b6521d59_fa45_4a78_bc5b_bd1261aec3a7_H

#include "../ailocal.h"

#include <cstdint>
#include <optional>
#include <utility>

#include "../../../qcommon/wswexceptions.h"
#include "../vec3.h"

template <typename T, typename This>
class OrderedComparableVar {
	friend class WorldState;
public:
	static_assert( std::is_trivial_v<T> );

	enum SatisfyOp : uint8_t { EQ, NE, GT, GE, LS, LE };

	OrderedComparableVar() = delete;

	explicit OrderedComparableVar( const T &value, SatisfyOp op = EQ ) noexcept : m_value( value ), m_op( op ) {}

	[[nodiscard]]
	operator const T &() const noexcept { return m_value; }

	[[nodiscard]]
	bool isSatisfiedBy( const This &that ) const {
		switch( m_op ) {
			case SatisfyOp::EQ: return m_value == that.m_value;
			case SatisfyOp::NE: return m_value != that.m_value;
			case SatisfyOp::GT: return m_value > that.m_value;
			case SatisfyOp::GE: return m_value >= that.m_value;
			case SatisfyOp::LS: return m_value < that.m_value;
			case SatisfyOp::LE: return m_value <= that.m_value;
			default: wsw::failWithLogicError( "Unreachable" );
		}
	}

	[[nodiscard]]
	bool operator==( const This &that ) const {
		return m_op == that.m_op && m_value == that.m_value;
	}

	[[nodiscard]]
	auto computeHash() const -> uint32_t {
		static_assert( sizeof( m_value ) <= sizeof( uint32_t ) );
		uint32_t valueBits = 0;
		memcpy( &valueBits, &m_value, sizeof( m_value ) );
		return 17 + 31 * ( 31 * valueBits + (uint32_t)m_op );
	}
protected:
	T m_value;
	SatisfyOp m_op;
};

class UIntVar : public OrderedComparableVar<unsigned, UIntVar> {
public:
	explicit UIntVar( unsigned value, SatisfyOp op = EQ ) noexcept :
		OrderedComparableVar<unsigned, UIntVar>( value, op ) {}
};

class FloatVar : public OrderedComparableVar<float, FloatVar> {
public:
	explicit FloatVar( float value, SatisfyOp op = EQ ) noexcept :
		OrderedComparableVar<float, FloatVar>( value, op ) {}
};

class BoolVar : public OrderedComparableVar<bool, BoolVar> {
public:
	explicit BoolVar( bool value, SatisfyOp op = EQ ) noexcept :
		OrderedComparableVar<bool, BoolVar>( value, op ) {}
};

class OriginVar {
	friend class WorldState;
public:
	enum SatisfyOp { EQ, NE };

	explicit OriginVar( const Vec3 &value, SatisfyOp op = EQ ) : m_value( value ), m_op( op ) {}

	[[nodiscard]]
	operator const Vec3 &() const noexcept { return m_value; }

	[[nodiscard]]
	bool isSatisfiedBy( const OriginVar &that ) const {
		switch( m_op ) {
			case SatisfyOp::EQ: return m_value.SquareDistanceTo( that.m_value ) < 1.0f;
			case SatisfyOp::NE:	return m_value.SquareDistanceTo( that.m_value ) >= 1.0f;
			default: wsw::failWithLogicError( "Unreachable" );
		}
	}

	[[nodiscard]]
	bool operator==( const OriginVar &that ) const {
		return m_value == that.m_value;
	}

	[[nodiscard]]
	auto computeHash() const -> uint32_t {
		uint32_t hash = 17 + (uint32_t)m_op;
		// -fno-strict-aliasing
		auto *const valueDWords = (const uint32_t *)m_value.Data();
		hash = hash * 31 + valueDWords[0];
		hash = hash * 31 + valueDWords[1];
		hash = hash * 31 + valueDWords[2];
		return hash;
	}
private:
	Vec3 m_value { 0.0f, 0.0f, 0.0f };
	SatisfyOp m_op { EQ };
};

class WorldState {
public:
	[[nodiscard]]
	WorldState() = default;

#ifndef PUBLIC_BUILD
	// Just to track assignment correctness (so every world state is an assignment descendant of a current world state)
	bool isCopiedFromOtherWorldState() { return m_isCopiedFromOtherWorldState; }

	[[nodiscard]]
	WorldState( const WorldState &that ) {
		copyFromOtherWorldState( that );
	}

	[[maybe_unused]]
	auto operator=( const WorldState &that ) -> WorldState & {
		if( this != std::addressof( that ) ) {
			copyFromOtherWorldState( that );
		}
		return *this;
	}

	[[nodiscard]]
	WorldState( WorldState &&that ) {
		copyFromOtherWorldState( that );
	}

	[[maybe_unused]]
	auto operator=( WorldState &&that ) noexcept -> WorldState & {
		copyFromOtherWorldState( that );
		return *this;
	}

	const char *producedByAction { nullptr };
#endif

	enum BuiltinUIntVarKeys {
		GoalItemWaitTime,
		SimilarWorldStateInstanceId,

		// This sucks and there's no viable alternative...
		_kBuiltinUIntVarsCount
	};

	[[nodiscard]]
	auto getUIntVar( BuiltinUIntVarKeys key ) const -> std::optional<UIntVar> {
		assert( key >= 0 && key < std::size( m_uintVars ) );
		return m_uintVars[key];
	}

	void setUIntVar( BuiltinUIntVarKeys key, const UIntVar &value ) {
		assert( key >= 0 && key < std::size( m_uintVars ) );
		m_uintVars[key] = value;
	}

	void clearUIntVar( BuiltinUIntVarKeys key ) {
		m_uintVars[key] = std::nullopt;
	}

	enum BuiltinFloatVarKeys {
		PotentialHazardDamage,
		ThreatInflictedDamage,

		_kBuiltinFloatVarsCount
	};

	[[nodiscard]]
	auto getFloatVar( BuiltinFloatVarKeys key ) const -> std::optional<FloatVar> {
		assert( key >= 0 && key < std::size( m_floatVars ) );
		return m_floatVars[key];
	}

	auto setFloatVar( BuiltinFloatVarKeys key, const FloatVar &value ) {
		assert( key >= 0 && key < std::size( m_floatVars ) );
		m_floatVars[key] = value;
	}

	void clearFloatVar( BuiltinFloatVarKeys key ) {
		m_floatVars[key] = std::nullopt;
	}

	enum BuiltinBoolVarKeys {
		HasThreateningEnemy,
		HasPickedGoalItem,

		IsRunningAway,
		HasRunAway,

		HasReactedToHazard,
		HasReactedToThreat,

		IsReactingToEnemyLost,
		HasReactedToEnemyLost,
		MightSeeLostEnemyAfterTurn,

		HasPositionalAdvantage,
		CanHitEnemy,
		EnemyCanHit,
		HasJustKilledEnemy,

		_kBuiltinBoolVarsCount
	};

	[[nodiscard]]
	auto getBoolVar( BuiltinBoolVarKeys key ) const -> std::optional<BoolVar> {
		assert( key >= 0 && key < std::size( m_boolVars ) );
		return m_boolVars[key];
	}

	auto setBoolVar( BuiltinBoolVarKeys key, const BoolVar &value ) {
		assert( key >= 0 && key < std::size( m_boolVars ) );
		m_boolVars[key] = value;
	}

	void clearBoolVar( BuiltinBoolVarKeys key ) {
		m_boolVars[key] = std::nullopt;
	}

	enum BuiltinOriginVarKeys {
		BotOrigin,
		EnemyOrigin,
		NavTargetOrigin,

		PendingCoverSpot,
		PendingElevatorDest,
		PendingJumppadDest,
		PendingTeleportDest,

		ThreatPossibleOrigin,
		LostEnemyLastSeenOrigin,

		_kBuiltinOriginVarsCount
	};

	[[nodiscard]]
	auto getOriginVar( BuiltinOriginVarKeys key ) const -> std::optional<OriginVar> {
		assert( key >= 0 && key < std::size( m_originVars ) );
		return m_originVars[key];
	}

	auto setOriginVar( BuiltinOriginVarKeys key, const OriginVar &value ) {
		assert( key >= 0 && key < std::size( m_originVars ) );
		m_originVars[key] = value;
	}

	void clearOriginVar( BuiltinOriginVarKeys key ) {
		m_originVars[key] = std::nullopt;
	}

	[[nodiscard]]
	auto computeHash() const -> uint32_t;

	[[nodiscard]]
	bool operator==( const WorldState &that ) const;

	[[nodiscard]]
	bool isSatisfiedBy( const WorldState &that ) const;

	void DebugPrint( const char *tag ) const;

	void DebugPrintDiff( const WorldState &that, const char *oldTag, const char *newTag ) const;
private:
	bool m_isCopiedFromOtherWorldState { false };

	// The memory layout could be more optimal, but we no longer care that much

	std::optional<UIntVar> m_uintVars[_kBuiltinUIntVarsCount];
	std::optional<FloatVar> m_floatVars[_kBuiltinFloatVarsCount];
	std::optional<BoolVar> m_boolVars[_kBuiltinBoolVarsCount];
	std::optional<OriginVar> m_originVars[_kBuiltinOriginVarsCount];

#ifndef PUBLIC_BUILD
	void copyFromOtherWorldState( const WorldState &that ) {
		// std::optionals are perfectly copyable in this case
		std::memcpy( (void *)this, (void *)std::addressof( that ), sizeof( WorldState ) );
		m_isCopiedFromOtherWorldState = true;
	}
#endif
};

#endif
