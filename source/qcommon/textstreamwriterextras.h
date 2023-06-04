#ifndef WSW_98ee64a0_2f51_4d25_a23a_ff1c85c9b8d9_H
#define WSW_98ee64a0_2f51_4d25_a23a_ff1c85c9b8d9_H

#include "textstreamwriter.h"

namespace wsw {

template <typename Chars>
struct unquoted {
	explicit constexpr unquoted( const Chars &chars ): chars( chars ) {}
	const Chars &chars;
};

template <typename T>
struct named {
	template <size_t N>
	[[nodiscard]]
	explicit constexpr named( const char ( &array )[N], const T &value ) : value( value ) {
		this->name    = array;
		this->nameLen = N && array[N - 1] == '\0' ? N - 1 : N;
	}
	const char *name;
	size_t nameLen;
	const T &value;
};

template <typename T>
struct noSep {
	[[nodiscard]]
	explicit constexpr noSep( const T &value ) : value( value ) {}
	const T &value;
};

template <std::integral T, unsigned Digits>
struct FormattedDecimal {
	[[nodiscard]]
	explicit constexpr FormattedDecimal( T value ) : value( value ) {}
	const T value;
};

template <std::integral T, unsigned Digits, bool Caps>
struct FormattedHexadecimal {
	[[nodiscard]]
	explicit constexpr FormattedHexadecimal( T value ) : value( value ) {}
	const T value;
};

template <unsigned Digits = 0, std::integral T>
[[nodiscard]]
constexpr auto ifmt( T value ) -> FormattedDecimal<T, Digits> {
	return FormattedDecimal<T, Digits>( value );
}

template <unsigned Digits = 0, std::integral T>
[[nodiscard]]
constexpr auto xfmt( T value ) -> FormattedHexadecimal<T, Digits, false> {
	return FormattedHexadecimal<T, Digits, false>( value );
}

template <unsigned Digits = 0, std::integral T>
[[nodiscard]]
constexpr auto Xfmt( T value ) -> FormattedHexadecimal<T, Digits, true> {
	return FormattedHexadecimal<T, Digits, true>( value );
}

template <typename Stream, typename Chars>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &streamWriter, unquoted<Chars> &&unquotedChars )
	-> TextStreamWriter<Stream> & {
	streamWriter.writeChars( unquotedChars.chars.data(), unquotedChars.chars.size() );
	return streamWriter;
}

template <typename Stream, typename T>
[[maybe_unused]]
wsw_noinline auto operator<<( TextStreamWriter<Stream> &streamWriter, named<T> &&namedValue )
	-> TextStreamWriter<Stream> & {
	streamWriter.writeChars( namedValue.name, namedValue.nameLen );
	streamWriter.hasPendingSeparator = false;
	streamWriter << '=';
	streamWriter.hasPendingSeparator = false;
	streamWriter << namedValue.value;
	return streamWriter;
}

template <typename Stream, typename T>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &streamWriter, noSep<T> &&noSeparatorsAroundTheValue )
	-> TextStreamWriter<Stream> & {
	// Dropping both separators (if any) is least confusing for users
	streamWriter.hasPendingSeparator = false;
	streamWriter << noSeparatorsAroundTheValue.value;
	streamWriter.hasPendingSeparator = false;
	return streamWriter;
}

template <typename Stream, unsigned Digits, std::integral T>
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &streamWriter, FormattedDecimal<T, Digits> &&value )
	-> TextStreamWriter<Stream> & {
	static_assert( Digits < 100 );
	char format[16], buffer[192];
	char *pformat = format;
	*pformat++ = '%';
	if( Digits > 0 ) {
		if( Digits >= 10 ) {
			*pformat++ = '0' + Digits / 10;
			*pformat++ = '0' + Digits % 10;
		} else {
			*pformat++ = '0' + Digits;
		}
	}
	if constexpr( sizeof( T ) <= 4 ) {
		if constexpr( std::is_signed_v<T> ) {
			*pformat++ = 'd';
		} else {
			*pformat++ = 'u';
		}
	} else {
		const char *spec;
		if constexpr( std::is_signed_v<T> ) {
			spec = PRIi64;
		} else {
			spec = PRIu64;
		}
		const char *pspec = spec;
		while( *pspec ) {
			*pformat++ = *pspec++;
		}
	}
	*pformat = '\0';
	const int res = snprintf( buffer, sizeof( buffer ), format, value );
	assert( res > 0 && res < (int)sizeof( buffer ) );
	streamWriter.writeChars( buffer, (size_t)res );
	return streamWriter;
}

template <typename Stream, unsigned Digits, bool Caps, std::integral T>
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &streamWriter, FormattedHexadecimal<T, Digits, Caps> &&value )
	-> TextStreamWriter<Stream> & {
	static_assert( Digits < 100 );
	char format[16], buffer[192];
	char *pformat = format;
	*pformat++ = '%';
	if( Digits > 0 ) {
		if( Digits >= 10 ) {
			*pformat++ = '0' + Digits / 10;
			*pformat++ = '0' + Digits % 10;
		} else {
			*pformat++ = '0' + Digits;
		}
	}
	if constexpr( sizeof( T ) <= 4 ) {
		if constexpr( Caps ) {
			*pformat++ = 'X';
		} else {
			*pformat++ = 'x';
		}
	} else {
		const char *spec;
		if constexpr( Caps ) {
			spec = PRIX64;
		} else {
			spec = PRIx64;
		}
		const char *pspec = spec;
		while( *pspec ) {
			*pformat++ = *pspec++;
		}
	}
	*pformat = '\0';
	const int res = snprintf( buffer, sizeof( buffer ), format, value );
	assert( res > 0 && res < (int)sizeof( buffer ) );
	streamWriter.writeChars( buffer, (size_t)res );
	return streamWriter;
}

}

#endif