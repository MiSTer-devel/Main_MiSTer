PROJECT(miniz C)
cmake_minimum_required(VERSION 2.8)
if(CMAKE_BUILD_TYPE STREQUAL "")
  # CMake defaults to leaving CMAKE_BUILD_TYPE empty. This screws up
  # differentiation between debug and release builds.
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
    "Choose the type of build, options are: None (CMAKE_CXX_FLAGS or \
CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

set(miniz_SOURCE miniz.c miniz_zip.c miniz_tinfl.c miniz_tdef.c)

add_library(miniz ${miniz_SOURCE})

set(EXAMPLE1_SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/examples/example1.c")
set(EXAMPLE2_SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/examples/example2.c")
set(EXAMPLE3_SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/examples/example3.c")
set(EXAMPLE4_SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/examples/example4.c")
set(EXAMPLE5_SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/examples/example5.c")
set(EXAMPLE6_SRC_LIST "${CMAKE_CURRENT_SOURCE_DIR}/examples/example6.c")
set(MINIZ_TESTER_SRC_LIST
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/miniz_tester.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/timer.cpp")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_executable(example1 ${EXAMPLE1_SRC_LIST})
target_link_libraries(example1 miniz)
add_executable(example2 ${EXAMPLE2_SRC_LIST})
target_link_libraries(example2 miniz)
add_executable(example3 ${EXAMPLE3_SRC_LIST})
target_link_libraries(example3 miniz)
add_executable(example4 ${EXAMPLE4_SRC_LIST})
target_link_libraries(example4 miniz)
add_executable(example5 ${EXAMPLE5_SRC_LIST})
target_link_libraries(example5 miniz)
add_executable(example6 ${EXAMPLE6_SRC_LIST})
target_link_libraries(example6 miniz)
if(${UNIX})
    target_link_libraries(example6 m)
endif()

# add_executable(miniz_tester ${MINIZ_TESTER_SRC_LIST})
# target_link_libraries(miniz_tester miniz)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Targets
    ARCHIVE  DESTINATION lib
    LIBRARY DESTINATION lib
    )
file(GLOB INSTALL_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
install(FILES ${INSTALL_HEADERS} DESTINATION include/${PROJECT_NAME})