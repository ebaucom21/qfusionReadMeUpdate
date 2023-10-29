#include "stringsplittertest.h"

#ifndef Q_strnicmp
#define Q_strnicmp strncasecmp
#endif

#include "../wswstringsplitter.h"

template <typename Separator>
[[nodiscard]]
const auto split( const wsw::StringView &data, Separator separator ) -> QStringList {
	QStringList result;
	wsw::StringSplitter splitter( data );
	while( const auto maybeToken = splitter.getNext( separator ) ) {
		result.append( QString::fromUtf8( maybeToken->data(), maybeToken->size() ) );
	}
	return result;
}

template <typename Separator>
[[nodiscard]]
const auto splitWithNums( const wsw::StringView &data, Separator separator ) -> std::pair<QStringList, QVector<int>> {
	QStringList tokens;
	QVector<int> nums;
	wsw::StringSplitter splitter( data );
	while( const auto maybeTokenAndIndex = splitter.getNextWithNum( separator ) ) {
		const auto [token, num] = *maybeTokenAndIndex;
		tokens.append( QString::fromUtf8( token.data(), token.size() ) );
		nums.append( num );
	}
	return std::make_pair( tokens, nums );
}

void StringSplitterTest::test_splitByChar() {
	const wsw::StringView data( "192.168.1.101" );
	const auto justTokens = split( data, '.' );
	const auto [tokens, indices] = splitWithNums( data, '.' );
	const QStringList expectedTokens { "192", "168", "1", "101" };
	const QVector<int> expectedIndices { 0, 1, 2, 3 };
	QCOMPARE( justTokens, expectedTokens );
	QCOMPARE( tokens, expectedTokens );
	QCOMPARE( indices, expectedIndices );
}

void StringSplitterTest::test_splitByChars() {
	const wsw::StringView data( "\tsurfaceparm nolightmap\n\n"
								"\tsurfaceparm nomarks\n" );
	const wsw::CharLookup chars( wsw::StringView( "\n\t " ) );
	const auto justTokens = split( data, chars );
	const auto [tokens, indices] = splitWithNums( data, chars );
	const QStringList expectedTokens { "surfaceparm", "nolightmap", "surfaceparm", "nomarks" };
	const QVector<int> expectedIndices { 0, 1, 2, 3 };
	QCOMPARE( justTokens, expectedTokens );
	QCOMPARE( tokens, expectedTokens );
	QCOMPARE( indices, expectedIndices );
}

void StringSplitterTest::test_splitByString() {
	const wsw::StringView data( "2001:db8::1:0" );
	const wsw::StringView string( "::" );
	const auto justTokens = split( data, string );
	const auto [tokens, indices] = splitWithNums( data, string );
	const QStringList expectedTokens { "2001:db8", "1:0" };
	const QVector<int> expectedIndices { 0, 1 };
	QCOMPARE( justTokens, expectedTokens );
	QCOMPARE( tokens, expectedTokens );
	QCOMPARE( indices, expectedIndices );
}
