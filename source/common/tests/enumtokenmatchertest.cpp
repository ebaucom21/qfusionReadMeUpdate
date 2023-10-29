#include "enumtokenmatchertest.h"

#ifndef Q_strnicmp
#define Q_strnicmp strncasecmp
#endif

#include "../enumtokenmatcher.h"

using wsw::operator""_asView;

namespace {

enum TestEnum {
	Collection,
	List,
	Set,
	Map,
	Hashtable
};

class TestMatcher : public wsw::EnumTokenMatcher<TestEnum, 8> {
public:
	TestMatcher() {
		add( "Collection"_asView, Collection );
		add( "List"_asView, List );
		add( "Set"_asView, Set );
		add( "Map"_asView, Map );
		add( "Hashtable"_asView, Hashtable );
	}
};

}

void EnumTokenMatcherTest::test() {
	TestMatcher matcher;

	QCOMPARE( matcher.match( ""_asView ), std::nullopt );
	QCOMPARE( matcher.match( "collection"_asView ), std::optional( Collection ) );
	QCOMPARE( matcher.match( "list"_asView ), std::optional( List ) );
	QCOMPARE( matcher.match( "set"_asView ), std::optional( Set ) );
	QCOMPARE( matcher.match( "map"_asView ), std::optional( Map ) );
	QCOMPARE( matcher.match( "hashtable"_asView ), std::optional( Hashtable ) );
	QCOMPARE( matcher.match( "string"_asView ), std::nullopt );
}

