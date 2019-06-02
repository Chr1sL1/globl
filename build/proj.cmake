function (add_project proj_name proj_type)
	project(${proj_name} LANGUAGES C ASM)

	message(">>>>>>>>>> generating project ${proj_name}")

	execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE arch)
	message(STATUS "Archtecture: ${arch}")

	string(COMPARE EQUAL ${arch} "x86_64" is_x86)

	set(result_src_list "")

	set(src_dir ${CMAKE_SOURCE_DIR}/../src/${proj_name})
	set(lib_dir ${CMAKE_SOURCE_DIR}/../lib)
	set(exe_dir ${CMAKE_SOURCE_DIR}/../bin)

	file(GLOB_RECURSE proj_src_list RELATIVE ${src_dir} ${src_dir}/*.c ${src_dir}/*.s)

	if(${is_x86})
		message(STATUS "Yay!! we can use x86 assembly.")
		file(GLOB_RECURSE asm_src_list RELATIVE ${src_dir} ${src_dir}/*.s)

		foreach(asm_file_name ${asm_src_list})
			string(CONCAT asm_file_path ${src_dir}/ ${asm_file_name})
			message(STATUS "${asm_file_path}")
			set_property(SOURCE ${asm_file_path} PROPERTY LANGUAGE C)
		endforeach()
	endif()

	foreach(file_name ${proj_src_list})
		string(CONCAT file_path ${src_dir}/ ${file_name})
		list(APPEND result_src_list ${file_path})
	endforeach()

	string(COMPARE EQUAL ${proj_type} "lib_static" is_static)
	string(COMPARE EQUAL ${proj_type} "lib_shared" is_shared)
	string(COMPARE EQUAL ${proj_type} "exe" is_exe)

	if(${is_static})
		add_library(${proj_name} STATIC ${result_src_list})
		set_target_properties(${proj_name} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${lib_dir})
	elseif(${is_shared})
		add_library(${proj_name} SHARED ${result_src_list})
		set_target_properties(${proj_name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${lib_dir})
	elseif(${is_exe})
		add_executable(${proj_name} ${result_src_list})
		set_target_properties(${proj_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${exe_dir})
	endif()

endfunction()

