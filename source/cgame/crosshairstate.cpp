#include "crosshairstate.h"

#include "cg_local.h"

#include "../qcommon/wswstaticstring.h"
#include "../qcommon/wswfs.h"

using wsw::operator""_asView;

/// A non-template base to reduce code duplication
class BaseCrosshairMaterialCache {
protected:
	shader_s **m_materials { nullptr };
	unsigned *m_cachedRequestedSize { nullptr };
	std::pair<unsigned, unsigned> *m_cachedActualSize { nullptr };
	const CrosshairState::Style m_style;
	const unsigned m_minSize, m_maxSize, m_maxNum;
public:
	BaseCrosshairMaterialCache( CrosshairState::Style style, const SizeProps &sizeProps, unsigned maxNum ) noexcept
		: m_style( style ), m_minSize( sizeProps.minSize ), m_maxSize( sizeProps.maxSize ), m_maxNum( maxNum ) {}

	[[nodiscard]]
	auto getMaterialForNumAndSize( unsigned num, unsigned size )
		-> std::optional<std::tuple<shader_s *, unsigned, unsigned>> {
		const unsigned index = num - 1;
		assert( index < m_maxNum );
		// These bounds could be template parameters but not in this (17) language version
		assert( size >= m_minSize && size <= m_maxSize );
		if( m_cachedRequestedSize[index] != size ) {
			wsw::StaticString<256> name;
			CrosshairState::makePath( &name, m_style, num );
			ImageOptions options {};
			options.fitSizeForCrispness = true;
			options.useOutlineEffect    = true;
			options.borderWidth         = 1;
			options.setDesiredSize( size, size );
			R_UpdateExplicitlyManaged2DMaterialImage( m_materials[index], name.data(), options );
			m_cachedRequestedSize[index] = size;
			m_cachedActualSize[index]    = *R_GetShaderDimensions( m_materials[index] );
		}
		if( m_materials[index] ) {
			const auto [width, height] = m_cachedActualSize[index];
			return std::make_tuple( m_materials[index], width, height );
		}
		return std::nullopt;
	}

	void initMaterials() {
		for( unsigned i = 0; i < m_maxNum; ++i ) {
			assert( !m_materials[i] && !m_cachedRequestedSize[i] );
			m_materials[i] = R_CreateExplicitlyManaged2DMaterial();
		}
	}

	void destroyMaterials() {
		for( unsigned i = 0; i < m_maxNum; ++i ) {
			R_ReleaseExplicitlyManaged2DMaterial( m_materials[i] );
			m_materials[i] = nullptr;
			m_cachedRequestedSize[i] = 0;
		}
	}
};

/// A descendant to allocate the exact storage
template <unsigned N>
class CrosshairMaterialCache : public BaseCrosshairMaterialCache {
	shader_s *m_storageOfMaterials[N] {};
	unsigned m_storageOfCachedRequestedSize[N] {};
	std::pair<unsigned, unsigned> m_storageOfCachedActualSize[N] {};
public:
	CrosshairMaterialCache( CrosshairState::Style style, const SizeProps &sizeProps ) noexcept
		: BaseCrosshairMaterialCache( style, sizeProps, N ) {
		m_materials           = m_storageOfMaterials;
		m_cachedRequestedSize = m_storageOfCachedRequestedSize;
		m_cachedActualSize    = m_storageOfCachedActualSize;
	}
};

static CrosshairMaterialCache<kNumRegularCrosshairs>
    g_regularCrosshairsMaterialCache( CrosshairState::Weak, kRegularCrosshairSizeProps );
static CrosshairMaterialCache<kNumStrongCrosshairs>
    g_strongCrosshairsMaterialCache( CrosshairState::Strong, kStrongCrosshairSizeProps );

void CrosshairState::checkValueVar( cvar_t *var, unsigned numCrosshairs ) {
	if( (unsigned)var->integer > numCrosshairs ) {
		Cvar_ForceSet( var->name, "1" );
	}
}

