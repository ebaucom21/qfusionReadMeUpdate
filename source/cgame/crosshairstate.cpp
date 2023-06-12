#include "crosshairstate.h"

#include "cg_local.h"

#include "../qcommon/wswfs.h"

using wsw::operator""_asView;

class CrosshairMaterialCache {
protected:
	const wsw::StringView m_pathPrefix;

	mutable wsw::StringSpanStorage<unsigned, unsigned> m_knownImageFiles;
	struct CacheEntry {
		shader_s *material { nullptr };
		unsigned cachedRequestedSize { 0 };
		std::pair<unsigned, unsigned> cachedActualSize { 0, 0 };
	};
	mutable wsw::StaticVector<CacheEntry, 16> m_cacheEntries;
public:
	explicit CrosshairMaterialCache( const wsw::StringView &pathPrefix ) noexcept : m_pathPrefix( pathPrefix ) {}

	[[nodiscard]]
	auto getMaterialForNameAndSize( const wsw::StringView &name, unsigned size ) -> std::optional<std::tuple<shader_s *, unsigned, unsigned>> {
		CacheEntry *foundEntry = nullptr;
		wsw::StringView foundName;
		// TODO: Isn't this design fragile?
		assert( m_cacheEntries.size() == m_knownImageFiles.size() );
		for( unsigned i = 0; i < m_cacheEntries.size(); ++i ) {
			if( m_knownImageFiles[i].equalsIgnoreCase( name ) ) {
				foundEntry = std::addressof( m_cacheEntries[i] );
				foundName  = m_knownImageFiles[i];
				break;
			}
		}
		if( foundEntry ) {
			if( foundEntry->cachedRequestedSize != size ) {
				wsw::StaticString<256> filePath;
				CrosshairState::makeFilePath( &filePath, m_pathPrefix, name );
				ImageOptions options {
					.desiredSize         = std::make_pair( size, size ),
					.borderWidth         = 1,
					.fitSizeForCrispness = true,
					.useOutlineEffect    = true,
				};
				R_UpdateExplicitlyManaged2DMaterialImage( foundEntry->material, filePath.data(), options );
				foundEntry->cachedRequestedSize = size;
				if( foundEntry->material ) {
					foundEntry->cachedActualSize = R_GetShaderDimensions( foundEntry->material ).value();
				}
			}
			if( foundEntry->material ) {
				return std::make_tuple( foundEntry->material, foundEntry->cachedActualSize.first, foundEntry->cachedActualSize.second );
			}
		}
		return std::nullopt;
	}

	[[nodiscard]]
	bool hasImageFile( const wsw::StringView &fileName ) const {
		for( const wsw::StringView &knownFileName: getFileSpans() ) {
			if( knownFileName.equalsIgnoreCase( fileName ) ) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]]
	auto getFileSpans() const -> const wsw::StringSpanStorage<unsigned, unsigned> & {
		if( m_knownImageFiles.empty() ) {
			wsw::fs::SearchResultHolder searchResultHolder;
			const wsw::StringView &extension = ".svg"_asView;
			if( const auto maybeSearchResult = searchResultHolder.findDirFiles( m_pathPrefix, extension ) ) {
				m_knownImageFiles.reserveSpans( m_cacheEntries.capacity() );
				for( const wsw::StringView &fileName: *maybeSearchResult ) {
					if( m_knownImageFiles.size() > m_cacheEntries.capacity() ) {
						cgWarning() << "Too many crosshair image files in" << m_pathPrefix;
						break;
					}
					if( fileName.endsWith( extension ) ) {
						m_knownImageFiles.add( fileName.dropRight( extension.size() ) );
					} else {
						m_knownImageFiles.add( fileName );
					}
				}
			}
			if( m_knownImageFiles.empty() ) {
				cgWarning() << "Failed to find crosshair files in" << m_pathPrefix;
			}
		}
		return m_knownImageFiles;
	}

	void initMaterials() {
		assert( m_cacheEntries.empty() );
		for( unsigned i = 0, maxEntries = getFileSpans().size(); i < maxEntries; ++i ) {
			m_cacheEntries.emplace_back( CacheEntry {
				.material = R_CreateExplicitlyManaged2DMaterial()
			});
		}
	}

	void destroyMaterials() {
		for( CacheEntry &entry: m_cacheEntries ) {
			R_ReleaseExplicitlyManaged2DMaterial( entry.material );
		}
		m_cacheEntries.clear();
	}
};

