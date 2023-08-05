#ifndef WSW_9595d89d_d3d4_4235_b7a4_608538f78cc9_H
#define WSW_9595d89d_d3d4_4235_b7a4_608538f78cc9_H

#include <cassert>
#include <cstdint>
#include <cinttypes>
#include <limits>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <system_error>

#include "wswbasicarch.h"

namespace wsw {

template <typename Stream>
class TextStreamWriter {
protected:
	Stream *const m_stream;

	// TODO: It's unclear what to do in case of errors.
	// Throwing an exception is definitely not an appropriate choice.
	// We can just set some hadError flag so callers that care can handle that.

	template <typename T>
	wsw_forceinline void writeFloatingPointValue( T value ) {
		static_assert( std::is_same_v<std::remove_cvref_t<T>, float> || std::is_same_v<std::remove_cvref_t<T>, double> );
		const size_t separatorLen  = ( hasPendingSeparator ? 1 : 0 );
		// TODO: Is it sufficient for edge cases?
		constexpr size_t numberLen = std::is_same_v<std::remove_cvref<T>, float> ? 32 : 96;
		const size_t sizeToReserve = separatorLen + numberLen + 1;
		if( char *bufferChars = m_stream->reserve( sizeToReserve ) ) [[likely]] {
			bufferChars[0] = separatorChar;
			int res;
			// Unfortunately, we have to fall back to snprintf due to
			// a lack of full std::to_chars/std::format() support
			if constexpr( std::is_same_v<std::remove_cvref_t<T>, double> ) {
				res = snprintf( bufferChars + separatorLen, numberLen + 1, "%lf", value );
			} else {
				res = snprintf( bufferChars + separatorLen, numberLen + 1, "%f", value );
			}
			if( res > 0 && (size_t)res <= numberLen ) {
				const size_t charsWritten = (size_t)res + separatorLen;
				m_stream->advance( charsWritten );
				hasPendingSeparator = usePendingSeparators;
			}
		}
	}

	template <typename T>
	wsw_forceinline void writeIntegralValue( T value ) {
		const size_t separatorLen  = ( hasPendingSeparator ? 1 : 0 );
		constexpr size_t numberLen = 32; // TODO???
		const size_t sizeToReserve = numberLen + separatorLen;
		if( char *const bufferChars = m_stream->reserve( sizeToReserve ) ) [[likely]] {
			bufferChars[0] = separatorChar;
			char *const toCharsBegin = bufferChars + separatorLen;
			char *const toCharsEnd  = bufferChars + separatorLen + numberLen;
			const auto [ptr, err] = std::to_chars( toCharsBegin, toCharsEnd, value );
			if( err == std::errc() ) [[likely]] {
				m_stream->advance( (size_t)( ptr - toCharsBegin ) + separatorLen );
				hasPendingSeparator = usePendingSeparators;
			}
		}
	}

public:
	explicit TextStreamWriter( Stream *stream ) : m_stream( stream ) {}

	TextStreamWriter( const TextStreamWriter & ) = delete;
	TextStreamWriter( TextStreamWriter && ) = delete;
	auto operator=( const TextStreamWriter & ) = delete;
	auto operator=( TextStreamWriter && ) = delete;

	// We try generating non-inlined implementations to suppress call sites code bloat
	// Note: these methods are made public to avoid hassle with declaring global operators as friends.

	wsw_noinline void writeFloat( float value ) { writeFloatingPointValue( value ); }
	wsw_noinline void writeDouble( double value ) { writeFloatingPointValue( value ); }

	wsw_noinline void writeInt8( int8_t value ) { writeIntegralValue( value ); }
	wsw_noinline void writeInt16( int16_t value ) { writeIntegralValue( value ); }
	wsw_noinline void writeInt32( int32_t value ) { writeIntegralValue( value ); }
	wsw_noinline void writeInt64( int64_t value ) { writeIntegralValue( value ); }
	wsw_noinline void writeUInt8( uint8_t value ) { writeIntegralValue( value ); }
	wsw_noinline void writeUInt16( uint16_t value ) { writeIntegralValue( value ); }
	wsw_noinline void writeUInt32( uint32_t value ) { writeIntegralValue( value ); }
	wsw_noinline void writeUInt64( uint64_t value ) { writeIntegralValue( value ); }

	wsw_noinline void writeBool( bool value ) {
		size_t valueLen;
		const char *valueChars;
		if( value ) {
			valueLen = 4;
			valueChars = "true";
		} else {
			valueLen = 5;
			valueChars = "false";
		}
		const size_t separatorLen = ( hasPendingSeparator ? 1 : 0 );
		const size_t charsToWrite = separatorLen + valueLen;
		if( char *const bufferChars = m_stream->reserve( charsToWrite ) ) [[likely]] {
			bufferChars[0] = separatorChar;
			std::memcpy( bufferChars + separatorLen, valueChars, valueLen );
			m_stream->advance( charsToWrite );
			hasPendingSeparator = usePendingSeparators;
		}
	}

