file(GLOB srcs src/*.c)
file(GLOB_RECURSE webfiles LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_LIST_DIR} webroot/*)

idf_component_register(SRCS ${srcs}
		       INCLUDE_DIRS "src"
		       EMBED_FILES ${webfiles})
