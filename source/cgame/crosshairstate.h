#ifndef WSW_05c83563_a0d9_4cc9_b34c_51c1b66795c1_H
#define WSW_05c83563_a0d9_4cc9_b34c_51c1b66795c1_H

#include "../qcommon/qcommon.h"
#include "../gameshared/q_math.h"
#include "../gameshared/q_shared.h"
#include "../gameshared/q_comref.h"
#include "../gameshared/q_cvar.h"
#include "../gameshared/gs_public.h"
#include "../qcommon/stringspanstorage.h"

struct shader_s;

struct SizeProps { unsigned minSize, maxSize, defaultSize; };

constexpr const SizeProps kRegularCrosshairSizeProps { 16, 48, 24 };
constexpr const SizeProps kStrongCrosshairSizeProps { 48, 72, 64 };

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
	static inline cvar_t *cg_crosshair_strong_color { nullptr };
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

	static void checkValueVar( cvar_t *var, const class CrosshairMaterialCache &cache );
	static void checkSizeVar( cvar_t *var, const SizeProps &sizeProps );
	static void checkColorVar( cvar_t *var, float *cachedColor = nullptr, int *oldPackedColor = nullptr );
public:
	CrosshairState( Style style, unsigned decayTime ) noexcept
		: m_decayTime( (int)decayTime ), m_invDecayTime( 1.0f / (float)decayTime ), m_style( style ) {}

	void touchDamageState() { m_decayTimeLeft = m_decayTime; }

	static void initPersistentState();
	static void handleCGameInit();
	static void handleCGameShutdown();
	static void updateSharedPart();

	void update( unsigned weapon );
	void clear();

	[[nodiscard]]
	auto getDrawingColor() -> const float *;
	[[nodiscard]]
	auto getDrawingMaterial() -> std::optional<std::tuple<shader_s *, unsigned, unsigned>>;

	// TODO: Return a java-style iterator
	[[nodiscard]]
	static auto getRegularCrosshairs() -> const wsw::StringSpanStorage<unsigned, unsigned> &;

	// TODO: Return a java-style iterator
	[[nodiscard]]
	static auto getStrongCrosshairs() -> const wsw::StringSpanStorage<unsigned, unsigned> &;

	static constexpr wsw::StringView kRegularCrosshairsDirName { "/gfx/hud/crosshairs/regular" };
	static constexpr wsw::StringView kStrongCrosshairsDirName { "/gfx/hud/crosshairs/strong" };

	template <typename Container, typename Appendable = wsw::StringView>
	static void makeFilePath( Container *container, const wsw::StringView &prefix, const wsw::StringView &fileName ) {
		container->clear();
		container->append( Appendable( prefix.data(), prefix.size() ) );
		container->append( Appendable( "/", 1 ) );
		container->append( Appendable( fileName.data(), fileName.size() ) );
		container->append( Appendable( ".svg", 4 ) );
	}
};

#endif
