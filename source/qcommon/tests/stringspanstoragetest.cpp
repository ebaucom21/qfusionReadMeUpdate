#include "stringspanstoragetest.h"

#ifndef Q_strnicmp
#define Q_strnicmp strncasecmp
#endif

#ifndef Q_vsnprintfz
#define Q_vsnprintfz snprintf
#endif

#include "../stringspanstorage.h"

#include <QByteArray>
#include <QVector>

using Off = unsigned;
using Len = uint8_t;
using InternalSpan = std::pair<Off, Len>;
constexpr int kLimit = 1000;

template <typename Storage>
void StringSpanStorageTest::performStorageTest( Storage &storage ) {
	// Make sure it stable in regard to .clear() calls
	for( int clearPass = 0; clearPass < 2; ++clearPass ) {
		for( int num = 0; num < kLimit; ++num ) {
			wsw::StaticString<16> buffer( "%d", num );
			storage.add( buffer.asView() );
		}
		for( int num = 0; num < kLimit; ++num ) {
			wsw::StaticString<16> buffer( "%d", num );
			QCOMPARE( storage[num], buffer.asView() );
		}
		QVERIFY( storage.end() - storage.begin() == kLimit );
		int num = 0;
		for( const wsw::StringView &view: storage ) {
			wsw::StaticString<16> buffer( "%d", num );
			QCOMPARE( view, buffer.asView() );
		}
		storage.clear();
	}
}

void StringSpanStorageTest::test_stdBasedStorage() {
	using CharBuffer = std::string;
	using SpanBuffer = std::vector<InternalSpan>;
	wsw::StringSpanStorage<Off, Len, CharBuffer, InternalSpan, SpanBuffer> storage;
	performStorageTest( storage );
}

void StringSpanStorageTest::test_qtBasedStorage() {
	using CharBuffer = QByteArray;
	using SpanBuffer = QVector<InternalSpan>;
	wsw::StringSpanStorage<Off, Len, CharBuffer, InternalSpan, SpanBuffer> storage;
	performStorageTest( storage );
}

void StringSpanStorageTest::test_staticBasedStorage() {
	constexpr auto SpansCapacity = (size_t)kLimit;
	constexpr auto CharsCapacity = (size_t)( kLimit * 16 );
	wsw::StringSpanStaticStorage<Off, Len, SpansCapacity, CharsCapacity> storage;
	performStorageTest( storage );
}

