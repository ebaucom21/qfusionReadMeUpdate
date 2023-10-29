#include "demometadatatest.h"
#include ".h"
#include "../snap.h"

using wsw::operator""_asView;

class TestWriter : public wsw::DemoMetadataWriter {
public:
	TestWriter( char *data ) : wsw::DemoMetadataWriter( data ) {}

	void writeAll( const QMap<QString, QString> &pairs, const QSet<QString> &tags ) {
		for( auto it = pairs.cbegin(); it != pairs.cend(); ++it ) {
			writePair( wsw::StringView( it.key().toLatin1().data() ), wsw::StringView( it.value().toLatin1().data() ) );
		}
		for( const QString &tag : tags ) {
			writeTag( wsw::StringView( tag.toLatin1().data() ) );
		}
	}
};

class TestReader : public wsw::DemoMetadataReader {
public:
	TestReader( const char *data, size_t dataSize ) : wsw::DemoMetadataReader( data, dataSize ) {}
	[[nodiscard]]
	auto readAll() -> QPair<QMap<QString, QString>, QSet<QString>> {
		QMap<QString, QString> pairs;
		QSet<QString> tags;
		while( hasNextPair() ) {
			if( const auto maybeKeyValue = readNextPair() ) {
				const auto [key, value] = *maybeKeyValue;
				// fromUtf8 and [] syntax allows fitting the line limit
				pairs[QString::fromUtf8( key.data(), key.size() )] = QString::fromUtf8( value.data(), value.size() );
			} else {
				return { {}, {} };
			}
		}
		while( hasNextTag() ) {
			if( const auto maybeTag = readNextTag() ) {
				tags.insert( QString::fromUtf8( maybeTag->data(), maybeTag->size() ) );
			} else {
				return { {}, {} };
			}
		}
		return { pairs, tags };
	}
};

void DemoMetadataTest::test_write() {
	char buffer[SNAP_MAX_DEMO_META_DATA_SIZE];
	wsw::DemoMetadataWriter writer( buffer );
	writer.writePair( "key1"_asView, "value1"_asView );
	writer.writePair( "key2"_asView, ""_asView );
	writer.writePair( "key3"_asView, "value3"_asView );

	auto [dataSize, wasComplete] = writer.markCurrentResult();
	QVERIFY( wasComplete );

	// _asView preserves the literal length
	const wsw::StringView stringPartView( "key1\0value1\0key2\0\0key3\0value3\0"_asView );
	const QString actualStringPart( QString::fromLatin1( buffer + 8, dataSize - 8 ) );
	const QString expectedStringPart( QString::fromLatin1( stringPartView.data(), stringPartView.size() ) );
	QCOMPARE( actualStringPart, expectedStringPart );
}

void DemoMetadataTest::test_read() {
	// _asView preserves the literal length
	const wsw::StringView stringPartView( "key1\0value1\0key2\0\0key3\0value3\0tag\0"_asView );
	QByteArray data;
	data.resize( 8 );
	uint32_t numPairs = 3, numTags = 1;
	numPairs = LittleLong( numPairs );
	numTags = LittleLong( numTags );
	std::memcpy( data.data() + 0, &numPairs, 4 );
	std::memcpy( data.data() + 4, &numTags, 4 );
	data += QByteArray( stringPartView.data(), stringPartView.size() );

	const QMap<QString, QString> expectedPairs { { { "key1", "value1" }, { "key2", "" }, { "key3", "value3" } } };
	const QSet<QString> expectedTags { "tag" };
	TestReader reader( data.data(), data.size() );
	const auto [actualPairs, actualTags] = reader.readAll();
	QCOMPARE( actualPairs, expectedPairs );
	QCOMPARE( actualTags, expectedTags );
}

void DemoMetadataTest::test_readWritten() {
	const QMap<QString, QString> originalPairs {
		{ "key1", "value1" }, { "key2", "value2" },
		{ "key3", "" }, { "key4", "value4" },
		{ "key5", "" }, { "key6", "" }, { "key7", "value7" }
	};
	const QSet<QString> originalTags { "tag1", "tag2", "tag3", "tag4", "tag5" };

	char buffer[SNAP_MAX_DEMO_META_DATA_SIZE];
	TestWriter writer( buffer );
	writer.writeAll( originalPairs, originalTags );

	auto [dataSize, wasComplete] = writer.markCurrentResult();
	QVERIFY( wasComplete );

	TestReader reader( buffer, dataSize );
	const auto [actualPairs, actualTags] = reader.readAll();
	QCOMPARE( actualPairs, originalPairs );
	QCOMPARE( actualTags, originalTags );
}