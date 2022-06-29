#include "configstringstoragetest.h"

#include "../configstringstorage.h"

static constexpr unsigned kMaxStrings = wsw::ConfigStringStorage::kMaxStrings;

void ConfigStringStorageTest::test_setEmptyString() {
	wsw::ConfigStringStorage storage;
}

void ConfigStringStorageTest::test_setShortString() {
	wsw::ConfigStringStorage storage;
}

void ConfigStringStorageTest::test_setShortString_overShortString() {
	wsw::ConfigStringStorage storage;
	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		storage.set( i, wsw::StringView() );
		QCOMPARE( storage.get( i ), std::nullopt );
	}
}

void ConfigStringStorageTest::test_setLongString() {
	wsw::ConfigStringStorage storage;
	std::string longStringBuffer;
	for( int i = 0; i < 8192; ++i ) {
		longStringBuffer.push_back( 'A' + ( i % 26 ) );
	}

	const wsw::StringView longString( longStringBuffer.data(), longStringBuffer.size() );
	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		storage.set( i, longString );
		QCOMPARE( storage.get( i ), std::optional( longString ) );
	}
}

void ConfigStringStorageTest::test_setLongString_overShortString() {
	wsw::ConfigStringStorage storage;
	const wsw::StringView shortString( "Hello, world!" );
	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		storage.set( i, shortString );
	}

	std::string longStringBuffer;
	for( unsigned i = 0; i < 2048; ++i ) {
		longStringBuffer.push_back( '0' + ( i % 10 ) );
	}

	const wsw::StringView longString( longStringBuffer.data(), longStringBuffer.size() );
	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		storage.set( i, wsw::StringView( longString.data(), longString.size() ) );
	}
	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		QCOMPARE( storage.get( i ), std::optional( longString ) );
	}
}

void ConfigStringStorageTest::test_setLongString_overLongString() {
	wsw::ConfigStringStorage storage;
	std::string longStringBuffer1;
	for( unsigned i = 0; i < 4096; ++i ) {
		longStringBuffer1.push_back( 'A' + ( i % 26 ) );
	}
	std::string longStringBuffer2;
	for( unsigned i = 0; i < 8192; ++i ) {
		longStringBuffer2.push_back( 'a' + ( i % 26 ) );
	}

	const wsw::StringView longString1( longStringBuffer1.data(), longStringBuffer1.size() );
	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		storage.set( i, longString1 );
		QCOMPARE( storage.get( i ), std::optional( longString1 ) );

		// Test reuse or reallocation behaviour
		wsw::StringView longString2( longStringBuffer2.data(), 2048 + 2048 * ( i % 4 ) );

		storage.set( i, longString2 );
		QCOMPARE( storage.get( i ), std::optional( longString2 ) );
	}
}

void ConfigStringStorageTest::test_setShortString_overLongString() {
	wsw::ConfigStringStorage storage;
	std::string longStringBuffer;
	for( unsigned i = 0; i < 2048; ++i ) {
		longStringBuffer.push_back( 'a' + ( i % 26 ) );
	}

	const wsw::StringView shortString( "Hello, world!" );
	const wsw::StringView longString( longStringBuffer.data(), longStringBuffer.size() );
	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		storage.set( i, longString );
		QCOMPARE( storage.get( i ), std::optional( longString ) );
		storage.set( i, shortString );
		QCOMPARE( storage.get( i ), std::optional( shortString ) );
	}
}

void ConfigStringStorageTest::test_clear() {
	wsw::ConfigStringStorage storage;

	const wsw::StringView someString( "Hello, world!" );
	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		storage.set( i, someString );
	}

	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		QVERIFY( storage.get( i ) != std::nullopt );
	}

	storage.clear();

	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		QCOMPARE( storage.get( i ), std::nullopt );
	}
}

void ConfigStringStorageTest::test_copyFrom() {
	wsw::ConfigStringStorage storage1, storage2;
	std::string longStringBuffer;
	for( unsigned i = 0; i < 2048; ++i ) {
		longStringBuffer.push_back( '0' + ( i % 10 ) );
	}

	const wsw::StringView shortString( "Hello, world!" );
	const wsw::StringView longString( longStringBuffer.data(), longStringBuffer.size() );
	for( unsigned i = 0; i < kMaxStrings; i += 2 ) {
		storage1.set( i, i % 4 ? longString : shortString );
	}

	storage2.copyFrom( storage1 );
	for( unsigned i = 0; i < kMaxStrings; ++i ) {
		QVERIFY( storage1.get( i ) == storage2.get( i ) );
	}
}

void *Q_malloc( size_t size ) {
	if( auto *p = ::calloc( size, 1 ) ) {
		return p;
	}
	wsw::failWithBadAlloc();
}

void *Q_realloc( void *p, size_t size ) {
	if( auto *newp = ::realloc( p, size ) ) {
		return newp;
	}
	wsw::failWithBadAlloc();
}

void Q_free( void *p ) {
	::free( p );
}