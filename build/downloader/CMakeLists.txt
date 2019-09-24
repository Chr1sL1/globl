message(STATUS "------------ ${CMAKE_CURRENT_BINARY_DIR}")

add_project(downloader exe)

target_link_libraries(downloader core rt nanopb proto aria2)
add_dependencies(downloader core proto)
