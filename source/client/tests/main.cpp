#include <QCoreApplication>
#include "materialifevaluatortest.h"
#include "materialsourcetest.h"
#include "materialparsertest.h"
#include "tokensplittertest.h"
#include "tokenstreamtest.h"

int main( int argc, char **argv ) {
	QCoreApplication app( argc, argv );
	(void)std::setlocale( LC_ALL, "C" );

	int result = 0;

	{
		TokenSplitterTest tokenSplitterTest;
		result |= QTest::qExec( &tokenSplitterTest, argc, argv );
	}

	{
		TokenStreamTest tokenStreamTest;
		result |= QTest::qExec( &tokenStreamTest, argc, argv );
	}

	{
		MaterialSourceTest materialSourceTest;
		result |= QTest::qExec( &materialSourceTest, argc, argv );
	}

	{
		MaterialIfEvaluatorTest materialIfEvaluatorTest;
		result |= QTest::qExec( &materialIfEvaluatorTest, argc, argv );
	}

	{
		MaterialParserTest materialParserTest;
		result |= QTest::qExec( &materialParserTest, argc, argv );
	}

	return result;
}

void Com_Printf( const char *fmt, ... ) {
	va_list va;
	va_start( va, fmt );
	vprintf( fmt, va );
	va_end( va );
}

void Sys_Error( const char *fmt, ... ) {
	va_list va;
	va_start( va, fmt );
	vprintf( fmt, va );
	va_end( va );
	fflush( stdout );
	abort();
}

int Q_vsnprintfz( char *buffer, size_t size, const char *fmt, va_list va ) {
	int res = vsnprintf( buffer, size, fmt, va );
	if( size ) {
		if( res >= 0 ) {
			buffer[std::min((size_t) res, size )] = '\0';
		} else {
			buffer[size] = '\0';
		}
	}
	return res;
}