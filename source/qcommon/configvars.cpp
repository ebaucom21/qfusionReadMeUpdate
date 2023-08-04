#include "configvars.h"
#include "local.h"
#include "cvar.h"
#include "wswexceptions.h"
#include "wswstringsplitter.h"
#include "wswtonum.h"
#include "outputmessages.h"

cvar_t *Cvar_Set2( const char *var_name, const char *value, bool force );

using wsw::operator""_asView;

DeclaredConfigVar::DeclaredConfigVar( const wsw::StringView &name, int registrationFlags )
	: m_name( name ), m_registrationFlags( registrationFlags ) {
	assert( m_name.isZeroTerminated() && !m_name.empty() );

	// Note: These lists differ for the game module and for the primary executable
	m_next     = s_listHead;
	s_listHead = this;
}

DeclaredConfigVar::~DeclaredConfigVar() {
	if( m_underlying ) {
		// TODO: This must be an atomic write, or even lock
		m_underlying->controller = nullptr;
	}
}

void DeclaredConfigVar::failOnOp( const char *op ) const {
	wsw::String message( "Attempt to" );
	message.append( op );
	message.append( "a value of an uninitailized var" );
	message.append( m_name.data(), m_name.size() );
	wsw::failWithRuntimeError( message.data() );
}

void DeclaredConfigVar::registerAllVars( DeclaredConfigVar *head ) {
	wsw::String defaultValueBuffer;

	for( DeclaredConfigVar *var = head; var; var = var->m_next ) {
		if( var->m_underlying ) {
			wsw::failWithRuntimeError( "Attempt to register a var twice" );
		}
		
		defaultValueBuffer.clear();
		var->getDefaultValueText( &defaultValueBuffer );

		var->m_underlying = Cvar_Get( var->m_name.data(), defaultValueBuffer.data(), var->m_registrationFlags, var );
	}
}

void DeclaredConfigVar::unregisterAllVars( DeclaredConfigVar *head ) {
	for( DeclaredConfigVar *var = head; var; var = var->m_next ) {
		if( var->m_underlying ) {
			var->m_underlying->controller = nullptr;
			var->m_underlying = nullptr;
		}
	}
}

template <typename T, typename Params>
[[nodiscard]]
static auto clampValue( T givenValue, const Params &params ) -> T {
	T value = givenValue;
	if( params.minInclusive != std::nullopt ) {
		value = std::max( value, *params.minInclusive );
	}
	if( params.maxInclusive != std::nullopt ) {
		value = std::min( value, *params.maxInclusive );
	}
	return value;
}

BoolConfigVar::BoolConfigVar( const wsw::StringView &name, Params &&params )
	: DeclaredConfigVar( name, params.flags ), m_params( params ) {}

void BoolConfigVar::getDefaultValueText( wsw::String *defaultValueBuffer ) const {
	assert( defaultValueBuffer->empty() );
	defaultValueBuffer->push_back( m_params.byDefault ? '1' : '0' );
}

auto BoolConfigVar::handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> {
	if( const auto maybeNum = wsw::toNum<int64_t>( newValue ) ) {
		if( maybeNum == 0 ) {
			m_cachedValue = false;
		} else if( maybeNum == 1 ) {
			m_cachedValue = true;
		} else {
			m_cachedValue = true;
			assert( tmpBuffer->empty() );
			tmpBuffer->push_back( '1' );
			return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
		}
	} else {
		if( newValue.equalsIgnoreCase( "true"_asView ) ) {
			m_cachedValue = true;
		} else if( newValue.equalsIgnoreCase( "false"_asView ) ) {
			m_cachedValue = false;
		} else {
			m_cachedValue = m_params.byDefault;
			assert( tmpBuffer->empty() );
			tmpBuffer->push_back( m_cachedValue ? '1' : '0' );
			return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
		}
	}
	return std::nullopt;
}

auto BoolConfigVar::correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> {
	if( const auto maybeNum = wsw::toNum<int64_t>( newValue ) ) {
		if( maybeNum != 0 && maybeNum != 1 ) {
			assert( tmpBuffer->empty() );
			tmpBuffer->push_back( '1' );
			return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
		}
	} else {
		if( !newValue.equalsIgnoreCase( "true"_asView ) && !newValue.equalsIgnoreCase( "false"_asView ) ) {
			assert( tmpBuffer->empty() );
			tmpBuffer->push_back( m_params.byDefault ? '1' : '0' );
			return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
		}
	}
	return std::nullopt;
}

bool BoolConfigVar::get() const {
	if( m_underlying ) [[likely]] {
		return m_cachedValue;
	} else {
		wsw::failWithRuntimeError( "Attempt to get uninitialized config variable" );
	}
}

