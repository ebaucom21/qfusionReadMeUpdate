#include "nativelydrawnitems.h"

#include "../qcommon/qcommon.h"
#include "../ref/frontend.h"

namespace wsw::ui {

NativelyDrawnImage::NativelyDrawnImage( QQuickItem *parent )
	: QQuickItem( parent ) {
}

void NativelyDrawnImage::setNativeZ( int nativeZ ) {
	if( m_nativeZ != nativeZ ) {
		m_nativeZ = nativeZ;
		Q_EMIT nativeZChanged( nativeZ );
	}
}

void NativelyDrawnImage::setMaterialName( const QString &materialName ) {
	if( m_materialName != materialName ) {
		m_materialName = materialName;
		m_reloadRequestMask = true;
		Q_EMIT materialNameChanged( materialName );
	}
}

bool NativelyDrawnImage::isLoaded() const {
	return m_material != nullptr;
}

void NativelyDrawnImage::updateSize( int w, int h ) {
	const bool hasChangedSize = m_sourceSize.width() != w || m_sourceSize.height() != h;
	m_sourceSize.setWidth( w );
	m_sourceSize.setHeight( h );
	if( hasChangedSize ) {
		Q_EMIT sourceSizeChanged( m_sourceSize );
	}
}

void NativelyDrawnImage::reloadIfNeeded() {
	if( !m_reloadRequestMask ) {
		return;
	}

	m_reloadRequestMask = 0;

	const bool wasLoaded = m_material != nullptr;
	m_material = R_RegisterPic( m_materialName.toUtf8().constData() );
	bool isLoaded = m_material != nullptr;
	if( wasLoaded != isLoaded ) {
		Q_EMIT isLoadedChanged( isLoaded );
	}

	if( !isLoaded ) {
		updateSize( 0, 0 );
		return;
	}

	int w, h;
	R_GetShaderDimensions( m_material, &w, &h );
	updateSize( w, h );
}

void NativelyDrawnImage::drawSelfNatively() {
	reloadIfNeeded();

	if( !m_material ) {
		return;
	}

	R_Set2DMode( true );
	vec4_t color { 1.0f, 1.0f, 1.0f, 1.0f };
	const QPointF globalPoint( mapToGlobal( QPointF( x(), y() ) ) );
	RF_DrawStretchPic( globalPoint.x(), globalPoint.y(), width(), height(), 0.0f, 0.0f, 1.0f, 1.0f, color, m_material );
	R_Set2DMode( false );
}

NativelyDrawnModel::NativelyDrawnModel( QQuickItem *parent )
	: QQuickItem( parent ) {
}

void NativelyDrawnModel::setNativeZ( int nativeZ ) {
	if( m_nativeZ != nativeZ ) {
		m_nativeZ = nativeZ;
		Q_EMIT nativeZChanged();
	}
}

void NativelyDrawnModel::setModelName( const QString &modelName ) {
	if( m_modelName != modelName ) {
		m_modelName = modelName;
		m_reloadRequestMask |= ReloadModel;
		Q_EMIT modelNameChanged();
	}
}

void NativelyDrawnModel::setSkinName( const QString &skinName ) {
	if( m_skinName != skinName ) {
		m_skinName = skinName;
		m_reloadRequestMask |= ReloadSkin;
		Q_EMIT skinNameChanged();
	}
}

void NativelyDrawnModel::setModelOrigin( const QVector3D &modelOrigin ) {
	if( m_modelOrigin != modelOrigin ) {
		m_modelOrigin = modelOrigin;
		Q_EMIT modelOriginChanged( modelOrigin );
	}
}

void NativelyDrawnModel::setViewOrigin( const QVector3D &viewOrigin ) {
	if( m_viewOrigin != viewOrigin ) {
		m_viewOrigin = viewOrigin;
		Q_EMIT viewOriginChanged( viewOrigin );
	}
}

void NativelyDrawnModel::setRotationAxis( const QVector3D &rotationAxis ) {
	if( m_rotationAxis != rotationAxis ) {
		m_rotationAxis = rotationAxis;
		Q_EMIT rotationAxisChanged( rotationAxis );
	}
}

void NativelyDrawnModel::setRotationSpeed( qreal rotationSpeed ) {
	if( m_rotationSpeed != rotationSpeed ) {
		m_rotationSpeed = rotationSpeed;
		Q_EMIT rotationSpeedChanged( rotationSpeed );
	}
}

void NativelyDrawnModel::setViewFov( qreal viewFov ) {
	if( m_viewFov != viewFov ) {
		m_viewFov = viewFov;
		Q_EMIT viewFovChanged( viewFov );
	}
}

bool NativelyDrawnModel::isLoaded() const {
	return m_model != nullptr;
}

void NativelyDrawnModel::reloadIfNeeded() {
	if( m_reloadRequestMask & ReloadModel ) {
		const bool wasLoaded = m_model != nullptr;
		m_model = R_RegisterModel( m_modelName.toUtf8().constData() );
		m_reloadRequestMask &= ~ReloadModel;
		const bool isLoaded = m_model != nullptr;
		if( wasLoaded != isLoaded ) {
			Q_EMIT isLoadedChanged( isLoaded );
		}
	}

	if( m_reloadRequestMask & ReloadSkin ) {
		m_skinFile = R_RegisterSkinFile( m_skinName.toUtf8().constData() );
		m_reloadRequestMask &= ~ReloadSkin;
	}
}

void NativelyDrawnModel::drawSelfNatively() {
	reloadIfNeeded();

	if( !m_model ) {
		return;
	}

	// TODO: Postponed to the renderer rewrite
}

}