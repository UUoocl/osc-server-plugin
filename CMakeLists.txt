cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

set(CMAKE_FRAMEWORK_PATH ${CMAKE_FRAMEWORK_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/Frameworks")

project(${_name} VERSION ${_version})

if(APPLE)
  add_link_options("-F${CMAKE_CURRENT_SOURCE_DIR}/Frameworks")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  set(WrapOpenGL_AGL "/System/Library/Frameworks/OpenGL.framework" CACHE FILEPATH "" FORCE)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE 
  src/plugin-main.cpp
  src/osc-manager.cpp
  src/osc-settings-dialog.cpp
  src/thirdparty/tinyosc.c
)

set(WrapOpenGL_AGL "/System/Library/Frameworks/OpenGL.framework" CACHE FILEPATH "" FORCE)
set(OPENGL_agl_LIBRARY "/System/Library/Frameworks/OpenGL.framework" CACHE FILEPATH "" FORCE)

if(APPLE)
  get_target_property(LIBS ${CMAKE_PROJECT_NAME} LINK_LIBRARIES)
  if(LIBS)
    list(REMOVE_ITEM LIBS "-framework AGL")
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINK_LIBRARIES "${LIBS}")
  endif()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
