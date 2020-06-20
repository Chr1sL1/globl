message(STATUS "------------ ${CMAKE_CURRENT_BINARY_DIR}")

add_project(lua_test exe)

target_link_libraries(lua_test core rt nanopb proto lua m dl)
#add_dependencies(lua_test)
