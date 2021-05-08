#include "crosshairstate.h"

#include "cg_local.h"

#include "../qcommon/wswstaticstring.h"

using wsw::operator""_asView;

void CrosshairState::checkValueVar( cvar_t *var, unsigned numCrosshairs ) {
	if( (unsigned)var->integer > numCrosshairs ) {
		Cvar_ForceSet( var->name, "1" );
	}
}

static const int kValidSizes[] { 16, 32, 64 };

void CrosshairState::checkSizeVar( cvar_t *var ) {
	if( std::find( std::begin( kValidSizes ), std::end( kValidSizes ), var->integer ) == std::end( kValidSizes ) ) {
		char buffer[16];
		Cvar_ForceSet( var->name, va_r( buffer, sizeof( buffer ), "%d", kValidSizes[std::size( kValidSizes ) / 2] ) );
	}
}

void CrosshairState::checkColorVar( cvar_s *var, float *cachedColor, int *oldPackedColor ) {
	const int packedColor = COM_ReadColorRGBString( var->string );
	if( packedColor == -1 ) {
		constexpr const char *defaultString = "255 255 255";
		if( !Q_stricmp( var->string, defaultString ) ) {
			Cvar_ForceSet( var->name, defaultString );
		}
	}
	// Update cached color values if their addresses are supplied and the packed value has changed
	// (tracking the packed value allows using cheap comparisons of a single integer)
	if( !oldPackedColor || ( packedColor != *oldPackedColor ) ) {
		if( oldPackedColor ) {
			*oldPackedColor = packedColor;
		}
		if( cachedColor ) {
			float r = 1.0f, g = 1.0f, b = 1.0f;
			if( packedColor != -1 ) {
				constexpr float normalizer = 1.0f / 255.0f;
				r = COLOR_R( packedColor ) * normalizer;
				g = COLOR_G( packedColor ) * normalizer;
				b = COLOR_B( packedColor ) * normalizer;
			}
			Vector4Set( cachedColor, r, g, b, 1.0f );
		}
	}
}

[[nodiscard]]
auto CrosshairState::getMaterials() -> MaterialsArray * {
	if( !m_materials ) {
		m_materials = cgs.media.findMaterialsArrayByTag( m_assetsTag );
	}
	return m_materials;
}

static_assert( WEAP_NONE == 0 && WEAP_GUNBLADE == 1 );
static inline const char *kWeaponNames[WEAP_TOTAL - 1] = {
	"gb", "mg", "rg", "gl", "rl", "pg", "lg", "eb", "sw", "ig"
};

void CrosshairState::init() {
	wsw::StaticString<64> varNameBuffer;
	varNameBuffer << "cg_crosshair_"_asView;
	const auto prefixLen = varNameBuffer.length();

	for( int i = 0; i < WEAP_TOTAL - 1; ++i ) {
		assert( std::strlen( kWeaponNames[i] ) == 2 );
		const wsw::StringView weaponName( kWeaponNames[i], 2 );

		varNameBuffer.erase( prefixLen );
		varNameBuffer << weaponName;
		s_valueVars[i] = Cvar_Get( varNameBuffer.data(), "1", CVAR_ARCHIVE );
		checkValueVar( s_valueVars[i], kNumCrosshairs );

		varNameBuffer.erase( prefixLen );
		varNameBuffer << "size_"_asView << weaponName;
		s_sizeVars[i] = Cvar_Get( varNameBuffer.data(), "32", CVAR_ARCHIVE );
		checkSizeVar( s_sizeVars[i] );

		varNameBuffer.erase( prefixLen );
		varNameBuffer << "color_"_asView << weaponName;
		s_colorVars[i] = Cvar_Get( varNameBuffer.data(), "255 255 255", CVAR_ARCHIVE );
		checkColorVar( s_colorVars[i] );
	}

	cg_crosshair = Cvar_Get( "cg_crosshair", "1", CVAR_ARCHIVE );
	checkValueVar( cg_crosshair, kNumCrosshairs );

	cg_crosshair_size = Cvar_Get( "cg_crosshair_size", "32", CVAR_ARCHIVE );
	checkSizeVar( cg_crosshair_size );

	cg_crosshair_color = Cvar_Get( "cg_crosshair_color", "255 255 255", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_color );

	cg_crosshair_strong = Cvar_Get( "cg_crosshair_strong", "1", CVAR_ARCHIVE );
	checkValueVar( cg_crosshair_strong, kNumStrongCrosshairs );

	cg_crosshair_strong_size = Cvar_Get( "cg_crosshair_strong_size", "64", CVAR_ARCHIVE );
	checkSizeVar( cg_crosshair_strong_size );

	cg_crosshair_damage_color = Cvar_Get( "cg_crosshair_damage_color", "255 0 0", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_damage_color );

	cg_separate_weapon_settings = Cvar_Get( "cg_separate_weapon_settings", "0", CVAR_ARCHIVE );
}

void CrosshairState::updateSharedPart() {
	checkColorVar( cg_crosshair_damage_color, s_damageColor, &s_oldPackedDamageColor );
}

void CrosshairState::update( unsigned weapon ) {
	assert( weapon > 0 && weapon < WEAP_TOTAL );

	const int isSeparate = cg_separate_weapon_settings->integer;
	const bool isStrong = m_style == Strong;

	if( isStrong ) {
		m_sizeVar = cg_crosshair_strong_size;
	} else if( isSeparate ) {
		m_sizeVar = s_sizeVars[weapon - 1];
	} else {
		m_sizeVar = cg_crosshair_size;
	}

	checkSizeVar( m_sizeVar );

	if( isSeparate ) {
		m_colorVar = s_colorVars[weapon - 1];
	} else {
		m_colorVar = cg_crosshair_color;
	}

	checkColorVar( m_colorVar, m_varColor, &m_oldPackedColor );

	if( isStrong ) {
		m_valueVar = cg_crosshair_strong;
	} else if( isSeparate ) {
		m_valueVar = s_valueVars[weapon - 1];
	} else {
		m_valueVar = cg_crosshair;
	}

	checkValueVar( m_valueVar, kNumCrosshairs );

	m_decayTimeLeft = std::max( 0, m_decayTimeLeft - cg.frameTime );
}

void CrosshairState::clear() {
	m_decayTimeLeft = 0;
	m_oldPackedColor = -1;
}

auto CrosshairState::getDrawingColor() -> const float * {
	if( m_decayTimeLeft > 0 ) {
		const float frac = 1.0f - Q_Sqrt( (float) m_decayTimeLeft * m_invDecayTime );
		assert( frac >= 0.0f && frac <= 1.0f );
		VectorLerp( s_damageColor, frac, m_varColor, m_drawColor );
		return m_drawColor;
	}
	return m_varColor;
}