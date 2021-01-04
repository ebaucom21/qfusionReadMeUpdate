#include "demometadatatest.h"
#include "../qcommon.h"
#include "../snap.h"

using wsw::operator""_asView;

class TestWriter : public wsw::DemoMetadataWriter {
public:
	TestWriter( char *data ) : wsw::DemoMetadataWriter( data ) {}

	void writeAll( const QMap<QString, QString> &pairs ) {
		for( auto it = pairs.cbegin(); it != pairs.cend(); ++it ) {
			write( wsw::StringView( it.key().toLatin1().data() ), wsw::StringView( it.value().toLatin1().data() ) );
		}
	}
};

class TestReader : public wsw::DemoMetadataReader {
public:
	TestReader( const char *data, size_t dataSize ) : wsw::DemoMetadataReader( data, dataSize ) {}
	[[nodiscard]]
	auto readAll() -> QMap<QString, QString> {
		QMap<QString, QString> result;
		while( hasNext() ) {
			if( const auto maybeKeyValue = readNext() ) {
				const auto [key, value] = *maybeKeyValue;
				// fromUtf8 and [] syntax allows fitting the line limit
				result[QString::fromUtf8( key.data(), key.size() )] = QString::fromUtf8( value.data(), value.size() );
			} else {
				return QMap<QString, QString>();
			}
		}
		return result;
	}
};

void DemoMetadataTest::test_write() {
	char buffer[SNAP_MAX_DEMO_META_DATA_SIZE];
	wsw::DemoMetadataWriter writer( buffer );
	writer.write( "key1"_asView, "value1"_asView );
	writer.write( "key2"_asView, ""_asView );
	writer.write( "key3"_asView, "value3"_asView );

	auto [dataSize, wasComplete] = writer.resultSoFar();
	QVERIFY( wasComplete );
	// _asView preserves the literal length
	const wsw::StringView dataView( "key1\0value1\0key2\0\0key3\0value3\0"_asView );
	QCOMPARE( QString::fromLatin1( buffer, dataSize ), QString::fromLatin1( dataView.data(), dataView.size() ) );
}

void DemoMetadataTest::test_read() {
	// _asView preserves the literal length
	const wsw::StringView dataView( "key1\0value1\0key2\0\0key3\0value3\0"_asView );
	const QMap<QString, QString> expected { { { "key1", "value1" }, { "key2", "" }, { "key3", "value3" } } };
	TestReader reader( dataView.data(), dataView.size() );
	QCOMPARE( reader.readAll(), expected );
}

void DemoMetadataTest::test_readWritten() {
	const QMap<QString, QString> original {
		{ "key1", "value1" }, { "key2", "value2" },
		{ "key3", "" }, { "key4", "value4" },
		{ "key5", "" }, { "key6", "" }, { "key7", "value7" }
	};

	char buffer[SNAP_MAX_DEMO_META_DATA_SIZE];
	TestWriter writer( buffer );
	writer.writeAll( original );

	auto [dataSize, wasComplete] = writer.resultSoFar();
	QVERIFY( wasComplete );

	TestReader reader( buffer, dataSize );
	QCOMPARE( reader.readAll(), original );
}