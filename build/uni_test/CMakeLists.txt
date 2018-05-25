message(STATUS "------------ ${CMAKE_CURRENT_BINARY_DIR}")

add_project(uni_test exe)

target_link_libraries(uni_test syslib rt protobuf-nanopb proto)
add_dependencies(uni_test syslib)
