#ifndef WSW_b870222c_6bbf_4129_b53e_1e672942e648_H
#define WSW_b870222c_6bbf_4129_b53e_1e672942e648_H

#include <QObject>
#include <QAbstractVideoSurface>
#include <QVideoFrame>

#include "../qcommon/wswfs.h"
#include "../qcommon/wswvector.h"

namespace wsw::ui {

class VideoSource;

class VideoDecoder : public QObject {
	Q_OBJECT

	friend class VideoSource;
	friend class VideoPlaybackSystem;

	Q_SIGNAL void frameAvailable( QVideoFrame frame );

	Q_SLOT void onUpdateRequested( int64_t timestamp );

	[[nodiscard]]
	auto decodeNextFrame() -> QImage;

	int64_t m_lastUpdateTimestamp { 0 };
	int64_t m_frameTime { 32 };
	VideoSource *const m_source;
	wsw::fs::ReadHandle m_handle;
	wsw::Vector<uint8_t> m_dataBuffer;

	VideoDecoder( VideoSource *source, wsw::fs::ReadHandle &&handle )
		: m_source( source ), m_handle( std::forward<wsw::fs::ReadHandle>( handle ) ) {}
};

class VideoPlaybackSystem;

class VideoSource : public QObject {
	Q_OBJECT

	friend class VideoPlaybackSystem;
public:
	enum Status { Idle, Running, Error };
	Q_ENUM( Status );

private:
	Q_SIGNAL void filePathChanged( const QByteArray &filePath );
	Q_PROPERTY( QByteArray filePath MEMBER m_filePath WRITE setFilePath NOTIFY filePathChanged );

	Q_SIGNAL void videoSurfaceChanged( QAbstractVideoSurface *surface );
	Q_PROPERTY( QAbstractVideoSurface *videoSurface MEMBER m_videoSurface WRITE setVideoSurface NOTIFY videoSurfaceChanged );

	Q_SIGNAL void statusChanged( Status status );
	Q_PROPERTY( Status status MEMBER m_status NOTIFY statusChanged );

	Q_SIGNAL void updateRequested( int64_t timestamp );
	Q_SIGNAL void deleteDecoder();

	Q_SLOT void onFrameAvailable( const QVideoFrame &frame );

	void setFilePath( const QByteArray &filePath );
	void setVideoSurface( QAbstractVideoSurface *videoSurface );

	void detachDecoder();
	void stopVideoSurface();
	void applyStatus( Status status );

	QByteArray m_filePath;
	QAbstractVideoSurface *m_videoSurface { nullptr };
	VideoDecoder *m_decoder { nullptr };
	VideoPlaybackSystem *m_playbackSystem { nullptr };
	Status m_status { Idle };
public:
	VideoSource();
	~VideoSource() override;

	// TODO: Make link()/unlink() operate on prefixed members
	VideoSource *prev { nullptr }, *next { nullptr };
};

class VideoPlaybackSystem : public QObject {
	Q_OBJECT

	Q_SLOT void decoderDestroyed( QObject * );

	VideoSource *m_sourcesHead { nullptr };
	QThread *m_decoderThread { nullptr };
	int64_t m_lastActivityTimestamp { 0 };
	int m_numActiveDecoders { 0 };
public:
	~VideoPlaybackSystem() override;

	static void init();
	static void shutdown();
	static auto instance() -> VideoPlaybackSystem *;

	void registerSource( VideoSource *source );
	void unregisterSource( VideoSource *source );

	[[nodiscard]]
	auto newDecoder( VideoSource *source, wsw::fs::ReadHandle &&handle ) -> VideoDecoder *;

	void update( int64_t timestamp );
};

}

#endif