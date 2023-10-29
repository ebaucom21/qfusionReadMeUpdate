#include "wswimageprovider.h"

#include <QPainter>
#include <QtConcurrent>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QImageReader>
#include <QBuffer>

#include "local.h"
#include "../common/wswfs.h"
#include "../client/imageloading.h"
#include "../ref/ref.h"

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

void WswImageResponse::exec() {
	// TODO: Use some sane API
	const auto numExtensions = (int)( std::end( kExtensions ) - std::begin( kExtensions ) );
	const char *ext = FS_FirstExtension( m_name.constData(), kExtensions, numExtensions );
	if( !ext ) {
		uiWarning() << "Failed to find a first extension for" << m_name;
		return;
	}

	wsw::StaticString<MAX_QPATH> path;
	path << wsw::StringView( m_name.constData() ) << wsw::StringView( ext );

	auto fileHandle = wsw::fs::openAsReadHandle( path.asView() );
	if( !fileHandle ) {
		uiWarning() << "Failed to open" << path;
		return;
	}

	QByteArray fileData( fileHandle->getInitialFileSize(), Qt::Uninitialized );
	if( fileHandle->read( fileData.data(), fileData.size() ) != std::optional( fileData.size() ) ) {
		uiWarning() << "Failed to read" << path;
		return;
	}

	if( !strcmp( ext, ".svg" ) ) {
		// No resize needed
		(void)loadSvg( fileData );
	} else {
		if( loadOther( fileData, ext ) ) {
			if( m_requestedSize.isValid() ) {
				m_image = m_image.scaled( m_requestedSize );
				if( m_image.isNull() ) {
					uiWarning() << "Failed to scale" << m_name << "to" << m_requestedSize;
				}
			}
		}
	}
}

bool WswImageResponse::loadSvg( const QByteArray &fileData ) {
	if( !m_requestedSize.isValid() || m_requestedSize.isEmpty() ) {
		uiWarning() << "A valid size must be specified for loading an SVG image %s\n" << m_name;
		return false;
	}
	ImageOptions imageOptions;
	imageOptions.setDesiredSize( m_requestedSize.width(), m_requestedSize.height() );
	imageOptions.fitSizeForCrispness = true;
	m_image = rasterizeSvg( fileData, imageOptions );
	if( m_image.isNull() ) {
		uiWarning() << "Failed to parse SVG for %s\n" << m_name;
		return false;
	}
	return true;
}

bool WswImageResponse::loadOther( const QByteArray &fileData, const char *ext ) {
	unsigned w = 0, h = 0, chans = 0;

	uint8_t *bytes = wsw::decodeImageData( fileData.constData(), (size_t)fileData.length(), &w, &h, &chans );
	if( !bytes ) {
		uiWarning() << "Failed to load" << m_name << wsw::StringView( ext ) << "from data";
		return false;
	}

	if( chans == 3 ) {
		m_image = QImage( bytes, w, h, QImage::Format_RGB888, ::free, bytes );
	} else if( chans == 4 ) {
		m_image = QImage( bytes, w, h, QImage::Format_RGBA8888, ::free, bytes );
	} else {
		uiWarning() << "Weird number of" << m_name << wsw::StringView( ext ) << "image channels" << chans;
		free( bytes );
		return false;
	}

	if( m_image.isNull() ) {
		uiWarning() << "Failed to load" << m_name << wsw::StringView( ext );
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
