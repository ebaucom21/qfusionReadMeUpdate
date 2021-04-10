#ifndef WSW_STRINGVIEW_H
#define WSW_STRINGVIEW_H

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <optional>
// For char_traits
#include <string>

namespace wsw {

[[nodiscard]]
auto getHashAndLength( const char *s ) -> std::pair<uint32_t, size_t>;
[[nodiscard]]
auto getHashForLength( const char *s, size_t length ) -> uint32_t;

class StringView;

class CharLookup {
	bool m_data[std::numeric_limits<unsigned char>::max()];
public:
	CharLookup( const wsw::StringView &chars );

	[[nodiscard]]
	bool operator()( char ch ) const {
		return m_data[(unsigned char)ch];
	}
};

class StringView {
protected:
	const char *m_s;
	size_t m_len: 31;
	bool m_terminated: 1;

	static constexpr unsigned kMaxLen = 1u << 31u;

	[[nodiscard]]
	static constexpr auto checkLen( size_t len ) -> size_t {
		assert( len < kMaxLen );
		return len;
	}

	[[nodiscard]]
	auto toOffset( const char *p ) const -> unsigned {
		// These casts are mandatory for 32-bit uintptr_t's
		assert( (uint64_t)(uintptr_t)( p - m_s ) < (uint64_t)kMaxLen );
		return (unsigned)( p - m_s );
	}


public:
	enum Terminated {
		Unspecified,
		ZeroTerminated,
	};

	constexpr StringView() noexcept
		: m_s( "" ), m_len( 0 ), m_terminated( true ) {}

	explicit StringView( const char *s ) noexcept
		: m_s( s ), m_len( checkLen( std::char_traits<char>::length( s ) ) ), m_terminated( true ) {}

	constexpr StringView( const char *s, size_t len, Terminated terminated = Unspecified ) noexcept
		: m_s( s ), m_len( checkLen( len ) ), m_terminated( terminated != Unspecified ) {
		assert( !m_terminated || !m_s[len] );
	}

	[[nodiscard]]
	bool isZeroTerminated() const { return m_terminated; }

	[[nodiscard]]
	auto data() const -> const char * { return m_s; }
	[[nodiscard]]
	auto size() const -> size_t { return m_len; }
	[[nodiscard]]
	auto length() const -> size_t { return m_len; }

	[[nodiscard]]
	bool empty() const { return !m_len; }

	[[nodiscard]]
	bool equals( const wsw::StringView &that ) const {
		return m_len == that.m_len && !std::strncmp( m_s, that.m_s, m_len );
	}

	[[nodiscard]]
	bool equalsIgnoreCase( const wsw::StringView &that ) const {
		return m_len == that.m_len && !Q_strnicmp( m_s, that.m_s, m_len );
	}

	[[nodiscard]]
	bool operator==( const wsw::StringView &that ) const { return equals( that ); }
	[[nodiscard]]
	bool operator!=( const wsw::StringView &that ) const { return !equals( that ); }

	[[nodiscard]]
	auto begin() const -> const char * { return m_s; }
	[[nodiscard]]
	auto end() const -> const char * { return m_s + m_len; }
	[[nodiscard]]
	auto cbegin() const -> const char * { return m_s; }
	[[nodiscard]]
	auto cend() const -> const char * { return m_s + m_len; }

	[[nodiscard]]
	auto front() const -> const char & {
		assert( m_len );
		return m_s[0];
	}

	[[nodiscard]]
	auto back() const -> const char & {
		assert( m_len );
		return m_s[0];
	}

	[[nodiscard]]
	auto maybeFront() const -> std::optional<char> {
		return m_len ? std::optional( m_s[0] ) : std::nullopt;
	}

	[[nodiscard]]
	auto maybeBack() const -> std::optional<char> {
		return m_len ? std::optional( m_s[m_len - 1] ) : std::nullopt;
	}

	[[nodiscard]]
	auto operator[]( size_t index ) const -> const char & {
		assert( index < m_len );
		return m_s[index];
	}

	[[nodiscard]]
	auto maybeAt( size_t index ) const -> std::optional<char> {
		return index < m_len ? std::optional( m_s[index] ) : std::nullopt;
	}

	[[nodiscard]]
	auto indexOf( char ch ) const -> std::optional<unsigned> {
		if( m_terminated ) {
			if( const char *p = strchr( m_s, ch ) ) {
				return toOffset( p );
			}
		} else {
			if( const char *p = std::find( m_s, m_s + m_len, ch ); p != m_s + m_len ) {
				return toOffset( p );
			}
		}
		return std::nullopt;
	}

	[[nodiscard]]
	auto lastIndexOf( char ch ) const -> std::optional<unsigned> {
		auto start = std::make_reverse_iterator( m_s + m_len ), end = std::make_reverse_iterator( m_s );
		if( const auto it = std::find( start, end, ch ); it != end ) {
			return toOffset( it.base() ) - 1u;
		}
		return std::nullopt;
	}

