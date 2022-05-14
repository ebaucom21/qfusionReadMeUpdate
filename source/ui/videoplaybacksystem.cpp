#include "videoplaybacksystem.h"

#include <QThread>
#include <QImage>
#include <QVideoSurfaceFormat>

#include "../qcommon/links.h"
#include "../qcommon/singletonholder.h"

namespace wsw::ui {

void VideoSource::setFilePath( const QByteArray &filePath ) {
	if( m_filePath != filePath ) {
		m_filePath = filePath;

		if( m_decoder ) {
			Q_EMIT deleteDecoder();
			disconnect( m_decoder );
			m_decoder = nullptr;
		}

		const Status oldStatus = m_status;
		if( auto maybeHandle = wsw::fs::openAsReadHandle( wsw::StringView( filePath.data(), filePath.size() ) ) ) {
			m_decoder = m_playbackSystem->newDecoder( this, std::move( *maybeHandle ) );

			connect( this, &VideoSource::updateRequested,
					 m_decoder, &VideoDecoder::onUpdateRequested, Qt::QueuedConnection );
			connect( this, &VideoSource::deleteDecoder,
					 m_decoder, &VideoDecoder::deleteLater, Qt::QueuedConnection );
			connect( m_decoder, &VideoDecoder::frameAvailable,
					 this, &VideoSource::onFrameAvailable, Qt::QueuedConnection );

			m_status = Running;
		} else {
			m_status = Error;
		}

		Q_EMIT filePathChanged( filePath );

		if( oldStatus != m_status ) {
			Q_EMIT statusChanged( m_status );
		}
	}
}

void VideoSource::setVideoSurface( QAbstractVideoSurface *videoSurface ) {
	if( m_videoSurface != videoSurface ) {
		m_videoSurface = videoSurface;
		const Status oldStatus = m_status;
		if( videoSurface ) {
			bool supportsDesiredFormat = false;
			const auto desiredFormat = QVideoFrame::Format_ARGB32;
			for( const QVideoFrame::PixelFormat &pixelFormat: videoSurface->supportedPixelFormats() ) {
				if( pixelFormat == desiredFormat ) {
					supportsDesiredFormat = true;
					break;
				}
			}
			if( !supportsDesiredFormat ) {
				m_status = Error;
			} else {
				if( !videoSurface->start( QVideoSurfaceFormat( QSize(), desiredFormat ) ) ) {
					m_status = Error;
				}
			}
		} else {
			if( m_decoder ) {
				Q_EMIT deleteDecoder();
				disconnect( m_decoder );
				m_decoder = nullptr;
			}
			m_status = Idle;
		}
		Q_EMIT videoSurfaceChanged( videoSurface );
		if( oldStatus != m_status ) {
			Q_EMIT statusChanged( m_status );
		}
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
	VideoPlaybackSystem::instance()->unregisterSource( this );
}

void VideoSource::onFrameAvailable( const QVideoFrame &frame ) {
	if( m_videoSurface ) {
		const Status oldStatus = m_status;
		if( frame.isValid() ) {
			if( !m_videoSurface->present( frame ) ) {
				m_status = Error;
			}
		} else {
			m_status = Error;
		}
		if( oldStatus != m_status ) {
			Q_EMIT statusChanged( m_status );
		}
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
		for( unsigned i = 0; i < dataSize - 1; ++i ) {
			if( data[i] == 0xFF && data[i + 1] == 0xD9 ) {
				return i + 2;
			}
		}
	}
	return std::nullopt;
}

auto VideoDecoder::decodeNextFrame() -> QImage {
	for(;; ) {
		if( const auto maybeDelimiter = findJpegEndFrameDelimiter( m_dataBuffer.data(), m_dataBuffer.size() ) ) {
			QImage image( QImage::fromData( m_dataBuffer.data(), (int)*maybeDelimiter ) );
			// This should be a circular buffer... but still, JPEG decoding is much more expensive than this memmove()
			m_dataBuffer.erase( m_dataBuffer.begin(), m_dataBuffer.begin() + *maybeDelimiter );
			return image;
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