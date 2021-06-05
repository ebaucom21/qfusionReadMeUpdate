#include "crosshairstate.h"

#include "cg_local.h"

#include "../qcommon/wswstaticstring.h"
#include "../qcommon/wswfs.h"
#include "../ref/frontend.h"

shader_t *R_CreateExplicitlyManaged2DMaterial();
void R_ReleaseExplicitlyManaged2DMaterial( shader_t *material );
void R_UpdateExplicitlyManaged2DMaterialImage( shader_t *material, const char *name, int w = -1, int h = -1 );

// TODO: Keep this stuff in the UI subdirectory
#include <QSvgRenderer>
#include <QImage>
#include <QPainter>

using wsw::operator""_asView;

template <size_t N>
class CrosshairMaterialCache {
	static_assert( sizeof( QSvgRenderer ) == 16 );

	shader_s *m_materials[N] {};
	unsigned m_cachedMaterialSize[N] {};
	const CrosshairState::Style m_style;

	static constexpr unsigned kSide { kMaxCrosshairSize };
public:
	explicit CrosshairMaterialCache( CrosshairState::Style style ) noexcept : m_style( style ) {}

	[[nodiscard]]
	auto getMaterialForNumAndSize( unsigned num, unsigned size ) -> shader_s * {
		const unsigned index = num - 1;
		assert( index < N );
		assert( size >= kMinCrosshairSize && size <= kMaxCrosshairSize );
		if( m_cachedMaterialSize[index] != size ) {
			wsw::StaticString<256> name;
			CrosshairState::makePath( &name, m_style, num );
			R_UpdateExplicitlyManaged2DMaterialImage( m_materials[index], name.data(), (int)size, (int)size );
			m_cachedMaterialSize[index] = size;
		}
		return m_materials[index];
	}

	void initMaterials() {
		for( unsigned i = 0; i < N; ++i ) {
			assert( !m_materials[i] && !m_cachedMaterialSize[i] );
			m_materials[i] = R_CreateExplicitlyManaged2DMaterial();
		}
	}

	void destroyMaterials() {
		for( unsigned i = 0; i < N; ++i ) {
			R_ReleaseExplicitlyManaged2DMaterial( m_materials[i] );
			m_materials[i] = nullptr;
			m_cachedMaterialSize[i] = 0;
		}
	}
};

static CrosshairMaterialCache<kNumCrosshairs> crosshairsMaterialCache( CrosshairState::Weak );
static CrosshairMaterialCache<kNumStrongCrosshairs> strongCrosshairsMaterialCache( CrosshairState::Strong );

void CrosshairState::checkValueVar( cvar_t *var, unsigned numCrosshairs ) {
	if( (unsigned)var->integer > numCrosshairs ) {
		Cvar_ForceSet( var->name, "1" );
	}
}

void CrosshairState::checkSizeVar( cvar_t *var ) {
	if( var->integer < (int)kMinCrosshairSize || var->integer > (int)kMaxCrosshairSize ) {
		char buffer[16];
		Cvar_ForceSet( var->name, va_r( buffer, sizeof( buffer ), "%d", (int)( kDefaultCrosshairSize ) ) );
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

	cg_crosshair_damage_color = Cvar_Get( "cg_crosshair_damage_color", "255 0 0", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_damage_color );

	cg_separate_weapon_settings = Cvar_Get( "cg_separate_weapon_settings", "0", CVAR_ARCHIVE );
}

void CrosshairState::beginRegistration() {
	::crosshairsMaterialCache.destroyMaterials();
	::strongCrosshairsMaterialCache.destroyMaterials();
}

void CrosshairState::endRegistration() {
	::crosshairsMaterialCache.initMaterials();
	::strongCrosshairsMaterialCache.initMaterials();
}

void CrosshairState::updateSharedPart() {
	checkColorVar( cg_crosshair_damage_color, s_damageColor, &s_oldPackedDamageColor );
}

void CrosshairState::update( unsigned weapon ) {
	assert( weapon > 0 && weapon < WEAP_TOTAL );

	const int isSeparate = cg_separate_weapon_settings->integer;
	const bool isStrong = m_style == Strong;

	if( !isStrong ) {
		if( isSeparate ) {
			m_sizeVar = s_sizeVars[weapon - 1];
		} else {
			m_sizeVar = cg_crosshair_size;
		}
		checkSizeVar( m_sizeVar );
	}

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

[[nodiscard]]
auto CrosshairState::getDrawingMaterial() -> const shader_s * {
	// Apply additional validation as it could be called by the UI code
	const unsigned maxNum = m_style == Weak ? kNumCrosshairs : kNumStrongCrosshairs;
	if( const auto num = (unsigned)m_valueVar->integer; num && num <= maxNum ) {
		if( m_style == Weak ) {
			const auto size = (unsigned)m_sizeVar->integer;
			if( size >= kMinCrosshairSize && size <= kMaxCrosshairSize ) {
				return ::crosshairsMaterialCache.getMaterialForNumAndSize( num, size );
			}
		} else {
			return ::strongCrosshairsMaterialCache.getMaterialForNumAndSize( num, kMaxCrosshairSize );
		}
	}
	return nullptr;
}