	[[nodiscard]]
	auto indexOf( const wsw::StringView &that ) const -> std::optional<unsigned> {
		if( const char *p = std::search( m_s, m_s + m_len, that.m_s, that.m_s + that.m_len ); p != m_s + m_len ) {
			return toOffset( p );
		}
		return std::nullopt;
	}

	[[nodiscard]]
	auto lastIndexOf( const wsw::StringView &that ) const -> std::optional<unsigned> {
		auto start = std::make_reverse_iterator( m_s + m_len );
		auto end = std::make_reverse_iterator( m_s );
		auto thatStart = std::make_reverse_iterator( that.m_s + that.m_len );
		auto thatEnd = std::make_reverse_iterator( that.m_s );
		if( const auto it = std::search( start, end, thatStart, thatEnd ); it != end ) {
			return toOffset( it.base() ) - that.m_len;
		}
		return std::nullopt;
	}

	[[nodiscard]]
	auto indexOf( const wsw::CharLookup &lookup ) const -> std::optional<unsigned> {
		for( unsigned i = 0; i < m_len; ++i ) {
			if( lookup( m_s[i] ) ) {
				return i;
			}
		}
		return std::nullopt;
	}

	[[nodiscard]]
	auto lastIndexOf( const wsw::CharLookup &lookup ) const -> std::optional<unsigned> {
		auto iLast = m_len;
		for( unsigned i = m_len; i <= iLast; iLast = i, i-- ) {
			if( lookup( m_s[i] ) ) {
				return i;
			}
		}
		return std::nullopt;
	}

	[[nodiscard]]
	bool contains( char ch ) const {
		return indexOf( ch ).has_value();
	}

	[[nodiscard]]
	bool contains( const wsw::StringView &that ) const {
		return indexOf( that ).has_value();
	}

	[[nodiscard]]
	bool containsAny( const wsw::StringView &chars ) const {
		CharLookup lookup( chars );
		return std::find_if( m_s, m_s + m_len, lookup ) != m_s + m_len;
	}

	[[nodiscard]]
	bool containsOnly( const wsw::StringView &chars ) const {
		CharLookup lookup( chars );
		return std::find_if_not( m_s, m_s + m_len, lookup ) == m_s + m_len;
	}

	[[nodiscard]]
	bool containsAll( const wsw::StringView &chars ) const {
		return chars.containsOnly( *this );
	}

	[[nodiscard]]
	bool startsWith( char ch ) const {
		return m_len && m_s[0] == ch;
	}

	[[nodiscard]]
	bool endsWith( char ch ) const {
		return m_len && m_s[m_len - 1] == ch;
	}

	[[nodiscard]]
	bool startsWith( const wsw::StringView &prefix ) const {
		return prefix.length() <= m_len && !memcmp( m_s, prefix.m_s, prefix.length() );
	}

	[[nodiscard]]
	bool endsWith( const wsw::StringView &suffix ) const {
		return suffix.length() <= m_len && !memcmp( m_s + m_len - suffix.length(), suffix.m_s, suffix.length() );
	}

	[[nodiscard]]
	auto trimLeft() const -> wsw::StringView {
		const char *p = std::find_if( m_s, m_s + m_len, []( char arg ) { return !std::isspace( arg ); } );
		return wsw::StringView( p, m_len - ( p - m_s ), (Terminated)m_terminated );
	}

	[[nodiscard]]
	auto trimLeft( char ch ) const -> wsw::StringView {
		const char *p = std::find_if( m_s, m_s + m_len, [=]( char arg ) { return arg != ch; });
		return wsw::StringView( p, m_len - ( p - m_s ), (Terminated)m_terminated );
	}

	[[nodiscard]]
	auto trimLeft( const wsw::StringView &chars ) const -> wsw::StringView {
		CharLookup lookup( chars );
		const char *p = std::find_if_not( m_s, m_s + m_len, lookup );
		return wsw::StringView( p, m_len - ( p - m_s ), (Terminated)m_terminated );
	}

