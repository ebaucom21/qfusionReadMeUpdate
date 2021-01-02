#include "crosshairstate.h"

#include "cg_local.h"

extern cvar_t *cg_crosshair_damage_color;
extern cvar_t *cg_separate_weapon_settings;

class SharedSeparateWeaponVarsCache {
	cvar_t *m_sizeVars[WEAP_TOTAL - 1] {};
	cvar_t *m_colorVars[WEAP_TOTAL - 1] {};
	cvar_t *m_valueVars[WEAP_TOTAL - 1] {};

	char m_buffer[64];

	static_assert( WEAP_NONE == 0 );
	static inline const char *kWeaponNames[WEAP_TOTAL - 1] = {
		"gb", "mg", "rg", "gl", "rl", "pg", "lg", "eb", "sw", "ig"
	};

	[[nodiscard]]
	auto mkVarName( const char *baseName, const char *suffix, unsigned weapon ) -> const char * {
		assert( weapon > WEAP_NONE && weapon < WEAP_TOTAL );
		return va_r( m_buffer, sizeof( m_buffer ), "%s%s_%s", baseName, suffix, kWeaponNames[weapon - 1] );
	}

	[[nodiscard]]
	auto getCVarForWeapon( cvar_t **vars, const char *baseName, const char *suffix,
						   const char *defaultValue, unsigned weapon ) -> cvar_t * {
		assert( weapon > WEAP_NONE && weapon < WEAP_TOTAL );
		// This currently assumes the same name.
		cvar_t **cached = vars + weapon - 1;
		// We don't want to always make the name in the release code path so it's called separately in the assertion.
		assert( !*cached || !Q_stricmp( ( *cached )->name, mkVarName( baseName, suffix, weapon ) ) );
		if( !*cached ) {
			*cached = Cvar_Get( mkVarName( baseName, suffix, weapon ), defaultValue, CVAR_ARCHIVE );
		}
		return *cached;
	}
public:
	[[nodiscard]]
	auto getSizeCVarForWeapon( const char *baseName, unsigned weapon ) -> cvar_t * {
		return getCVarForWeapon( m_sizeVars, baseName, "_size", "64", weapon );
	}
	[[nodiscard]]
	auto getColorCVarForWeapon( const char *baseName, unsigned weapon ) -> cvar_t * {
		return getCVarForWeapon( m_colorVars, baseName, "_color", "255 255 255", weapon );
	}
	[[nodiscard]]
	auto getValueCVarForWeapon( const char *baseName, unsigned weapon ) -> cvar_t * {
		return getCVarForWeapon( m_valueVars, baseName, "", "1", weapon );
	}
} weaponVarsCache;

float CrosshairState::s_damageColor[4];
int CrosshairState::s_oldPackedDamageColor { -1 };

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

auto CrosshairState::getOwnCVar( cvar_t **cached, const char *suffix, const char *defaultValue ) -> cvar_t * {
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

void CrosshairState::update( unsigned weapon ) {
	assert( weapon > 0 && weapon < WEAP_TOTAL );

	const int separate = cg_separate_weapon_settings->integer;

	if( separate && m_separateSizeVarBaseName ) {
		m_sizeVar = ::weaponVarsCache.getSizeCVarForWeapon( m_separateColorVarBaseName, weapon );
	} else {
		m_sizeVar = getOwnCVar( &m_ownSizeVar, "_size", "64" );
	}
	checkSizeVar( m_sizeVar, 64 );

	if( separate && m_separateColorVarBaseName ) {
		m_colorVar = ::weaponVarsCache.getColorCVarForWeapon( m_separateColorVarBaseName, weapon );
	} else {
		m_colorVar = getOwnCVar( &m_ownColorVar, "_color", "255 255 255" );
	}
	checkColorVar( m_colorVar, m_varColor, &m_oldPackedColor );

	if( separate && m_separateValueVarBaseName ) {
		m_valueVar = ::weaponVarsCache.getValueCVarForWeapon( m_separateValueVarBaseName, weapon );
	} else {
		m_valueVar = getOwnCVar( &m_ownValueVar, "", "1" );
	}
	checkValueVar( m_valueVar, *getMaterials() );

	m_decayTimeLeft = std::max( 0, m_decayTimeLeft - cg.frameTime );
}

void CrosshairState::clear() {
	m_decayTimeLeft = 0;
	m_oldPackedColor = -1;
}

auto CrosshairState::getDrawingColor() -> const float * {
	if( m_decayTimeLeft <= 0 ) {
		return m_varColor;
	}
	const float frac = 1.0f - Q_Sqrt( (float)m_decayTimeLeft * m_invDecayTime );
	VectorLerp( s_damageColor, frac, m_varColor, m_drawColor );
	return m_drawColor;
}