static CrosshairMaterialCache g_regularCrosshairsMaterialCache( CrosshairState::kRegularCrosshairsDirName );
static CrosshairMaterialCache g_strongCrosshairsMaterialCache( CrosshairState::kStrongCrosshairsDirName );

void CrosshairState::checkValueVar( cvar_t *var, const CrosshairMaterialCache &cache ) {
	if( const wsw::StringView name( var->string ); !name.empty() ) {
		if( !cache.hasImageFile( name ) ) {
			Cvar_ForceSet( var->name, "" );
		}
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
	checkValueVar( cg_crosshair, g_regularCrosshairsMaterialCache );

	cg_crosshair_size = Cvar_Get( "cg_crosshair_size", sizeStringBuffer.data(), CVAR_ARCHIVE );
	checkSizeVar( cg_crosshair_size, kRegularCrosshairSizeProps );

	cg_crosshair_color = Cvar_Get( "cg_crosshair_color", "255 255 255", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_color );

	cg_crosshair_strong = Cvar_Get( "cg_crosshair_strong", "1", CVAR_ARCHIVE );
	checkValueVar( cg_crosshair_strong, g_strongCrosshairsMaterialCache );

	(void)sizeStringBuffer.assignf( "%d", kStrongCrosshairSizeProps.defaultSize );
	cg_crosshair_strong_size = Cvar_Get( "cg_crosshair_strong_size", sizeStringBuffer.data(), CVAR_ARCHIVE );
	checkSizeVar( cg_crosshair_strong_size, kStrongCrosshairSizeProps );

	cg_crosshair_strong_color = Cvar_Get( "cg_crosshair_strong_color", "255 255 255", CVAR_ARCHIVE );
	checkColorVar( cg_crosshair_strong_color );

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

void CrosshairState::update( [[maybe_unused]] unsigned weapon ) {
	assert( weapon > 0 && weapon < WEAP_TOTAL );

	const bool isStrong  = m_style == Strong;
	if( isStrong ) {
		m_sizeVar  = cg_crosshair_strong_size;
		m_colorVar = cg_crosshair_strong_color;
		m_valueVar = cg_crosshair_strong;
	} else {
		if( cg_separate_weapon_settings->integer ) {
			m_sizeVar  = s_sizeVars[weapon - 1];
			m_colorVar = s_colorVars[weapon - 1];
			m_valueVar = s_valueVars[weapon - 1];
		} else {
			m_sizeVar  = cg_crosshair_size;
			m_colorVar = cg_crosshair_color;
			m_valueVar = cg_crosshair;
		}
	}

	checkSizeVar( m_sizeVar, isStrong ? kStrongCrosshairSizeProps : kRegularCrosshairSizeProps );
	checkColorVar( m_colorVar, m_varColor, &m_oldPackedColor );
	checkValueVar( m_valueVar, isStrong ? ::g_strongCrosshairsMaterialCache : ::g_regularCrosshairsMaterialCache );

	m_decayTimeLeft = wsw::max( 0, m_decayTimeLeft - cg.frameTime );
}

void CrosshairState::clear() {
	m_decayTimeLeft  = 0;
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
	if( const wsw::StringView name = wsw::StringView( m_valueVar->string ); !name.empty() ) {
		if( const auto size = (unsigned)m_sizeVar->integer ) {
			CrosshairMaterialCache *cache;
			const SizeProps *sizeProps;
			if( m_style == Strong ) {
				cache     = &::g_strongCrosshairsMaterialCache;
				sizeProps = &kStrongCrosshairSizeProps;
			} else {
				cache     = &::g_regularCrosshairsMaterialCache;
				sizeProps = &kRegularCrosshairSizeProps;
			}
			if( size >= sizeProps->minSize && size <= sizeProps->maxSize ) {
				return cache->getMaterialForNameAndSize( name, size );
			}
		}
	}
	return std::nullopt;
}

auto CrosshairState::getRegularCrosshairs() -> const wsw::StringSpanStorage<unsigned, unsigned> & {
	return g_regularCrosshairsMaterialCache.getFileSpans();
}

auto CrosshairState::getStrongCrosshairs() -> const wsw::StringSpanStorage<unsigned, unsigned> & {
	return g_strongCrosshairsMaterialCache.getFileSpans();
}