	[[nodiscard]]
	auto trimRight() const -> wsw::StringView {
		auto start = std::make_reverse_iterator( m_s + m_len ), end = std::make_reverse_iterator( m_s );
		auto it = std::find_if( start, end, []( char arg ) { return !std::isspace( arg ); } );
		const char *p = it.base();
		Terminated terminated = ( m_terminated && p == m_s + m_len ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( m_s, p - m_s, terminated );
	}

	[[nodiscard]]
	auto trimRight( char ch ) const -> wsw::StringView {
		auto start = std::make_reverse_iterator( m_s + m_len ), end = std::make_reverse_iterator( m_s );
		auto it = std::find_if( start, end, [=]( char arg ) { return arg != ch; });
		const char *p = it.base();
		Terminated terminated = ( m_terminated && p == m_s + m_len ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( m_s, p - m_s, terminated );
	}

	[[nodiscard]]
	auto trimRight( const wsw::StringView &chars ) const -> wsw::StringView {
		CharLookup lookup( chars );
		auto begin = std::make_reverse_iterator( m_s + m_len ), end = std::make_reverse_iterator( m_s );
		auto it = std::find_if_not( begin, end, lookup );
		Terminated terminated = ( m_terminated && it == begin ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( m_s, it.base() - m_s, terminated );
	}

	[[nodiscard]]
	auto trim() const -> wsw::StringView {
		return trimLeft().trimRight();
	}

	[[nodiscard]]
	auto trim( char ch ) const -> wsw::StringView {
		return trimLeft( ch ).trimRight( ch );
	}

	[[nodiscard]]
	auto trim( const wsw::StringView &chars ) const -> wsw::StringView {
		CharLookup lookup( chars );
		const char *left = std::find_if_not( m_s, m_s + m_len, lookup );
		if( left == m_s + m_len ) {
			return wsw::StringView();
		}

		auto begin = std::make_reverse_iterator( m_s + m_len ), end = std::make_reverse_iterator( m_s );
		auto it = std::find_if_not( begin, end, lookup );
		const char *right = it.base();
		Terminated terminated = ( m_terminated && right == m_s + m_len ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( left, right - left, terminated );
	}

	[[nodiscard]]
	auto take( size_t n ) const -> wsw::StringView {
		Terminated terminated = m_terminated && n >= m_len ? ZeroTerminated : Unspecified;
		return wsw::StringView( m_s, std::min( m_len, n ), terminated );
	}

	[[nodiscard]]
	auto takeExact( size_t n ) const -> std::optional<wsw::StringView> {
		if( n <= m_len ) {
			Terminated terminated = m_terminated && n == m_len ? ZeroTerminated : Unspecified;
			return wsw::StringView( m_s, n, terminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	auto takeWhile( Predicate predicate ) const -> wsw::StringView {
		const char *p = std::find_if_not( m_s, m_s + m_len, predicate );
		Terminated terminated = m_terminated && p == m_s + m_len ? ZeroTerminated : Unspecified;
		return wsw::StringView( m_s, p - m_s, terminated );
	}

	[[nodiscard]]
	auto drop( size_t n ) const -> wsw::StringView {
		size_t prefixLen = std::min( n, m_len );
		return wsw::StringView( m_s + prefixLen, m_len - prefixLen, (Terminated)m_terminated );
	}

	[[nodiscard]]
	auto dropExact( size_t n ) const -> std::optional<wsw::StringView> {
		if( n <= m_len ) {
			return wsw::StringView( m_s + n, m_len - n, (Terminated)m_terminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	auto dropWhile( Predicate predicate ) const -> wsw::StringView {
		const char *p = std::find_if_not( m_s, m_s + m_len, predicate );
		return wsw::StringView( p, m_len - ( p - m_s ), (Terminated)m_terminated );
	}

	[[nodiscard]]
	auto takeRight( size_t n ) const -> wsw::StringView {
		size_t suffixLen = std::min( n, m_len );
		return wsw::StringView( m_s + m_len - suffixLen, suffixLen, (Terminated)m_terminated );
	}

	[[nodiscard]]
	auto takeRightExact( size_t n ) const -> std::optional<wsw::StringView> {
		if( n <= m_len ) {
			return wsw::StringView( m_s + m_len - n, n, (Terminated)m_terminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	auto takeRightWhile( Predicate predicate ) const -> wsw::StringView {
		auto begin = std::make_reverse_iterator( m_s + m_len ), end = std::make_reverse_iterator( m_s );
		auto it = std::find_if_not( begin, end, predicate );
		const char *p = it.base();
		return wsw::StringView( p, m_len - ( p - m_s ), (Terminated)m_terminated );
	}

	[[nodiscard]]
	auto dropRight( size_t n ) const -> wsw::StringView {
		Terminated terminated = ( m_terminated && n == 0 ) ? ZeroTerminated : Unspecified;
		return wsw::StringView( m_s, m_len - std::min( n, m_len ), terminated );
	}

	[[nodiscard]]
	auto dropRightExact( size_t n ) const -> std::optional<wsw::StringView> {
		if( n <= m_len ) {
			Terminated terminated = m_terminated && n == 0 ? ZeroTerminated : Unspecified;
			return wsw::StringView( m_s, m_len - n, terminated );
		}
		return std::nullopt;
	}

	template <typename Predicate>
	[[nodiscard]]
	auto dropRightWhile( Predicate predicate ) const -> wsw::StringView {
		auto begin = std::make_reverse_iterator( m_s + m_len ), end = std::make_reverse_iterator( m_s );
		auto it = std::find_if_not( begin, end, predicate );
		const char *p = it.base();
		Terminated terminated = m_terminated && p == m_s + m_len ? ZeroTerminated : Unspecified;
		return wsw::StringView( m_s, (unsigned)( p - m_s ), terminated );
	}

	[[nodiscard]]
	auto takeMid( size_t index, size_t n ) const -> wsw::StringView {
		assert( index < m_len );
		Terminated terminated = m_terminated && index + n >= m_len ? ZeroTerminated : Unspecified;
		return wsw::StringView( m_s + index, std::min( n, m_len - index ), terminated );
	}

	[[nodiscard]]
	auto takeMidExact( size_t index, size_t n ) const -> std::optional<wsw::StringView> {
		assert( index < m_len );
		if( index + n <= m_len ) {
			Terminated terminated = m_terminated && index + n == m_len ? ZeroTerminated : Unspecified;
			return wsw::StringView( m_s + index, n, terminated );
		}
		return std::nullopt;
	}

	[[nodiscard]]
	auto dropMid( size_t index, size_t n ) const -> std::pair<wsw::StringView, wsw::StringView> {
		assert( index < m_len );
		wsw::StringView left( m_s, index );
		const auto off2 = std::min( index + n, m_len );
		const auto len2 = m_len - off2;
		wsw::StringView right( m_s + off2, len2, m_terminated ? ZeroTerminated : Unspecified );
		assert( left.length() + std::min( n, m_len - index ) + right.length() == m_len );
		return std::make_pair( left, right );
	}

	[[nodiscard]]
	auto dropMidExact( size_t index, size_t n ) const -> std::optional<std::pair<wsw::StringView, wsw::StringView>> {
		assert( index < m_len );
		if( index + n > m_len ) {
			return std::nullopt;
		}
		wsw::StringView left( m_s, index );
		const auto off2 = index + n;
		const auto len2 = m_len - off2;
		wsw::StringView right( m_s + off2, len2, m_terminated ? ZeroTerminated : Unspecified );
		assert( left.length() + n + right.length() == m_len );
		return std::make_pair( left, right );
	}

	void copyTo( char *buffer, size_t bufferSize ) const {
		assert( bufferSize > m_len );
		memcpy( buffer, m_s, m_len );
		buffer[m_len] = '\0';
	}

	template <size_t N>
	void copyTo( char buffer[N] ) const {
		copyTo( buffer, N );
	}
};

inline CharLookup::CharLookup( const wsw::StringView &chars ) {
	std::memset( m_data, 0, sizeof( m_data ) );
	for( char ch: chars ) {
		m_data[(unsigned char)ch] = true;
	}
}

/**
 * An extension of {@code StringView} that stores a value of a case-insensitive hash code in addition.
 */
class HashedStringView : public StringView {
protected:
	uint32_t m_hash;
public:
	constexpr HashedStringView() : StringView(), m_hash( 0 ) {}

	explicit HashedStringView( const char *s ) : StringView( s ) {
		m_hash = getHashForLength( s, m_len );
	}

	HashedStringView( const char *s, size_t len, Terminated terminated = Unspecified )
		: StringView( s, len, terminated ) {
		m_hash = getHashForLength( s, len );
	}

	HashedStringView( const char *s, size_t len, uint32_t hash, Terminated terminated = Unspecified )
		: StringView( s, len, terminated ), m_hash( hash ) {}

	explicit HashedStringView( const wsw::StringView &that )
		: StringView( that.data(), that.size(), that.isZeroTerminated() ? ZeroTerminated : Unspecified ) {
		m_hash = getHashForLength( m_s, m_len );
	}

	[[nodiscard]]
	auto getHash() const -> uint32_t { return m_hash; }

	[[nodiscard]]
	bool equals( const wsw::HashedStringView &that ) const {
		return m_hash == that.m_hash && m_len == that.m_len && !std::strncmp( m_s, that.m_s, m_len );
	}

	[[nodiscard]]
	bool equalsIgnoreCase( const wsw::HashedStringView &that ) const {
		return m_hash == that.m_hash && m_len == that.m_len && !Q_strnicmp( m_s, that.m_s, m_len );
	}

	bool operator==( const wsw::HashedStringView &that ) const { return equals( that ); }
	bool operator!=( const wsw::HashedStringView &that ) const { return !equals( that ); }
};

inline constexpr auto operator "" _asView( const char *s, std::size_t len ) -> wsw::StringView {
	return len ? wsw::StringView( s, len, wsw::StringView::ZeroTerminated ) : wsw::StringView();
}

inline auto operator "" _asHView( const char *s, std::size_t len ) -> wsw::HashedStringView {
	return len ? wsw::HashedStringView( s, len, wsw::StringView::ZeroTerminated ) : wsw::HashedStringView();
}

}

#endif
