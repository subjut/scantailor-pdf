FIND_PROGRAM(
	MAKENSIS_EXE makensis
	PATHS "[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NSIS;InstallLocation]"
)
IF(NOT MAKENSIS_EXE)
	MESSAGE(
		STATUS
		"makensis.exe could not be found.\n"
		"Installer will not be built.\n"
		"makensis.exe is a part of NSIS. Get NSIS from http://nsis.sf.net/"
	)
ENDIF(NOT MAKENSIS_EXE)
	
IF(MSVC)
	GET_FILENAME_COMPONENT(linker_dir "${CMAKE_LINKER}" PATH)
	FIND_FILE(
		VC_REDIST_DIR VC/Redist PATHS "${linker_dir}"
		PATH_SUFFIXES .. ../.. ../../.. ../../../.. ../../../../.. ../../../../../.. ../../../../../../.. DOC "VC/redist directory."
	)
	IF(VC_REDIST_DIR)
		# Get rid of .. components in the path.
		GET_FILENAME_COMPONENT(VC_REDIST_DIR "${VC_REDIST_DIR}" ABSOLUTE)
		SET(VC_REDIST_DIR "${VC_REDIST_DIR}" CACHE PATH "VC/redist directory." FORCE)
		
		IF(MSVC_VERSION GREATER 1900)
			SET(VC_REDIST_DIR "${VC_REDIST_DIR}/MSVC")
		ENDIF()
		
		# x86 vs x64
		IF(CMAKE_SIZEOF_VOID_P EQUAL 8)
			SET(arch "x64")
		ELSE()
			SET(arch "x86")
		ENDIF()
		
		# Find CRT redistributables.
		IF(MSVC_VERSION GREATER 1900)
			FILE(GLOB crt_redist_dirs "${VC_REDIST_DIR}/*/${arch}/Microsoft.VC*.CRT")
		ELSE()
			FILE(GLOB crt_redist_dirs "${VC_REDIST_DIR}/${arch}/Microsoft.VC*.CRT")
		ENDIF()
		IF(crt_redist_dirs)
			LIST(GET crt_redist_dirs 0 CRT_REDIST_PATH)
		ENDIF(crt_redist_dirs)
	ENDIF(VC_REDIST_DIR)

	IF (NOT CRT_REDIST_PATH)
		MESSAGE(
			FATAL_ERROR
			"Could not find Visual Studio redistributables.\n"
			"They are typically located in a directory like:\n"
			"C:\\Program Files\\Microsoft Visual Studio 9.0\\VC\\redist\n"
			"Please set VC_REDIST_DIR accordinally.\n"
			"Specifically, we are after redist\\x86\\Microsoft.VC*.CRT"
		)
	ENDIF(NOT CRT_REDIST_PATH)
ENDIF()

MATH(EXPR bits "${CMAKE_SIZEOF_VOID_P} * 8")
SET(LICENSE_FILE "${CMAKE_SOURCE_DIR}/GPL3.txt")
SET(INSTALLER_FILENAME "scantailor-${VERSION}-${bits}bit-install.exe")
SET(REGISTER_EXTENSION_NSH "${CMAKE_CURRENT_SOURCE_DIR}/registerExtension.nsh")

SET(
	SOURCES
	scantailor.nsi.in "${CMAKE_SOURCE_DIR}/cmake/generate_nsi_file.cmake.in"
	"${CMAKE_SOURCE_DIR}/cmake/prepare_staging_dir.cmake.in"
)

SOURCE_GROUP("Sources" FILES ${SOURCES})
SOURCE_GROUP("Generated" FILES "${CMAKE_BINARY_DIR}/scantailor.nsi")

CONFIGURE_FILE(
	"${CMAKE_SOURCE_DIR}/cmake/prepare_staging_dir.cmake.in"
	"${CMAKE_BINARY_DIR}/prepare_staging_dir.cmake" @ONLY
)

SET(
	dist_targets
	scantailor scantailor-cli foundation math imageproc dewarping
	acceleration CrashReporter opencl_plugin
)
SET(BUILT_BINARIES "")
FOREACH(target ${dist_targets})
	IF(TARGET ${target})
		LIST(APPEND BUILT_BINARIES "$<TARGET_FILE:${target}>")
	ENDIF()
ENDFOREACH()

SET(MAYBE_CRT_REDIST_PATH "")
IF(CRT_REDIST_PATH)
	SET(MAYBE_CRT_REDIST_PATH "-DCRT_REDIST_PATH=${CRT_REDIST_PATH}")