void CrosshairState::checkSizeVar( cvar_t *var, const SizeProps &sizeProps ) {
	if( var->integer < (int)sizeProps.minSize || var->integer > (int)sizeProps.maxSize ) {
		char buffer[16];
		Cvar_ForceSet( var->name, va_r( buffer, sizeof( buffer ), "%d", (int)( sizeProps.defaultSize ) ) );
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

void CrosshairState::initPersistentState() {
	wsw::StaticString<64> varNameBuffer;
	varNameBuffer << "cg_crosshair_"_asView;
	const auto prefixLen = varNameBuffer.length();

	wsw::StaticString<8> sizeStringBuffer;
	(void)sizeStringBuffer.assignf( "%d", kRegularCrosshairSizeProps.defaultSize );

	for( int i = 0; i < WEAP_TOTAL - 1; ++i ) {
		assert( std::strlen( kWeaponNames[i] ) == 2 );
		const wsw::StringView weaponName( kWeaponNames[i], 2 );

		varNameBuffer.erase( prefixLen );
		varNameBuffer << weaponName;
		s_valueVars[i] = Cvar_Get( varNameBuffer.data(), "1", CVAR_ARCHIVE );
		checkValueVar( s_valueVars[i], kNumRegularCrosshairs );

		varNameBuffer.erase( prefixLen );
		varNameBuffer << "size_"_asView << weaponName;
		s_sizeVars[i] = Cvar_Get( varNameBuffer.data(), sizeStringBuffer.data(), CVAR_ARCHIVE );
		checkSizeVar( s_sizeVars[i], kRegularCrosshairSizeProps );

		varNameBuffer.erase( prefixLen );
		varNameBuffer << "color_"_asView << weaponName;
		s_colorVars[i] = Cvar_Get( varNameBuffer.data(), "255 255 255", CVAR_ARCHIVE );
		checkColorVar( s_colorVars[i] );
	}

	cg_crosshair = Cvar_Get( "cg_crosshair", "1", CVAR_ARCHIVE );
	checkValueVar( cg_crosshair, kNumRegularCrosshairs );

	cg_crosshair_size = Cvar_Get( "cg_crosshair_size", sizeStringBuffer.data(), CVAR_ARCHIVE );
	checkSizeVar( cg_crosshair_size, kRegularCrosshairSizeProps );

	cg_crosshair_color = Cvar_Get( "cg_crosshair_color", "255 255 255", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_color );

	cg_crosshair_strong = Cvar_Get( "cg_crosshair_strong", "1", CVAR_ARCHIVE );
	checkValueVar( cg_crosshair_strong, kNumStrongCrosshairs );

	(void)sizeStringBuffer.assignf( "%d", kStrongCrosshairSizeProps.defaultSize );
	cg_crosshair_strong_size = Cvar_Get( "cg_crosshair_strong_size", sizeStringBuffer.data(), CVAR_ARCHIVE );
	checkSizeVar( cg_crosshair_strong_size, kStrongCrosshairSizeProps );

	cg_crosshair_damage_color = Cvar_Get( "cg_crosshair_damage_color", "255 0 0", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_damage_color );

	cg_separate_weapon_settings = Cvar_Get( "cg_separate_weapon_settings", "0", CVAR_ARCHIVE );
}

void CrosshairState::handleCGameShutdown() {
	::g_regularCrosshairsMaterialCache.destroyMaterials();
	::g_strongCrosshairsMaterialCache.destroyMaterials();
}

void CrosshairState::handleCGameInit() {
	::g_regularCrosshairsMaterialCache.initMaterials();
	::g_strongCrosshairsMaterialCache.initMaterials();
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
		checkSizeVar( m_sizeVar, kStrongCrosshairSizeProps );
	} else {
		if( isSeparate ) {
			m_sizeVar = s_sizeVars[weapon - 1];
		} else {
			m_sizeVar = cg_crosshair_size;
		}
		checkSizeVar( m_sizeVar, kRegularCrosshairSizeProps );
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

	checkValueVar( m_valueVar, isStrong ? kNumStrongCrosshairs : kNumRegularCrosshairs );

	m_decayTimeLeft = wsw::max( 0, m_decayTimeLeft - cg.frameTime );
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
auto CrosshairState::getDrawingMaterial() -> std::optional<std::tuple<shader_s *, unsigned, unsigned>> {
	// Apply an additional validation as it could be called by the UI code
	if( const auto num = (unsigned)m_valueVar->integer ) {
		BaseCrosshairMaterialCache *cache;
		const SizeProps *sizeProps;
		unsigned maxNum;
		if( m_style == Strong ) {
			maxNum = kNumStrongCrosshairs;
			cache = &::g_strongCrosshairsMaterialCache;
			sizeProps = &kStrongCrosshairSizeProps;
		} else {
			maxNum = kNumRegularCrosshairs;
			cache = &::g_regularCrosshairsMaterialCache;
			sizeProps = &kRegularCrosshairSizeProps;
		}
		if( num < maxNum ) {
			if( const auto size = (unsigned) m_sizeVar->integer ) {
				if ( size >= sizeProps->minSize && size <= sizeProps->maxSize ) {
					return cache->getMaterialForNumAndSize( num, size );
				}
			}
		}
	}
	return std::nullopt;
}