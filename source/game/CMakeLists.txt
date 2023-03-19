project (game)

set(ANGELSCRIPT_DIR ${CMAKE_HOME_DIRECTORY}/../third-party/angelscript/sdk/angelscript)

include_directories(${ANGELSCRIPT_DIR}/include)
link_directories(${ANGELSCRIPT_DIR}/lib)

file(GLOB GAME_HEADERS
	"*.h"
	"ai/*.h"
	"ai/awareness/*.h"
	"ai/combat/*.h"
	"ai/movement/*.h"
	"ai/navigation/*.h"
	"ai/planning/*.h"
	"ai/teamplay/*.h"
	"ascript/*.h"
	"ascript/addon/*.h"
	"../gameshared/*.h"
	"../qcommon/base64.h"
	"../qcommon/hash.h"
	"../qcommon/links.h"
	"../qcommon/md5.h"
	"../qcommon/net.h" # constants and types for address parsing
	"../qcommon/snap.h" # constants for the antihack system
	"../qcommon/wswsortbyfield.h"
)

file(GLOB GAME_SOURCES
	"*.cpp"
	"ai/*.cpp"
	"ai/awareness/*.cpp"
	"ai/combat/*.cpp"
	"ai/movement/*.cpp"
	"ai/navigation/*.cpp"
	"ai/planning/*.cpp"
	"ai/teamplay/*.cpp"
	"ascript/*.cpp"
	"ascript/addon/*.cpp"
	"../gameshared/*.cpp"
	"../qcommon/mmrating.cpp"
	"../qcommon/base64.cpp"
	"../qcommon/md5.cpp"
	"../qcommon/hash.cpp"
	"../qcommon/cjson.cpp"
	"../qcommon/userinfo.cpp"
	"../qcommon/wswexceptions.cpp"
	"../qcommon/wswstringview.cpp"
	"../qcommon/wswsortbyfield.cpp"
	"../qcommon/wswfs.cpp"
)

add_library(game SHARED ${GAME_SOURCES} ${GAME_HEADERS})
target_link_libraries(game PRIVATE angelscript)
qf_set_output_dir(game ${QFUSION_GAME_DIR})