void BoolConfigVar::helperOfSet( bool value, bool force ) {
	if( m_underlying ) [[likely]] {
		m_cachedValue = value;
		Cvar_Set2( m_name.data(), value ? "1" : "0", force );
	} else {
		failOnSet();
	}
}

IntConfigVar::IntConfigVar( const wsw::StringView &name, Params &&params )
	: DeclaredConfigVar( name, params.flags ), m_params( params ) {}

void IntConfigVar::getDefaultValueText( wsw::String *defaultValueBuffer ) const {
	assert( defaultValueBuffer->empty() );
	// Should not allocate in this case
	defaultValueBuffer->append( std::to_string( m_params.byDefault ) );
}

auto IntConfigVar::handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> {
	// Note: the double parsing kind of sucks, but it reduces implementation complexity
	// and strings are still the primary serialization/exchange form of config vars data.
	if( const std::optional<wsw::StringView> &correctedValueView = correctValue( newValue, tmpBuffer ) ) {
		m_cachedValue = wsw::toNum<int>( *correctedValueView ).value();
		return correctedValueView;
	} else {
		m_cachedValue = wsw::toNum<int>( newValue ).value();
		return std::nullopt;
	}
}

auto IntConfigVar::correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> {
	if( const auto maybeNum = wsw::toNum<int64_t>( newValue ) ) {
		int correctedValue;
		const int minAllowedValue = m_params.minInclusive.value_or( std::numeric_limits<int>::min() );
		if( *maybeNum < (int64_t)minAllowedValue ) {
			correctedValue = minAllowedValue;
		} else {
			const int maxAllowedValue = m_params.minInclusive.value_or( std::numeric_limits<int>::max() );
			if( *maybeNum > (int64_t)maxAllowedValue ) {
				correctedValue = maxAllowedValue;
			} else {
				correctedValue = (int)*maybeNum;
			}
		}
		if( correctedValue != *maybeNum ) {
			assert( tmpBuffer->empty() );
			tmpBuffer->append( std::to_string( correctedValue ) );
			return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
		}
	} else {
		assert( tmpBuffer->empty() );
		tmpBuffer->append( std::to_string( m_params.byDefault ) );
		return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
	}
	return std::nullopt;
}

auto IntConfigVar::get() const -> int {
	if( m_underlying ) {
		return m_cachedValue;
	} else {
		failOnGet();
	}
}

void IntConfigVar::helperOfSet( int value, bool force ) {
	if( m_underlying ) {
		value = clampValue( value, m_params );
		m_cachedValue = value;
		Cvar_Set2( m_name.data(), std::to_string( value ).data(), force );
	} else {
		failOnSet();
	}
}

UnsignedConfigVar::UnsignedConfigVar( const wsw::StringView &name, Params &&params )
	: DeclaredConfigVar( name, params.flags ), m_params( params ) {}

void UnsignedConfigVar::getDefaultValueText( wsw::String *defaultValueBuffer ) const {
	assert( defaultValueBuffer->empty() );
	// Should not allocate in this case
	defaultValueBuffer->append( std::to_string( m_params.byDefault ) );
}

auto UnsignedConfigVar::handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> {
	// See IntConfigVar remarks
	if( const std::optional<wsw::StringView> &correctedValueView = correctValue( newValue, tmpBuffer ) ) {
		m_cachedValue = wsw::toNum<unsigned>( *correctedValueView ).value();
		return correctedValueView;
	} else {
		m_cachedValue = wsw::toNum<unsigned>( newValue ).value();
		return std::nullopt;
	}
}

auto UnsignedConfigVar::correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> {
	if( const auto maybeNum = wsw::toNum<uint64_t>( newValue ) ) {
		unsigned correctedValue;
		const unsigned minAllowedValue = m_params.minInclusive.value_or( 0u );
		if( *maybeNum < (uint64_t)minAllowedValue ) {
			correctedValue = minAllowedValue;
		} else {
			const unsigned maxAllowedValue = m_params.maxInclusive.value_or( std::numeric_limits<unsigned>::max() );
			if( *maybeNum > (uint64_t)maxAllowedValue ) {
				correctedValue = maxAllowedValue;
			} else {
				correctedValue = (unsigned)*maybeNum;
			}
		}
		if( correctedValue != *maybeNum ) {
			assert( tmpBuffer->empty() );
			tmpBuffer->append( std::to_string( correctedValue ) );
			return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
		}
	} else {
		assert( tmpBuffer->empty() );
		tmpBuffer->append( std::to_string( m_params.byDefault ) );
		return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
	}
	return std::nullopt;
}

