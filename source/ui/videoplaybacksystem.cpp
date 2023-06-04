#include "videoplaybacksystem.h"

#include <QThread>
#include <QImage>
#include <QVideoSurfaceFormat>

#include "../qcommon/links.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/singletonholder.h"
#include "../client/imageloading.h"
#include "local.h"

#include <QDebug>

namespace wsw::ui {

void VideoSource::setFilePath( const QByteArray &filePath ) {
	if( m_filePath != filePath ) {
		m_filePath = filePath;

		detachDecoder();
		stopVideoSurface();

		Status status = m_status;
		if( filePath.isEmpty() ) {
			status = Idle;
		} else if( auto maybeHandle = wsw::fs::openAsReadHandle( wsw::StringView( filePath.data(), filePath.size() ) ) ) {
			m_decoder = m_playbackSystem->newDecoder( this, std::move( *maybeHandle ) );

			connect( this, &VideoSource::updateRequested,
					 m_decoder, &VideoDecoder::onUpdateRequested, Qt::QueuedConnection );
			connect( this, &VideoSource::deleteDecoder,
					 m_decoder, &VideoDecoder::deleteLater, Qt::QueuedConnection );
			connect( m_decoder, &VideoDecoder::frameAvailable,
					 this, &VideoSource::onFrameAvailable, Qt::QueuedConnection );

			status = Running;
		} else {
			uiWarning() << "Failed to open the video file" << filePath;
			status = Error;
		}

		Q_EMIT filePathChanged( filePath );
		applyStatus( status );
	}
}

void VideoSource::setVideoSurface( QAbstractVideoSurface *videoSurface ) {
	if( m_videoSurface != videoSurface ) {
		m_videoSurface = videoSurface;
		Status status = m_status;
		if( videoSurface ) {
			bool supportsDesiredFormat = false;
			const auto desiredFormat = QVideoFrame::Format_ARGB32;
			for( const QVideoFrame::PixelFormat &pixelFormat: videoSurface->supportedPixelFormats() ) {
				if( pixelFormat == desiredFormat ) {
					supportsDesiredFormat = true;
				}
			}
			if( !supportsDesiredFormat ) {
				status = Error;
			} else {
				if( !videoSurface->start( QVideoSurfaceFormat( QSize(), desiredFormat ) ) ) {
					status = Error;
				}
			}
		} else {
			status = Idle;
		}
		Q_EMIT videoSurfaceChanged( videoSurface );
		applyStatus( status );
	}
}

VideoSource::VideoSource() {
	VideoPlaybackSystem::instance()->registerSource( this );
}

VideoSource::~VideoSource() {
	if( m_decoder ) {
		Q_EMIT deleteDecoder();
		m_decoder = nullptr;
	}
	stopVideoSurface();
	VideoPlaybackSystem::instance()->unregisterSource( this );
}

void VideoSource::onFrameAvailable( const QVideoFrame &frame ) {
	if( m_videoSurface ) {
		Status status = m_status;
		if( frame.isValid() ) {
			if( !m_videoSurface->isFormatSupported( QVideoSurfaceFormat( frame.size(), frame.pixelFormat() ) ) ) {
				status = Error;
			} else {
				if( !m_videoSurface->present( frame ) ) {
					status = Error;
				}
			}
		} else {
			status = Error;
		}
		applyStatus( status );
	} else {
		applyStatus( Idle );
	}
}

void VideoSource::detachDecoder() {
	if( m_decoder ) {
		Q_EMIT deleteDecoder();
		disconnect( m_decoder );
		m_decoder = nullptr;
	}
}

void VideoSource::stopVideoSurface() {
	if( m_videoSurface ) {
		m_videoSurface->stop();
	}
}

void VideoSource::applyStatus( Status status ) {
	if( m_status != status ) {
		switch( status ) {
			case Idle: uiDebug() << "Video source status=Idle filePath=" << m_filePath; break;
			case Running: uiDebug() << "Video source status=Running filePath=" << m_filePath; break;
			case Error: uiDebug() << "Video source status=Error filePath=" << m_filePath; break;
		}
		if( status != Running ) {
			detachDecoder();
			stopVideoSurface();
			m_filePath.clear();
		}
		m_status = status;
		Q_EMIT statusChanged( status );
	}
}

void VideoDecoder::onUpdateRequested( int64_t timestamp ) {
	if( m_lastUpdateTimestamp + m_frameTime <= timestamp ) {
		// Update it prior to decoding
		m_lastUpdateTimestamp = timestamp;
		Q_EMIT frameAvailable( QVideoFrame( decodeNextFrame() ) );
	}
}

[[nodiscard]]
static auto findJpegEndFrameDelimiter( const uint8_t *__restrict data, unsigned dataSize ) -> std::optional<unsigned> {
	if( dataSize > 1 ) {
		unsigned i = 0;
		do {
			if( ( data[i] == 0xFF ) & ( data[i + 1] == 0xD9 ) ) {
				return i + 2;
			}
		} while( ++i < dataSize - 1 );
	}
	return std::nullopt;
}

auto VideoDecoder::decodeNextFrame() -> QImage {
	for(;; ) {
		if( const auto maybeDelimiter = findJpegEndFrameDelimiter( m_dataBuffer.data(), m_dataBuffer.size() ) ) {
			unsigned width = 0, height = 0;
			uint8_t *bytes = wsw::decodeImageData( m_dataBuffer.data(), *maybeDelimiter, &width, &height, nullptr, 4 );
			// This should be a circular buffer... but still, JPEG decoding is much more expensive than this memmove()
			m_dataBuffer.erase( m_dataBuffer.begin(), m_dataBuffer.begin() + *maybeDelimiter );
			if( bytes ) {
				return QImage( bytes, width, height, QImage::Format_ARGB32, ::free );
			}
			return QImage();
		}

		if( m_handle.isAtEof() ) {
			(void)m_handle.rewind();
			continue;
		}

		// Append additional read bytes to the end of the buffer and continue finding the delimiter on the next step

		const auto oldSize = m_dataBuffer.size();
		constexpr auto chunkSize = 16u * 4096u;
		m_dataBuffer.resize( m_dataBuffer.size() + chunkSize );

		const auto maybeBytesRead = m_handle.read( m_dataBuffer.data() + oldSize, chunkSize );
		if( maybeBytesRead == std::nullopt ) {
			return QImage();
		}

		const auto bytesRead = *maybeBytesRead;
		if( bytesRead < chunkSize ) {
			const auto unusedTailSize = ( chunkSize - bytesRead );
			assert( m_dataBuffer.size() > unusedTailSize );
			m_dataBuffer.resize( m_dataBuffer.size() - unusedTailSize );
		}
	}
}

static SingletonHolder<VideoPlaybackSystem> g_instanceHolder;

void VideoPlaybackSystem::init() {
	g_instanceHolder.init();
}

void VideoPlaybackSystem::shutdown() {
	g_instanceHolder.shutdown();
}

auto VideoPlaybackSystem::instance() -> VideoPlaybackSystem * {
	return g_instanceHolder.instance();
}

void VideoPlaybackSystem::registerSource( VideoSource *source ) {
	wsw::link( source, &m_sourcesHead );
	source->m_playbackSystem = this;
}

void VideoPlaybackSystem::unregisterSource( VideoSource *source ) {
	wsw::unlink( source, &m_sourcesHead );
}

auto VideoPlaybackSystem::newDecoder( VideoSource *source, wsw::fs::ReadHandle &&handle ) -> VideoDecoder * {
	auto *decoder = new VideoDecoder( source, std::move( handle ) );
	if( !m_decoderThread ) {
		m_decoderThread = new QThread;
		m_decoderThread->start( QThread::NormalPriority );
	}
	decoder->moveToThread( m_decoderThread );
	connect( decoder, &QObject::destroyed, this, &VideoPlaybackSystem::decoderDestroyed, Qt::QueuedConnection );
	assert( m_numActiveDecoders >= 0 );
	m_numActiveDecoders++;
	return decoder;
}

void VideoPlaybackSystem::decoderDestroyed( QObject * ) {
	assert( m_numActiveDecoders > 0 );
	m_numActiveDecoders--;
}

void VideoPlaybackSystem::update( int64_t timestamp ) {
	if( m_sourcesHead ) {
		for( VideoSource *source = m_sourcesHead; source; source = source->next ) {
			Q_EMIT source->updateRequested( timestamp );
		}
		m_lastActivityTimestamp = timestamp;
	} else {
		if( m_decoderThread ) {
			if( m_lastActivityTimestamp + 10'000 < timestamp ) {
				// TODO: Just check? This relies upon timings, unlikely to fail though
				assert( !m_numActiveDecoders );
				m_decoderThread->quit();
				m_decoderThread = nullptr;
			}
		}
	}
}

VideoPlaybackSystem::~VideoPlaybackSystem() {
	if( m_decoderThread ) {
		m_decoderThread->quit();
		m_decoderThread->wait();
	}
}

}