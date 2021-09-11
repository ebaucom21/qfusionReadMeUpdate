#include "materialparsertest.h"
#include "../../ref/materiallocal.h"
#include "../../ref/image.h"
#include "../../ref/ref.h"

using wsw::operator""_asView;
using wsw::operator""_asHView;

r_shared_t rsh;

static cvar_t r_portalmaps_holder;
cvar_t *r_portalmaps = &r_portalmaps_holder;

static cvar_t r_lighting_vertexlight_holder;
cvar_t *r_lighting_vertexlight = &r_lighting_vertexlight_holder;

static cvar_t r_lighting_specular_holder;
cvar_t *r_lighting_specular = &r_lighting_specular_holder;

glconfig_t glConfig;
mapconfig_t mapConfig;

image_t *R_FindImage( const wsw::StringView &, const wsw::StringView &, int, int, int ) {
	return new image_s;
}

auto MaterialCache::findImage( const wsw::StringView &name, int flags, int imageTags, int minMipSize ) -> image_s * {
	return new image_s;
}

auto MaterialCache::expandTemplate(const wsw::StringView &, const wsw::StringView *, size_t ) -> MaterialLexer * {
	return nullptr;
}

class ParserTestWrapper {
	char m_data[1024];
	wsw::Vector<TokenSpan> m_tokens;
public:
	explicit ParserTestWrapper( const char *fmt, ... ) __attribute__( ( format( printf, 2, 3 ) ) );

	[[nodiscard]]
	auto exec() -> shader_t * {
		TokenStream stream( m_data, m_tokens.data(), (int)m_tokens.size() );
		MaterialParser parser( nullptr, &stream, ""_asView, ""_asHView, SHADER_TYPE_DELUXEMAP );
		assert( parser.allowUnknownEntries );
		parser.allowUnknownEntries = false;
		assert( !parser.m_strict );
		parser.m_strict = true;
		return parser.exec();
	}
};

ParserTestWrapper::ParserTestWrapper( const char *fmt, ... ) {
	va_list va;
	va_start( va, fmt );
	(void)vsnprintf( m_data, sizeof( m_data ), fmt, va );
	va_end( va );

	TokenSplitter splitter( m_data, std::strlen( m_data ) );
	uint32_t line = 0;
	while( !splitter.isAtEof() ) {
		if( auto maybeToken = splitter.fetchNextTokenInLine() ) {
			auto [off, len] = *maybeToken;
			m_tokens.push_back( { (int32_t)off, len, line } );
		} else {
			line++;
		}
	}
}

static const std::pair<unsigned, const char *> kShaderFuncsAndNames[] = {
	{ SHADER_FUNC_SIN, "Sin" },
	{ SHADER_FUNC_TRIANGLE, "Triangle" },
	{ SHADER_FUNC_SQUARE, "Square" },
	{ SHADER_FUNC_SAWTOOTH, "Sawtooth" },
	{ SHADER_FUNC_INVERSESAWTOOTH, "InvSawtooth" },
	{ SHADER_FUNC_NOISE, "Noise" },
	{ SHADER_FUNC_RAMP, "DistanceRamp" }
};

void MaterialParserTest::test_parseDepthFunc() {
	const char *strings[] = { "Greater", "Equal", "Illegal" };
	bool expectedResults[] = { true, true, false };
	int expectedFlags[] = { GLSTATE_DEPTHFUNC_GT, GLSTATE_DEPTHFUNC_EQ, 0 };

	for( int i = 0; i < 3; ++i ) {
		ParserTestWrapper parser( "{\nDepthfunc %s\n}", strings[i] );
		auto *material = parser.exec();
		QCOMPARE( (bool)material, expectedResults[i] );
		if( !material ) {
			continue;
		}
		QCOMPARE( material->numpasses, 1 );
		QCOMPARE( material->passes[0].flags & ( GLSTATE_DEPTHFUNC_GT | GLSTATE_DEPTHFUNC_EQ ), expectedFlags[i] );
	}
}

