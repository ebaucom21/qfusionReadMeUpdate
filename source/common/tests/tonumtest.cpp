#include "tonumtest.h"

#ifndef Q_strnicmp
#define Q_strnicmp strncasecmp
#endif

#include "../wswstringview.h"
#include "../wswtonum.h"

using wsw::operator""_asView;

#include <sstream>

void ToNumTest::test_parse_bool() {
	QVERIFY( wsw::toNum<bool>( "" ) == std::nullopt );

	{
		wsw::StringView s1( "1" ), s2( "1", 1 );
		QVERIFY( wsw::toNum<bool>( s1 ) == std::optional( true ) );
		QVERIFY( wsw::toNum<bool>( s2 ) == std::optional( true ) );
	}
	{
		wsw::StringView s1( "0" ), s2( "0", 1 );
		QVERIFY( wsw::toNum<bool>( s1 ) == std::optional( false ) );
		QVERIFY( wsw::toNum<bool>( s2 ) == std::optional( false ) );
	}
	{
		// Out-of-range
		wsw::StringView s1( "3" ), s2( "3", 1 );
		QVERIFY( wsw::toNum<bool>( s1 ) == std::nullopt );
		QVERIFY( wsw::toNum<bool>( s2 ) == std::nullopt );
	}
	{
		unsigned stoppedAtIndex = 0;
		wsw::StringView s1( "0" ), s2( "0;" );
		QVERIFY( wsw::toNum<bool>( s1, &stoppedAtIndex ) == std::optional( false ) );
		QCOMPARE( stoppedAtIndex, 1 );

		QVERIFY( wsw::toNum<bool>( s2 ) == std::nullopt );
		QVERIFY( wsw::toNum<bool>( s2, &stoppedAtIndex ) == std::optional( false ) );
		QCOMPARE( stoppedAtIndex, 1 );
	}
}

struct StringValueHolder {
	std::string s;

	template <typename T>
	StringValueHolder( T value ) {
		std::stringstream ss;
		ss << value;
		s = ss.str();
	}

	template <typename T>
	[[nodiscard]]
	auto parse() const -> std::optional<T> {
		return wsw::toNum<T>( wsw::StringView( s.data(), s.size() ) );
	}

	template <typename T>
	auto parse( unsigned *stoppedAtIndex ) -> std::optional<T> {
		return wsw::toNum<T>( wsw::StringView( s.data(), s.size() ), stoppedAtIndex );
	}
};

template <typename T>
[[nodiscard]]
static auto convertAndParseBack( T value ) -> std::optional<T> {
	std::optional<T> res1, res2, res3;
	// This is not very nice but helps to avoid tons of excessive test case boilerplate
	{
		StringValueHolder holder( value );
		res1 = holder.parse<T>();
	}
	{
		unsigned stoppedAtIndex = 0;
		StringValueHolder holder( value );
		res2 = holder.parse<T>( &stoppedAtIndex );
		if( stoppedAtIndex != holder.s.size() ) abort();
	}
	{
		unsigned stoppedAtIndex = 0;
		StringValueHolder holder( value );
		holder.s += ";";
		res3 = holder.parse<T>( &stoppedAtIndex );
		if( stoppedAtIndex + 1 != holder.s.size() ) abort();
	}
	if( res1 != res2 || res1 != res3 ) abort();
	return res1;
}

void ToNumTest::test_parse_int() {
	QVERIFY( wsw::toNum<int>( ""_asView ) == std::nullopt );
	{
		StringValueHolder holder( std::numeric_limits<int64_t>::max() );
		QVERIFY( holder.parse<int>() == std::nullopt );
	}
	{
		auto val = std::numeric_limits<int>::min();
		QCOMPARE( convertAndParseBack<int>( val ), std::optional( val ) );
	}
	{
		auto val = std::numeric_limits<int>::max();
		QCOMPARE( convertAndParseBack<int>( val ), std::optional( val ) );
	}
}

void ToNumTest::test_parse_unsigned() {
	QVERIFY( wsw::toNum<int>( ""_asView ) == std::nullopt );
	{
		StringValueHolder holder( std::numeric_limits<int64_t>::max() );
		QVERIFY( holder.parse<unsigned>() == std::nullopt );
	}
	{
		auto val = std::numeric_limits<unsigned>::min();
		QCOMPARE( convertAndParseBack<unsigned>( val ), std::optional( val ) );
	}
	{
		auto val = std::numeric_limits<unsigned>::max();
		QCOMPARE( convertAndParseBack<unsigned>( val ), std::optional( val ) );
	}
}

