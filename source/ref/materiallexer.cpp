/*
Copyright (C) 1999 Stephen C. Taylor
Copyright (C) 2002-2007 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "materiallocal.h"

#include "../qcommon/links.h"
#include "../qcommon/enumtokenmatcher.h"

using wsw::operator""_asView;

class DeformTokenMatcher : public wsw::EnumTokenMatcher<Deform> {
public:
	DeformTokenMatcher() noexcept {
		add( "Wave"_asView, Deform::Wave );
		add( "Bulge"_asView, Deform::Bulge );
		add( "Move"_asView, Deform::Move );
		add( "Autosprite"_asView, Deform::Autosprite );
		add( "Autosprite2"_asView, Deform::Autosprite2 );
		add( "Autoparticle"_asView, Deform::Autoparticle );
	}
};

static DeformTokenMatcher deformTokenMatcher;

class FuncTokenMatcher : public wsw::EnumTokenMatcher<Func> {
public:
	FuncTokenMatcher() noexcept {
		add( "Sin"_asView, Func::Sin );
		add( "Triangle"_asView, Func::Triangle );
		add( "Square"_asView, Func::Square );
		add( "Sawtooth"_asView, Func::Sawtooth );
		add( "InvSawtooth"_asView, Func::InvSawtooth );
		add( "InverseSawtooth"_asView, Func::InvSawtooth );
		add( "Noize"_asView, Func::Noize );
		add( "DistanceRamp"_asView, Func::DistanceRamp );
	}
};

static FuncTokenMatcher funcTokenMatcher;

class PassKeyMatcher : public wsw::EnumTokenMatcher<PassKey> {
public:
	PassKeyMatcher() noexcept {
		add( "RgbGen"_asView, PassKey::RgbGen );
		add( "BlendFunc"_asView, PassKey::BlendFunc );
		add( "DepthFunc"_asView, PassKey::DepthFunc );
		add( "DepthWrite"_asView, PassKey::DepthWrite );
		add( "AlphaFunc"_asView, PassKey::AlphaFunc );
		add( "TCMod"_asView, PassKey::TCMod );
		add( "Map"_asView, PassKey::Map );
		add( "AnimMap"_asView, PassKey::AnimMap );
		add( "CubeMap"_asView, PassKey::CubeMap );
		add( "ShadeCubeMap"_asView, PassKey::ShadeCubeMap );
		add( "ClampMap"_asView, PassKey::ClampMap );
		add( "AnimClampMap"_asView, PassKey::AnimClampMap );
		add( "Material"_asView, PassKey::Material );
		add( "Distortion"_asView, PassKey::Distortion );
		add( "CelShade"_asView, PassKey::CelShade );
		add( "TCGen"_asView, PassKey::TCGen );
		add( "AlphaGen"_asView, PassKey::AlphaGen );
		add( "Detail"_asView, PassKey::Detail );
		add( "Greyscale"_asView, PassKey::Grayscale );
		add( "Grayscale"_asView, PassKey::Grayscale );
		add( "Skip"_asView, PassKey::Skip );
	}
};

static PassKeyMatcher passKeyMatcher;

class IntConditionVarMatcher : public wsw::EnumTokenMatcher<IntConditionVar> {
public:
	IntConditionVarMatcher() noexcept {
		add( "MaxTextureSize"_asView, IntConditionVar::MaxTextureSize );
		add( "MaxTextureCubemapSize"_asView, IntConditionVar::MaxTextureCubemapSize );
		add( "MaxTextureUnits"_asView, IntConditionVar::MaxTextureUnits );
	}
};

static IntConditionVarMatcher intVarMatcher;

class BoolConditionVarMatcher : public wsw::EnumTokenMatcher<BoolConditionVar> {
public:
	BoolConditionVarMatcher() noexcept {
		add( "TextureCubeMap"_asView, BoolConditionVar::TextureCubeMap );
		add( "Glsl"_asView, BoolConditionVar::Glsl );
		add( "Deluxe"_asView, BoolConditionVar::DeluxeMaps );
		add( "DeluxeMaps"_asView, BoolConditionVar::DeluxeMaps );
		add( "PortalMaps"_asView, BoolConditionVar::PortalMaps );
	}
};

static BoolConditionVarMatcher boolVarMatcher;

class LogicOpMatcher : public wsw::EnumTokenMatcher<LogicOp, 2> {
public:
	LogicOpMatcher() noexcept {
		add( "&&"_asView, LogicOp::And );
		add( "||"_asView, LogicOp::Or );
	}
};

static LogicOpMatcher logicOpMatcher;

class CmpOpMatcher : public wsw::EnumTokenMatcher<CmpOp, 2> {
public:
	CmpOpMatcher() noexcept {
		add( "<"_asView, CmpOp::LS );
		add( "<="_asView, CmpOp::LE );
		add( ">"_asView, CmpOp::GT );
		add( ">="_asView, CmpOp::GE );
		add( "!="_asView, CmpOp::NE );
		add( "=="_asView, CmpOp::EQ );
	}
};

static CmpOpMatcher cmpOpMatcher;

class CullModeMatcher : public wsw::EnumTokenMatcher<CullMode> {
public:
	CullModeMatcher() noexcept {
		add( "None"_asView, CullMode::None );
		add( "Disable"_asView, CullMode::None );
		add( "Twosided"_asView, CullMode::None );

		add( "Back"_asView, CullMode::Back );
		add( "Backside"_asView, CullMode::Back );
		add( "Backsided"_asView, CullMode::Back );

		add( "Front"_asView, CullMode::Front );
	}
};

static CullModeMatcher cullModeMatcher;

class SortModeMatcher : public wsw::EnumTokenMatcher<SortMode> {
public:
	SortModeMatcher() noexcept {
		add( "Portal"_asView, SortMode::Portal );
		add( "Sky"_asView, SortMode::Sky );
		add( "Opaque"_asView, SortMode::Opaque );
		add( "Banner"_asView, SortMode::Banner );
		add( "Underwater"_asView, SortMode::Underwater );
		add( "Additive"_asView, SortMode::Additive );
		add( "Nearest"_asView, SortMode::Nearest );
	}
};

static SortModeMatcher sortModeMatcher;

class MaterialKeyParser : public wsw::EnumTokenMatcher<MaterialKey> {
public:
	MaterialKeyParser() noexcept {
		add( "Cull"_asView, MaterialKey::Cull );
		add( "SkyParams"_asView, MaterialKey::SkyParams );
		add( "SkyParams2"_asView, MaterialKey::SkyParams2 );
		add( "SkyParamsSides"_asView, MaterialKey::SkyParamsSides );
		add( "FogParams"_asView, MaterialKey::FogParams );
		add( "NoMipMaps"_asView, MaterialKey::NoMipMaps );
		add( "NoPicMip"_asView, MaterialKey::NoPicMip );
		add( "NoCompress"_asView, MaterialKey::NoCompress );
		add( "NoFiltering"_asView, MaterialKey::NoFiltering );
		add( "SmallestMipSize"_asView, MaterialKey::SmallestMipSize );
		add( "PolygonOffset"_asView, MaterialKey::PolygonOffset );
		add( "StencilTest"_asView, MaterialKey::StencilTest );
		add( "Sort"_asView, MaterialKey::Sort );
		add( "DeformVertexes"_asView, MaterialKey::DeformVertexes );
		add( "Portal"_asView, MaterialKey::Portal );
		add( "EntityMergable"_asView, MaterialKey::EntityMergable );
		add( "If"_asView, MaterialKey::If );
		add( "EndIf"_asView, MaterialKey::EndIf );
		add( "OffsetMappingScale"_asView, MaterialKey::OffsetMappingScale );
		add( "GlossExponent"_asView, MaterialKey::GlossExponent );
		add( "GlossIntensity"_asView, MaterialKey::GlossIntensity );
		add( "Template"_asView, MaterialKey::Template );
		add( "Skip"_asView, MaterialKey::Skip );
		add( "SoftParticle"_asView, MaterialKey::SoftParticle );
		add( "ForceWorldOutlines"_asView, MaterialKey::ForceWorldOutlines );
	}
};

static MaterialKeyParser materialKeyParser;

class RgbGenMatcher : public wsw::EnumTokenMatcher<RgbGen> {
public:
	RgbGenMatcher() noexcept {
		add( "Identity"_asView, RgbGen::Identity );
		add( "IdentityLighting"_asView, RgbGen::Identity );
		add( "LightingIdentity"_asView, RgbGen::Identity );
		add( "Wave"_asView, RgbGen::Wave );
		add( "ColorWave"_asView, RgbGen::ColorWave );
		add( "Custom"_asView, RgbGen::Custom );
		add( "CustomWave"_asView, RgbGen::Custom );
		add( "TeamColor"_asView, RgbGen::Custom );
		add( "CustomColorWave"_asView, RgbGen::Custom );
		add( "TeamColorWave"_asView, RgbGen::CustomWave );
		add( "Entity"_asView, RgbGen::Entity );
		add( "EntityWave"_asView, RgbGen::EntityWave );
		add( "OneMinusEntity"_asView, RgbGen::OneMinusEntity );
		add( "Vertex"_asView, RgbGen::Vertex );
		add( "OneMinusVertex"_asView, RgbGen::OneMinusVertex );
		add( "LightingDiffuse"_asView, RgbGen::LightingDiffuse );
		add( "ExactVertex"_asView, RgbGen::ExactVertex );
		add( "Const"_asView, RgbGen::Const );
		add( "Constant"_asView, RgbGen::Const );
	}
};

static RgbGenMatcher rgbGenMatcher;

class AlphaGenMatcher : public wsw::EnumTokenMatcher<AlphaGen> {
public:
	AlphaGenMatcher() noexcept {
		add( "Vertex"_asView, AlphaGen::Vertex );
		add( "OneMinusVertex"_asView, AlphaGen::OneMinusVertex );
		add( "Entity"_asView, AlphaGen::Entity );
		add( "Wave"_asView, AlphaGen::Wave );
		add( "Const"_asView, AlphaGen::Const );
		add( "Constant"_asView, AlphaGen::Const );
	}
};

static AlphaGenMatcher alphaGenMatcher;

class SrcBlendMatcher : public wsw::EnumTokenMatcher<SrcBlend> {
public:
	SrcBlendMatcher() noexcept {
		add( "GL_zero"_asView, SrcBlend::Zero );
		add( "GL_one"_asView, SrcBlend::One );
		add( "GL_dst_color"_asView, SrcBlend::DstColor );
		add( "GL_one_minus_dst_color"_asView, SrcBlend::OneMinusDstColor );
		add( "GL_src_alpha"_asView, SrcBlend::SrcAlpha );
		add( "GL_one_minus_src_alpha"_asView, SrcBlend::OneMinusSrcAlpha );
		add( "GL_dst_alpha"_asView, SrcBlend::DstAlpha );
		add( "GL_one_minus_dst_alpha"_asView, SrcBlend::OneMinusDstAlpha );
	}
};

static SrcBlendMatcher srcBlendMatcher;

class DstBlendMatcher : public wsw::EnumTokenMatcher<DstBlend> {
public:
	DstBlendMatcher() noexcept {
		add( "GL_zero"_asView, DstBlend::Zero );
		add( "GL_one"_asView, DstBlend::One );
		add( "GL_src_color"_asView, DstBlend::SrcColor );
		add( "GL_one_minus_src_color"_asView, DstBlend::OneMinusSrcColor );
		add( "GL_src_alpha"_asView, DstBlend::SrcAlpha );
		add( "GL_one_minus_src_alpha"_asView, DstBlend::OneMinusSrcAlpha );
		add( "GL_dst_alpha"_asView, DstBlend::DstAlpha );
		add( "GL_one_minus_dst_alpha"_asView, DstBlend::OneMinusDstAlpha );
	}
};

static DstBlendMatcher dstBlendMatcher;

class UnaryBlendFuncMatcher : public wsw::EnumTokenMatcher<UnaryBlendFunc> {
public:
	UnaryBlendFuncMatcher() noexcept {
		add( "Blend"_asView, UnaryBlendFunc::Blend );
		add( "Filter"_asView, UnaryBlendFunc::Filter );
		add( "Add"_asView, UnaryBlendFunc::Add );
	}
};

static UnaryBlendFuncMatcher unaryBlendFuncMatcher;

class AlphaFuncMatcher : public wsw::EnumTokenMatcher<AlphaFunc> {
public:
	AlphaFuncMatcher() noexcept {
		add( "Gt0"_asView, AlphaFunc::GT0 );
		add( "Lt128"_asView, AlphaFunc::LT128 );
		add( "Ge128"_asView, AlphaFunc::GE128 );
	}
};

static AlphaFuncMatcher alphaFuncMatcher;

class TCModMatcher : public wsw::EnumTokenMatcher<TCMod> {
public:
	TCModMatcher() noexcept {
		add( "Rotate"_asView, TCMod::Rotate );
		add( "Scale"_asView, TCMod::Scale );
		add( "Scroll"_asView, TCMod::Scroll );
		add( "Stretch"_asView, TCMod::Stretch );
		add( "Transform"_asView, TCMod::Transform );
		add( "Turb"_asView, TCMod::Turb );
	}
};

static TCModMatcher tcModMatcher;

class TCGenMatcher : public wsw::EnumTokenMatcher<TCGen> {
public:
	TCGenMatcher() noexcept {
		add( "Base"_asView, TCGen::Base );
		add( "Lightmap"_asView, TCGen::Lightmap );
		add( "Environment"_asView, TCGen::Environment );
		add( "Vector"_asView, TCGen::Vector );
		add( "Reflection"_asView, TCGen::Reflection );
		add( "Celshade"_asView, TCGen::Celshade );
		add( "Surround"_asView, TCGen::Surround );
	}
};

static TCGenMatcher tcGenMatcher;

class SkySideMatcher : public wsw::EnumTokenMatcher<SkySide> {
public:
	SkySideMatcher() noexcept {
		add( "Rt"_asView, SkySide::Right );
		add( "Bk"_asView, SkySide::Back );
		add( "Lf"_asView, SkySide::Left );
		add( "Rt"_asView, SkySide::Right );
		add( "Up"_asView, SkySide::Up );
		add( "Dn"_asView, SkySide::Down );
	}
};

static SkySideMatcher skySideMatcher;

#define IMPLEMENT_GET_ENUM_METHOD( type, method, matcher ) \
auto MaterialLexer::method() -> std::optional<type> {\
	if( const auto token = getNextTokenInLine() ) {\
		if( const auto func = ::matcher.match( *token ) ) {\
			return func;\
		}\
		unGetToken();\
	}\
	return std::nullopt;\
}

IMPLEMENT_GET_ENUM_METHOD( Func, getFunc, funcTokenMatcher )
IMPLEMENT_GET_ENUM_METHOD( Deform, getDeform, deformTokenMatcher )
IMPLEMENT_GET_ENUM_METHOD( PassKey, getPassKey, passKeyMatcher )
IMPLEMENT_GET_ENUM_METHOD( IntConditionVar, getIntConditionVar, intVarMatcher )
IMPLEMENT_GET_ENUM_METHOD( BoolConditionVar, getBoolConditionVar, boolVarMatcher )
IMPLEMENT_GET_ENUM_METHOD( LogicOp, getLogicOp, logicOpMatcher )
IMPLEMENT_GET_ENUM_METHOD( CmpOp, getCmpOp, cmpOpMatcher )
IMPLEMENT_GET_ENUM_METHOD( CullMode, getCullMode, cullModeMatcher )
IMPLEMENT_GET_ENUM_METHOD( SortMode, getSortMode, sortModeMatcher )
IMPLEMENT_GET_ENUM_METHOD( MaterialKey, getMaterialKey, materialKeyParser )
IMPLEMENT_GET_ENUM_METHOD( RgbGen, getRgbGen, rgbGenMatcher )
IMPLEMENT_GET_ENUM_METHOD( AlphaGen, getAlphaGen, alphaGenMatcher )
IMPLEMENT_GET_ENUM_METHOD( SrcBlend, getSrcBlend, srcBlendMatcher )
IMPLEMENT_GET_ENUM_METHOD( DstBlend, getDstBlend, dstBlendMatcher )
IMPLEMENT_GET_ENUM_METHOD( UnaryBlendFunc, getUnaryBlendFunc, unaryBlendFuncMatcher )
IMPLEMENT_GET_ENUM_METHOD( AlphaFunc, getAlphaFunc, alphaFuncMatcher )
IMPLEMENT_GET_ENUM_METHOD( TCMod, getTCMod, tcModMatcher )
IMPLEMENT_GET_ENUM_METHOD( TCGen, getTCGen, tcGenMatcher )
IMPLEMENT_GET_ENUM_METHOD( SkySide, getSkySide, skySideMatcher )

static const wsw::StringView kEqualLiteral( "equal" );

[[nodiscard]]
auto MaterialLexer::getDepthFunc() -> std::optional<DepthFunc> {
	// Keep the original behaviour
	if( const auto maybeToken = getNextTokenInLine() ) {
		return maybeToken->equalsIgnoreCase( kEqualLiteral ) ? DepthFunc::EQ : DepthFunc::GT;
	}
	return std::nullopt;
}

static const wsw::StringView kTrueLiteral( "true" );
static const wsw::StringView kFalseLiteral( "false" );

auto MaterialLexer::getBool() -> std::optional<bool> {
	if( const auto maybeToken = getNextToken() ) {
		if( kTrueLiteral.equalsIgnoreCase( *maybeToken ) ) {
			return true;
		}
		if( kFalseLiteral.equalsIgnoreCase( *maybeToken ) ) {
			return false;
		}
		unGetToken();
	}
	return std::nullopt;
}

bool MaterialLexer::parseVector( float *dest, size_t numElems ) {
	assert( numElems > 1 && numElems <= 8 );
	float scratchpad[8];

	bool hadParenAtStart = false;
	if( const auto maybeFirstToken = getNextTokenInLine() ) {
		auto token = *maybeFirstToken;
		if( token.equals( wsw::StringView( "(" ) ) ) {
			hadParenAtStart = true;
		} else if( !unGetToken() ) {
			return false;
		}
	}

	for( size_t i = 0; i < numElems; ++i ) {
		if( const auto maybeFloat = getFloat() ) {
			scratchpad[i] = *maybeFloat;
		} else {
			return false;
		}
	}

	// Modify the dest array if and only if parsing has succeeded

	if( !hadParenAtStart ) {
		std::copy( scratchpad, scratchpad + numElems, dest );
		return true;
	}

	if( const auto maybeNextToken = getNextTokenInLine() ) {
		const auto token = *maybeNextToken;
		if( token.equals( wsw::StringView( ")" ) ) ) {
			std::copy( scratchpad, scratchpad + numElems, dest );
			return true;
		}
	}

	return false;
}

void MaterialLexer::parseVectorOrFill( float *dest, size_t numElems, float defaultValue ) {
	assert( numElems > 1 && numElems <= 8 );

	bool hadParenAtStart = false;
	if( const auto maybeFirstToken = getNextTokenInLine() ) {
		if( ( *maybeFirstToken ).equals( wsw::StringView( "(" ) ) ) {
			hadParenAtStart = true;
		} else {
			unGetToken();
		}
	}

	size_t i = 0;
	for(; i < numElems; ++i ) {
		if( const auto maybeFloat = getFloat() ) {
			dest[i] = *maybeFloat;
		} else {
			break;
		}
	}

	std::fill( dest + i, dest + numElems, defaultValue );

	if( hadParenAtStart ) {
		if( const auto maybeNextToken = getNextTokenInLine() ) {
			if( !( *maybeNextToken ).equals( wsw::StringView( ")" ) ) ) {
				unGetToken();
			}
		}
	}
}

bool MaterialLexer::skipToEndOfLine() {
	// Could be optimized but it gets called rarely (TODO: Really?)
	for(;; ) {
		if( getNextTokenInLine() == std::nullopt ) {
			return true;
		}
	}
}

template <typename Predicate>
class CharLookupTable {
	bool values[256];
public:
	CharLookupTable() noexcept {
		memset( values, 0, sizeof( values ) );

		const Predicate predicate;
		for( int i = 0; i < 256; ++i ) {
			if( predicate( (uint8_t)i ) ) {
				values[i] = true;
			}
		}
	}

	bool operator()( char ch ) const {
		return values[(uint8_t)ch];
	}
};

struct IsSpace {
	bool operator()( uint8_t ch ) const {
		for( uint8_t spaceCh : { ' ', '\f', '\n', '\r', '\t', '\v' } ) {
			if( spaceCh == ch ) {
				return true;
			}
		}
		return false;
	}
};

static CharLookupTable<IsSpace> isSpace;

struct IsNewlineChar {
	bool operator()( uint8_t ch ) const {
		return ch == (uint8_t)'\n' || ch == (uint8_t)'\r';
	}
};

static CharLookupTable<IsNewlineChar> isNewlineChar;

struct IsValidNonNewlineChar {
	bool operator()( uint8_t ch ) const {
		return ch != (uint8_t)'\0' && ch != (uint8_t)'\n' && ch != (uint8_t)'\r';
	}
};

static CharLookupTable<IsValidNonNewlineChar> isValidNonNewlineChar;

struct IsLastStringLiteralChar {
	bool operator()( uint8_t ch ) const {
		return ch == (uint8_t)'"' || ch == (uint8_t)'\0';
	}
};

static CharLookupTable<IsLastStringLiteralChar> isLastStringLiteralChar;

auto TokenSplitter::fetchNextTokenInLine() -> std::optional<std::pair<unsigned, unsigned>> {
	const char *__restrict p = m_data + m_offset;

start:
	// Strip whitespace characters until a non-whitespace one or a newline character is met
	for(;; p++ ) {
		if( !isSpace( *p ) ) {
			break;
		}
		if( !isNewlineChar( *p ) ) {
			continue;
		}
		// Strip newline characters
		p++;
		while( isNewlineChar( *p ) ) {
			p++;
		}
		m_offset = p - m_data;
		return std::nullopt;
	}

	if( !*p ) {
		m_offset = p - m_data;
		return std::nullopt;
	}

	if( *p == '/' ) {
		if( p[1] == '/' ) {
			// Skip till end of line
			while( isValidNonNewlineChar( *p ) ) {
				p++;
			}
			// Strip newline at the end
			while( isNewlineChar( *p ) ) {
				p++;
			}
			m_offset = p - m_data;
			return std::nullopt;
		}

		if( p[1] == '*' ) {
			bool metNewline = false;
			// Skip till "*/" is met
			for(;; p++ ) {
				if( !*p ) {
					m_offset = p - m_data;
					return std::nullopt;
				}
				// TODO: Should we mark newlines met?
				if( *p == '*' ) {
					if( p[1] == '/' ) {
						p += 2;
						m_offset = p - m_data;
						break;
					}
				}
				metNewline |= isNewlineChar( *p );
			}
			if( metNewline ) {
				m_offset = p - m_data;
				return std::nullopt;
			}
			// We may just recurse but this can lead to an overflow at bogus files with tons of comments
			goto start;
		}
	}

	if( *p == '"' ) {
		p++;
		const char *tokenStart = p;
		for(;; p++ ) {
			// TODO: What if '\n', '\r' (as a single byte) are met inside a string?
			if( isLastStringLiteralChar( *p ) ) {
				m_offset = p - m_data + 1;
				// What if a string is empty?
				return std::make_pair( tokenStart - m_data, p - tokenStart );
			}
		}
	}

	if( const auto maybeSpanLen = tryMatching1Or2CharsToken( p ) ) {
		const auto len = *maybeSpanLen;
		m_offset = ( p - m_data ) + len;
		return std::make_pair( p - m_data, len );
	}

	const char *tokenStart = p;
	for(;; p++ ) {
		if( !mustCloseTokenAtChar( p[0], p[1] ) ) {
			continue;
		}
		m_offset = p - m_data;
		auto len = p - tokenStart;
		assert( len >= 0 );
		return std::make_pair( tokenStart - m_data, len );
	}
}

