message(STATUS "------------ ${CMAKE_CURRENT_BINARY_DIR}")

add_project(harbor exe)

target_link_libraries(harbor core rt nanopb proto lua m dl)
add_dependencies(harbor core proto)
