#include "stringviewtest.h"
#include "../qcommon.h"
#include "../wswstdtypes.h"

using wsw::operator""_asView;

static auto toQString( const wsw::StringView &sv ) -> QString {
	return QString::fromUtf8( QByteArray( sv.data(), sv.size() ) );
}

void StringViewTest::test_constructor_default() {
	const wsw::StringView sv;
	QVERIFY( sv.empty() );
	QCOMPARE( sv.length(), 0 );
	QCOMPARE( sv.size(), 0 );
	QVERIFY( sv.isZeroTerminated() );
	QCOMPARE( *sv.data(), '\0' );
}

void StringViewTest::test_constructor_charPtr() {
	const char *s = "Hello, world!";
	const wsw::StringView sv( s );
	QVERIFY( !sv.empty() );
	QCOMPARE( sv.length(), std::strlen( s) );
	QCOMPARE( sv.size(), std::strlen( s ) );
	QCOMPARE( sv.data(), s );
	QVERIFY( sv.isZeroTerminated() );
	QCOMPARE( toQString( sv ), s );
}

void StringViewTest::test_constructor_3Args() {
	const char *s = "Hello, world!";

	const wsw::StringView sv1( s, std::strlen( s ) );
	QVERIFY( !sv1.isZeroTerminated() );

	const wsw::StringView sv2( s, std::strlen( s ), wsw::StringView::ZeroTerminated );
	QVERIFY( sv2.isZeroTerminated() );

	QCOMPARE( sv1.size(), std::strlen( s ) );
	QCOMPARE( sv2.size(), std::strlen( s ) );

	QCOMPARE( sv1.data(), s );
	QCOMPARE( sv2.data(), s );

	QCOMPARE( toQString( sv1 ), s );
	QCOMPARE( toQString( sv2 ), s );
}

void StringViewTest::test_equalsIgnoreCase() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );

	QVERIFY( wsw::StringView().equalsIgnoreCase( ""_asView ) );
	QVERIFY( sv1.equalsIgnoreCase( sv2 ) && sv2.equalsIgnoreCase( sv1 ) );
	QVERIFY( "HELLO"_asView.equalsIgnoreCase( "Hello"_asView ) );
	QVERIFY( "Hello"_asView.equalsIgnoreCase( "hellO"_asView ) );
	QVERIFY( !"Hello!"_asView.equalsIgnoreCase( "Hello?"_asView ) );
}

void StringViewTest::test_maybeFront() {
	QCOMPARE( ""_asView.maybeFront(), std::nullopt );
	QCOMPARE( "Hello"_asView.maybeFront(), std::optional( 'H' ) );
}

void StringViewTest::test_maybeBack() {
	QCOMPARE( ""_asView.maybeBack(), std::nullopt );
	QCOMPARE( "Hello!"_asView.maybeBack(), std::optional( '!' ) );
}

void StringViewTest::test_maybeAt() {
	QCOMPARE( ""_asView.maybeAt( 0 ), std::nullopt );
	QCOMPARE( "Hello, world!"_asView.maybeAt( ~0u ), std::nullopt );
	QCOMPARE( "Hello, world!"_asView.maybeAt( 0 ), std::optional( 'H' ) );
	QCOMPARE( "Hello, world!"_asView.maybeAt( 12 ), std::optional( '!' ) );
}

void StringViewTest::test_indexOf_char() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );
	QVERIFY( sv1.indexOf( 'H' ) == std::optional( 0 ) && sv2.indexOf( 'H' ) == std::optional( 0 ) );
	QVERIFY( sv1.indexOf( '!' ) == std::optional( 12 ) && sv2.indexOf( '!' ) == std::optional( 12 ) );
	QVERIFY( sv1.indexOf( 'o' ) == std::optional( 4 ) && sv2.indexOf( 'o' ) == std::optional( 4 ) );
	QVERIFY( sv1.indexOf( '?' ) == std::nullopt && sv2.indexOf( '?' ) == std::nullopt );
}

void StringViewTest::test_lastIndexOf_char() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );
	QVERIFY( sv1.lastIndexOf( 'H' ) == std::optional( 0 ) && sv1.lastIndexOf( 'H' ) == std::optional( 0 ) );
	QVERIFY( sv1.lastIndexOf( '!' ) == std::optional( 12 ) && sv2.lastIndexOf( '!' ) == std::optional( 12 ) );
	QVERIFY( sv1.lastIndexOf( 'o' ) == std::optional( 8 ) && sv1.lastIndexOf( 'o' ) == std::optional( 8 ) );
	QVERIFY( sv1.lastIndexOf( 'l' ) == std::optional( 10 ) && sv1.lastIndexOf( 'l' ) == std::optional( 10 ) );
	QVERIFY( sv1.lastIndexOf( '?' ) == std::nullopt && sv2.lastIndexOf( '?' ) == std::nullopt );
}

