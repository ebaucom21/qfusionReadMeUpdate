#include "local.h"
#include "../qcommon/qcommon.h"
#include "../ref/ref.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

namespace wsw::ui {

class HtmlColorNamesCache {
	QByteArray names[10];
public:
	auto getColorName( int colorNum ) -> const QByteArray & {
		assert( (unsigned)colorNum < 10u );
		if( !names[colorNum].isEmpty() ) {
			return names[colorNum];
		}
		const float *rawColor = color_table[colorNum];
		names[colorNum] = QColor::fromRgbF( rawColor[0], rawColor[1], rawColor[2] ).name( QColor::HexRgb ).toLatin1();
		return names[colorNum];
	}
};

static HtmlColorNamesCache htmlColorNamesCache;

static const QLatin1String kFontOpeningTagPrefix( "<font color=\"" );
static const QLatin1String kFontOpeningTagSuffix( "\">" );
static const QLatin1String kFontClosingTag( "</font>" );

// Can't be written as a regular ASCII literal with a 5-character length
static const char kEntityCharsToEscapeData[5] { '>', '<', '&', '\"', (char)0xA0 };
static const wsw::CharLookup kEntityCharsToEscapeLookup( wsw::StringView( kEntityCharsToEscapeData, 5 ) );

static const QLatin1String kEntityGt( "&gt;" );
static const QLatin1String kEntityLt( "&lt;" );
static const QLatin1String kEntityAmp( "&amp;" );
static const QLatin1String kEntityQuot( "&quot;" );
static const QLatin1String kEntityNbsp( "&nbsp;" );

static void appendUtf8View( QString *__restrict dest, const wsw::StringView &__restrict view ) {
	bool hasHiChars = true;
	// Try saving allocations
	if( view.length() < 64 ) {
		hasHiChars = false;
		for( const char ch : view ) {
			if( (unsigned char)ch > 127u ) {
				hasHiChars = true;
				break;
			}
		}
	}
	if( hasHiChars ) {
		dest->append( QString::fromUtf8( view.data(), (int)view.size() ) );
	} else {
		dest->append( QLatin1String( view.data(), (int)view.size() ) );
	}
}

static void appendEscapingEntities( QString *__restrict dest, const wsw::StringView &__restrict src ) {
	wsw::StringView sv( src );
	for(;; ) {
		const std::optional<unsigned> maybeIndex = sv.indexOf( kEntityCharsToEscapeLookup );
		if( !maybeIndex ) {
			appendUtf8View( dest, sv );
			return;
		}

		const auto index = *maybeIndex;
		appendUtf8View( dest, sv.take( index ) );
		const char ch = sv[index];
		sv = sv.drop( index + 1 );

		// The subset of actually supported entities is tiny
		// https://github.com/qt/qtdeclarative/blob/dev/src/quick/util/qquickstyledtext.cpp
		// void QQuickStyledTextPrivate::parseEntity(const QChar *&ch, const QString &textIn, QString &textOut)

		// TODO: Aren't &quot; and &nbsp; stripped by the engine?
		// Still, some content could be supplied by external sources like info servers.
		if( ch == '>' ) {
			dest->append( kEntityGt );
		} else if( ch == '<' ) {
			dest->append( kEntityLt );
		} else if( ch == '&' ) {
			dest->append( kEntityAmp );
		} else if( ch == '"' ) {
			dest->append( kEntityQuot );
		} else if( (int)ch == 0xA0 ) {
			dest->append( kEntityNbsp );
		}
	}
}

auto toStyledText( const wsw::StringView &text ) -> QString {
	QString result;
	result.reserve( (int)text.size() + 1 );

	bool hadColorToken = false;
	wsw::StringView sv( text );
	for(;; ) {
		const std::optional<unsigned> maybeIndex = sv.indexOf( '^' );
		if( !maybeIndex ) {
			appendEscapingEntities( &result, sv );
			if( hadColorToken ) {
				result.append( kFontClosingTag );
			}
			return result;
		}

		const auto index = *maybeIndex;
		appendEscapingEntities( &result, sv.take( index ) );
		sv = sv.drop( index + 1 );
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

auto wrapInColorTags( const wsw::StringView &text, int rgb ) -> QString {
	const int r = COLOR_R( rgb ), g = COLOR_G( rgb ), b = COLOR_B( rgb );
	constexpr const char *digits = "0123456789ABCDEF";
	const char colorBuffer[7] {
		'#', digits[r / 16], digits[r % 16], digits[g / 16], digits[g % 16], digits[b / 16], digits[b % 16]
	};

	auto sizeToReserve = (int)text.size();
	sizeToReserve += kFontOpeningTagPrefix.size() + kFontOpeningTagSuffix.size() + kFontClosingTag.size();
	sizeToReserve += sizeof( colorBuffer );

	QString result;
	result.reserve( sizeToReserve );

	result.append( kFontOpeningTagPrefix );
	result.append( QLatin1String( colorBuffer, (int)std::size( colorBuffer ) ) );
	result.append( kFontOpeningTagSuffix );
	appendEscapingEntities( &result, text );
	result.append( kFontClosingTag );

	return result;
}

auto formatPing( int ping ) -> QByteArray {
	ping = wsw::clamp( ping, 0, 999 );
	int colorNum;
	if( ping < 50 ) {
		colorNum = 2;
	} else if( ping < 100 ) {
		colorNum = 3;
	} else if( ping < 150 ) {
		colorNum = 8;
	} else {
		colorNum = 1;
	}
	const QByteArray &colorName = htmlColorNamesCache.getColorName( colorNum );
	const int totalTagsSize = kFontOpeningTagPrefix.size() + kFontOpeningTagSuffix.size() + kFontClosingTag.size();
	QByteArray result;
	result.reserve( totalTagsSize + colorName.size() + 3 );
	result.setNum( ping );
	result.prepend( kFontOpeningTagSuffix.data(), kFontOpeningTagSuffix.size() );
	result.prepend( colorName );
	result.prepend( kFontOpeningTagPrefix.data(), kFontOpeningTagPrefix.size() );
	result.append( kFontClosingTag.data(), kFontClosingTag.size() );
	return result;
}

[[nodiscard]]
static auto rasterizeSvg( QSvgRenderer *renderer, int w, int h, int border ) -> QImage {
	assert( border >= 0 && w > 2 * border && h > 2 * border );
	QImage image( w, h, QImage::Format_RGBA8888 );
	image.fill( Qt::transparent );
	QPainter painter( &image );
	painter.setRenderHint( QPainter::Antialiasing );
	renderer->render( &painter, QRect( border, border, w - 2 * border, h - 2 * border ) );
	return image;
}

[[nodiscard]]
static auto estimateCrispness( const QImage &image ) -> float {
	if( !image.size().isValid() || image.size().isEmpty() ) {
		return 0.0f;
	}

	assert( image.format() == QImage::Format_RGBA8888 );
	const auto *__restrict data = image.constBits();
	const unsigned dataSize = image.sizeInBytes();

	unsigned numAlphaTransitionPixels = 0;
	for( unsigned i = 0; i + 3 < dataSize; ++i ) {
		const auto alpha = data[i + 3];
		numAlphaTransitionPixels += ( alpha != 0 ) & ( alpha != 255 );
	}

	const auto totalNumPixels = (unsigned)image.width() * (unsigned)image.height();
	assert( totalNumPixels == dataSize / 4 );
	return (float)( totalNumPixels - numAlphaTransitionPixels ) / (float)( numAlphaTransitionPixels );
}

[[nodiscard]]
auto rasterizeSvg( const QByteArray &data, const ImageOptions &options ) -> QImage {
	assert( options.desiredSize );
	const auto desiredWidth  = (int)options.desiredSize->first;
	const auto desiredHeight = (int)options.desiredSize->second;
	const auto borderWidth   = (int)options.borderWidth;
	assert( desiredWidth > 2 * borderWidth && desiredHeight > 2 * borderWidth );

	QSvgRenderer renderer( data );
	if( !renderer.isValid() ) {
		return QImage();
	}

	QImage image( rasterizeSvg( &renderer, desiredWidth, desiredHeight, borderWidth ) );
	if( desiredWidth < 2 || desiredHeight < 2 ) {
		return image;
	}

	if( !options.fitSizeForCrispness ) {
		return image;
	}

	QImage altImage( rasterizeSvg( &renderer, desiredWidth - 1, desiredHeight - 1, borderWidth ) );
	return estimateCrispness( image ) > estimateCrispness( altImage ) ? image : altImage;
}

// The dest is assumed to accept ARGB8 pixels
// TODO: Allow caching parsed SVG data
// TODO: Use spans for supplying arguments
[[nodiscard]]
auto rasterizeSvg( const void *rawSvgData, size_t rawSvgDataSize,
				   void *dest, size_t destCapacity, const ImageOptions &options )
				   -> std::optional<std::pair<unsigned, unsigned>> {
	if( !options.desiredSize ) {
		wsw::failWithLogicError( "The desired size must be specified" );
	}

	const auto [desiredWidth, desiredHeight] = *options.desiredSize;
	const size_t expectedSize = 4 * desiredWidth * desiredHeight;
	if( destCapacity < expectedSize ) {
		wsw::failWithOutOfRange( "The dest buffer has an insufficient capacity" );
	}

	const QByteArray data( QByteArray::fromRawData( (const char *)rawSvgData, (int)rawSvgDataSize ) );
	const QImage image( rasterizeSvg( data, options ) );
	if( image.isNull() ) {
		return std::nullopt;
	}

	assert( ( (size_t)image.sizeInBytes() == expectedSize ) || options.fitSizeForCrispness );
	std::memcpy( dest, image.constBits(), image.sizeInBytes() );
	return std::make_pair( (unsigned)image.width(), (unsigned)image.height() );
}

}