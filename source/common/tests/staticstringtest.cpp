#include "staticstringtest.h"
#include <vector>

#ifndef Q_strnicmp
#define Q_strnicmp strncasecmp
#endif

#ifndef Q_vsnpritnfz
#define Q_vsnprintfz vsnprintf
#endif

#include "../wswstaticstring.h"

using wsw::operator""_asView;

template <unsigned N>
auto toQString( const wsw::StaticString<N> &s ) -> QString {
	auto result = QString::fromUtf8( s.data(), s.size() );
	auto z = QString::fromUtf8( s.data() );
	// Check zero-termination
	if ( result != z ) {
		printf( "from chars ptr and size: %s\n", result.toUtf8().constData() );
		printf( "from chars ptr: %s\n", z.toUtf8().constData() );
		abort();
	}
	return result;
}

void StaticStringTest::test_ctor_vararg() {
	wsw::StaticString<13> s( "%s, %s%c", "Hello", "world", '!' );
	QCOMPARE( toQString( s ), "Hello, world!" );
}

void StaticStringTest::test_push_back() {
	wsw::StaticString<32> s;
	s.push_back( 'H' );
	QCOMPARE( toQString( s ), "H" );
	s.push_back( '!' );
	QCOMPARE( toQString( s ), "H!" );
}

void StaticStringTest::test_pop_back() {
	wsw::StaticString<32> s( "Hello, world!"_asView );
	QCOMPARE( toQString( s ), "Hello, world!" );
	s.pop_back();
	QCOMPARE( toQString( s ), "Hello, world" );
}

void StaticStringTest::test_append() {
	wsw::StaticString<13> s;
	s.append( "Hello"_asView )
		.append( ""_asView )
		.append( ", "_asView )
		.append( ""_asView )
		.append( "world!"_asView );
	QCOMPARE( toQString( s ), "Hello, world!" );
}

void StaticStringTest::test_insert_container() {
	wsw::StaticString<13> s;
	s.append( "elloworld"_asView );
	s.insert( std::find( s.begin(), s.end(), 'o' ) + 1, ", "_asView );
	s.insert( 0u, "H"_asView );
	s.insert( s.size(), "!"_asView );
	s.insert( s.begin(), ""_asView )
		.insert( s.end(), ""_asView );
	QCOMPARE( toQString( s ), "Hello, world!" );
}

void StaticStringTest::test_erase_unbound() {
	{
		wsw::StaticString<13> s( "Hello, world!"_asView );
		s.erase( s.indexOf( ',' ).value() );
		QCOMPARE( toQString( s ), "Hello" );
	}
	{
		wsw::StaticString<13> s( "Hello, world!"_asView );
		s.erase( 0u );
		QVERIFY( s.empty() );
	}
	{
		wsw::StaticString<13> s( "Hello, world!"_asView );
		s.erase( s.size() );
		QCOMPARE( toQString( s ), "Hello, world!" );
	}
}

void StaticStringTest::test_erase_countSpecified() {
	{
		wsw::StaticString<13> s( "Hello, world!"_asView );
		s.erase( 0u, ~0u );
		QVERIFY( s.empty() );
	}
	{
		wsw::StaticString<13> s( "Hello, world!"_asView );
		s.erase( 0u, 5 ).erase( 0u, 2 );
		QCOMPARE( toQString( s ), "world!" );
	}
	{
		wsw::StaticString<13> s( "Hello, world!"_asView );
		s.erase( s.indexOf( ',' ).value(), ~0u );
		QCOMPARE( toQString( s ), "Hello" );
	}
	{
		wsw::StaticString<13> s( "Hello, world!"_asView );
		s.erase( s.size(), ~0u );
		QCOMPARE( toQString( s ), "Hello, world!" );
	}
	{
		wsw::StaticString<13> s( "Hello, world!"_asView );
		s.erase( s.indexOf( ',' ).value(), 2 );
		QCOMPARE( toQString( s ), "Helloworld!" );
	}
	{
		wsw::StaticString<13> s( "Hello, world!"_asView );
		s.erase( s.indexOf( 'w' ).value(), 6 )
			.erase( s.indexOf( ',' ).value(), 2 );
		QCOMPARE( toQString( s ), "Hello" );
	}
}

void StaticStringTest::test_assignf() {
	{
		wsw::StaticString<13> s;
		bool result = s.assignf( "%s, %s%c", "Hello", "world", '!' );
		QVERIFY( result );
		QCOMPARE( toQString( s ), "Hello, world!" );
	}
	{
		wsw::StaticString<12> s;
		bool result = s.assignf( "%s, %s%c", "Hello", "world", '!' );
		QVERIFY( !result );
	}
}

void StaticStringTest::test_appendf() {
	{
		wsw::StaticString<13> s;
		QVERIFY( s.appendf( "%s", "Hello" ) );
		QVERIFY( s.appendf( ", %s", "world!" ) );
		QCOMPARE( toQString( s ), "Hello, world!" );
	}
	{
		wsw::StaticString<12> s;
		QVERIFY( s.appendf( "%s", "Hello" ) );
		QVERIFY( s.appendf( ", %s", "world" ) );
		QVERIFY( !s.appendf( "%c", '!' ) );
		QCOMPARE( toQString( s ), "Hello, world" );
	}
}

void StaticStringTest::test_insertf() {
	{
		wsw::StaticString<13> s( "Helloworld!"_asView );
		QVERIFY( s.insertf( s.indexOf( 'w' ).value(), "%c%c", ',', ' ' ) );
		QCOMPARE( toQString( s ), "Hello, world!" );
	}
	{
		wsw::StaticString<12> s( "Helloworld!"_asView );
		QVERIFY( !s.insertf( s.indexOf( 'w' ).value(), "%c%c", ',', ' ' ) );
		QCOMPARE( toQString( s ), "Helloworld!" );
	}
}