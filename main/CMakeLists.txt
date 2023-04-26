file(GLOB srcs src/*.c)
file(GLOB_RECURSE webfiles webroot/*)

idf_component_register(SRCS ${srcs}
		       INCLUDE_DIRS "src"
		       EMBED_FILES ${webfiles})