auto TokenSplitter::tryMatching1Or2CharsToken( const char *tokenStart ) const -> std::optional<unsigned> {
	const char ch = tokenStart[0];

	if( ch == '{' || ch == '}' || ch == '(' || ch == ')' ) {
		return 1;
	}

	if( ch == '<' || ch == '>' || ch == '!' ) {
		return ( tokenStart[1] == '=' ) ? 2 : 1;
	}

	if( ch == '=' && tokenStart[1] == '=' ) {
		return 2;
	}

	return std::nullopt;
}

struct CloseTokenAt1Char {
	bool operator()( char ch ) const {
		if( isSpace( ch ) ) {
			return true;
		}
		if( ch == '\0' || ch == '"' ) {
			return true;
		}
		if( ch == '{' || ch == '}' || ch == '(' || ch == ')' ) {
			return true;
		}
		if( ch == '>' || ch == '<' || ch == '!' ) {
			return true;
		}
		return false;
	}
};

static CharLookupTable<CloseTokenAt1Char> closeTokenAt1Char;

bool TokenSplitter::mustCloseTokenAtChar( char ch, char nextCh ) {
	if( closeTokenAt1Char( ch ) ) {
		return true;
	}

	if( ch == '/' && ( nextCh == '/' || nextCh == '*' ) ) {
		return true;
	}

	return ch == '=' && nextCh == '=';
}