#ifndef WSW_MATERIALLOCAL_H
#define WSW_MATERIALLOCAL_H

#include "../gameshared/q_shared.h"
#include "../qcommon/memspecbuilder.h"
#include "../qcommon/wswstdtypes.h"
#include "../qcommon/wswtonum.h"
#include "../qcommon/qcommon.h"

#include "vattribs.h"
#include "shader.h"
#include "glimp.h"
#include "../qcommon/wswstaticvector.h"
#include "../qcommon/stringspanstorage.h"

#include <optional>

enum class PassKey {
	RgbGen,
	BlendFunc,
	DepthFunc,
	DepthWrite,
	AlphaFunc,
	TCMod,
	Map,
	AnimMap,
	CubeMap,
	ShadeCubeMap,
	ClampMap,
	AnimClampMap,
	Material,
	Distortion,
	CelShade,
	TCGen,
	AlphaGen,
	Detail,
	Grayscale,
	Skip,
};

enum class Deform {
	Wave = DEFORMV_WAVE,
	Bulge = DEFORMV_BULGE,
	Move = DEFORMV_MOVE,
	Autosprite = DEFORMV_AUTOSPRITE,
	Autosprite2 = DEFORMV_AUTOSPRITE2,
	Autoparticle = DEFORMV_AUTOPARTICLE,
};

enum class Func {
	Sin = SHADER_FUNC_SIN,
	Triangle = SHADER_FUNC_TRIANGLE,
	Square = SHADER_FUNC_SQUARE,
	Sawtooth = SHADER_FUNC_SAWTOOTH,
	InvSawtooth = SHADER_FUNC_INVERSESAWTOOTH,
	Noize = SHADER_FUNC_NOISE,
	DistanceRamp = SHADER_FUNC_RAMP,
};

enum class IntConditionVar {
	MaxTextureSize,
	MaxTextureCubemapSize,
	MaxTextureUnits,
};

enum class BoolConditionVar {
	TextureCubeMap,
	Glsl,
	DeluxeMaps,
	PortalMaps,
};

enum class LogicOp {
	And,
	Or
};

enum class CmpOp {
	LS,
	LE,
	GT,
	GE,
	NE,
	EQ
};

enum class CullMode {
	None,
	Back,
	Front
};

enum class SortMode {
	Portal = SHADER_SORT_PORTAL,
	Sky = SHADER_SORT_SKY,
	Opaque = SHADER_SORT_OPAQUE,
	Banner = SHADER_SORT_BANNER,
	Underwater = SHADER_SORT_UNDERWATER,
	Additive = SHADER_SORT_ADDITIVE,
	Nearest = SHADER_SORT_NEAREST
};

enum class MaterialKey {
	Cull,
	SkyParams,
	SkyParams2,
	SkyParamsSides,
	FogParams,
	NoMipMaps,
	NoPicMip,
	NoCompress,
	NoFiltering,
	SmallestMipSize,
	PolygonOffset,
	StencilTest,
	Sort,
	DeformVertexes,
	Portal,
	EntityMergable,
	If,
	EndIf,
	OffsetMappingScale,
	GlossExponent,
	GlossIntensity,
	Template,
	Skip,
	SoftParticle,
	ForceWorldOutlines,
};

enum class RgbGen {
	Identity,
	Wave,
	ColorWave,
	Custom,
	CustomWave,
	Entity,
	EntityWave,
	OneMinusEntity,
	Vertex,
	OneMinusVertex,
	LightingDiffuse,
	ExactVertex,
	Const
};

enum class AlphaGen {
	Vertex = ALPHA_GEN_VERTEX,
	OneMinusVertex = ALPHA_GEN_ONE_MINUS_VERTEX,
	Entity = ALPHA_GEN_ENTITY,
	Wave = ALPHA_GEN_WAVE,
	Const = ALPHA_GEN_CONST,
};