void StringViewTest::test_indexOf_charLookup() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );
	{
		const wsw::CharLookup lookup( ",!"_asView );
		QVERIFY( sv1.indexOf( lookup ) == std::optional( 5 ) && sv2.indexOf( lookup ) == std::optional( 5 ) );
	}
	{
		const wsw::CharLookup lookup( "hHG"_asView );
		QVERIFY( sv1.indexOf( lookup ) == std::optional( 0 ) && sv2.indexOf( lookup ) == std::optional( 0 ) );
	}
	{
		const wsw::CharLookup lookup( "!D?"_asView );
		QVERIFY( sv1.indexOf( lookup ) == std::optional( 12 ) && sv2.indexOf( lookup ) == std::optional( 12 ) );
	}
	{
		const wsw::CharLookup lookup( "X"_asView );
		QVERIFY( sv1.indexOf( lookup ) == std::nullopt && sv2.indexOf( lookup ) == std::nullopt );
	}
}

void StringViewTest::test_lastIndexOf_charLookup() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );
	{
		const wsw::CharLookup lookup( ",!"_asView );
		QVERIFY( sv1.lastIndexOf( lookup ) == std::optional( 12 ) && sv1.lastIndexOf( lookup ) == std::optional( 12 ) );
	}
	{
		const wsw::CharLookup lookup( "hHG"_asView );
		QVERIFY( sv1.lastIndexOf( lookup ) == std::optional( 0 ) && sv2.lastIndexOf( lookup ) == std::optional( 0 ) );
	}
	{
		const wsw::CharLookup lookup( "oX"_asView );
		QVERIFY( sv1.lastIndexOf( lookup ) == std::optional( 8 ) && sv2.lastIndexOf( lookup ) == std::optional( 8 ) );
	}
	{
		const wsw::CharLookup lookup( "X"_asView );
		QVERIFY( sv1.lastIndexOf( lookup ) == std::nullopt && sv2.lastIndexOf( lookup ) == std::nullopt );
	}
}

void StringViewTest::test_indexOf_view() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );
	QCOMPARE( sv1.indexOf( "world!"_asView ), std::optional( 7 ) );
	QCOMPARE( sv2.indexOf( "world!"_asView ), std::optional( 7 ) );
	QCOMPARE( sv1.indexOf( "Hello,"_asView ), std::optional( 0 ) );
	QCOMPARE( sv2.indexOf( "Hello,"_asView ), std::optional( 0 ) );
	QCOMPARE( sv1.indexOf( "ll"_asView ), std::optional( 2 ) );
	QCOMPARE( sv2.indexOf( "ll"_asView ), std::optional( 2 ) );
	QCOMPARE( sv1.indexOf( "world?"_asView ), std::nullopt );
	QCOMPARE( sv2.indexOf( "world?"_asView ), std::nullopt );
}

void StringViewTest::test_lastIndexOf_view() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );
	QCOMPARE( sv1.lastIndexOf( "world!"_asView ), std::optional( 7 ) );
	QCOMPARE( sv2.lastIndexOf( "world!"_asView ), std::optional( 7 ) );
	QCOMPARE( sv1.lastIndexOf( "Hello"_asView ), std::optional( 0 ) );
	QCOMPARE( sv2.lastIndexOf( "Hello"_asView ), std::optional( 0 ) );
	QCOMPARE( sv1.lastIndexOf( "ll"_asView ), std::optional( 2 ) );
	QCOMPARE( sv2.lastIndexOf( "ll"_asView ), std::optional( 2 ) );
	QCOMPARE( sv1.lastIndexOf( "Hello?"_asView ), std::nullopt );
	QCOMPARE( sv2.lastIndexOf( "Hello?"_asView ), std::nullopt );
}

void StringViewTest::test_contains_char() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );
	QVERIFY( sv1.contains( ',' ) && sv2.contains( ',' ) );
	QVERIFY( !sv1.contains( '#' ) && !sv2.contains( '#' ) );
}

