#include "local.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/wswstringview.h"

#include <QColor>

namespace wsw::ui {

class HtmlColorNamesCache {
	QString names[10];
public:
	auto getColorName( int colorNum ) -> const QString & {
		assert( (unsigned)colorNum < 10u );
		if( !names[colorNum].isEmpty() ) {
			return names[colorNum];
		}
		const float *rawColor = color_table[colorNum];
		names[colorNum] = QColor::fromRgbF( rawColor[0], rawColor[1], rawColor[2] ).name( QColor::HexRgb );
		return names[colorNum];
	}
};

static HtmlColorNamesCache htmlColorNamesCache;

static const QLatin1String kFontOpeningTagPrefix( "<font color=\"" );
static const QLatin1String kFontOpeningTagSuffix( "\">" );
static const QLatin1String kFontClosingTag( "</font>" );

auto toStyledText( const wsw::StringView &text ) -> QString {
	QString result;
	result.reserve( (int)text.size() + 1 );

	bool hadColorToken = false;
	wsw::StringView sv( text );
	for(;;) {
		std::optional<unsigned> index = sv.indexOf( '^' );
		if( !index ) {
			result.append( QLatin1String( sv.data(), sv.size() ) );
			if( hadColorToken ) {
				result.append( kFontClosingTag );
			}
			return result;
		}

		result.append( QLatin1String( sv.data(), (int)*index ) );
		sv = sv.drop( *index + 1 );
		if( sv.empty() ) {
			if( hadColorToken ) {
				result.append( kFontClosingTag );
			}
			return result;
		}

		if( sv.front() < '0' || sv.front() > '9' ) {
			if( sv.front() == '^' ) {
				result.append( '^' );
				sv = sv.drop( 1 );
			}
			continue;
		}

		if( hadColorToken ) {
			result.append( kFontClosingTag );
		}

		result.append( kFontOpeningTagPrefix );
		result.append( htmlColorNamesCache.getColorName( sv.front() - '0' ) );
		result.append( kFontOpeningTagSuffix );

		sv = sv.drop( 1 );
		hadColorToken = true;
	}
}

}