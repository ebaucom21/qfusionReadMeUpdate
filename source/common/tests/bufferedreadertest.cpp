#include "bufferedreadertest.h"

#include <QFile>
#include <QTextStream>

#ifndef Q_strnicmp
#define Q_strnicmp strncasecmp
#endif

#include ".h"
#include "../wswstringsplitter.h"
#include "../wswfs.h"

static const wsw::StringView kFilePath( "README.md" );

void BufferedReaderTest::test_readToNewline() {
	QStringList expectedLines;
	{
		QFile file( kFilePath.data() );
		QVERIFY( file.open( QIODevice::ReadOnly ) );
		QTextStream stream( &file );
		while( !stream.atEnd() ) {
			expectedLines.append( stream.readLine() );
		}
	}

	auto reader = wsw::fs::openAsBufferedReader( kFilePath );
	QVERIFY( reader );
	QVERIFY( !reader->isAtEof() );

	// Use two buffers to test using different sizes
	char buffer1[8], buffer2[128];
	char *buffers[2] = { &buffer1[0], &buffer2[0] };
	const size_t sizes[2] = { sizeof( buffer1 ) - 1, sizeof( buffer2 ) - 1 };

	int turn = 0;
	QString line;
	QStringList actualLines;
	for(; !reader->isAtEof(); turn = ( turn + 1 ) % 2 ) {
		auto maybeReadResult = reader->readToNewline( buffers[turn], sizes[turn] );
		QVERIFY( maybeReadResult );
		auto [bytesRead, wasIncomplete] = *maybeReadResult;
		line.append( QString::fromUtf8( buffers[turn], bytesRead ) );
		if( !wasIncomplete ) {
			actualLines.append( line );
			line.clear();
		}
	}

	QCOMPARE( actualLines, expectedLines );
}

void BufferedReaderTest::test_readAfterReadToNewline() {
	QString expectedContent;
	{
		QFile file( kFilePath.data() );
		QVERIFY( file.open( QIODevice::ReadOnly ) );
		expectedContent = QString::fromUtf8( file.readAll() );
	}

	auto reader = wsw::fs::openAsBufferedReader( kFilePath );
	QVERIFY( reader );
	QVERIFY( !reader->isAtEof() );

	QString actualContent;

	char buffer[4096];
	auto maybeReadToNewlineResult = reader->readToNewline( buffer, 32 );
	QVERIFY( maybeReadToNewlineResult );

	actualContent.append( QString::fromUtf8( buffer, maybeReadToNewlineResult->bytesRead ) );

	QVERIFY( !reader->isAtEof() );
	auto bulkReadResult = reader->read( buffer, sizeof( buffer ) );
	QVERIFY( bulkReadResult );
	// Make sure we have the entire content
	QVERIFY( *bulkReadResult < sizeof( buffer ) );
	actualContent.append( QString::fromUtf8( buffer, *bulkReadResult ) );

	QRegularExpression regex( "[\n\r]" );
	// We've lost the first line separator by reading to a new line.
	// Do not assume whether it was a CR or a LF or a CR,LF pair.
	// Just strip newline characters for comparison.
	expectedContent = expectedContent.replace( regex, "" );
	actualContent = actualContent.replace( regex, "" );
	QCOMPARE( actualContent, expectedContent );
}

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int FS_FOpenFile( const char *path, int *fd, int mode ) {
	int rawFlags = ( mode & FS_WRITE ) ? O_RDWR : O_RDONLY;
	*fd = ::open( path, rawFlags );
	return *fd != 0;
}

void FS_FCloseFile( int fd ) {
	::close( fd );
}

int FS_Eof( int fd ) {
	// This is fine for testing purposes
	auto currOff = ::lseek( fd, 0, SEEK_CUR );
	auto endOff = ::lseek( fd, 0, SEEK_END );
	if( endOff == currOff ) {
		return 1;
	}
	(void)::lseek( fd, currOff, SEEK_SET );
	return 0;
}

int FS_Read( void *buffer, size_t length, int fd ) {
	return (int)::read( fd, buffer, length );
}

int FS_Write( const void *buffer, size_t length, int fd ) {
	return (int)::write( fd, buffer, length );
}

// Define it just to make wswfs.cpp linking with the test code
int FS_GetFileList( char const*, char const*, char*, unsigned long, int, int ) {
	abort();
}

int FS_Seek( int, int, int ) {
	abort();
}