void MaterialParserTest::test_parseDepthWrite() {
	ParserTestWrapper parser( "{\nDepthwrite\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
	QCOMPARE( material->numpasses, 1 );
	QVERIFY( material->passes[0].flags & GLSTATE_DEPTHWRITE );
}

void MaterialParserTest::test_parseAlphaFunc() {
	const char *strings[] = { "Gt0", "Lt128", "Ge128", "Illegal" };
	const bool expectedResults[] = { true, true, true, false };
	const int expectedFlags[] = {
		SHADERPASS_AFUNC_GT0,
		SHADERPASS_AFUNC_LT128,
		SHADERPASS_AFUNC_GE128,
		0
	};

	for( int i = 0; i < 4; ++i ) {
		ParserTestWrapper parser( "{\nAlphaFunc %s\n}", strings[i] );
		auto *material = parser.exec();
		QCOMPARE( (bool)material, expectedResults[i] );
		if( !material ) {
			continue;
		}
		QCOMPARE( material->numpasses, 1 );
		QCOMPARE( material->passes[0].flags & SHADERPASS_ALPHAFUNC, expectedFlags[i] );
		QVERIFY( material->passes[0].flags & GLSTATE_ALPHATEST );
	}
}

void MaterialParserTest::test_parseMap() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseAnimMap() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseCubeMap() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseSurroundMap() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseClampMap() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseAnimClampMap() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseMaterial() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseDistortion() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseCelshade() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseTCGen() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseAlphaGen() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseDetail() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseGrayscale() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseSkip() {

}

void MaterialParserTest::test_parseRgbGen_identity() {
	ParserTestWrapper parser( "{\nrgbgen identity\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseRgbGen_wave() {
	ParserTestWrapper parser( "{\nrgbgen wave Inversesawtooth 0.0 5 0 1\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseRgbGen_colorWave() {
	ParserTestWrapper parser( "{\nrgbgen colorwave 0 0 0 Inversesawtooth 0.0 5 0 1\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseRgbGen_custom() {
	ParserTestWrapper parser( "{\nrgbgen custom 1\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numpasses == 1 );
	auto &pass = material->passes[0];
	QVERIFY( pass.rgbgen.type == RGB_GEN_CUSTOMWAVE );
	QVERIFY( pass.rgbgen.args[0] == 1 );
}

void MaterialParserTest::test_parseRgbGen_customWave() {
	ParserTestWrapper parser( "{\nrgbgen customwave 1 Inversesawtooth 0.0 5 0 1\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numpasses == 1 && material->passes[0].rgbgen.type == RGB_GEN_CUSTOMWAVE );
}

void MaterialParserTest::test_parseRgbGen_entity() {
	ParserTestWrapper parser( "{\nrgbgen entity\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numpasses == 1 && material->passes[0].rgbgen.type == RGB_GEN_ENTITYWAVE );
}

void MaterialParserTest::test_parseRgbGen_entityWave() {
	ParserTestWrapper parser( "{\nrgbgen entitywave 0 0 0 Inversesawtooth 0.0 5 0 1\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numpasses == 1 && material->passes[0].rgbgen.type == RGB_GEN_ENTITYWAVE );
}

void MaterialParserTest::test_parseRgbGen_oneMinusEntity() {
	ParserTestWrapper parser( "{\nrgbgen oneMinusEntity\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numpasses == 1 && material->passes[0].rgbgen.type == RGB_GEN_ONE_MINUS_ENTITY );
}

void MaterialParserTest::test_parseRgbGen_vertex() {
	ParserTestWrapper parser( "{\nrgbgen vertex\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numpasses == 1 && material->passes[0].rgbgen.type == RGB_GEN_VERTEX );
}

void MaterialParserTest::test_parseRgbGen_oneMinusVertex() {
	ParserTestWrapper parser( "{\nrgbgen oneMinusVertex\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseRgbGen_lightingDiffuse() {
	ParserTestWrapper parser( "{\nrgbgen lightingDiffuse\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseRgbGen_exactVertex() {
	ParserTestWrapper parser( "{\nrgbgen exactVertex\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseRgbGen_const() {
	ParserTestWrapper parser( "{\nrgbgen const 0.75 0.75 0.75\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseBlendFunc_unary() {
	const std::tuple<const char *, unsigned, bool> expected[] = {
		{ "Blend", GLSTATE_SRCBLEND_SRC_ALPHA | GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA, true },
		{ "Filter", GLSTATE_SRCBLEND_DST_COLOR | GLSTATE_DSTBLEND_ZERO, true },
		{ "Add", GLSTATE_SRCBLEND_ONE | GLSTATE_DSTBLEND_ONE, true },
		{ "Illegal", 0, false }
	};

	for( const auto &[token, flags, outcome]: expected ) {
		ParserTestWrapper parser( "{\nBlendFunc %s\n}", token );
		auto *material = parser.exec();
		QCOMPARE( (bool)material, outcome );
		if( !material ) {
			continue;
		}
		QCOMPARE( material->numpasses, 1 );
		QCOMPARE( material->passes[0].flags & GLSTATE_BLEND_MASK, flags );
	}
}

void MaterialParserTest::test_parseBlendFunc_binary() {
	const std::tuple<const char *, unsigned, bool> srcBlendParts[] = {
		{ "GL_Zero", GLSTATE_SRCBLEND_ZERO, true },
		{ "GL_One", GLSTATE_SRCBLEND_ONE, true },
		{ "GL_Dst_Color", GLSTATE_SRCBLEND_DST_COLOR, true },
		{ "GL_One_Minus_Dst_Color", GLSTATE_SRCBLEND_ONE_MINUS_DST_COLOR, true },
		{ "GL_Src_Alpha", GLSTATE_SRCBLEND_SRC_ALPHA, true },
		{ "GL_Dst_Alpha", GLSTATE_SRCBLEND_DST_ALPHA, true },
		{ "GL_One_Minus_Src_Alpha", GLSTATE_SRCBLEND_ONE_MINUS_SRC_ALPHA, true },
		{ "GL_One_Minus_Dst_Alpha", GLSTATE_SRCBLEND_ONE_MINUS_DST_ALPHA, true },
		{ "Illegal", 0, false }
	};

	const std::tuple<const char *, unsigned, bool> dstBlendParts[] = {
		{ "GL_Zero", GLSTATE_DSTBLEND_ZERO, true },
		{ "GL_One", GLSTATE_DSTBLEND_ONE, true },
		{ "GL_Src_Color", GLSTATE_DSTBLEND_SRC_COLOR, true },
		{ "GL_One_Minus_Src_Color", GLSTATE_DSTBLEND_ONE_MINUS_SRC_COLOR, true },
		{ "GL_Src_Alpha", GLSTATE_DSTBLEND_SRC_ALPHA, true },
		{ "GL_One_Minus_Src_Alpha", GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA, true },
		{ "GL_Dst_Alpha", GLSTATE_DSTBLEND_DST_ALPHA, true },
		{ "GL_One_Minus_Dst_Alpha", GLSTATE_DSTBLEND_ONE_MINUS_DST_ALPHA, true },
		{ "Illegal", 0, false }
	};

	for( const auto &[srcToken, srcFlags, srcOutcome]: srcBlendParts ) {
		for( const auto &[dstToken, dstFlags, dstOutcome]: dstBlendParts ) {
			ParserTestWrapper parser( "{\nBlendFunc %s %s\n}", srcToken, dstToken );
			auto *material = parser.exec();
			QCOMPARE( (bool)material, srcOutcome && dstOutcome );
			if( !material ) {
				continue;
			}
			QCOMPARE( material->numpasses, 1 );
			auto expectedFlags = srcFlags | dstFlags;
			// Account for blending getting finally disabled in this case
			if( expectedFlags == ( GLSTATE_SRCBLEND_ONE | GLSTATE_DSTBLEND_ZERO ) ) {
				expectedFlags = 0;
			}
			QCOMPARE( material->passes[0].flags & GLSTATE_BLEND_MASK, expectedFlags );
		}
	}
}

void MaterialParserTest::test_parseTCMod_rotate() {
	ParserTestWrapper parser( "{\nTCMod rotate 333\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseTCMod_scale() {
	ParserTestWrapper parser( "{\ntcMod scale 3 3\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseTCMod_scroll() {
	ParserTestWrapper parser( "{\ntcMod scroll 0.05 0.05\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseTCMod_stretch() {
	ParserTestWrapper parser( "{\ntcMod stretch sin 1 0 0 0.1\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseTCMod_turb() {
	ParserTestWrapper parser( "{\ntcMod turb 0.1 0.07 0.1 0.01\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseTCMod_transform() {
	ParserTestWrapper parser( "{\ntcMod transform 1 0 1 1 1 1\n}" );
	auto *material = parser.exec();
	QVERIFY( material );
}

void MaterialParserTest::test_parseCull() {
	const std::tuple<const char *, unsigned, bool> expected[] = {
		{ "None", 0, true },
		{ "Front", SHADER_CULL_FRONT, true },
		{ "Back", SHADER_CULL_BACK, true },
		{ "Illegal", 0, false }
	};
	for( const auto &[token, flags, outcome]: expected ) {
		ParserTestWrapper parser( "cull %s", token );
		auto *material = parser.exec();
		QCOMPARE( (bool)material, outcome );
		if( !material ) {
			continue;
		}
		QCOMPARE( material->flags & ( SHADER_CULL_FRONT | SHADER_CULL_BACK ), flags );
	}
}

void MaterialParserTest::test_parseSkyParms() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseSkyParms2() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseSkyParmsSides() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseFogParams() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseNoMipmaps() {
	QCOMPARE( (bool)ParserTestWrapper( "nomipmaps_" ).exec(), false );
	QCOMPARE( (bool)ParserTestWrapper( "nomipmaps" ).exec(), true );
}

void MaterialParserTest::test_parseNoPicmpip() {
	QCOMPARE( (bool)ParserTestWrapper( "nopicmip_" ).exec(), false );
	QCOMPARE( (bool)ParserTestWrapper( "nopicmip" ).exec(), true );
}

void MaterialParserTest::test_parseNoCompress() {
	QCOMPARE( (bool)ParserTestWrapper( "nocompress_" ).exec(), false );
	QCOMPARE( (bool)ParserTestWrapper( "nocompress" ).exec(), true );
}

void MaterialParserTest::test_parseNofiltering() {
	QCOMPARE( (bool)ParserTestWrapper( "nofiltering_" ).exec(), false );
	QCOMPARE( (bool)ParserTestWrapper( "nofiltering" ).exec(), true );
}

void MaterialParserTest::test_parseSmallestMipSize() {
	QCOMPARE( (bool)ParserTestWrapper( "smallestMipSize" ).exec(), false );
	QCOMPARE( (bool)ParserTestWrapper( "smallestMipSize qwerty" ).exec(), false );
	QCOMPARE( (bool)ParserTestWrapper( "smallestMipSize 16" ).exec(), true );
}

void MaterialParserTest::test_parsePolygonOffset() {
	QCOMPARE( (bool)ParserTestWrapper( "polygonoffset_" ).exec(), false );
	QCOMPARE( (bool)ParserTestWrapper( "polygonoffset" ).exec(), true );
}

void MaterialParserTest::test_parseStencilTest() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseEntityMergable() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseSort() {
	QVERIFY( false );
}

void MaterialParserTest::test_parsePortal() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseIf() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseOffsetMappingScale() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseGlossExponent() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseGlossIntensity() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseSoftParticle() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseForceWorldOutlines() {
	QVERIFY( false );
}

void MaterialParserTest::test_parseDeform_wave() {
	ParserTestWrapper parser( "deformVertexes wave 0.0 sin 0.5 0 2.5" );
	const auto *material = parser.exec();
	QVERIFY( material );
	QCOMPARE( material->numdeforms, 1 );
	const auto &deform = material->deforms[0];
	QCOMPARE( deform.type, DEFORMV_WAVE );
	QCOMPARE( deform.func.type, SHADER_FUNC_SIN );
}

void MaterialParserTest::test_parseDeform_bulge() {
	ParserTestWrapper parser( "deformVertexes bulge 1 1 1 1" );
	const auto *material = parser.exec();
	QVERIFY( material );
	QCOMPARE( material->numdeforms, 1 );
	const auto &deform = material->deforms[0];
	QCOMPARE( deform.type, DEFORMV_BULGE );
}

void MaterialParserTest::test_parseDeform_move() {
	ParserTestWrapper parser( "deformVertexes move 0 0 148 sawtooth 0 1 0 1" );
	const auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numdeforms == 1 && material->deforms[0].type == DEFORMV_MOVE );
}

void MaterialParserTest::test_parseDeform_autosprite() {
	ParserTestWrapper parser( "deformVertexes autosprite" );
	const auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numdeforms == 1 && material->deforms[0].type == DEFORMV_AUTOSPRITE );
}

void MaterialParserTest::test_parseDeform_autosprite2() {
	ParserTestWrapper parser( "deformVertexes autosprite2" );
	const auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numdeforms == 1 && material->deforms[0].type == DEFORMV_AUTOSPRITE2 );
}

void MaterialParserTest::test_parseDeform_autoparticle() {
	ParserTestWrapper parser( "deformVertexes autoparticle" );
	const auto *material = parser.exec();
	QVERIFY( material );
	QVERIFY( material->numdeforms == 1 && material->deforms[0].type == DEFORMV_AUTOPARTICLE );
}
