#include "wswimageprovider.h"

#include <QPainter>
#include <QtConcurrent>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QImageReader>
#include <QBuffer>

#include "local.h"
#include "../ref/frontend.h"
#include "../qcommon/wswfs.h"

#include "../../third-party/stb/stb_image.h"

namespace wsw::ui {

WswImageProvider::WswImageProvider() {
	// Don't spawn an excessive number of threads
	m_threadPool.setMaxThreadCount( 1 );
	m_threadPool.setExpiryTimeout( 3000 );
}

class WswImageRunnable : public QRunnable {
	WswImageResponse *const m_response;
public:
	explicit WswImageRunnable( WswImageResponse *response ) : m_response( response ) {}
	void run() override;
};

auto WswImageProvider::requestImageResponse( const QString &id, const QSize &requestedSize ) -> WswImageResponse * {
	auto *response = new WswImageResponse( id, requestedSize );
	auto *runnable = new WswImageRunnable( response );

	// Listen to the ready() signal. Emit finished() in this calling thread
	QObject::connect( response, &WswImageResponse::ready, response, &WswImageResponse::finished, Qt::QueuedConnection );

	m_threadPool.start( runnable );
	return response;
}

void WswImageRunnable::run() {
	m_response->exec();
	Q_EMIT m_response->ready();
}

static const char *kExtensions[] = { ".svg", ".tga", ".png", ".webp", ".jpg" };
static const char *kTag = "WswImageResponse";

void WswImageResponse::exec() {
	// TODO: Use some sane API
	const auto numExtensions = (int)( std::end( kExtensions ) - std::begin( kExtensions ) );
	const char *ext = FS_FirstExtension( m_name.constData(), kExtensions, numExtensions );
	if( !ext ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to find a first extension for %s\n", kTag, m_name.constData() );
		return;
	}

	wsw::StaticString<MAX_QPATH> path;
	path << wsw::StringView( m_name.constData() ) << wsw::StringView( ext );

	auto fileHandle = wsw::fs::openAsReadHandle( path.asView() );
	if( !fileHandle ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to open %s\n", kTag, path.data() );
		return;
	}

	QByteArray fileData( fileHandle->getInitialFileSize(), Qt::Uninitialized );
	if( fileHandle->read( fileData.data(), fileData.size() ) != std::optional( fileData.size() ) ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to read %s\n", kTag, path.data() );
		return;
	}

	if( !strcmp( ext, ".svg" ) ) {
		// No resize needed
		(void)loadSvg( fileData );
		return;
	}

	if( !strcmp( ext, ".tga" ) ) {
		if( !loadTga( fileData ) ) {
			return;
		}
	} else {
		if( !loadOther( fileData, ext ) ) {
			return;
		}
	}

	if( m_requestedSize.isValid() ) {
		m_image = m_image.scaled( m_requestedSize );
		if( m_image.isNull() ) {
			auto [w, h] = std::make_pair( m_requestedSize.width(), m_requestedSize.height() );
			Com_Printf( S_COLOR_YELLOW "%s: Failed to scale %s to %dx%d\n", kTag, m_name.constData(), w, h );
		}
	}
}

bool WswImageResponse::loadSvg( const QByteArray &fileData ) {
	if( !m_requestedSize.isValid() || m_requestedSize.isEmpty() ) {
		Com_Printf( S_COLOR_YELLOW "%s: A valid size must be specified for loading an SVG image %s\n", kTag,
					m_name.constData() );
		return false;
	}
	ImageOptions imageOptions;
	imageOptions.setDesiredSize( m_requestedSize.width(), m_requestedSize.height() );
	imageOptions.fitSizeForCrispness = true;
	m_image = rasterizeSvg( fileData, imageOptions );
	if( m_image.isNull() ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to parse SVG for %s\n", kTag, m_name.constData() );
		return false;
	}
	return true;
}

bool WswImageResponse::loadTga( const QByteArray &fileData ) {
	int w = 0, h = 0, chans = 0;
	// No in-place loading wtf?
	// How does this thing get broadly suggested in the internets?
	stbi_uc *bytes = stbi_load_from_memory((stbi_uc *)fileData.data(), fileData.length(), &w, &h, &chans, 0 );
	if( !bytes ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to load %s.tga from data\n", kTag, m_name.constData() );
		return false;
	}

	if( chans == 3 ) {
		m_image = QImage( bytes, w, h, QImage::Format_RGB888, stbi_image_free );
	} else if( chans == 4 ) {
		m_image = QImage( bytes, w, h, QImage::Format_RGBA8888, stbi_image_free );
	} else {
		Com_Printf( S_COLOR_YELLOW "%s: Weird number of %s.tga image channels %d\n", kTag, m_name.constData(), chans );
		stbi_image_free( bytes );
		return false;
	}

	if( m_image.isNull() ) {
		Com_Printf( S_COLOR_YELLOW "%s: Failed to load %s.tga", kTag, m_name.constData() );
		return false;
	}

	return true;
}

bool WswImageResponse::loadOther( const QByteArray &fileData, const char *ext ) {
	QBuffer buffer( const_cast<QByteArray *>( std::addressof( fileData ) ) );
	QImageReader reader( &buffer );
	if( !reader.read( &m_image ) ) {
		const char *format = S_COLOR_YELLOW "%s: Failed to load %s from data with %s\n";
		Com_Printf( format, kTag, m_name.constData(), ext, reader.errorString().constData() );
		return false;
	}
	return true;
}

auto WswImageResponse::textureFactory() const -> QQuickTextureFactory * {
	if( m_image.isNull() ) {
		return nullptr;
	}
	return QQuickTextureFactory::textureFactoryForImage( m_image );
}

}