void StringViewTest::test_contains_view() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );

	const wsw::StringView matchAtStart1( "Hello" ), matchAtStart2( "Hello", 5 );
	QVERIFY( sv1.contains( matchAtStart1 ) && sv1.contains( matchAtStart2 ) );
	QVERIFY( sv2.contains( matchAtStart1 ) && sv2.contains( matchAtStart2 ) );

	const wsw::StringView matchAtMid1( "or" ), matchAtMid2( "or", 2 );
	QVERIFY( sv1.contains( matchAtMid1 ) && sv1.contains( matchAtMid2 ) );
	QVERIFY( sv2.contains( matchAtMid1 ) && sv2.contains( matchAtMid2 ) );

	const wsw::StringView matchAtEnd1( "world!" ), matchAtEnd2( "world!", 6 );
	QVERIFY( sv1.contains( matchAtEnd1 ) && sv1.contains( matchAtEnd2 ) );
	QVERIFY( sv2.contains( matchAtEnd1 ) && sv2.contains( matchAtEnd2 ) );

	const wsw::StringView mismatchAtTokenStart1( "World" ), mismatchAtTokenStart2( "World", 5 );
	QVERIFY( !sv1.contains( mismatchAtTokenStart1 ) && !sv1.contains( mismatchAtTokenStart2 ) );
	QVERIFY( !sv2.contains( mismatchAtTokenStart1 ) && !sv2.contains( mismatchAtTokenStart2 ) );

	const wsw::StringView mismatchAtTokenMid1( "worLd" ), mismatchAtTokenMid2( "worLd", 5 );
	QVERIFY( !sv1.contains( mismatchAtTokenMid1 ) && !sv1.contains( mismatchAtTokenMid2 ) );
	QVERIFY( !sv2.contains( mismatchAtTokenMid1 ) && !sv2.contains( mismatchAtTokenMid2 ) );

	const wsw::StringView mismatchAtTokenEnd1( "world?" ), mismatchAtTokenEnd2( "world?", 6 );
	QVERIFY( !sv1.contains( mismatchAtTokenEnd1 ) && !sv1.contains( mismatchAtTokenEnd2 ) );
	QVERIFY( !sv2.contains( mismatchAtTokenEnd1 ) && !sv2.contains( mismatchAtTokenEnd2 ) );
}

void StringViewTest::test_containsAny() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );

	QVERIFY( sv1.containsAny( ":!x"_asView ) );
	QVERIFY( sv2.containsAny( ":!x"_asView ) );

	QVERIFY( !sv1.containsAny( ""_asView ) );
	QVERIFY( !sv2.containsAny( ""_asView ) );

	QVERIFY( !sv1.containsAny( "f?"_asView ) );
	QVERIFY( !sv2.containsAny( "f?"_asView ) );

	QVERIFY( sv1.containsAny( sv1 ) && sv1.containsAny( sv2 ) );
	QVERIFY( sv2.containsAny( sv1 ) && sv2.containsAny( sv2 ) );
}

void StringViewTest::test_containsOnly() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );

	QVERIFY( ""_asView.containsOnly( sv1 ) );
	QVERIFY( ""_asView.containsOnly( sv2 ) );
	QVERIFY( sv1.containsOnly( sv1 ) && sv1.containsOnly( sv2 ) );
	QVERIFY( sv2.containsOnly( sv1 ) && sv2.containsOnly( sv2 ) );
	QVERIFY( !sv1.containsOnly( ""_asView ) );
	QVERIFY( !sv2.containsOnly( ""_asView ) );
	QVERIFY( sv1.containsOnly( "Hello, world!."_asView ) );
	QVERIFY( sv2.containsOnly( "Hello, world!."_asView ) );
	QVERIFY( !sv1.containsOnly( "Hello, World!"_asView ) );
	QVERIFY( !sv2.containsOnly( "Hello, World!"_asView ) );
}

void StringViewTest::test_containsAll() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );

	QVERIFY( sv1.containsAll( sv1 ) && sv1.containsAll( sv2 ) );
	QVERIFY( sv2.containsAll( sv1 ) && sv2.containsAll( sv2 ) );
	QVERIFY( sv1.containsAll( ""_asView ) );
	QVERIFY( sv2.containsAll( ""_asView ) );
	QVERIFY( !sv1.containsAll( "Hello, world!."_asView ) );
	QVERIFY( !sv2.containsAll( "Hello, world!."_asView ) );
	QVERIFY( !sv1.containsAll( "World!"_asView ) );
	QVERIFY( !sv2.containsAll( "World!"_asView ) );
	QVERIFY( !sv1.containsAll( "HJ"_asView ) );
	QVERIFY( !sv2.containsAll( "HJ"_asView ) );
}

void StringViewTest::test_startsWith_char() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1(s), sv2( s, std::strlen( s ) );

	QVERIFY( !""_asView.startsWith( 'H' ) );
	QVERIFY( sv1.startsWith( 'H' ) && sv2.startsWith( 'H' ) );
	QVERIFY( !sv1.startsWith( 'h' ) && !sv2.startsWith( 'h') );
}

