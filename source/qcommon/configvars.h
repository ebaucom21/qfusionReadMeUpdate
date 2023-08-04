#ifndef WSW_a8647f1f_5b56_40a9_aaf5_811a1e2e3ee0_H
#define WSW_a8647f1f_5b56_40a9_aaf5_811a1e2e3ee0_H

#include "wswstringview.h"
#include "wswstring.h"
#include "wswvector.h"
#include "../gameshared/q_cvar.h"

#include <atomic>

struct cvar_s;

class DeclaredConfigVar {
public:
	virtual ~DeclaredConfigVar();

	DeclaredConfigVar( const DeclaredConfigVar & ) = delete;
	auto operator=( const DeclaredConfigVar & ) -> DeclaredConfigVar & = delete;
	DeclaredConfigVar( DeclaredConfigVar && ) = delete;
	auto operator=( DeclaredConfigVar && ) -> DeclaredConfigVar & = delete;

	[[nodiscard]]
	bool initialized() const { return m_underlying != nullptr; }
	[[nodiscard]]
	auto modificationId() const -> uint64_t { return m_underlying->modificationId; }

	static void registerAllVars( DeclaredConfigVar *head );
	static void unregisterAllVars( DeclaredConfigVar *head );

	// TODO: We require that the buffer is empty, should we, assuming that we can perfectly return a correct slice?
	[[nodiscard]]
	virtual auto handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> = 0;
	[[nodiscard]]
	virtual auto correctValue( const wsw::StringView &value, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> = 0;

	static DeclaredConfigVar *s_listHead;
protected:
	explicit DeclaredConfigVar( const wsw::StringView &name, int registrationFlags );

	// TODO: There's inconsistency with handleValueChanges()/correctValue() with regard to signature
	virtual void getDefaultValueText( wsw::String *defaultValueBuffer ) const = 0;

	[[noreturn]]
	void failOnSet() const { failOnOp( "set" ); }
	[[noreturn]]
	void failOnGet() const { failOnOp( "get" ); }

	// TODO: Should be an atomic reference
	mutable cvar_s *m_underlying { nullptr };
	DeclaredConfigVar *m_next { nullptr };
	const wsw::StringView m_name;
	const int m_registrationFlags;
private:
	[[noreturn]]
	void failOnOp( const char *op ) const;
};

class VarModificationTracker {
public:
	explicit VarModificationTracker( const DeclaredConfigVar *trackedVar ) : m_trackedVar( trackedVar ) {}

	[[nodiscard]]
	bool checkAndReset() {
		if( m_trackedVar->initialized() ) [[likely]] {
			if( const uint64_t modificationId = m_trackedVar->modificationId(); modificationId != m_lastModificationId ) {
				m_lastModificationId = modificationId;
				return true;
			}
		}
		return false;
	}
private:
	const DeclaredConfigVar *const m_trackedVar;
	uint64_t m_lastModificationId { ~0u };
};

class BoolConfigVar final : public DeclaredConfigVar {
public:
	struct Params {
		const bool byDefault { false };
		const int flags { 0 };
		const char *desc;
	};

	BoolConfigVar( const wsw::StringView &name, Params &&params );

	[[nodiscard]]
	bool get() const;
	void set( bool value ) { helperOfSet( value, false ); }
	void setImmediately( bool value ) { helperOfSet( value, true ); }
private:
	void getDefaultValueText( wsw::String *defaultValueBuffer ) const override;
	void helperOfSet( bool value, bool force );

	auto handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> override;
	auto correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> override;

	const Params m_params;
	// Note: Due to correctness reasons, we have to use atomic wrappers for values
	// that get read/written from multiple threads. Their operations default to relaxed loads/stores.
	std::atomic<bool> m_cachedValue { false };
};

class IntConfigVar final : public DeclaredConfigVar {
public:
	struct Params {
		const int byDefault { 0 };
		const std::optional<int> minInclusive;
		const std::optional<int> maxInclusive;
		const int flags { 0 };
		const char *desc;
	};

	IntConfigVar( const wsw::StringView &name, Params &&params );

