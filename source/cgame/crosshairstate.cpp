#include "crosshairstate.h"

#include "cg_local.h"

extern cvar_t *cg_crosshair_damage_color;

void CrosshairState::checkValueVar( cvar_t *var, const MaterialsArray &materials ) {
	if( (unsigned)var->integer >= ( materials.length() + 1u ) ) {
		Cvar_Set( var->name, "1" );
	}
}

void CrosshairState::checkSizeVar( cvar_t *var, unsigned maxSize ) {
	if( (unsigned)var->integer > maxSize ) {
		char buffer[32];
		Cvar_Set( var->name, va_r( buffer, sizeof( buffer ), "%u", maxSize ) );
	}
}

void CrosshairState::checkColorVar( cvar_s *var, float *cachedColor, int *oldPackedColor ) {
	float r = 1.0f, g = 1.0f, b = 1.0f;
	const int packedColor = COM_ReadColorRGBString( var->string );
	if( !oldPackedColor || packedColor != *oldPackedColor ) {
		*oldPackedColor = packedColor;
		if ( packedColor != -1 ) {
			constexpr float normalizer = 1.0f / 255.0f;
			r = COLOR_R( packedColor ) * normalizer;
			g = COLOR_G( packedColor ) * normalizer;
			b = COLOR_B( packedColor ) * normalizer;
		}
		Vector4Set( cachedColor, r, g, b, 1.0f );
	}
}

auto CrosshairState::getCVar( cvar_t **cached, const char *suffix, const char *defaultValue ) -> cvar_t * {
	if( !*cached ) {
		char buffer[256];
		const char *name = va_r( buffer, sizeof( buffer ), "%s%s", m_baseVarName, suffix );
		*cached = Cvar_Get( name, defaultValue, CVAR_ARCHIVE );
	}
	return *cached;
}

[[nodiscard]]
auto CrosshairState::getMaterials() -> MaterialsArray * {
	if( !m_materials ) {
		m_materials = cgs.media.findMaterialsArrayByTag( m_assetsTag );
	}
	return m_materials;
}

void CrosshairState::staticUpdate() {
	checkColorVar( cg_crosshair_damage_color, s_damageColor, &s_oldPackedDamageColor );
}

void CrosshairState::update() {
	checkSizeVar( getCVar( &m_sizeVar, "_size", "64" ), 64 );
	checkColorVar( getCVar( &m_colorVar, "_color", "255 255 255" ), m_varColor, &m_oldPackedColor );
	checkValueVar( getCVar( &m_valueVar, "", "1" ), *getMaterials() );
	m_decayTimeLeft = std::max( 0, m_decayTimeLeft - cg.frameTime );
}

auto CrosshairState::getDrawingColor() -> const float * {
	if( m_decayTimeLeft <= 0 ) {
		return m_varColor;
	}
	const float frac = 1.0f - Q_Sqrt( (float)m_decayTimeLeft * m_invDecayTime );
	VectorLerp( s_damageColor, frac, m_varColor, m_drawColor );
	return m_drawColor;
}