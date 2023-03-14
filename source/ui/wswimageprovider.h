#ifndef WSW_1094ec95_82ea_4513_8646_70d16650429e_H
#define WSW_1094ec95_82ea_4513_8646_70d16650429e_H

#include <QQuickAsyncImageProvider>
#include <QThreadPool>
#include <QRunnable>

#include "../qcommon/qcommon.h"
#include "../qcommon/wswstaticstring.h"

namespace wsw::ui {

class WswImageResponse : public QQuickImageResponse {
	Q_OBJECT

	friend class WswImageProvider;

	const QByteArray m_name;
	const QSize m_requestedSize;

	QImage m_image;

	[[nodiscard]]
	bool loadSvg( const QByteArray &fileData );
	[[nodiscard]]
	bool loadOther( const QByteArray &fileData, const char *ext );
public:
	WswImageResponse( const QString &name, const QSize &requestedSize )
		: m_name( name.toUtf8() ), m_requestedSize( requestedSize ) {}

	Q_SIGNAL void ready();

	void exec();

	[[nodiscard]]
	auto textureFactory() const -> QQuickTextureFactory * override;
};

class WswImageProvider : public QQuickAsyncImageProvider {
	QThreadPool m_threadPool;
public:
	WswImageProvider();

	auto requestImageResponse( const QString &id, const QSize &requestedSize ) -> WswImageResponse * override;
};

}

#endif
