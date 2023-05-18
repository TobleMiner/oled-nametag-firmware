file(GLOB srcs src/*.c)
file(GLOB_RECURSE webfiles LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_LIST_DIR} assets/webroot/*)
file(GLOB_RECURSE bitmaps LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_LIST_DIR} assets/bitmaps/*)
file(GLOB_RECURSE fonts LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_LIST_DIR} assets/fonts/*)

idf_component_register(SRCS ${srcs}
		       INCLUDE_DIRS "src"
		       EMBED_FILES ${webfiles} ${bitmaps} ${fonts})

execute_process(COMMAND git describe --always --dirty OUTPUT_VARIABLE badge_app_version)
add_compile_definitions(BADGE_APP_VERSION=${badge_app_version})
