#ifndef WSW_05c83563_a0d9_4cc9_b34c_51c1b66795c1_H
#define WSW_05c83563_a0d9_4cc9_b34c_51c1b66795c1_H

#include "../qcommon/qcommon.h"
#include "../gameshared/q_math.h"
#include "../gameshared/q_shared.h"
#include "../gameshared/q_comref.h"
#include "../gameshared/q_cvar.h"

#include "mediacache.h"

using MaterialsArray = const MediaCache::LinkedMaterialsArray;

class CrosshairState {
	const char *const m_baseVarName;

	// We have to use a lazy loading for these vars/materials due to lifetime issues. This should eventually be gone.
	cvar_t *m_ownValueVar { nullptr };
	cvar_t *m_ownColorVar { nullptr };
	cvar_t *m_ownSizeVar { nullptr };

	cvar_t *m_valueVar { nullptr };
	cvar_t *m_colorVar { nullptr };
	cvar_t *m_sizeVar { nullptr };
protected:
	const char *m_separateValueVarBaseName { nullptr };
	const char *m_separateColorVarBaseName { nullptr };
	const char *m_separateSizeVarBaseName { nullptr };
private:

	MediaCache::LinkedMaterialsArray *m_materials { nullptr };

	const unsigned m_assetsTag;
	const int m_decayTime;
	const float m_invDecayTime;

	int m_decayTimeLeft { 0 };
	int m_oldPackedColor { -1 };
	float m_varColor[4] { 1.0f, 1.0f, 1.0f, 1.0f };
	float m_drawColor[4] { 1.0f, 1.0f, 1.0f, 1.0f };

	static float s_damageColor[4];
	static int s_oldPackedDamageColor;

	// Don't use var->modified flags as this is error-prone (multiple subsystems could reset it).
	// Just check values caching whether its needed. We do the same for the UI var tracking code.

	static void checkValueVar( cvar_t *var, const MaterialsArray &materials );
	static void checkSizeVar( cvar_t *var, unsigned maxSize );
	static void checkColorVar( cvar_t *var, float *cachedColor, int *oldPackedColor = nullptr );

	[[nodiscard]]
	auto getOwnCVar( cvar_t **cached, const char *suffix, const char *defaultValue ) -> cvar_t *;

	[[nodiscard]]
	auto getMaterials() -> MaterialsArray *;
public:
	CrosshairState( const char *baseVarName, unsigned assetsTag, int decayTime ) noexcept
		: m_baseVarName( baseVarName ), m_assetsTag( assetsTag )
		, m_decayTime( decayTime ), m_invDecayTime( 1.0f / (float)decayTime ) {}

	void touchDamageState() { m_decayTimeLeft = m_decayTime; }

	static void staticUpdate();

	void update( unsigned weapon );
	void clear();

	[[nodiscard]]
	auto getDrawingColor() -> const float *;
	[[nodiscard]]
	auto getDrawingOffsets() const -> std::pair<int, int> {
		assert( m_sizeVar );
		return { -m_sizeVar->integer / 2, -m_sizeVar->integer / 2 };
	}
	[[nodiscard]]
	auto getDrawingDimensions() const -> std::pair<int, int> {
		assert( m_sizeVar );
		return { m_sizeVar->integer, m_sizeVar->integer };
	}
	[[nodiscard]]
	auto getDrawingMaterial() -> shader_s * {
		assert( m_valueVar && m_valueVar->integer && m_materials );
		return ( *m_materials )[m_valueVar->integer - 1];
	}
	[[nodiscard]]
	bool canBeDrawn() const {
		assert( m_valueVar && m_sizeVar );
		return m_valueVar->integer && m_sizeVar->integer;
	}
};

#endif
