
set(INC
  ..
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blender/intern/glew-mx
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blender/intern/guardedalloc
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blender/intern/mikktspace
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blender/source/blender/makesdna
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blender/source/blender/makesrna
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blender/source/blender/blenlib
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blender/source/blender/makesrna/intern
)

set(INC_SYS
  ${PYTHON_INCLUDE_DIRS}
  ${GLEW_INCLUDE_DIR}
)

set(SRC
  python_module.cpp
  renderer.cpp
  session.cpp
)

set(ADDON_FILES
  addon/__init__.py
  addon/engine.py
  addon/operators.py
  addon/osl.py
  addon/presets.py
  addon/properties.py
  addon/ui.py
  addon/version_update.py
)

#FIND_PACKAGE(embree 3.0 REQUIRED)

# find python libs
find_package( PythonLibs 3 REQUIRED )
include_directories( ${PYTHON_INCLUDE_DIRS} )

# find boost python libs
if(APPLE)
 find_package( Boost COMPONENTS python37 REQUIRED )
else()
 find_package( Boost COMPONENTS python3 REQUIRED )
endif()

include_directories( ${Boost_INCLUDE_DIRS} )

SET(Boost_USE_STATIC_LIBS OFF)
SET(Boost_USE_MULTITHREADED ON)
SET(Boost_USE_STATIC_RUNTIME OFF)

add_definitions(${GL_DEFINITIONS})

if(WITH_CYCLES_DEVICE_OPENCL)
  add_definitions(-DWITH_OPENCL)
endif()

if(WITH_NEW_OBJECT_TYPES)
  add_definitions(-DWITH_NEW_OBJECT_TYPES)
endif()

# define wrapper library
add_library( _steam SHARED ${SRC} )
set_target_properties( _steam PROPERTIES PREFIX "" )
target_include_directories ( _steam PUBLIC ${STEAM_INCLUDE_DIRECTORY} )

#blender_add_lib(bf_intern_steam "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

target_link_libraries( 
  _steam
  ${Boost_LIBRARIES} 
  ${PYTHON_LIBRARIES} 
  ${OpenCL_LIBRARY} 
)

# avoid link failure with clang 3.4 debug
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT ${CMAKE_C_COMPILER_VERSION} VERSION_LESS '3.4')
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -gline-tables-only")
endif()

#add_dependencies(bf_intern_steam bf_rna)

install(TARGETS _steam DESTINATION ${BLENDER_STEAM_INSTALL_PATH})

install(FILES ${ADDON_FILES} DESTINATION ${BLENDER_STEAM_INSTALL_PATH})