enum class SrcBlend {
	Zero = GLSTATE_SRCBLEND_ZERO,
	One = GLSTATE_SRCBLEND_ONE,
	DstColor = GLSTATE_SRCBLEND_DST_COLOR,
	OneMinusDstColor = GLSTATE_SRCBLEND_ONE_MINUS_DST_COLOR,
	SrcAlpha = GLSTATE_SRCBLEND_SRC_ALPHA,
	DstAlpha = GLSTATE_SRCBLEND_DST_ALPHA,
	OneMinusSrcAlpha = GLSTATE_SRCBLEND_ONE_MINUS_SRC_ALPHA,
	OneMinusDstAlpha = GLSTATE_SRCBLEND_ONE_MINUS_DST_ALPHA
};

enum class DstBlend {
	Zero = GLSTATE_DSTBLEND_ZERO,
	One = GLSTATE_DSTBLEND_ONE,
	SrcColor = GLSTATE_DSTBLEND_SRC_COLOR,
	OneMinusSrcColor = GLSTATE_DSTBLEND_ONE_MINUS_SRC_COLOR,
	SrcAlpha = GLSTATE_DSTBLEND_SRC_ALPHA,
	OneMinusSrcAlpha = GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA,
	DstAlpha = GLSTATE_DSTBLEND_DST_ALPHA,
	OneMinusDstAlpha = GLSTATE_DSTBLEND_ONE_MINUS_DST_ALPHA
};

enum class UnaryBlendFunc {
	Blend = GLSTATE_SRCBLEND_SRC_ALPHA | GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA,
	Filter = GLSTATE_SRCBLEND_DST_COLOR | GLSTATE_DSTBLEND_ZERO,
	Add = GLSTATE_SRCBLEND_ONE | GLSTATE_DSTBLEND_ONE
};

enum class AlphaFunc {
	GT0,
	LT128,
	GE128
};

enum class DepthFunc {
	GT,
	EQ
};

enum class TCMod {
	Rotate = TC_MOD_ROTATE,
	Scale = TC_MOD_SCALE,
	Scroll = TC_MOD_SCROLL,
	Stretch = TC_MOD_STRETCH,
	Transform = TC_MOD_TRANSFORM,
	Turb = TC_MOD_TURB
};

enum class TCGen {
	Base = TC_GEN_BASE,
	Lightmap = TC_GEN_LIGHTMAP,
	Environment = TC_GEN_ENVIRONMENT,
	Vector = TC_GEN_VECTOR,
	Reflection = TC_GEN_REFLECTION,
	Celshade = TC_GEN_REFLECTION_CELSHADE,
	Surround = TC_GEN_SURROUND
};

enum class SkySide {
	Right,
	Back,
	Left,
	Front,
	Up,
	Down
};

class TokenSplitter {
	const char *const data;
	size_t dataSize;
	ptrdiff_t offset { 0 };

	auto tryMatching1Or2CharsToken( const char *tokenStart ) const -> std::optional<unsigned>;

	[[nodiscard]]
	static bool mustCloseTokenAtChar( char ch, char nextCh );
public:
	TokenSplitter( const char *data_, size_t dataSize_ )
		: data( data_ ), dataSize( dataSize_ ) {}

	[[nodiscard]]
	bool isAtEof() const {
		if( (ptrdiff_t)offset >= dataSize ) {
			return true;
		}
		// Protect against bogus files
		return data[offset] == '\0';
	}

	auto fetchNextTokenInLine() -> std::optional<std::pair<uint32_t, uint32_t>>;
};

struct TokenSpan {
	int32_t offset;
	uint32_t len;
	uint32_t line;
};

class TokenStream {
	// Initialize just to suppress a lint warning
	const char *data[2] { nullptr, nullptr };

	const TokenSpan *const tokenSpans;
	const int numTokens;

	int currToken { 0 };
	int currLine { 0 };

	[[nodiscard]]
	auto getView( int offset, unsigned len ) -> wsw::StringView {
		// Use either a data or an alt data as a base ptr based on the offset sign
		const char *p = this->data[offset < 0] + std::abs( offset );
		assert( p );
		return wsw::StringView( p, len );
	}
public:
	TokenStream( const char *data_, const TokenSpan *tokenSpans_, const int numTokens_, const char *altData_ = nullptr )
		: tokenSpans( tokenSpans_ ), numTokens( numTokens_ ) {
		data[0] = data_;
		data[1] = altData_;
	}

