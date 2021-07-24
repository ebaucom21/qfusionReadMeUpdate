#ifndef WSW_05c83563_a0d9_4cc9_b34c_51c1b66795c1_H
#define WSW_05c83563_a0d9_4cc9_b34c_51c1b66795c1_H

#include "../qcommon/qcommon.h"
#include "../gameshared/q_math.h"
#include "../gameshared/q_shared.h"
#include "../gameshared/q_comref.h"
#include "../gameshared/q_cvar.h"
#include "../gameshared/gs_public.h"

struct shader_s;

constexpr const unsigned kNumCrosshairs = 10;
constexpr const unsigned kNumStrongCrosshairs = 6;

constexpr const unsigned kMinCrosshairSize = 16;
constexpr const unsigned kDefaultCrosshairSize = 32;
constexpr const unsigned kMaxCrosshairSize = 48;
constexpr const unsigned kStrongCrosshairSize = 64;

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

	static inline const wsw::StringView kWeakPathPrefix { "gfx/hud/crosshair_" };
	static inline const wsw::StringView kStrongPathPrefix { "gfx/hud/crosshair_strong_" };
public:
	CrosshairState( Style style, unsigned decayTime ) noexcept
		: m_decayTime( (int)decayTime ), m_invDecayTime( 1.0f / (float)decayTime ), m_style( style ) {}

	void touchDamageState() { m_decayTimeLeft = m_decayTime; }

	static void init();
	static void beginRegistration();
	static void endRegistration();
	static void updateSharedPart();

	template <typename Buffer, typename AppendArg = wsw::StringView>
	static void makePath( Buffer *buffer, Style style, unsigned num ) {
		buffer->clear();
		if( style == Weak ) {
			buffer->append( AppendArg( kWeakPathPrefix.data(), kWeakPathPrefix.size() ) );
		} else {
			buffer->append( AppendArg( kStrongPathPrefix.data(), kStrongPathPrefix.size() ) );
		}
		assert( num && num < 20 );
		// This is the most compatible approach for tiny numbers
		char numData[2];
		if( num < 10 ) {
			numData[0] = (char)( '0' + (int)num );
			buffer->append( AppendArg( numData, 1 ) );
		} else {
			numData[0] = (char)( '0' + ( (int)num / 10 ) );
			numData[1] = (char)( '0' + ( (int)num % 10 ) );
			buffer->append( AppendArg( numData, 2 ) );
		}
		buffer->append( AppendArg( ".svg", 4 ) );
	}

	void update( unsigned weapon );
	void clear();

	[[nodiscard]]
	auto getDrawingColor() -> const float *;
	[[nodiscard]]
	auto getDrawingMaterial() -> std::optional<std::tuple<shader_s *, unsigned, unsigned>>;

	[[nodiscard]]
	bool canBeDrawn() const {
		if( m_style == Weak ) {
			assert( m_valueVar && m_sizeVar );
			return m_valueVar->integer && m_sizeVar->integer;
		}
		return true;
	}
};

#endif