auto UnsignedConfigVar::get() const -> unsigned {
	if( m_underlying ) {
		return m_cachedValue;
	} else {
		failOnGet();
	}
}

void UnsignedConfigVar::helperOfSet( unsigned value, bool force ) {
	if( m_underlying ) {
		value = clampValue( value, m_params );
		m_cachedValue = value;
		Cvar_Set2( m_name.data(), std::to_string( value ).data(), force );
	} else {
		failOnSet();
	}
}

FloatConfigVar::FloatConfigVar( const wsw::StringView &name, Params &&params )
	: DeclaredConfigVar( name, params.flags ), m_params( params ) {}

void FloatConfigVar::getDefaultValueText( wsw::String *defaultValueBuffer ) const {
	assert( defaultValueBuffer->empty() );
	defaultValueBuffer->append( std::to_string( m_params.byDefault ) );
}

auto FloatConfigVar::handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> {
	// See IntConfigVar remarks
	if( const std::optional<wsw::StringView> &correctedValueView = correctValue( newValue, tmpBuffer ) ) {
		m_cachedValue = wsw::toNum<float>( *correctedValueView ).value();
		return correctedValueView;
	} else {
		m_cachedValue = wsw::toNum<float>( newValue ).value();
		return std::nullopt;
	}
}

auto FloatConfigVar::correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> {
	if( const auto maybeNum = wsw::toNum<float>( newValue ); maybeNum && std::isfinite( *maybeNum ) ) {
		float correctedValue;
		const float minAllowedValue = m_params.minInclusive.value_or( std::numeric_limits<float>::min() );
		if( *maybeNum < minAllowedValue ) {
			correctedValue = minAllowedValue;
		} else {
			const float maxAllowedValue = m_params.maxInclusive.value_or( std::numeric_limits<float>::max() );
			if( *maybeNum > maxAllowedValue ) {
				correctedValue = maxAllowedValue;
			} else {
				correctedValue = *maybeNum;
			}
		}
		if( correctedValue != *maybeNum ) {
			assert( tmpBuffer->empty() );
			tmpBuffer->append( std::to_string( correctedValue ) );
			return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
		}
	} else {
		assert( tmpBuffer->empty() );
		tmpBuffer->append( std::to_string( m_params.byDefault ) );
		return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
	}
	return std::nullopt;
}

auto FloatConfigVar::get() const -> float {
	if( m_underlying ) {
		return m_cachedValue;
	} else {
		failOnGet();
	}
}

void FloatConfigVar::helperOfSet( float value, bool force ) {
	if( m_underlying ) {
		value = clampValue( value, m_params );
		m_cachedValue = value;
		Cvar_Set2( m_name.data(), std::to_string( value ).data(), force );
	} else {
		failOnSet();
	}
}

StringConfigVar::StringConfigVar( const wsw::StringView &name, Params &&params )
	: DeclaredConfigVar( name, params.flags ), m_params( params ) {}

void StringConfigVar::getDefaultValueText( wsw::String *defaultValueBuffer ) const {
	assert( defaultValueBuffer->empty() );
	defaultValueBuffer->append( m_params.byDefault.data(), m_params.byDefault.size() );
}

auto StringConfigVar::handleValueChanges( const wsw::StringView &newValue, wsw::String * ) -> std::optional<wsw::StringView> {
	// TODO: What should we do for strings?
	return std::nullopt;
}

auto StringConfigVar::correctValue( const wsw::StringView &newValue, wsw::String * ) const -> std::optional<wsw::StringView> {
	// TODO: What should we do for strings?
	return std::nullopt;
}

auto StringConfigVar::get() const -> wsw::StringView {
	if( m_underlying ) {
		// Not thread-safe... Should we make copies? Should the underlying value be a shared pointer?
		return wsw::StringView( m_underlying->string );
	} else {
		failOnGet();
	}
}

void StringConfigVar::helperOfSet( const wsw::StringView &value, bool force ) {
	if( m_underlying ) {
		if( value.isZeroTerminated() ) {
			Cvar_Set2( m_name.data(), value.data(), force );
		} else {
			wsw::String ztValue( m_name.data(), m_name.size() );
			Cvar_Set2( m_name.data(), ztValue.data(), force );
		}
	} else {
		failOnSet();
	}
}