	[[nodiscard]]
	bool isAtEof() const {
		return currToken >= numTokens;
	}

	[[nodiscard]]
	auto getCurrTokenNum() const -> int { return currToken; }

	void setCurrTokenNum( int num ) {
		assert( num >= 0 && num <= numTokens );
		currToken = num;
		if( num < numTokens ) {
			currLine = tokenSpans[num].line;
		} else {
			currLine = std::numeric_limits<int>::max();
		}
	}

	[[nodiscard]]
	auto getNextTokenInLine() -> std::optional<wsw::StringView> {
		if( currToken >= numTokens ) {
			return std::nullopt;
		}
		const auto &[off, len, line] = tokenSpans[currToken];
		if( line != currLine ) {
			return std::nullopt;
		}
		currToken++;
		return std::optional( getView( off, len ) );
	}

	[[nodiscard]]
	auto getNextToken() -> std::optional<wsw::StringView> {
		if( currToken >= numTokens ) {
			return std::nullopt;
		}
		const auto &[off, len, line] = tokenSpans[currToken++];
		currLine = line;
		return std::optional( getView( off, len ) );
	}

	[[maybe_unused]]
	bool unGetToken() {
		assert( currToken <= numTokens );
		if( currToken == 0 ) {
			return false;
		}
		currToken = currToken - 1;
		currLine = tokenSpans[currToken].line;
		return true;
	}
};

class MaterialLexer {
	TokenStream *stream { nullptr };

	template <typename T>
	auto getNumber() -> std::optional<T> {
		if( auto maybeToken = getNextTokenInLine() ) {
			if( auto maybeNumber = wsw::toNum<T>( *maybeToken ) ) {
				return maybeNumber;
			}
			unGetToken();
		}
		return std::nullopt;
	}

	template <typename T>
	auto getNumberOr( T defaultValue ) -> T {
		if( auto maybeToken = getNextTokenInLine() ) {
			if( auto maybeNumber = wsw::toNum<T>( *maybeToken ) ) {
				return *maybeNumber;
			}
			unGetToken();
		}
		return defaultValue;
	}

	bool parseVector( float *dest, size_t numElems );
	void parseVectorOrFill( float *dest, size_t numElems, float defaultValue );

	auto getSingleCharToken( char ch ) -> std::optional<bool> {
		if( auto maybeToken = getNextToken() ) {
			auto token = *maybeToken;
			return std::optional( token.length() == 1 && token[0] == ch );
		}
		return std::nullopt;
	}
public:
	explicit MaterialLexer( TokenStream *tokenStream_ ) : stream( tokenStream_ ) {}

	[[nodiscard]]
	bool isAtEof() const {
		return stream->isAtEof();
	}

	[[nodiscard]]
	auto getNextToken() -> std::optional<wsw::StringView> {
		return stream->getNextToken();
	}

	auto getNextTokenInLine() -> std::optional<wsw::StringView> {
		return stream->getNextTokenInLine();
	}

	bool unGetToken() {
		return stream->unGetToken();
	}

	std::optional<PassKey> getPassKey();
	std::optional<Deform> getDeform();
	std::optional<Func> getFunc();
	std::optional<IntConditionVar> getIntConditionVar();
	std::optional<BoolConditionVar> getBoolConditionVar();
	std::optional<LogicOp> getLogicOp();
	std::optional<CmpOp> getCmpOp();
	std::optional<CullMode> getCullMode();
	std::optional<SortMode> getSortMode();
	std::optional<MaterialKey> getMaterialKey();
	std::optional<RgbGen> getRgbGen();
	std::optional<AlphaGen> getAlphaGen();
	std::optional<SrcBlend> getSrcBlend();
	std::optional<DstBlend> getDstBlend();
	std::optional<UnaryBlendFunc> getUnaryBlendFunc();
	std::optional<AlphaFunc> getAlphaFunc();
	std::optional<DepthFunc> getDepthFunc();
	std::optional<TCMod> getTCMod();
	std::optional<TCGen> getTCGen();
	std::optional<SkySide> getSkySide();