ENDIF()
ADD_CUSTOM_TARGET(
	prepare_staging_dir
	COMMAND "${CMAKE_COMMAND}" ARGS -DCFG=$<CONFIGURATION>
	"-DCONF_BUILD_DIR=$<TARGET_FILE_DIR:scantailor>"
	"-DBUILT_BINARIES=${BUILT_BINARIES}"
	${MAYBE_CRASHREPORTER_EXE}
	${MAYBE_OPENCL_PLUGIN_DLL}
	${MAYBE_CRT_REDIST_PATH}
	-P "${CMAKE_BINARY_DIR}/prepare_staging_dir.cmake"
	DEPENDS "${CMAKE_SOURCE_DIR}/cmake/prepare_staging_dir.cmake.in"
	VERBATIM
)
ADD_DEPENDENCIES(
	prepare_staging_dir
	scantailor compile_translations #scantailor-cli
)
IF(ENABLE_CRASH_REPORTER)
	ADD_DEPENDENCIES(prepare_staging_dir CrashReporter)
ENDIF()

SET(extra_deps scantailor compile_translations)

SET(DUMP_SYMBOLS_COMMANDS "")
IF(ENABLE_CRASH_REPORTER)
	LIST(APPEND extra_deps CrashReporter)
	SET(
		DUMP_SYMBOLS_COMMANDS
		COMMAND "${CMAKE_COMMAND}" ARGS -E echo "Dumping symbols..."
		
		COMMAND "${DUMP_SYMS_EXECUTABLE}" ARGS "$<TARGET_FILE:scantailor>"
		">" "${SYMBOLS_PATH}/temp.sym"
		COMMAND "${CMAKE_COMMAND}" ARGS "-DSYMBOLS_PATH=${SYMBOLS_PATH}"
		-P "${CMAKE_SOURCE_DIR}/cmake/move_sym_file.cmake"
		
		# COMMAND "${DUMP_SYMS_EXECUTABLE}" ARGS "$<TARGET_FILE:scantailor-cli>"
		# ">" "${SYMBOLS_PATH}/temp.sym"
		COMMAND "${CMAKE_COMMAND}" ARGS "-DSYMBOLS_PATH=${SYMBOLS_PATH}"
		-P "${CMAKE_SOURCE_DIR}/cmake/move_sym_file.cmake"
	)
ENDIF(ENABLE_CRASH_REPORTER)

IF (MAKENSIS_EXE)
	CONFIGURE_FILE(
		"${CMAKE_SOURCE_DIR}/cmake/generate_nsi_file.cmake.in"
		"${CMAKE_BINARY_DIR}/generate_nsi_file.cmake" @ONLY
	)

	SET(
		scantailor_nsi_command
		OUTPUT "${CMAKE_BINARY_DIR}/scantailor.nsi"
		COMMAND "${CMAKE_COMMAND}" "-DCFG=$<CONFIGURATION>"
	)
	IF(ENABLE_CRASH_REPORTER)
		LIST(
			APPEND scantailor_nsi_command
			"-DCRASHREPORTER_EXE=$<TARGET_FILE:CrashReporter>"
		)
	ENDIF()
	LIST(
		APPEND scantailor_nsi_command
		-P "${CMAKE_BINARY_DIR}/generate_nsi_file.cmake"
		DEPENDS scantailor.nsi.in
		"${CMAKE_SOURCE_DIR}/cmake/generate_nsi_file.cmake.in"
		VERBATIM
	)
	ADD_CUSTOM_COMMAND(${scantailor_nsi_command})

	ADD_CUSTOM_COMMAND(
		OUTPUT "${INSTALLER_FILENAME}"
		${DUMP_SYMBOLS_COMMANDS}
		COMMAND "${MAKENSIS_EXE}" /V2 scantailor.nsi
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
		MAIN_DEPENDENCY "${CMAKE_BINARY_DIR}/scantailor.nsi"
		DEPENDS ${extra_deps}
		VERBATIM
	)

	OPTION(BUILD_INSTALLER "Wheter the \"installer\" target gets built by default" OFF)
	SET(maybe_all "")
	IF(BUILD_INSTALLER)
		SET(maybe_all ALL)
	ENDIF()
	ADD_CUSTOM_TARGET(
		installer ${maybe_all}
		SOURCES ${SOURCES}
		DEPENDS "${INSTALLER_FILENAME}"
	)
	ADD_DEPENDENCIES(installer prepare_staging_dir)
ENDIF (MAKENSIS_EXE)
