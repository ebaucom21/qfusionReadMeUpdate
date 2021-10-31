#ifndef WSW_9595d89d_d3d4_4235_b7a4_608538f78cc9_H
#define WSW_9595d89d_d3d4_4235_b7a4_608538f78cc9_H

#include <cstdint>
#include <cinttypes>
#include <limits>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <system_error>

#ifdef _MSC_VER
#define wsw_forceinline __forceinline
#define wsw_noinline __declspec( noinline )
#else
#define wsw_forceinline __attribute__( ( always_inline ) )
#define wsw_noinline __attribute__( ( noinline ) )
#endif

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
		using Limits = std::numeric_limits<T>;
		const size_t separatorLen  = ( hasPendingSeparator ? 1 : 0 );
		const size_t sizeToReserve = separatorLen + Limits::max_digits10 + 1;
		if( char *bufferChars = m_stream->reserve( sizeToReserve ) ) [[likely]] {
			bufferChars[0] = separatorChar;
			int res;
			// Unfortunately, we have to fall back to snprintf due to
			// a lack of full std::to_chars/std::format() support
			if constexpr( std::is_same_v<std::remove_reference_t<T>, long double> ) {
				res = snprintf( bufferChars + separatorLen, Limits::max_digits10 + 1, "%Lf", value );
			} else if constexpr( std::is_same_v<std::remove_reference_t<T>, double> ) {
				res = snprintf( bufferChars + separatorLen, Limits::max_digits10 + 1, "%lf", value );
			} else {
				res = snprintf( bufferChars + separatorLen, Limits::max_digits10 + 1, "%f", value );
			}
			if( res > 0 && (size_t)res <= Limits::max_digits10 ) {
				const size_t charsWritten = (size_t)res + separatorLen;
				m_stream->advance( charsWritten );
				hasPendingSeparator = usePendingSeparators;
			}
		}
	}

	// We try generating non-inlined implementations to suppress call sites code bloat

	wsw_noinline void writeFloat( float value ) { writeFloatingPointValue( value ); }
	wsw_noinline void writeDouble( double value ) { writeFloatingPointValue( value ); }
	wsw_noinline void writeLongDouble( long double value ) { writeFloatingPointValue( value ); }

	template <typename T>
	wsw_forceinline void writeIntegralValue( T value ) {
		using Limits = std::numeric_limits<T>;
		const size_t separatorLen = ( hasPendingSeparator ? 1 : 0 );
		const size_t sizeToReserve = Limits::digits10 + separatorLen;
		if( char *const bufferChars = m_stream->reserve( sizeToReserve ) ) [[likely]] {
			bufferChars[0] = separatorChar;
			char *const toCharsBegin = bufferChars + separatorLen;
			char *const toCharsEnd  = bufferChars + separatorLen + Limits::digits10;
			const auto [ptr, err] = std::to_chars( toCharsBegin, toCharsEnd, value );
			if( err == std::errc() ) [[likely]] {
				m_stream->advance( (size_t)( ptr - toCharsBegin ) + separatorLen );
				hasPendingSeparator = usePendingSeparators;
			}
		}
	}

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

public:
	char separatorChar { ' ' };
	bool hasPendingSeparator { false };
	bool usePendingSeparators { true };

	TextStreamWriter( const TextStreamWriter & ) = delete;
	TextStreamWriter( TextStreamWriter && ) = delete;
	auto operator=( const TextStreamWriter & ) = delete;
	auto operator=( TextStreamWriter && ) = delete;

	explicit TextStreamWriter( Stream *stream ) : m_stream( stream ) {}

	[[maybe_unused]]
	wsw_forceinline auto operator<<( bool value ) -> TextStreamWriter & {
		writeBool( value ); return *this;
	}

	[[maybe_unused]]
	wsw_forceinline auto operator<<( char value ) -> TextStreamWriter & {
		writeChar( value ); return *this;
	}

	[[maybe_unused]]
	wsw_forceinline auto operator<<( int8_t value ) -> TextStreamWriter & {
		writeInt8( value ); return *this;
	}
	[[maybe_unused]]
	wsw_forceinline auto operator<<( int16_t value ) -> TextStreamWriter & {
		writeInt16( value ); return *this;
	}
	[[maybe_unused]]
	wsw_forceinline auto operator<<( int32_t value ) -> TextStreamWriter & {
		writeInt32( value ); return *this;
	}
	[[maybe_unused]]
	wsw_forceinline auto operator<<( int64_t value ) -> TextStreamWriter & {
		writeInt8( value ); return *this;
	}

	[[maybe_unused]]
	wsw_forceinline auto operator<<( uint8_t value ) -> TextStreamWriter & {
		writeUInt8( value ); return *this;
	}
	[[maybe_unused]]
	wsw_forceinline auto operator<<( uint16_t value ) -> TextStreamWriter & {
		writeUInt16( value ); return *this;
	}
	[[maybe_unused]]
	wsw_forceinline auto operator<<( uint32_t value ) -> TextStreamWriter & {
		writeUInt32( value ); return *this;
	}
	[[maybe_unused]]
	wsw_forceinline auto operator<<( uint64_t value ) -> TextStreamWriter & {
		writeUInt64( value ); return *this;
	}

	[[maybe_unused]]
	wsw_forceinline auto operator<<( float value ) -> TextStreamWriter & {
		writeFloat( value ); return *this;
	}
	[[maybe_unused]]
	wsw_forceinline auto operator<<( double value ) -> TextStreamWriter & {
		writeDouble( value ); return *this;
	}
	[[maybe_unused]]
	wsw_forceinline auto operator<<( long double value ) -> TextStreamWriter & {
		writeLongDouble( value ); return *this;
	}

	[[maybe_unused]]
	wsw_forceinline auto operator<<( const void *value ) -> TextStreamWriter & {
		writePtr( value ); return *this;
	}

	template <size_t N>
	[[maybe_unused]]
	wsw_forceinline auto operator<<( const char ( &array )[N] ) -> TextStreamWriter & {
		if( N ) {
			// Protect from adding \0 to the output. This only covers the prevalent case.
			if( array[N - 1] == '\0' ) [[likely]] {
				writeChars( array, N - 1 );
			} else {
				writeChars( array, N );
			}
		}
		return *this;
	}

	template <typename Chars>
	[[maybe_unused]]
	wsw_forceinline auto operator<<( const Chars &chars ) -> TextStreamWriter & {
		writeChars( chars.data(), chars.size() ); return *this;
	}

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
};

}

#endif
