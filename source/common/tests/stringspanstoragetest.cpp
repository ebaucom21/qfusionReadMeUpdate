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

void StringSpanStorageTest::initTestCase() {
	// Make sure the internal buffer does not get reallocated
	// so std::string instances we push do not get destructed
	// and reconstructed again with same data but different addresses
	m_referenceStringsStorage.reserve( kLimit );
	for( int num = 0; num < kLimit; ++num ) {
		wsw::StaticString<16> buffer( "%d", num );
		m_referenceStringsStorage.emplace_back( std::string( buffer.data(), buffer.size() ) );
		const std::string &backString = m_referenceStringsStorage.back();
		m_referenceStrings.emplace_back( wsw::StringView( backString.data(), backString.size() ) );
	}
}

template <typename Storage>
void StringSpanStorageTest::performGenericTest( Storage &storage ) {
	assert( !m_referenceStrings.empty() );
	// Make sure it stable in regard to .clear() calls
	for( int clearPass = 0; clearPass < 2; ++clearPass ) {
		storage.clear();
		for( const wsw::StringView &reference: m_referenceStrings ) {
			storage.add( reference );
		}
		QVERIFY( storage.end() - storage.begin() == kLimit );
		for( int num = 0; num < kLimit; ++num ) {
			QCOMPARE( storage[num], m_referenceStrings[num] );
		}
		int num = 0;
		for( const wsw::StringView &view: storage ) {
			QCOMPARE( view, m_referenceStrings[num] );
			num++;
		}
	}
}

template <typename Storage>
void StringSpanStorageTest::performPopBackTest( Storage &storage ) {
	for( int clearPass = 0; clearPass < 2; ++clearPass ) {
		storage.clear();
		for( const wsw::StringView &reference: m_referenceStrings ) {
			storage.add( reference );
		}
		while( !storage.empty() ) {
			storage.pop_back();
			for( int num = 0; num < (int)storage.size(); ++num ) {
				QCOMPARE( storage[num], m_referenceStrings[num] );
			}
		}
		// Test interleaved add() and pop_back() calls
		assert( m_referenceStrings.size() % 2 == 0 );
		for( int num = 0; num < m_referenceStrings.size(); num += 2 ) {
			storage.add( m_referenceStrings[num + 0] );
			storage.add( m_referenceStrings[num + 1] );
			storage.pop_back();
			QCOMPARE( storage.back(), m_referenceStrings[num] );
		}
	}
}

void StringSpanStorageTest::test_stdBasedStorage() {
	using CharBuffer = std::string;
	using SpanBuffer = std::vector<InternalSpan>;
	wsw::StringSpanStorage<Off, Len, CharBuffer, InternalSpan, SpanBuffer> storage;
	performGenericTest( storage );
	performPopBackTest( storage );
}

void StringSpanStorageTest::test_qtBasedStorage() {
	using CharBuffer = QByteArray;
	using SpanBuffer = QVector<InternalSpan>;
	wsw::StringSpanStorage<Off, Len, CharBuffer, InternalSpan, SpanBuffer> storage;
	performGenericTest( storage );
	// QByteArray is structurally incompatible with Storage::pop_back() code
}

void StringSpanStorageTest::test_staticBasedStorage() {
	constexpr auto SpansCapacity = (size_t)kLimit;
	constexpr auto CharsCapacity = (size_t)( kLimit * 16 );
	wsw::StringSpanStaticStorage<Off, Len, SpansCapacity, CharsCapacity> storage;
	performGenericTest( storage );
	performPopBackTest( storage );
}

