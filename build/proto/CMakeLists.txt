set(proto_dir ${CMAKE_SOURCE_DIR}/../proto)
set(proto_tool_dir ${CMAKE_SOURCE_DIR}/../3rd/nanopb/generator-bin)
set(proto_inc_dir ${CMAKE_SOURCE_DIR}/../inc/proto)
set(proto_src_dir ${CMAKE_SOURCE_DIR}/../src/proto)

file(GLOB_RECURSE proto_list RELATIVE ${proto_dir} ${proto_dir}/*.proto)

foreach(proto_file ${proto_list})
	message(STATUS ">>> proto file: ${proto_file}")
	string(CONCAT proto_file_path ${proto_dir}/${proto_file})
	execute_process(COMMAND ${proto_tool_dir}/protoc "--nanopb_out=${proto_dir}" ${proto_file} WORKING_DIRECTORY ${proto_dir})
endforeach()

file(GLOB_RECURSE h_list RELATIVE ${proto_dir} ${proto_dir}/*.h)
file(GLOB_RECURSE c_list RELATIVE ${proto_dir} ${proto_dir}/*.c)

foreach(h_file ${h_list})
	execute_process(COMMAND mv -f ${h_file} ${proto_inc_dir} WORKING_DIRECTORY ${proto_dir})
endforeach()

foreach(c_file ${c_list})
	execute_process(COMMAND mv -f ${c_file} ${proto_src_dir} WORKING_DIRECTORY ${proto_dir})
endforeach()

add_project(proto lib_static)
