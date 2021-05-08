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
public:
	enum Style : bool { Weak, Strong };

private:
	cvar_t *m_valueVar { nullptr };
	cvar_t *m_colorVar { nullptr };
	cvar_t *m_sizeVar { nullptr };

	static inline cvar_t *s_sizeVars[WEAP_TOTAL - 1] {};
	static inline cvar_t *s_colorVars[WEAP_TOTAL - 1] {};
	static inline cvar_t *s_valueVars[WEAP_TOTAL - 1] {};

	static inline cvar_t *cg_crosshair { nullptr };
	static inline cvar_t *cg_crosshair_size { nullptr };
	static inline cvar_t *cg_crosshair_color { nullptr };
	static inline cvar_t *cg_crosshair_strong { nullptr };
	static inline cvar_t *cg_crosshair_strong_size { nullptr };
	static inline cvar_t *cg_crosshair_damage_color { nullptr };
	static inline cvar_t *cg_separate_weapon_settings { nullptr };
	static inline float s_damageColor[4] {};
	static inline int s_oldPackedDamageColor { -1 };

	MediaCache::LinkedMaterialsArray *m_materials { nullptr };

	const unsigned m_assetsTag;
	const int m_decayTime;
	const float m_invDecayTime;

	int m_decayTimeLeft { 0 };
	int m_oldPackedColor { -1 };
	float m_varColor[4] { 1.0f, 1.0f, 1.0f, 1.0f };
	float m_drawColor[4] { 1.0f, 1.0f, 1.0f, 1.0f };

	const Style m_style;

	// Don't use var->modified flags as this is error-prone (multiple subsystems could reset it).
	// Just check values caching whether its needed. We do the same for the UI var tracking code.

	static void checkValueVar( cvar_t *var, unsigned numCrosshairs );
	static void checkSizeVar( cvar_t *var );
	static void checkColorVar( cvar_t *var, float *cachedColor = nullptr, int *oldPackedColor = nullptr );

	[[nodiscard]]
	auto getMaterials() -> MaterialsArray *;
public:
	CrosshairState( unsigned assetsTag, int decayTime, Style style ) noexcept
		: m_assetsTag( assetsTag ) , m_decayTime( decayTime )
		, m_invDecayTime( 1.0f / (float)decayTime ), m_style( style ) {}

	void touchDamageState() { m_decayTimeLeft = m_decayTime; }

	static void init();
	static void updateSharedPart();

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
	auto getDrawingMaterial() -> const shader_s * {
		assert( m_valueVar && m_valueVar->integer );
		return ( *getMaterials() )[m_valueVar->integer - 1];
	}
	[[nodiscard]]
	bool canBeDrawn() const {
		assert( m_valueVar && m_sizeVar );
		return m_valueVar->integer && m_sizeVar->integer;
	}
};

#endif