	[[nodiscard]]
	auto get() const -> int;
	void set( int value ) { helperOfSet( value, false ); }
	void setImmediately( int value ) { helperOfSet( value, true ); }
private:
	void getDefaultValueText( wsw::String *defaultValueBuffer ) const override;
	void helperOfSet( int value, bool force );

	auto handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> override;
	auto correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> override;

	std::atomic<int> m_cachedValue { 0 };
	const Params m_params;
};

class UnsignedConfigVar final : public DeclaredConfigVar {
public:
	struct Params {
		const unsigned byDefault { 0 };
		const std::optional<unsigned> minInclusive;
		const std::optional<unsigned> maxInclusive;
		const int flags { 0 };
		const char *desc;
	};

	UnsignedConfigVar( const wsw::StringView &name, Params &&params );

	[[nodiscard]]
	auto get() const -> unsigned;
	void set( unsigned value ) { helperOfSet( value, false ); }
	void setImmediately( unsigned value ) { helperOfSet( value, true ); }
private:
	void getDefaultValueText( wsw::String *defaultValueBuffer ) const override;
	void helperOfSet( unsigned value, bool force );

	auto handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> override;
	auto correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> override;

	std::atomic<unsigned> m_cachedValue;
	const Params m_params;
};

class FloatConfigVar final : public DeclaredConfigVar {
public:
	struct Params {
		const float byDefault { 0.0 };
		std::optional<float> minInclusive;
		std::optional<float> maxInclusive;
		const int flags { 0 };
		const char *desc;
	};

	FloatConfigVar( const wsw::StringView &name, Params &&params );

	[[nodiscard]]
	auto get() const -> float;
	void set( float value ) { helperOfSet( value, false ); }
	void setImmediately( float value ) { helperOfSet( value, true ); }
private:
	void getDefaultValueText( wsw::String *defaultValueBuffer ) const override;
	void helperOfSet( float value, bool force );

	auto handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> override;
	auto correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> override;

	std::atomic<float> m_cachedValue { 0.0f };
	const Params m_params;
};

class StringConfigVar final : public DeclaredConfigVar {
public:
	struct Params {
		const wsw::StringView byDefault;
		const int flags { 0 };
		const char *desc;
	};

	StringConfigVar( const wsw::StringView &name, Params &&params );

	[[nodiscard]]
	auto get() const -> wsw::StringView;
	void set( const wsw::StringView &value ) { helperOfSet( value, false ); }
	void setImmediately( const wsw::StringView &value ) { helperOfSet( value, true ); }
private:
	void getDefaultValueText( wsw::String *defaultValueBuffer ) const override;
	void helperOfSet( const wsw::StringView &value, bool force );

	auto handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> override;
	auto correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> override;

	const Params m_params;
};

class UntypedEnumValueConfigVar : public DeclaredConfigVar {
public:
	using MatcherObj  = const void *;
	using MatcherFn   = std::optional<int> (*)( const void *, const wsw::StringView & );

protected:
	UntypedEnumValueConfigVar( const wsw::StringView &name, int registrationFlags,
							   MatcherObj matcherObj, MatcherFn matcherFn,
							   wsw::Vector<int> &&enumValues, int defaultValue );

	[[nodiscard]]
	auto helperOfGet() const -> int;
	void helperOfSet( int value, bool force );
private:
	void getDefaultValueText( wsw::String *defaultValueBuffer ) const final;

	auto handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> final;
	auto correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> final;

	[[nodiscard]]
	bool isAValidValue( int value ) const;

	const MatcherObj m_matcherObj;
	const MatcherFn m_matcherFn;
	const wsw::Vector<int> m_enumValues;
	int m_minEnumValue { 0 };
	int m_maxEnumValue { 0 };
	std::atomic<int> m_cachedValue { 0 };
	const int m_defaultValue;
};

template <typename Enum, typename Matcher>
class EnumValueConfigVar final : public UntypedEnumValueConfigVar {
	using NumericType   = std::underlying_type_t<Enum>;
	static_assert( sizeof( NumericType ) <= 4, "The code assumes that values fit a regular integer" );
public:
	struct Params {
		// TODO : EnumTraits?
		const Enum byDefault {};
		const int flags { 0 };
		const char *desc;
	};