	bool skipToEndOfLine();

	auto getFloat() -> std::optional<float> { return getNumber<float>(); }
	auto getInt() -> std::optional<int> { return getNumber<int>(); }

	auto getFloatOr( float defaultValue ) -> float { return getNumberOr<float>( defaultValue ); }
	auto getIntOr( int defaultValue ) -> int { return getNumberOr<int>( defaultValue ); }

	template <size_t N>
	bool getVector( float *dest ) {
		static_assert( N && N <= 8 );
		if constexpr( N == 1 ) {
			if( auto number = getFloat() ) {
				*dest = *number;
				return true;
			}
			return false;
		}
		// Make sure it rolls back offset properly on failure like everything else does
		const auto oldTokenNum = stream->getCurrTokenNum();
		if( parseVector( dest, N ) ) {
			return true;
		}
		stream->setCurrTokenNum( oldTokenNum );
		return false;
	}

	template <size_t N>
	void getVectorOrFill( float *dest, float defaultValue ) {
		static_assert( N && N < 8 );
		if constexpr( N == 1 ) {
			*dest = getFloatOr( defaultValue );
		} else {
			parseVectorOrFill( dest, N, defaultValue );
		}
	}

	auto getBool() -> std::optional<bool>;
};

#include <vector>

struct PlaceholderSpan {
	uint32_t tokenNum;
	uint16_t offset;
	uint8_t len;
	uint8_t argNum;
};

struct MaterialFileContents {
	MaterialFileContents *next {nullptr };
	const char *data { nullptr };
	size_t dataSize { 0 };
	TokenSpan *spans { nullptr };
	unsigned numSpans { 0 };
};

class MaterialSource {
	friend class MaterialCache;
	friend class MaterialSourceTest;

	using Placeholders = wsw::Vector<PlaceholderSpan>;

	std::optional<Placeholders> m_placeholders;

	MaterialSource *nextInList { nullptr };
	MaterialSource *nextInBin { nullptr };

	wsw::HashedStringView m_name;

	const MaterialSource *m_firstInSameMemChunk { nullptr };
	const MaterialFileContents *m_fileContents {nullptr };
	unsigned m_tokenSpansOffset { ~0u };
	unsigned m_numTokens { ~0u };
	bool m_triedPreparingPlaceholders { false };

	struct ExpansionParams {
		const wsw::StringView *args;
		const size_t numArgs;
		const Placeholders &placeholders;
	};

	struct ExpansionState {
		wsw::String &expansionBuffer;
		wsw::Vector<TokenSpan> &resultingTokens;
		size_t tokenStart { 1 };
		size_t lastOffsetInSpan { 0 };
	};

	[[nodiscard]]
	bool expandTemplate( const ExpansionParams &params, ExpansionState &state ) const;

	void addTheRest( ExpansionState &state, size_t lastSpanNum, size_t currSpanNum ) const;

	[[nodiscard]]
	auto validateAndEstimateExpandedDataSize( const ExpansionParams &params ) const -> std::optional<unsigned>;
public:
	[[nodiscard]]
	auto getName() const -> const wsw::HashedStringView & { return m_name; }

	[[nodiscard]]
	auto getCharData() const -> const char * { return m_fileContents->data; }

	// TODO: std::span
	[[nodiscard]]
	auto getTokenSpans() const -> std::pair<const TokenSpan *, unsigned> {
		return std::make_pair( m_fileContents->spans + m_tokenSpansOffset, m_numTokens );
	}

	[[nodiscard]]
	auto preparePlaceholders() -> std::optional<Placeholders>;

	static void findPlaceholdersInToken( const wsw::StringView &token, unsigned tokenNum,
										 wsw::Vector<PlaceholderSpan> &spans );

	[[nodiscard]]
	bool expandTemplate( const wsw::StringView *args, size_t numArgs,
						 wsw::String &expansionBuffer,
						 wsw::Vector<TokenSpan> &resultingTokens );
};