UntypedEnumValueConfigVar::UntypedEnumValueConfigVar( const wsw::StringView &name, int registrationFlags,
													  MatcherObj matcherObj, MatcherFn matcherFn,
													  wsw::Vector<int> &&enumValues, int defaultValue )
	: DeclaredConfigVar( name, registrationFlags ), m_matcherObj( matcherObj ), m_matcherFn( matcherFn )
	, m_enumValues( std::forward<wsw::Vector<int>>( enumValues ) ), m_defaultValue( defaultValue ) {
	assert( !m_enumValues.empty() );
	m_minEnumValue = std::numeric_limits<int>::max();
	m_maxEnumValue = std::numeric_limits<int>::min();
	for( const int value: m_enumValues ) {
		m_minEnumValue = std::min( value, m_minEnumValue );
		m_maxEnumValue = std::max( value, m_maxEnumValue );
	}
}

auto UntypedEnumValueConfigVar::helperOfGet() const -> int {
	if( m_underlying ) [[likely]] {
		return m_cachedValue;
	} else {
		failOnGet();
	}
}

void UntypedEnumValueConfigVar::helperOfSet( int value, bool force ) {
	if( m_underlying ) [[likely]] {
		if( !isAValidValue( value ) ) {
			value = m_defaultValue;
		}
		m_cachedValue = value;
		Cvar_Set2( m_name.data(), std::to_string( value ).data(), force );
	} else {
		failOnSet();
	}
}

void UntypedEnumValueConfigVar::getDefaultValueText( wsw::String *defaultValueBuffer ) const {
	assert( defaultValueBuffer->empty() );
	defaultValueBuffer->append( std::to_string( m_defaultValue ) );
}

auto UntypedEnumValueConfigVar::correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> {
	std::optional<int> validatedValue;
	if( const auto maybeNum = wsw::toNum<int>( newValue ) ) {
		if( isAValidValue( *maybeNum ) ) {
			return std::nullopt;
		}
	} else if( m_matcherFn( m_matcherObj, newValue ) != std::nullopt ) {
		return std::nullopt;
	}
	getDefaultValueText( tmpBuffer );
	return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
}

auto UntypedEnumValueConfigVar::handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> {
	std::optional<int> validatedValue;
	if( const auto maybeNum = wsw::toNum<int>( newValue ) ) {
		if( isAValidValue( *maybeNum ) ) {
			validatedValue = maybeNum;
		}
	} else if( const auto maybeMatchedValue = m_matcherFn( m_matcherObj, newValue ) ) {
		validatedValue = maybeMatchedValue;
	}
	if( validatedValue ) {
		m_cachedValue = *validatedValue;
		return std::nullopt;
	} else {
		m_cachedValue = m_defaultValue;
		getDefaultValueText( tmpBuffer );
		return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
	}
}

bool UntypedEnumValueConfigVar::isAValidValue( int value ) const {
	if( value >= m_minEnumValue && value <= m_maxEnumValue ) {
		return std::find( m_enumValues.begin(), m_enumValues.end(), value ) != m_enumValues.end();
	}
	return false;
}

UntypedEnumFlagsConfigVar::UntypedEnumFlagsConfigVar( const wsw::StringView &name, int registrationFlags,
													  MatcherObj matcherObj, MatcherFn matcherFn,
													  wsw::Vector<unsigned> &&enumValues,
													  size_t typeSizeInBytes, unsigned defaultValue )
	: DeclaredConfigVar( name, registrationFlags ), m_matcherObj( matcherObj ), m_matcherFn( matcherFn )
	, m_enumValues( std::forward<wsw::Vector<unsigned>>( enumValues ) ) {
	assert( !m_enumValues.empty() );
	m_allBitsInEnumValues = 0;
	m_hasZeroInValues     = false;
	for( const unsigned value: m_enumValues ) {
		m_allBitsInEnumValues |= value;
		if( !value ) {
			m_hasZeroInValues = true;
		}
	}
	m_defaultValue = defaultValue & m_allBitsInEnumValues;
	// We can't just use "~0u" as lesser than "~0u" all-bits-set default values may be specified for types of lesser size
	assert( typeSizeInBytes == 1 || typeSizeInBytes == 2 || typeSizeInBytes == 4 );
	m_allBitsSetValueForType = 255;
	for( size_t i = 1; i < typeSizeInBytes; ++i ) {
		m_allBitsSetValueForType = ( m_allBitsSetValueForType << 8 ) | 255;
	}
}

void UntypedEnumFlagsConfigVar::getDefaultValueText( wsw::String *defaultValueBuffer ) const {
	assert( defaultValueBuffer->empty() );
	// TODO: retrieve the default value string using the matcher?
	if( m_defaultValue != m_allBitsSetValueForType && m_defaultValue != m_allBitsInEnumValues ) {
		defaultValueBuffer->append( std::to_string( m_defaultValue ) );
	} else {
		// Try displaying it nicer
		defaultValueBuffer->append( "-1", 2 );
	}
}