	EnumValueConfigVar( const wsw::StringView &name, Params &&params )
		: UntypedEnumValueConfigVar( name, params.flags, std::addressof( Matcher::instance() ),
									 &Matcher::template matchFn<int>,
		    						 Matcher::instance().template getEnumValues<int>(), (int)params.byDefault )
		, m_params( params ) {}

	[[nodiscard]]
	auto get() const -> Enum {
		return (Enum)UntypedEnumValueConfigVar::helperOfGet();
	}
	void set( Enum value ) {
		UntypedEnumValueConfigVar::helperOfSet( (int)value, false );
	}
	void setImmediately( Enum value ) {
		UntypedEnumValueConfigVar::helperOfSet( (int)value, true );
	}
private:
	const Params m_params;
};

class UntypedEnumFlagsConfigVar : public DeclaredConfigVar {
public:
	using MatcherObj  = const void *;
	using MatcherFn   = std::optional<unsigned> (*)( const void *, const wsw::StringView & );
protected:
	UntypedEnumFlagsConfigVar( const wsw::StringView &name, int registrationFlags,
							   MatcherObj matcherObj, MatcherFn matcherFn,
							   wsw::Vector<unsigned> &&enumValues, size_t typeSizeInBytes, unsigned defaultValue );

	[[nodiscard]]
	auto helperOfGet() const -> unsigned;
	void helperOfSet( unsigned value, bool force );
private:
	void getDefaultValueText( wsw::String *defaultValueBuffer ) const final;

	auto handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> final;
	auto correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> final;

	[[nodiscard]]
	auto parseValueFromString( const wsw::StringView &string ) const -> std::optional<unsigned>;
	[[nodiscard]]
	bool isAnAcceptableValue( unsigned value ) const;

	const MatcherObj m_matcherObj;
	const MatcherFn m_matcherFn;
	const wsw::Vector<unsigned> m_enumValues;
	unsigned m_defaultValue;
	unsigned m_allBitsInEnumValues { 0 };
	unsigned m_allBitsSetValueForType { 0 };
	std::atomic<unsigned> m_cachedValue { 0 };
	bool m_hasZeroInValues { false };
};

template <typename Enum, typename Matcher>
class EnumFlagsConfigVar final : public UntypedEnumFlagsConfigVar {
	using NumericType = std::underlying_type_t<Enum>;
	static_assert( std::is_unsigned_v<NumericType>, "Using signed types for flags is wrong" );
	static_assert( sizeof( NumericType ) <= 4, "The code assumes that values fit a regular unsigned integer" );
public:
	struct Params {
		// TODO: EnumTraits?
		const Enum byDefault {};
		const int flags { 0 };
		const char *desc;
	};

	EnumFlagsConfigVar( const wsw::StringView &name, Params &&params )
		: UntypedEnumFlagsConfigVar( name, params.flags, std::addressof( Matcher::instance() ), &Matcher::template matchFn<unsigned>,
									 Matcher::instance().template getEnumValues<unsigned>(), sizeof( Enum ), (unsigned)params.byDefault )
		, m_params( params ) {}

	[[nodiscard]]
	auto get() const -> Enum {
		return (Enum)UntypedEnumFlagsConfigVar::helperOfGet();
	}
	[[nodiscard]]
	bool isAnyBitSet( Enum value ) const {
		return ( UntypedEnumFlagsConfigVar::helperOfGet() & (unsigned)value ) != 0;
	}
	[[nodiscard]]
	bool areAllBitsSet( Enum value ) const {
		return ( UntypedEnumFlagsConfigVar::helperOfGet() & (unsigned)value ) == (unsigned)value;
	}
	void set( Enum value ) {
		UntypedEnumFlagsConfigVar::helperOfSet( (unsigned)value, false );
	}
	void setImmediately( Enum value ) {
		UntypedEnumFlagsConfigVar::helperOfSet( (unsigned)value, true );
	}
private:
	const Params m_params;
};

#endif