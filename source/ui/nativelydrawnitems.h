#ifndef WSW_a0dc8c82_6aa3_43c1_8c33_e5fad1cf6177_H
#define WSW_a0dc8c82_6aa3_43c1_8c33_e5fad1cf6177_H

#include <QQuickItem>
#include <QtGui/QVector3D>

struct shader_s;
struct model_s;
struct skinfile_s;

namespace wsw::ui {

class QtUISystem;

class NativelyDrawn {
	friend class QtUISystem;
protected:
	virtual ~NativelyDrawn() = default;

	int m_nativeZ { 0 };
	unsigned m_reloadRequestMask { 0 };
	bool m_isLinked { false };
public:
	NativelyDrawn *next { nullptr };
	NativelyDrawn *prev { nullptr };

	[[nodiscard]]
	virtual bool isLoaded() const = 0;
	virtual void drawSelfNatively() = 0;
};

class NativelyDrawnImage : public QQuickItem, public NativelyDrawn {
	Q_OBJECT

	shader_s *m_material { nullptr };
	QString m_materialName;

	QSize m_sourceSize { 0, 0 };

	Q_SIGNAL void materialNameChanged( const QString &materialName );
	void setMaterialName( const QString &materialName );

	Q_SIGNAL void nativeZChanged( int nativeZ );
	void setNativeZ( int nativeZ );

	Q_SIGNAL void sourceSizeChanged( const QSize &sourceSize );

	Q_PROPERTY( int nativeZ MEMBER m_nativeZ WRITE setNativeZ NOTIFY nativeZChanged )
	Q_PROPERTY( QString materialName MEMBER m_materialName WRITE setMaterialName NOTIFY materialNameChanged )
	Q_PROPERTY( QSize sourceSize MEMBER m_sourceSize NOTIFY sourceSizeChanged );

	Q_SIGNAL void isLoadedChanged( bool isLoaded );
	Q_PROPERTY( bool isLoaded READ isLoaded NOTIFY isLoadedChanged )

	[[nodiscard]]
	bool isLoaded() const override;

	void reloadIfNeeded();
	void updateSize( int w, int h );
public:
	explicit NativelyDrawnImage( QQuickItem *parent = nullptr );

	void drawSelfNatively() override;
};

class NativelyDrawnModel : public QQuickItem, public NativelyDrawn {
	Q_OBJECT

	model_s *m_model { nullptr };
	skinfile_s *m_skinFile { nullptr };

	enum : unsigned { ReloadModel = 0x1, ReloadSkin = 0x2 };

	QString m_modelName;
	QString m_skinName;
	QVector3D m_modelOrigin;
	QVector3D m_viewOrigin;
	QVector3D m_rotationAxis;

	qreal m_rotationSpeed { 0.0 };
	qreal m_viewFov { 90.0 };
private:
	Q_SIGNAL void modelNameChanged();
	void setModelName( const QString &modelName );

	Q_SIGNAL void skinNameChanged();
	void setSkinName( const QString &skinName );

	Q_SIGNAL void nativeZChanged();
	void setNativeZ( int nativeZ );

	Q_SIGNAL void modelOriginChanged( const QVector3D &modelOrigin );
	void setModelOrigin( const QVector3D &modelOrigin );

	Q_SIGNAL void viewOriginChanged( const QVector3D &viewOrigin );
	void setViewOrigin( const QVector3D &viewOrigin );

	Q_SIGNAL void rotationAxisChanged( const QVector3D &rotationAxis );
	void setRotationAxis( const QVector3D &rotationAxis );

	Q_SIGNAL void rotationSpeedChanged( qreal rotationSpeed );
	void setRotationSpeed( qreal rotationSpeed );

	Q_SIGNAL void viewFovChanged( qreal viewFov );
	void setViewFov( qreal viewFov );

	Q_PROPERTY( QString modelName MEMBER m_modelName WRITE setModelName NOTIFY modelNameChanged )
	Q_PROPERTY( QString skinName MEMBER m_skinName WRITE setSkinName NOTIFY skinNameChanged )
	Q_PROPERTY( int nativeZ MEMBER m_nativeZ WRITE setNativeZ NOTIFY nativeZChanged )
	Q_PROPERTY( QVector3D modelOrigin MEMBER m_modelOrigin WRITE setModelOrigin NOTIFY modelOriginChanged )
	Q_PROPERTY( QVector3D viewOrigin MEMBER m_viewOrigin WRITE setViewOrigin NOTIFY viewOriginChanged )
	Q_PROPERTY( QVector3D rotationAxis MEMBER m_rotationAxis WRITE setRotationAxis NOTIFY rotationAxisChanged )
	Q_PROPERTY( qreal rotationSpeed MEMBER m_rotationSpeed WRITE setRotationSpeed NOTIFY rotationSpeedChanged )
	Q_PROPERTY( qreal viewFov MEMBER m_viewFov WRITE setViewFov NOTIFY viewFovChanged )

	Q_SIGNAL void isLoadedChanged( bool isLoaded );
	Q_PROPERTY( bool isLoaded READ isLoaded NOTIFY isLoadedChanged )

	[[nodiscard]]
	bool isLoaded() const override;

	void reloadIfNeeded();
public:
	explicit NativelyDrawnModel( QQuickItem *parent = nullptr );

	void drawSelfNatively() override;
};

}

#endif
