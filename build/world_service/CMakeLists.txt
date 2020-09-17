message(STATUS "------------ ${CMAKE_CURRENT_BINARY_DIR}")

add_project(world_service exe)

target_link_libraries(world_service core rt nanopb proto lua m dl)
add_dependencies(world_service core proto)
