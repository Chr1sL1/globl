cmake_minimum_required(VERSION 3.5)

include(proj.cmake)

#set(CMAKE_ASM_COMPILER_OBJECT ${CMAKE_C_COMPILER_OBJECT})
#set(CMAKE_ASM_CREATE_STATIC_LIBRARY ${CMAKE_C_CREATE_STATIC_LIBRARY})

message(STATUS "${CMAKE_SOURCE_DIR}")

MACRO(SUB_DIR_LIST result cur_dir)
	file(GLOB sub_folder RELATIVE ${cur_dir} ${cur_dir}/*)
	set(result_list "")
	foreach(sub ${sub_folder})
		set(_cur ${cur_dir}/${sub})
		if(IS_DIRECTORY ${_cur})
			file(GLOB proj_folder RELATIVE ${_cur} ${_cur}/*)
			foreach(filename ${proj_folder})
				string(COMPARE EQUAL ${filename} "CMakeLists.txt" is_equal)
				if(${is_equal})
					list(APPEND result_list ${sub})
				endif()
			endforeach()
		endif()
	endforeach()
	set(${result} ${result_list})
ENDMACRO()

SUB_DIR_LIST(sub_dirs ${CMAKE_SOURCE_DIR})

include_directories(${CMAKE_SOURCE_DIR}/../inc)
include_directories(${CMAKE_SOURCE_DIR}/../inc/proto)
include_directories(${CMAKE_SOURCE_DIR}/../3rd/inc)

link_directories(${CMAKE_SOURCE_DIR}/../3rd/lib)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --std=gnu11 -Wint-to-pointer-cast -Wpointer-to-int-cast -fPIC")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -finline-functions")
add_compile_options(-g)
add_compile_options(-O2)

foreach(_folder ${sub_dirs})
	message(STATUS ">>> sub_directory: ${_folder}")
	add_subdirectory(${_folder})
endforeach()



