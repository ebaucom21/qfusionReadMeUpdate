#ifndef WSW_MATERIALPARSERTEST_H
#define WSW_MATERIALPARSERTEST_H

#include <QtTest/QtTest>

class MaterialParserTest : public QObject {
	Q_OBJECT

private slots:
	void test_parseDepthFunc();
	void test_parseDepthWrite();
	void test_parseAlphaFunc();
	void test_parseMap();
	void test_parseAnimMap();
	void test_parseCubeMap();
	void test_parseSurroundMap();
	void test_parseClampMap();
	void test_parseAnimClampMap();
	void test_parseMaterial();
	void test_parseDistortion();
	void test_parseCelshade();
	void test_parseTCGen();
	void test_parseAlphaGen();
	void test_parseDetail();
	void test_parseGrayscale();
	void test_parseSkip();

	void test_parseRgbGen_identity();
	void test_parseRgbGen_wave();
	void test_parseRgbGen_colorWave();
	void test_parseRgbGen_custom();
	void test_parseRgbGen_customWave();
	void test_parseRgbGen_entity();
	void test_parseRgbGen_entityWave();
	void test_parseRgbGen_oneMinusEntity();
	void test_parseRgbGen_vertex();
	void test_parseRgbGen_oneMinusVertex();
	void test_parseRgbGen_lightingDiffuse();
	void test_parseRgbGen_exactVertex();
	void test_parseRgbGen_const();

	void test_parseBlendFunc_unary();
	void test_parseBlendFunc_binary();

	void test_parseTCMod_rotate();
	void test_parseTCMod_scale();
	void test_parseTCMod_scroll();
	void test_parseTCMod_stretch();
	void test_parseTCMod_turb();
	void test_parseTCMod_transform();

	void test_parseCull();
	void test_parseSkyParms();
	void test_parseSkyParms2();
	void test_parseSkyParmsSides();
	void test_parseFogParams();
	void test_parseNoMipmaps();
	void test_parseNoPicmpip();
	void test_parseNoCompress();
	void test_parseNofiltering();
	void test_parseSmallestMipSize();
	void test_parsePolygonOffset();
	void test_parseStencilTest();
	void test_parseEntityMergable();
	void test_parseSort();
	void test_parsePortal();
	void test_parseIf();
	void test_parseOffsetMappingScale();
	void test_parseGlossExponent();
	void test_parseGlossIntensity();
	void test_parseSoftParticle();
	void test_parseForceWorldOutlines();

	void test_parseDeform_wave();
	void test_parseDeform_bulge();
	void test_parseDeform_move();
	void test_parseDeform_autosprite();
	void test_parseDeform_autosprite2();
	void test_parseDeform_autoparticle();
};

#endif