	wsw_noinline void writeChar( char value ) {
		const size_t separatorLen = ( hasPendingSeparator ? 1 : 0 );
		const size_t charsToWrite = separatorLen + 1;
		if( char *const bufferChars = m_stream->reserve( charsToWrite ) ) [[likely]] {
			bufferChars[0] = separatorChar;
			bufferChars[separatorLen] = value;
			m_stream->advance( charsToWrite );
			hasPendingSeparator = usePendingSeparators;
		}
	}

	wsw_noinline void writePtr( const void *value ) {
		const size_t separatorLen = ( hasPendingSeparator ? 1 : 0 );
		// The required length for 64-bit architectures is actually lesser
		// but this is too hard to handle in a robust fashion.
		const size_t addressCharsToReserve = 2 + ( ( sizeof( void * ) == 8 ) ? 16 : 8 );
		if( char *const bufferChars = m_stream->reserve( separatorLen + addressCharsToReserve ) ) [[likely]] {
			bufferChars[0] = separatorChar;
			// Let "%p" cut off unused address bits
			const int res = snprintf( bufferChars + separatorLen, addressCharsToReserve, "%p", value );
			assert( res > 0 );
			m_stream->advance( separatorLen + res );
			hasPendingSeparator = usePendingSeparators;
		}
	}

	char separatorChar { ' ' };
	char quotesChar { '\'' };
	bool hasPendingSeparator { false };
	bool usePendingSeparators { true };

	wsw_noinline void writeChars( const char *chars, size_t numGivenChars ) {
		if( numGivenChars ) [[likely]] {
			const size_t separatorLen  = ( hasPendingSeparator ? 1 : 0 );
			const size_t charsToWrite  = separatorLen + numGivenChars;
			if( char *bufferChars = m_stream->reserve( charsToWrite ) ) [[likely]] {
				bufferChars[0] = separatorChar;
				std::memcpy( bufferChars + separatorLen, chars, numGivenChars );
				m_stream->advance( charsToWrite );
				hasPendingSeparator = usePendingSeparators;
			}
		}
	}

	wsw_noinline void writeQuotedChars( const char *chars, size_t numGivenChars ) {
		if( numGivenChars ) [[likely]] {
			const size_t separatorLen = ( hasPendingSeparator ? 1 : 0 );
			const size_t charsToWrite = separatorLen + numGivenChars + 2;
			if( char *bufferChars = m_stream->reserve( charsToWrite ) ) [[likely]] {
				bufferChars[0] = separatorChar;
				bufferChars[separatorLen] = quotesChar;
				std::memcpy( bufferChars + separatorLen + 1, chars, numGivenChars );
				bufferChars[separatorLen + 1 + numGivenChars] = quotesChar;
				m_stream->advance( charsToWrite );
				hasPendingSeparator = usePendingSeparators;
			}
		}
	}
};

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, char value ) -> TextStreamWriter<Stream> & {
	writer.writeChar( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, int8_t value ) -> TextStreamWriter<Stream> & {
	writer.writeInt8( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, int16_t value ) -> TextStreamWriter<Stream> & {
	writer.writeInt16( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, int32_t value ) -> TextStreamWriter<Stream> & {
	writer.writeInt32( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, int64_t value ) -> TextStreamWriter<Stream> & {
	writer.writeInt64( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, uint8_t value ) -> TextStreamWriter<Stream> & {
	writer.writeUInt8( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, uint16_t value ) -> TextStreamWriter<Stream> & {
	writer.writeUInt16( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, uint32_t value ) -> TextStreamWriter<Stream> & {
	writer.writeUInt32( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, uint64_t value ) -> TextStreamWriter<Stream> & {
	writer.writeUInt64( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, float value ) -> TextStreamWriter<Stream> & {
	writer.writeFloat( value ); return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, double value ) -> TextStreamWriter<Stream> & {
	writer.writeDouble( value ); return writer;
}

// https://stackoverflow.com/a/74922953
struct StringLiteral {
private:
	[[nodiscard]]
	static consteval auto trimTrailingZeros( const char *s, size_t n ) -> size_t {
		while( n && s[n - 1] == '\0' ) {
			n--;
		}
		return n;
	}
public:
	template<class T, std::size_t N, std::enable_if_t<std::is_same_v<T, const char>>...>
	consteval StringLiteral( T ( &chars )[N] ) : data( chars ), length( trimTrailingZeros( chars, N ) ) {}

	const char *const data;
	const size_t length;
};

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, const StringLiteral &literal ) -> TextStreamWriter<Stream> & {
	if( literal.length ) [[likely]] {
		writer.writeChars( literal.data, literal.length );
	}
	return writer;
}

template <typename Stream, typename Chars>
	requires
		requires( const Chars &ch ) {
		{ ch.data() } -> std::same_as<const char *>;
		{ ch.size() } -> std::integral;
	}
[[maybe_unused]]
wsw_noinline auto operator<<( TextStreamWriter<Stream> &writer, const Chars &chars ) -> TextStreamWriter<Stream> & {
	writer.writeQuotedChars( chars.data(), chars.size() );
	return writer;
}

template <typename Stream>
[[maybe_unused]]
wsw_forceinline auto operator<<( TextStreamWriter<Stream> &writer, TextStreamWriter<Stream> &(*fn)( TextStreamWriter<Stream> & ) ) -> TextStreamWriter<Stream> & {
	fn( writer );
	return writer;
}

}

#endif
