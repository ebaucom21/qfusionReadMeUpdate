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
			Q_EMIT terminateRequested();
			disconnect( m_decoder );
			m_decoder = nullptr;
		}

		if( auto maybeHandle = wsw::fs::openAsReadHandle( wsw::StringView( filePath.data(), filePath.size() ) ) ) {
			m_decoder = new VideoDecoder( this, std::move( *maybeHandle ) );
			assert( m_decoderThread );
			m_decoder->moveToThread( m_decoderThread );

			connect( this, &VideoSource::updateRequested,
					 m_decoder, &VideoDecoder::onUpdateRequested, Qt::QueuedConnection );
			connect( this, &VideoSource::terminateRequested,
					 m_decoder, &VideoDecoder::onTerminateRequested, Qt::QueuedConnection );
			connect( m_decoder, &VideoDecoder::frameAvailable,
					 this, &VideoSource::onFrameAvailable, Qt::QueuedConnection );
		}

		Q_EMIT filePathChanged( filePath );
	}
}

void VideoSource::setVideoSurface( QAbstractVideoSurface *videoSurface ) {
	if( m_videoSurface != videoSurface ) {
		m_videoSurface = videoSurface;
		if( videoSurface ) {
			// Assume this format to be always available
			(void)videoSurface->start( QVideoSurfaceFormat( QSize(), QVideoFrame::Format_ARGB32 ) );
		}
		Q_EMIT videoSurfaceChanged( videoSurface );
	}
}

VideoSource::VideoSource() {
	VideoPlaybackSystem::instance()->registerSource( this );
}

VideoSource::~VideoSource() {
	VideoPlaybackSystem::instance()->unregisterSource( this );
}

void VideoSource::onFrameAvailable( const QVideoFrame &frame ) {
	if( m_videoSurface ) {
		(void)m_videoSurface->present( frame );
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

void VideoDecoder::onTerminateRequested() {
	this->deleteLater();
}

static SingletonHolder<VideoPlaybackSystem> g_instanceHolder;

void VideoPlaybackSystem::init() {
	g_instanceHolder.Init();
}

void VideoPlaybackSystem::shutdown() {
	g_instanceHolder.Shutdown();
}

auto VideoPlaybackSystem::instance() -> VideoPlaybackSystem * {
	return g_instanceHolder.Instance();
}

void VideoPlaybackSystem::registerSource( VideoSource *source ) {
	wsw::link( source, &m_sourcesHead );
	if( !m_decoderThread ) {
		m_decoderThread = new QThread;
		m_decoderThread->start( QThread::NormalPriority );
	}
	source->m_decoderThread = m_decoderThread;
}

void VideoPlaybackSystem::unregisterSource( VideoSource *source ) {
	wsw::unlink( source, &m_sourcesHead );
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