class Skin {
	friend class MaterialCache;
private:
	wsw::StringSpanStorage<uint16_t, uint16_t> m_stringDataStorage;
	wsw::StaticVector<std::pair<shader_s *, unsigned>, 8> m_meshPartMaterials;
	unsigned m_registrationSequence { 0 };
public:
	[[nodiscard]]
	auto getName() const -> wsw::StringView { return m_stringDataStorage.back(); }
};

class MaterialCache {
	friend class MaterialParser;
	friend class MaterialSource;

	MaterialFileContents *fileContentsHead {nullptr };

	enum { kNumBins = 307 };

	MaterialSource *sourcesHead { nullptr };
	MaterialSource *sourceBins[kNumBins] { nullptr };

	shader_t *materialsHead { nullptr };
	shader_t *materialBins[kNumBins] { nullptr };

	shader_t *materialById[MAX_SHADERS] { nullptr };

	wsw::String pathNameBuffer;
	wsw::String cleanNameBuffer;
	wsw::String expansionBuffer;
	wsw::String fileContentsBuffer;

	wsw::Vector<TokenSpan> fileTokenSpans;
	wsw::Vector<TokenSpan> templateTokenSpans;

	wsw::Vector<wsw::StringView> fileMaterialNames;
	wsw::Vector<std::pair<unsigned, unsigned>> fileSourceSpans;

	wsw::Vector<uint16_t> freeMaterialIds;

	wsw::StaticVector<TokenStream, 1> templateTokenStreamHolder;
	wsw::StaticVector<MaterialLexer, 1> templateLexerHolder;
	wsw::StaticVector<TokenStream, 1> primaryTokenStreamHolder;

	wsw::StaticVector<Skin, 16> m_skins;

	auto loadFileContents( const wsw::StringView &fileName ) -> MaterialFileContents *;
	auto readRawContents( const wsw::StringView &fileName ) -> const wsw::String *;

	auto findSourceByName( const wsw::StringView &name ) -> MaterialSource * {
		return findSourceByName( wsw::HashedStringView( name ) );
	}

	auto findSourceByName( const wsw::HashedStringView &name ) -> MaterialSource *;

	auto findImage( const wsw::StringView &name, int flags, int imageTags, int minMipSize = 1 ) -> image_s *;
	void loadMaterial( image_s **images, const wsw::StringView &fullName, int flags, int imageTags, int minMipSize = 1 );

	void loadDirContents( const wsw::StringView &dir );

	void addFileContents( const wsw::StringView &fileName );
	bool tryAddingFileContents( const MaterialFileContents *contents );

	void unlinkAndFree( shader_t *s );

	auto getNextMaterialId() -> unsigned;

	auto makeCleanName( const wsw::StringView &name ) -> wsw::HashedStringView;

	auto getTokenStreamForShader( const wsw::HashedStringView &cleanName ) -> TokenStream *;