// Note: We do not correct "-1", despite internally truncating it. Let users type "-1" without hassle.

auto UntypedEnumFlagsConfigVar::correctValue( const wsw::StringView &newValue, wsw::String *tmpBuffer ) const -> std::optional<wsw::StringView> {
	std::optional<unsigned> validatedValue;
	if( const auto maybeNum = wsw::toNum<unsigned>( newValue ) ) {
		if( isAnAcceptableValue( *maybeNum ) ) {
			return std::nullopt;
		}
	} else {
		// Try parsing '|' - separated string of tokens or values
		if( parseValueFromString( newValue ) != std::nullopt ) {
			return std::nullopt;
		}
	}
	getDefaultValueText( tmpBuffer );
	return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
}

auto UntypedEnumFlagsConfigVar::handleValueChanges( const wsw::StringView &newValue, wsw::String *tmpBuffer ) -> std::optional<wsw::StringView> {
	std::optional<unsigned> validatedValue;
	if( const auto maybeNum = wsw::toNum<unsigned>( newValue ) ) {
		if( isAnAcceptableValue( *maybeNum ) ) {
			// While accepting (enum-type)~0u, make sure that it does not contain extra bits
			validatedValue = *maybeNum & m_allBitsInEnumValues;
		}
	} else if( wsw::toNum<int>( newValue ) == std::optional<int>( -1 ) ) {
		validatedValue = m_allBitsInEnumValues;
	} else {
		// Try parsing '|' - separated string of tokens or values
		validatedValue = parseValueFromString( newValue );
	}
	if( validatedValue ) {
		m_cachedValue = *validatedValue;
		return std::nullopt;
	} else {
		m_cachedValue = m_defaultValue;
		getDefaultValueText( tmpBuffer );
		return wsw::StringView( tmpBuffer->data(), tmpBuffer->size(), wsw::StringView::ZeroTerminated );
	}
}

auto UntypedEnumFlagsConfigVar::parseValueFromString( const wsw::StringView &string ) const -> std::optional<unsigned> {
	if( string.empty() ) {
		if( m_hasZeroInValues ) {
			return 0;
		}
		return std::nullopt;
	}

	unsigned newValueBits = 0;
	wsw::StringSplitter splitter( string );
	while( const auto maybeNextToken = splitter.getNext( '|' ) ) {
		if( const auto maybeTokenNum = wsw::toNum<unsigned>( *maybeNextToken ) ) {
			// Match against individual values TODO: Check range for doing lookups fast?
			if( std::find( m_enumValues.begin(), m_enumValues.end(), *maybeTokenNum ) != m_enumValues.end() ) {
				newValueBits |= *maybeTokenNum;
			} else {
				return std::nullopt;
			}
		} else {
			if( const auto maybeMatchedValue = m_matcherFn( m_matcherObj, *maybeNextToken ) ) {
				newValueBits |= *maybeTokenNum;
			} else {
				return std::nullopt;
			}
		}
	}
	return newValueBits;
}

bool UntypedEnumFlagsConfigVar::isAnAcceptableValue( unsigned value ) const {
	// If it matches some bits (or is zero and zeroes are allowed)
	if( ( m_allBitsInEnumValues & value ) || ( m_hasZeroInValues && !value ) ) {
		// If it does not add extra bits, with the exception of the all-bits-set value
		if( !( ~m_allBitsInEnumValues & value ) || ( value == m_allBitsSetValueForType ) ) {
			// Note: to be really correct, we should check whether it's in set of actual possible combinations of flags
			return true;
		}
	}
	return false;
}

auto UntypedEnumFlagsConfigVar::helperOfGet() const -> unsigned {
	if( m_underlying ) [[likely]] {
		return m_cachedValue;
	} else {
		failOnGet();
	}
}

void UntypedEnumFlagsConfigVar::helperOfSet( unsigned value, bool force ) {
	if( m_underlying ) [[likely]] {
		if( isAnAcceptableValue( value ) ) {
			m_cachedValue = value & m_allBitsInEnumValues;
		} else {
			assert( !( m_defaultValue & ~m_allBitsInEnumValues ) );
			m_cachedValue = m_defaultValue;
		}
		m_cachedValue = value & m_allBitsInEnumValues;
		if( value != m_allBitsSetValueForType && value != m_allBitsInEnumValues ) {
			Cvar_Set2( m_name.data(), std::to_string( value ).data(), force );
		} else {
			Cvar_Set2( m_name.data(), "-1", force );
		}
	} else {
		failOnSet();
	}
}