void ToNumTest::test_parse_int64() {
	QVERIFY( wsw::toNum<int64_t>( ""_asView ) == std::nullopt );
	{
		StringValueHolder holder( std::numeric_limits<int64_t>::max() );
		QVERIFY( holder.parse<int64_t>() != std::nullopt );
		holder.s += "0";
		QVERIFY( holder.parse<int64_t>() == std::nullopt );
	}
	{
		auto val = std::numeric_limits<int64_t>::min();
		QCOMPARE( convertAndParseBack<int64_t>( val ), std::optional( val ) );
	}
	{
		auto val = std::numeric_limits<int64_t>::max();
		QCOMPARE( convertAndParseBack<int64_t>( val ), std::optional( val ) );
	}
}

void ToNumTest::test_parse_uint64() {
	QVERIFY( wsw::toNum<uint64_t>( ""_asView ) == std::nullopt );
	{
		StringValueHolder holder( std::numeric_limits<uint64_t>::max() );
		QVERIFY( holder.parse<uint64_t>() != std::nullopt );
		holder.s += "0";
		QVERIFY( holder.parse<uint64_t>() == std::nullopt );
	}
	{
		auto val = std::numeric_limits<uint64_t>::min();
		QCOMPARE( convertAndParseBack<uint64_t>( val ), std::optional( val ) );
	}
	{
		auto val = std::numeric_limits<uint64_t>::max();
		QCOMPARE( convertAndParseBack<uint64_t>( val ), std::optional( val ) );
	}
}

template <typename T>
static bool fuzzyCompare( T a, T b ) {
	// This is a quite robust approach for testing purposes
	std::stringstream s1, s2;
	s1 << a;
	s2 << b;
	return s1.str() == s2.str();
}

void ToNumTest::test_parse_float() {
	QVERIFY( wsw::toNum<float>( ""_asView ) == std::nullopt );
	{
		StringValueHolder holder( std::numeric_limits<float>::max() );
		QVERIFY( holder.parse<float>() != std::nullopt );
		holder.s += "0";
		QVERIFY( holder.parse<float>() == std::nullopt );
	}
	{
		auto val = std::numeric_limits<float>::lowest();
		auto parsed = convertAndParseBack<float>( val );
		QVERIFY( parsed && fuzzyCompare( val, *parsed ) );
	}
	{
		auto val = std::numeric_limits<float>::max();
		auto parsed = convertAndParseBack<float>( val );
		QVERIFY( parsed && fuzzyCompare( val, *parsed ) );
	}
}

void ToNumTest::test_parse_double() {
	QVERIFY( wsw::toNum<double>( ""_asView ) == std::nullopt );
	{
		StringValueHolder holder( std::numeric_limits<double>::max() );
		QVERIFY( holder.parse<double>() != std::nullopt );
		holder.s += "0";
		QVERIFY( holder.parse<double>() == std::nullopt );
	}
	{
		auto val = std::numeric_limits<double>::min();
		auto parsed = convertAndParseBack<double>( val );
		QVERIFY( parsed && fuzzyCompare( val, *parsed ) );
	}
	{
		auto val = std::numeric_limits<double>::max();
		auto parsed = convertAndParseBack<double>( val );
		QVERIFY( parsed && fuzzyCompare( val, *parsed ) );
	}
}

void ToNumTest::test_parse_longDouble() {
	QVERIFY( wsw::toNum<long double>( ""_asView ) == std::nullopt );
	{
		StringValueHolder holder( std::numeric_limits<long double>::max() );
		QVERIFY( holder.parse<long double>() != std::nullopt );
		holder.s += "0";
		QVERIFY( holder.parse<long double>() == std::nullopt );
	}
	{
		auto val = std::numeric_limits<long double>::min();
		auto parsed = convertAndParseBack<long double>( val );
		QVERIFY( parsed && fuzzyCompare( val, *parsed ) );
	}
	{
		auto val = std::numeric_limits<long double>::max();
		auto parsed = convertAndParseBack<long double>( val );
		QVERIFY( parsed && fuzzyCompare( val, *parsed ) );
	}
}
