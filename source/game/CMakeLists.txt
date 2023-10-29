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
	"../common/*.h" # TODO?
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
)

add_library(game SHARED ${GAME_SOURCES} ${GAME_HEADERS})
add_dependencies(game wswcommon)
target_link_libraries(game PRIVATE wswcommon angelscript)
qf_set_output_dir(game ${QFUSION_GAME_DIR})
