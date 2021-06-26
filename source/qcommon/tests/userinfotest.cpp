#include "userinfotest.h"
#include "../userinfo.h"

using wsw::operator""_asView;
using wsw::operator""_asHView;

void UserInfoTest::test_serialize() {
	wsw::UserInfo userInfo;
	(void)userInfo.set( "name"_asHView, "Player"_asView );
	(void)userInfo.set( "clan"_asHView, "INSECT"_asView );
	QString actual;
	userInfo.serialize<QString, QLatin1String>( &actual );
	QCOMPARE( actual, "\\name\\Player\\clan\\INSECT" );
}

void UserInfoTest::test_deserialize() {
	wsw::UserInfo userInfo;
	const bool succeeded = userInfo.parse( "\\name\\Player\\clan\\INSECT"_asView );
	QVERIFY( succeeded );
	QVERIFY( userInfo.get( "name"_asHView ) == std::optional( "Player"_asView ) );
	QVERIFY( userInfo.get( "clan"_asHView ) == std::optional( "INSECT"_asView ) );
}