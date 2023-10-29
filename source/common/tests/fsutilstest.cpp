#include "fsutilstest.h"
#include "../wswfs.h"

using wsw::operator""_asView;

void FSUtilsTest::test_splitAtExtension() {
	QVERIFY( !wsw::fs::splitAtExtension( ""_asView ).has_value() );
	QVERIFY( !wsw::fs::splitAtExtension( "$whiteimage"_asView ).has_value() );
	QVERIFY( !wsw::fs::splitAtExtension( "./../../"_asView ).has_value() );
	QVERIFY( !wsw::fs::splitAtExtension( "foo.bar/baz"_asView ).has_value() );
	QVERIFY( !wsw::fs::splitAtExtension( "gfx/misc/playerspawn"_asView ).has_value() );

	{
		const auto maybeParts = wsw::fs::splitAtExtension( "foo.bar.baz"_asView );
		QVERIFY( maybeParts.has_value() );
		const auto [name, ext] = *maybeParts;
		QCOMPARE( name, "foo.bar"_asView );
		QCOMPARE( ext, ".baz"_asView );
	}
	{
		const auto maybeParts = wsw::fs::splitAtExtension( "foo.bar/baz.qux"_asView );
		QVERIFY( maybeParts.has_value() );
		const auto [name, ext] = *maybeParts;
		QCOMPARE( name, "foo.bar/baz"_asView );
		QCOMPARE( ext, ".qux"_asView );
	}
	{
		const auto maybeParts = wsw::fs::splitAtExtension( "gfx/misc/cartoonhit.tga"_asView );
		QVERIFY( maybeParts.has_value() );
		const auto [name, ext] = *maybeParts;
		QCOMPARE( name, "gfx/misc/cartoonhit"_asView );
		QCOMPARE( ext, ".tga"_asView );
	}
}

const char *FS_FirstExtension( const char *, const char **, int ) {
	return nullptr;
}