void StringViewTest::test_startsWith_view() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );

	QVERIFY( ""_asView.startsWith( ""_asView ) );
	QVERIFY( !""_asView.startsWith( sv1 ) && !""_asView.startsWith( sv2 ) );
	QVERIFY( sv1.startsWith( sv1 ) && sv1.startsWith( sv2 ) );
	QVERIFY( sv2.startsWith( sv1 ) && sv2.startsWith( sv2 ) );
	QVERIFY( sv1.startsWith( ""_asView ) && sv2.startsWith( ""_asView ) );
	QVERIFY( sv1.startsWith( "Hello"_asView ) );
	QVERIFY( sv2.startsWith( "Hello"_asView ) );
	QVERIFY( !sv1.startsWith( "Hello!"_asView ) );
	QVERIFY( !sv2.startsWith( "Hello!"_asView ) );
	QVERIFY( !sv1.startsWith( "Hello, world!."_asView ) );
	QVERIFY( !sv2.startsWith( "Hello, world!."_asView ) );
}

void StringViewTest::test_endsWith_char() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );

	QVERIFY( !""_asView.endsWith( '!' ) );
	QVERIFY( sv1.endsWith( '!' ) && sv2.endsWith( '!' ) );
	QVERIFY( !sv1.endsWith( '?' ) && !sv2.endsWith( '?' ) );
}

void StringViewTest::test_endsWith_view() {
	const char *s = "Hello, world!";
	const wsw::StringView sv1( s ), sv2( s, std::strlen( s ) );

	QVERIFY( ""_asView.endsWith( ""_asView ) );
	QVERIFY( !""_asView.endsWith( sv1 ) && !""_asView.endsWith( sv2 ) );
	QVERIFY( sv1.endsWith( sv1 ) && sv1.endsWith( sv2) );
	QVERIFY( sv2.endsWith( sv1 ) && sv2.endsWith( sv2 ) );
	QVERIFY( sv1.endsWith( ""_asView ) && sv2.endsWith( ""_asView ) );
	QVERIFY( sv1.endsWith( "world!"_asView ) );
	QVERIFY( sv2.endsWith( "world!"_asView ) );
	QVERIFY( !sv1.endsWith( "world"_asView ) );
	QVERIFY( !sv2.endsWith( "world"_asView ) );
	QVERIFY( !sv1.endsWith( ".Hello, world!"_asView ) );
	QVERIFY( !sv2.endsWith( ".Hello, world!"_asView ) );
}

void StringViewTest::test_trimLeft_noArgs() {
	QCOMPARE( ""_asView.trimLeft(), ""_asView );
	QCOMPARE( "\t\n "_asView.trimLeft(), ""_asView );
	QCOMPARE( "Hello, world!"_asView.trimLeft(), "Hello, world!"_asView );
	QCOMPARE( "\t Hello, world!"_asView.trimLeft(), "Hello, world!"_asView );

	QVERIFY( wsw::StringView("\tHello" ).trimLeft().isZeroTerminated() );
	QVERIFY( !wsw::StringView("\tHello", 6 ).trimLeft().isZeroTerminated() );
}