	auto loadMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name, int type, TokenStream *tokenStream ) -> shader_t *;

	// This must go once sane material classes get implemented
	auto initMaterial( int type, const wsw::HashedStringView &cleanName, wsw::MemSpecBuilder memSpec ) -> shader_t *;

	auto newDefaultMaterial( int type, const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_t *;
	auto newDefaultVertexMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_t *;
	auto newDefaultDeluxeMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_t *;
	auto newDefaultCoronaMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_t *;
	auto newDefaultDiffuseMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_t *;
	auto newDefault2DLikeMaterial( int type, const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_t *;
	auto newOpaqueEnvMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_t *;
	auto newFogMaterial( const wsw::HashedStringView &cleanName, const wsw::StringView &name ) -> shader_t *;

	auto findSkinByName( const wsw::StringView &name ) -> Skin *;
	[[nodiscard]]
	auto parseSkinFileData( const wsw::StringView &name, const wsw::StringView &fileData ) -> Skin *;
	[[nodiscard]]
	bool parseSkinFileData( Skin *skin, const wsw::StringView &fileData );
	[[nodiscard]]
	auto readSkinFileData( const wsw::StringView &name, char *buffer, size_t bufferSize ) ->
		std::optional<wsw::StringView>;
public:
	MaterialCache();
	~MaterialCache();

	static void init();
	static void shutdown();

	[[nodiscard]]
	static auto instance() -> MaterialCache *;

	void freeUnusedMaterialsByType( const shaderType_e *types, unsigned numTypes );

	void freeUnusedObjects();

	void touchMaterialsByName( const wsw::StringView &name );

	[[nodiscard]]
	auto getMaterialById( int id ) -> shader_t * {
		return materialById[id];
	}

	[[nodiscard]]
	auto expandTemplate( const wsw::StringView &name, const wsw::StringView *args, size_t numArgs ) -> MaterialLexer *;

	[[nodiscard]]
	auto loadMaterial( const wsw::StringView &name, int type, bool forceDefault, image_s *defaultImage ) -> shader_t *;

	[[nodiscard]]
	auto loadDefaultMaterial( const wsw::StringView &name, int type ) -> shader_t *;

	[[nodiscard]]
	auto registerSkin( const wsw::StringView &name ) -> Skin *;

	[[nodiscard]]
	auto findMeshMaterialInSkin( const Skin *skin, const wsw::StringView &meshName ) -> shader_t *;
};

struct shader_s;
struct shaderpass_s;
struct shaderfunc_s;
struct image_s;

class MaterialParser {
	friend class ParserTestWrapper;

	MaterialCache *const materialCache;
	MaterialLexer defaultLexer;
	MaterialLexer *lexer;

	const wsw::StringView name;
	const wsw::HashedStringView cleanName;

	wsw::StaticVector<int, 256> deformSig;
	wsw::StaticVector<shaderpass_t, MAX_SHADER_PASSES> passes;
	wsw::StaticVector<deformv_t, MAX_SHADER_DEFORMVS> deforms;
	wsw::StaticVector<tcmod_t, MAX_SHADER_PASSES * MAX_SHADER_TCMODS> tcMods;

	int sort { 0 };
	int flags { SHADER_CULL_FRONT };
	shaderType_e type { (shaderType_e)0 };

	std::optional<int> minMipSize;

	uint8_t fog_color[4] { 0, 0, 0, 0 };
	float fog_dist { 0.0f };
	float fog_clearDist { 0.0f };

	float glossIntensity { 0.0f };
	float glossExponent { 0.0f };
	float offsetMappingScale { 0.0f };

	float portalDistance { 0.0f };

	int imageTags { 0 };

	bool noPicMip { false };
	bool noMipMaps { false };
	bool noCompress { false };
	bool noFiltering { false };

	bool hasLightmapPass { false };

	bool allowUnknownEntries { true };

	bool m_strict { false };

	[[nodiscard]]
	auto currPass() -> shaderpass_t * {
		assert( !passes.empty() );
		return &passes.back();
	}

	auto tryAddingPassTCMod( TCMod modType ) -> tcmod_t *;
	auto tryAddingDeform( Deform deformType ) -> deformv_t *;

	bool parsePass();
	bool parsePassKey();
	bool parseKey();

	bool parseRgbGen();
	bool parseBlendFunc();
	bool parseDepthFunc();
	bool parseDepthWrite();
	bool parseAlphaFunc();
	bool parseTCMod();
	bool parseMap();
	bool parseAnimMap();
	bool parseCubeMap();
	bool parseShadeCubeMap();
	bool parseSurroundMap();
	bool parseClampMap();
	bool parseAnimClampMap();
	bool parseMaterial();
	bool parseDistortion();
	bool parseCelshade();
	bool parseTCGen();
	bool parseAlphaGen();
	bool parseDetail();
	bool parseGrayscale();
	bool parseSkip();

	bool parseAlphaGenPortal();

	bool parseMapExt( int addFlags );
	bool tryMatchingPortalMap( const wsw::StringView &texNameToken );
	bool tryMatchingLightMap( const wsw::StringView &texNameToken );

	bool parseAnimMapExt( int addFlags );
	bool parseCubeMapExt( int addFlags, int tcGen );

	bool parseCull();
	bool parseSkyParms();
	bool parseSkyParms2();
	bool parseSkyParmsSides();
	bool parseFogParams();
	bool parseNoMipmaps();
	bool parseNoPicmip();
	bool parseNoCompress();
	bool parseNofiltering();
	bool parseSmallestMipSize();
	bool parsePolygonOffset();
	bool parseStencilTest();
	bool parseEntityMergable();
	bool parseSort();
	bool parseDeformVertexes();
	bool parsePortal();
	bool parseIf();
	bool parseOffsetMappingScale();
	bool parseGlossExponent();
	bool parseGlossIntensity();
	bool parseTemplate();
	bool parseSoftParticle();
	bool parseForceWorldOutlines();

	auto parseCondition() -> std::optional<bool>;
	void skipConditionBlock();
	static auto getIntConditionVarValue( IntConditionVar var ) -> int;
	static auto getBoolConditionVarValue( BoolConditionVar var ) -> bool;

	bool parseDeformWave();
	bool parseDeformBulge();
	bool parseDeformMove();

	[[nodiscard]]
	bool parseFunc( shaderfunc_s *func );

	template <typename... Args>
	[[nodiscard]]
	bool addToDeformSignature( Args ... args ) {
		return _addToDeformSignature( args... );
	}

	template <typename... Args>
	[[nodiscard]]
	bool _addToDeformSignature( int arg, Args... rest ) {
		return tryAddingToSignature( arg ) && _addToDeformSignature( rest... );
	}

	template <typename... Args>
	[[nodiscard]]
	bool _addToDeformSignature( unsigned arg, Args... rest ) {
		return tryAddingToSignature( (int)arg ) && _addToDeformSignature( rest... );
	}

	template <typename... Args>
	[[nodiscard]]
	bool _addToDeformSignature( float arg, Args... rest ) {
		union { float f; int32_t i; } u;
		u.f = arg;
		return tryAddingToSignature( u.i ) && _addToDeformSignature( rest... );
	}

	[[nodiscard]]
	bool tryAddingToSignature( int value ) {
		if( deformSig.size() != deformSig.capacity() ) {
			deformSig.push_back( value );
			return true;
		}
		return false;
	}

	[[nodiscard]]
	static bool _addToDeformSignature() { return true; }

	int getImageFlags();

	auto findImage( const wsw::StringView &name_, int flags_ ) -> image_s * {
		return materialCache->findImage( name_, flags_, imageTags, minMipSize.value_or( 1 ) );
	}

	void fixLightmapsForVertexLight();
	void fixFlagsAndSortingOrder();

	auto build() -> shader_t *;

	auto buildVertexAttribs() -> int;
	static auto getDeformVertexAttribs( const deformv_t &deform ) -> int;
	static auto getPassVertexAttribs( const shaderpass_t &pass ) -> int;
	static auto getRgbGenVertexAttribs( const shaderpass_t &pass, const colorgen_t &gen ) -> int;
	static auto getAlphaGenVertexAttribs( const colorgen_t &gen ) -> int;
	static auto getTCGenVertexAttribs( unsigned gen ) -> int;
public:
	MaterialParser( MaterialCache *materialCache_,
					TokenStream *mainTokenStream_,
					const wsw::StringView &name_,
					const wsw::HashedStringView &cleanName_,
					shaderType_e type_ );

    auto exec() -> shader_t *;
};

const wsw::StringView kNormSuffix( "_norm" );
const wsw::StringView kGlossSuffix( "_gloss" );
const wsw::StringView kDecalSuffix( "_decal" );
const wsw::StringView kAddSuffix( "_add" );

class MaterialIfEvaluator {
public:
	static constexpr size_t Capacity = 32;
private:
	enum class Tag: uint8_t {
		Value,
		UnaryNot,
		LogicOp,
		CmpOp,
		LParen,
		RParen
	};

	struct alignas( 8 )TapeEntry {
		uint8_t data[7];
		Tag tag;
	};

	TapeEntry m_tape[Capacity];
	int m_numEntries { 0 };
	int m_tapeCursor { 0 };
	bool m_hadError { false };

#pragma pack( push, 2 )
	struct Value {
		union {
			int32_t i;
			bool b;
		} u;

		bool isBool;
		bool isInputValue { false };

		explicit Value( bool b ) : isBool( true ) {
			u.b = b;
		}
		explicit Value( int32_t i ) : isBool( false ) {
			u.i = i;
		}
		operator bool() const {
			return isBool ? u.b : u.i;
		}
		operator int() const {
			return isBool ? u.b : u.i;
		}
	};

	static_assert( alignof( Value ) <= 8 );
	static_assert( sizeof( Value ) == 6 );
#pragma pack( pop )

	auto makeEntry( Tag tag ) -> TapeEntry * {
		assert( m_numEntries < Capacity );
		auto *e = &m_tape[m_numEntries++];
		e->tag = tag;
		return e;
	}

	auto nextTokenTag() -> std::optional<Tag> {
		return ( m_tapeCursor < m_numEntries ) ? std::optional( m_tape[m_tapeCursor++].tag ) : std::nullopt;
	}

	void ungetToken() {
		assert( m_tapeCursor >= 0 );
		m_tapeCursor = m_tapeCursor ? m_tapeCursor - 1 : 0;
	}

	[[nodiscard]]
	auto lastEntry() const -> const TapeEntry & {
		assert( m_tapeCursor - 1 < m_numEntries );
		return m_tape[m_tapeCursor - 1];
	}

	template <typename T>
	[[nodiscard]]
	auto lastEntryAs() const -> T {
		return *( ( const T *)lastEntry().data );
	}

	[[nodiscard]]
	auto lastTag() const -> Tag {
		assert( m_tapeCursor - 1 < m_numEntries );
		return m_tape[m_tapeCursor - 1].tag;
	}

	[[nodiscard]]
	auto lastValue() -> std::optional<Value> {
		return ( lastTag() == Tag::Value ) ? std::optional( lastEntryAs<Value>() ) : std::nullopt;
	}

	[[nodiscard]]
	auto lastLogicOp() -> std::optional<LogicOp> {
		return ( lastTag() == Tag::LogicOp ) ? std::optional( lastEntryAs<LogicOp>() ) : std::nullopt;
	}

	[[nodiscard]]
	auto lastCmpOp() -> std::optional<CmpOp> {
		return ( lastTag() == Tag::CmpOp ) ? std::optional( lastEntryAs<CmpOp>() ) : std::nullopt;
	}

#ifndef _MSC_VER
	std::nullopt_t withError( const char *fmt, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
	void warn( const char *fmt, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
#else
	auto withError( _Printf_format_string_ const char *fmt, ... ) -> std::nullopt_t;
	void warn( _Printf_format_string_ const char *fmt, ... );
#endif

	void warnBooleanToIntegerConversion( const Value &value, const char *desc, const char *context );
	void warnIntegerToBooleanConversion( const Value &value, const char *desc, const char *context );

	[[nodiscard]]
	auto evalUnaryExpr() -> std::optional<Value>;
	[[nodiscard]]
	auto evalCmpExpr() -> std::optional<Value>;
	[[nodiscard]]
	auto evalLogicExpr() -> std::optional<Value>;

	[[nodiscard]]
	auto evalExpr() -> std::optional<Value> { return evalLogicExpr(); }
public:

	void addInt( int value ) {
		auto *v = new( makeEntry( Tag::Value ) )Value( value );
		v->isInputValue = true;
	}

	void addBool( bool value ) {
		auto *v = new( makeEntry( Tag::Value ) )Value( value );
		v->isInputValue = true;
	}

	void addLogicOp( LogicOp op ) {
		new( makeEntry( Tag::LogicOp ) )LogicOp( op );
	}

	void addCmpOp( CmpOp op ) {
		new( makeEntry( Tag::CmpOp ) )CmpOp( op );
	}

	void addUnaryNot() { makeEntry( Tag::UnaryNot ); }

	void addLeftParen() { makeEntry( Tag::LParen ); }
	void addRightParen() { makeEntry( Tag::RParen ); }

	[[nodiscard]]
	auto exec() -> std::optional<bool>;
};

#endif