void StringViewTest::test_trimLeft_char() {
	QCOMPARE( ""_asView.trimLeft('H'), ""_asView );
	QCOMPARE( "HHHH"_asView.trimLeft('H'), ""_asView );
	QCOMPARE( "Hello, world!"_asView.trimLeft('H'), "ello, world!"_asView );
	QCOMPARE( "world!"_asView.trimLeft('H'), "world!"_asView );

	QVERIFY( wsw::StringView( "H" ).trimLeft( 'H' ).isZeroTerminated() );
	QVERIFY( wsw::StringView( "Hello!" ).trimLeft( 'H' ).isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello!", 6 ).trimLeft( 'H' ).isZeroTerminated() );
}

void StringViewTest::test_trimLeft_chars() {
	QCOMPARE( ""_asView.trimLeft( "oleH"_asView ), ""_asView );
	QCOMPARE( "Hello"_asView.trimLeft( "!oleH"_asView ), ""_asView );
	QCOMPARE( "Hello"_asView.trimLeft( "eH"_asView ), "llo"_asView );
	QCOMPARE( "Hello"_asView.trimLeft( "!?"_asView ), "Hello"_asView );

	QVERIFY( wsw::StringView( "Hello" ).trimLeft( "eH"_asView ).isZeroTerminated() );
	QVERIFY( wsw::StringView( "Hello" ).trimLeft( "Hello"_asView ).isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello", 5 ).trimLeft( "eH"_asView ).isZeroTerminated() );
}

void StringViewTest::test_trimRight_noArgs() {
	QCOMPARE( ""_asView.trimRight(), ""_asView );
	QCOMPARE( "\t \n"_asView.trimRight(), ""_asView );
	QCOMPARE( "Hello, world! \t\n"_asView.trimRight(), "Hello, world!"_asView );

	QVERIFY( wsw::StringView( "Hello!" ).trimRight().isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello!\n" ).trimRight().isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello!", 6 ).trimRight().isZeroTerminated() );
}

void StringViewTest::test_trimRight_char() {
	QCOMPARE( ""_asView.trimRight( '!' ), ""_asView );
	QCOMPARE( "!!!!!"_asView.trimRight( '!' ), ""_asView );
	QCOMPARE( "Hello, world!"_asView.trimRight( '!' ), "Hello, world"_asView );
	QCOMPARE( "Hello, world!"_asView.trimRight( '?' ), "Hello, world!"_asView );

	QVERIFY( "Hello, world!"_asView.trimRight( '?' ).isZeroTerminated() );
	QVERIFY( !"Hello, world!"_asView.trimRight( '!' ).isZeroTerminated() );
}

void StringViewTest::test_trimRight_chars() {
	QCOMPARE( ""_asView.trimRight( "!?X"_asView ), ""_asView );
	QCOMPARE( "Hello"_asView.trimRight( "!Hloe"_asView ), ""_asView );
	QCOMPARE( "Hello, world!?"_asView.trimRight( ">!?%"_asView ), "Hello, world"_asView );

	QVERIFY( "Hello!"_asView.trimRight( "eH"_asView ).isZeroTerminated() );
	QVERIFY( !"Hello!"_asView.trimRight( ">!"_asView ).isZeroTerminated() );
}

void StringViewTest::test_trim_noArgs() {
	QCOMPARE( ""_asView.trim(), ""_asView );
	QCOMPARE( "\n\n \t"_asView.trim(), ""_asView );
	QCOMPARE( "Hello, world!"_asView.trim(), "Hello, world!"_asView );
	QCOMPARE( "Hello, world!\n"_asView.trim(), "Hello, world!"_asView );
	QCOMPARE( "\tHello"_asView.trim(), "Hello"_asView );
	QCOMPARE( "\tHello\n"_asView.trim(), "Hello"_asView );

	QVERIFY( "\n\n\t"_asView.trim().isZeroTerminated() );
	QVERIFY( "\tHello!"_asView.trim().isZeroTerminated() );
	QVERIFY( "Hello!"_asView.trim().isZeroTerminated() );
	QVERIFY( !"Hello!\n"_asView.trim().isZeroTerminated() );
}

void StringViewTest::test_trim_char() {
 	QCOMPARE( ""_asView.trim( '*' ), ""_asView );
 	QCOMPARE( "*******"_asView.trim( '*' ), ""_asView );
 	QCOMPARE( "*Hello, world!*"_asView.trim( '*' ), "Hello, world!"_asView );

 	QVERIFY( "\nHello, world!"_asView.trim( '\n' ).isZeroTerminated() );
	QVERIFY( !"Hello, world!"_asView.trim( '!' ).isZeroTerminated() );
	QVERIFY( !"*Hello, world!*"_asView.trim( '*' ).isZeroTerminated() );
}

void StringViewTest::test_trim_chars() {
	QCOMPARE( ""_asView.trim( "?!*"_asView ), ""_asView );
	QCOMPARE( "++="_asView.trim( "=+*"_asView ), ""_asView );
	QCOMPARE( "Hello"_asView.trim( "<elHo>?"_asView ), ""_asView );

	QVERIFY( "Hello"_asView.trim( "oleH"_asView ).isZeroTerminated() );
	QVERIFY( "Hello!"_asView.trim( "oleH"_asView ).isZeroTerminated() );
	QVERIFY( !"Hello!?"_asView.trim( "!><?"_asView ).isZeroTerminated() );
}

void StringViewTest::test_take() {
	QCOMPARE( ""_asView.take( 3 ), ""_asView );
	QCOMPARE( "Hello, world!"_asView.take( 5 ), "Hello"_asView );
	QCOMPARE( "Hello, world!"_asView.take( 100 ), "Hello, world!"_asView );

	QVERIFY( !"Hello, world!"_asView.take( 5 ).isZeroTerminated() );
	QVERIFY( "Hello, world!"_asView.take( 100 ).isZeroTerminated() );
}

void StringViewTest::test_takeExact() {
	QCOMPARE( ""_asView.takeExact( 3 ), std::nullopt );
	QCOMPARE( "Hello, world!"_asView.takeExact( 5 ), std::optional( "Hello"_asView ) );
	QCOMPARE( "Hello, world!"_asView.takeExact( 13 ), std::optional( "Hello, world!"_asView ) );
	QCOMPARE( "Hello, world!"_asView.takeExact( 100 ), std::nullopt );

	QVERIFY( !"Hello, world!"_asView.takeExact( 5 ).value().isZeroTerminated() );
	QVERIFY( "Hello, world!"_asView.takeExact( 13 ).value().isZeroTerminated() );
}

void StringViewTest::test_takeWhile() {
	QCOMPARE( ""_asView.takeWhile( []( char c ) { return false; } ), ""_asView );
	QCOMPARE( "Hello"_asView.takeWhile( []( char c ) { return false; } ), ""_asView );
	QCOMPARE( ""_asView.takeWhile( []( char c ) { return true; } ), ""_asView );
	QCOMPARE( "Hello"_asView.takeWhile( []( char c ) { return true; } ), "Hello"_asView );
	QCOMPARE( "Hello"_asView.takeWhile( []( char c ) { return ::isupper( c ); } ), "H"_asView );

	QVERIFY( !"Hello"_asView.takeWhile( []( char c ) { return false; } ).isZeroTerminated() );
	QVERIFY( !"Hello"_asView.takeWhile( []( char c ) { return ::isupper( c ); } ).isZeroTerminated() );
	QVERIFY( "Hello"_asView.takeWhile( []( char c ) { return true; } ).isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello", 5 ).takeWhile( []( char c ) { return true; } ).isZeroTerminated() );
}

void StringViewTest::test_drop() {
	QCOMPARE( ""_asView.drop( 3 ), ""_asView );
	QCOMPARE( "Hello, world!"_asView.drop( 5 ), ", world!"_asView );
	QCOMPARE( "Hello, world!"_asView.drop( 100 ), ""_asView );

	QVERIFY( "Hello"_asView.drop( 5 ).isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello", 5 ).drop( 5 ).isZeroTerminated() );
}

void StringViewTest::test_dropExact() {
	QCOMPARE( ""_asView.dropExact( 3 ), std::nullopt );
	QCOMPARE( "Hello, world!"_asView.dropExact( 5 ), std::optional( ", world!"_asView ) );
	QCOMPARE( "Hello, world!"_asView.dropExact( 13 ), std::optional( ""_asView ) );
	QCOMPARE( "Hello, world!"_asView.dropExact( 100 ), std::nullopt );

	QVERIFY( "Hello"_asView.dropExact( 3 ).value().isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello", 5 ).dropExact( 3 ).value().isZeroTerminated() );
}

void StringViewTest::test_dropWhile() {
	QCOMPARE( ""_asView.dropWhile( []( char c ) { return false; } ), ""_asView );
	QCOMPARE( "Hello"_asView.dropWhile( []( char c ) { return false; } ), "Hello"_asView );
	QCOMPARE( ""_asView.dropWhile( []( char c ) { return true; } ), ""_asView );
	QCOMPARE( "Hello"_asView.dropWhile( []( char c ) { return true; } ), ""_asView );
	QCOMPARE( "Hello"_asView.dropWhile( []( char c ) { return ::isupper( c ); } ), "ello"_asView );

	QVERIFY( "Hello"_asView.dropWhile( []( char c ) { return false; } ).isZeroTerminated() );
	QVERIFY( "Hello"_asView.dropWhile( []( char c ) { return ::isupper( c ); } ).isZeroTerminated() );
	QVERIFY( "Hello"_asView.dropWhile( []( char c ) { return true; } ).isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello", 5 ).dropWhile( []( char c ) { return ::isupper( c ); } ).isZeroTerminated() );
}

void StringViewTest::test_takeRight() {
	QCOMPARE( ""_asView.takeRight( 3 ), ""_asView );
	QCOMPARE( "Hello, world!"_asView.takeRight( 6 ), "world!"_asView );
	QCOMPARE( "Hello, world!"_asView.takeRight( 13 ), "Hello, world!"_asView );
	QCOMPARE( "Hello, world!"_asView.takeRight( 100 ), "Hello, world!"_asView );

	QVERIFY( "Hello!"_asView.takeRight( 3 ).isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello!", 6 ).takeRight( 3 ).isZeroTerminated() );
}

void StringViewTest::test_takeRightExact() {
	QCOMPARE( ""_asView.takeRightExact( 3 ), std::nullopt );
	QCOMPARE( "Hello, world!"_asView.takeRightExact( 6 ), std::optional( "world!"_asView ) );
	QCOMPARE( "Hello, world!"_asView.takeRightExact( 13 ), std::optional( "Hello, world!"_asView ) );
	QCOMPARE( "Hello, world!"_asView.takeRightExact( 100 ), std::nullopt );

	QVERIFY( "Hello!"_asView.takeRightExact( 3 ).value().isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello!", 6 ).takeRightExact( 3 ).value().isZeroTerminated() );
}

void StringViewTest::test_takeRightWhile() {
	QCOMPARE( ""_asView.takeRightWhile( []( char c ) { return false; } ), ""_asView );
	QCOMPARE( ""_asView.takeRightWhile( []( char c ) { return true; } ), ""_asView );
	QCOMPARE( "Hello!"_asView.takeRightWhile( []( char c ) { return false; } ), ""_asView );
	QCOMPARE( "Hello!"_asView.takeRightWhile( []( char c ) { return true; } ), "Hello!"_asView );
	const auto isPunct = []( char c ) { return ::ispunct( c ); };
	QCOMPARE( "Hello!"_asView.takeRightWhile( isPunct ), "!"_asView );

	QVERIFY( "Hello!"_asView.takeRightWhile( isPunct ).isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello!", 6 ).takeRightWhile( isPunct ).isZeroTerminated() );
}

void StringViewTest::test_dropRight() {
	QCOMPARE( ""_asView.dropRight( 3 ), ""_asView );
	QCOMPARE( "Hello, world!"_asView.dropRight( 1 ), "Hello, world"_asView );
	QCOMPARE( "Hello, world!"_asView.dropRight( 12 ), "H"_asView );
	QCOMPARE( "Hello, world!"_asView.dropRight( 100 ), ""_asView );

	QVERIFY( "Hello!"_asView.dropRight( 0 ).isZeroTerminated() );
	QVERIFY( !"Hello!"_asView.dropRight( 1 ).isZeroTerminated() );
}

void StringViewTest::test_dropRightExact() {
	QCOMPARE( ""_asView.dropRightExact( 0 ), std::optional( ""_asView ) );
	QCOMPARE( ""_asView.dropRightExact( 1 ), std::nullopt );
	QCOMPARE( "Hello, world!"_asView.dropRightExact( 1 ), std::optional( "Hello, world"_asView ) );
	QCOMPARE( "Hello, world!"_asView.dropRightExact( 13 ), std::optional( ""_asView ) );
	QCOMPARE( "Hello, world!"_asView.dropRightExact( 100 ), std::nullopt );

	QVERIFY( "Hello!"_asView.dropRightExact( 0 ).value().isZeroTerminated() );
	QVERIFY( !"Hello!"_asView.dropRightExact( 1 ).value().isZeroTerminated() );
}

void StringViewTest::test_dropRightWhile() {
	QCOMPARE( ""_asView.dropRightWhile( []( char c ) { return false; } ), ""_asView );
	QCOMPARE( ""_asView.dropRightWhile( []( char c ) { return true; } ), ""_asView );
	QCOMPARE( "Hello!"_asView.dropRightWhile( []( char c ) { return false; } ), "Hello!"_asView );
	QCOMPARE( "Hello!"_asView.dropRightWhile( []( char c ) { return true; } ), ""_asView );
	const auto isPunct = []( char c ) { return ::ispunct( c ); };
	QCOMPARE( "Hello!"_asView.dropRightWhile( isPunct ), "Hello"_asView );
	const auto isFalse = []( char c ) { return false; };
	QVERIFY( "Hello!"_asView.dropWhile( isFalse ).isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello!", 6 ).dropWhile( isFalse ).isZeroTerminated() );
	QVERIFY( !"Hello!"_asView.dropRightWhile( isPunct ).isZeroTerminated() );
	QVERIFY( !wsw::StringView( "Hello!", 6 ).takeRightWhile( isPunct ).isZeroTerminated() );
}

void StringViewTest::test_takeMid() {
	QCOMPARE( "Hello, world!"_asView.takeMid( 0, 0 ), ""_asView );
	QCOMPARE( "Hello, world!"_asView.takeMid( 5, 0 ), ""_asView );
	QCOMPARE( "Hello, world!"_asView.takeMid( 12, 0 ), ""_asView );

	QCOMPARE( "Hello, world!"_asView.takeMid( 0, 13 ), "Hello, world!"_asView );
	QCOMPARE( "Hello, world!"_asView.takeMid( 0, 99 ), "Hello, world!"_asView );

	QCOMPARE( "Hello, world!"_asView.takeMid( 0, 5 ), "Hello"_asView );
	QCOMPARE( "Hello, world!"_asView.takeMid( 5, 1 ), ","_asView );
	QCOMPARE( "Hello, world!"_asView.takeMid( 7, 5 ), "world"_asView );
	QVERIFY( !"Hello, world!"_asView.takeMid( 7, 5 ).isZeroTerminated() );
	QCOMPARE( "Hello, world!"_asView.takeMid( 7, 6 ), "world!"_asView );
	QVERIFY( "Hello, world!"_asView.takeMid( 7, 6 ).isZeroTerminated() );
	QCOMPARE( "Hello, world!"_asView.takeMid( 7, 99 ), "world!"_asView );
	QVERIFY( "Hello, world!"_asView.takeMid( 7, 99 ).isZeroTerminated() );
}

void StringViewTest::test_takeMidExact() {
	QCOMPARE( "Hello, world!"_asView.takeMidExact( 0, 0 ), std::optional( ""_asView ) );
	QCOMPARE( "Hello, world!"_asView.takeMidExact( 5, 0 ), std::optional( ""_asView ) );
	QCOMPARE( "Hello, world!"_asView.takeMidExact( 12, 0 ), std::optional( ""_asView ) );

	QCOMPARE( "Hello, world!"_asView.takeMidExact( 0, 13 ), std::optional( "Hello, world!"_asView ) );
	QCOMPARE( "Hello, world!"_asView.takeMidExact( 0, 99 ), std::nullopt );

	QCOMPARE( "Hello, world!"_asView.takeMidExact( 0, 5 ), std::optional( "Hello"_asView ) );
	QCOMPARE( "Hello, world!"_asView.takeMidExact( 5, 1 ), std::optional( ","_asView ) );
	QCOMPARE( "Hello, world!"_asView.takeMidExact( 7, 5 ), std::optional( "world"_asView ) );
	QVERIFY( !"Hello, world!"_asView.takeMidExact( 7, 5 ).value().isZeroTerminated() );
	QCOMPARE( "Hello, world!"_asView.takeMidExact( 7, 6 ), std::optional( "world!"_asView ) );
	QVERIFY( "Hello, world!"_asView.takeMidExact( 7, 6 ).value().isZeroTerminated() );
	QCOMPARE( "Hello, world!"_asView.takeMidExact( 7, 99 ), std::nullopt );
}

void StringViewTest::test_dropMid() {
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 0, 0 );
		QCOMPARE( left, ""_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, "Hello, world!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 0, 7 );
		QCOMPARE( left, ""_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, "world!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 0, 13 );
		QCOMPARE( left, ""_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, ""_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 0, 99 );
		QCOMPARE( left, ""_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, ""_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 5, 0 );
		QCOMPARE( left, "Hello"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, ", world!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 5, 2 );
		QCOMPARE( left, "Hello"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, "world!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 5, 7 );
		QCOMPARE( left, "Hello"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, "!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 5, 99 );
		QCOMPARE( left, "Hello"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, ""_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 12, 1 );
		QCOMPARE( left, "Hello, world"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, ""_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto [left, right] = "Hello, world!"_asView.dropMid( 12, 99 );
		QCOMPARE( left, "Hello, world"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, ""_asView );
		QVERIFY( right.isZeroTerminated() );
	}
}

void StringViewTest::test_dropMidExact() {
	{
		const auto maybeParts = "Hello, world!"_asView.dropMidExact( 0, 0 );
		QVERIFY( maybeParts.has_value() );
		const auto [left, right] = *maybeParts;
		QCOMPARE( left, ""_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, "Hello, world!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto maybeParts = "Hello, world!"_asView.dropMidExact( 0, 7 );
		QVERIFY( maybeParts.has_value() );
		const auto [left, right] = *maybeParts;
		QCOMPARE( left, ""_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, "world!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto maybeParts = "Hello, world!"_asView.dropMidExact( 0, 13 );
		QVERIFY( maybeParts.has_value() );
		const auto [left, right] = *maybeParts;
		QCOMPARE( left, ""_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, ""_asView );
		QVERIFY( right.isZeroTerminated() );
	}

	QVERIFY( !"Hello, world!"_asView.dropMidExact( 0, 99 ).has_value() );

	{
		const auto maybeParts = "Hello, world!"_asView.dropMidExact( 5, 0 );
		QVERIFY( maybeParts.has_value() );
		const auto [left, right] = *maybeParts;
		QCOMPARE( left, "Hello"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, ", world!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto maybeParts = "Hello, world!"_asView.dropMidExact( 5, 2 );
		QVERIFY( maybeParts.has_value() );
		const auto [left, right] = *maybeParts;
		QCOMPARE( left, "Hello"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, "world!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}
	{
		const auto maybeParts = "Hello, world!"_asView.dropMidExact( 5, 7 );
		QVERIFY( maybeParts.has_value() );
		const auto [left, right] = *maybeParts;
		QCOMPARE( left, "Hello"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, "!"_asView );
		QVERIFY( right.isZeroTerminated() );
	}

	QVERIFY( !"Hello, world!"_asView.dropMidExact( 5, 99 ).has_value() );

	{
		const auto maybeParts = "Hello, world!"_asView.dropMidExact( 12, 1 );
		QVERIFY( maybeParts.has_value() );
		const auto [left, right] = *maybeParts;
		QCOMPARE( left, "Hello, world"_asView );
		QVERIFY( !left.isZeroTerminated() );
		QCOMPARE( right, ""_asView );
		QVERIFY( right.isZeroTerminated() );
	}

	QVERIFY( !"Hello, world!"_asView.dropMidExact( 12, 99 ).has_value() );
}