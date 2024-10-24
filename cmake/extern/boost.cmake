# SPDX-FileCopyrightText: © 2022-24 Daniel Just <justibus@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only

# Suppress a warning
set(Boost_NO_WARN_NEW_VERSIONS 1)

if(POLICY CMP0167)
	# Use BoostConfig.cmake (since 1.70) from boost itself instead of the FindBoost package from cmake
	cmake_policy(SET CMP0167 NEW)
endif()
if(POLICY CMP0144)
	# Use BOOST_ROOT Variable
	cmake_policy(SET CMP0144 NEW)
endif()


if(NOT WIN32 AND BUILD_SHARED_LIBS)

	find_package(Boost REQUIRED COMPONENTS test_exec_monitor unit_test_framework)
	
else() # Local static build
	
	set(Boost_USE_STATIC_LIBS ON)
	if(NOT BUILD_SHARED_LIBS AND MINGW)
		set(Boost_USE_STATIC_RUNTIME ON)
	endif()
	
	find_package(Boost COMPONENTS test_exec_monitor unit_test_framework
		NO_MODULE				# Don't use installed modules for the search
		NO_DEFAULT_PATH		# Only search in ${EXTERN}
		HINTS ${EXTERN}
		QUIET
	)

	if(Boost_FOUND)

		add_definitions(-DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION)
		
		message(STATUS "Found Boost in ${Boost_DIR}:\n"
							"         ${Boost_LIBRARIES}")
		# Needed for dependency satisfaction after external project has been built
		add_custom_target(boost-extern DEPENDS Boost::test_exec_monitor Boost::unit_test_framework)

	else()	# Boost has not been built yet. Configure for build.
	
		set(HAVE_DEPENDENCIES FALSE)
		
		set(BOOST_64BIT_FLAGS "")
		if(CMAKE_SIZEOF_VOID_P EQUAL 8)
			list(APPEND BOOST_64BIT_FLAGS "address-model=64")
		endif()
		
		set(BOOST_TOOLSET msvc)	# Assume MSVC
		set(BOOST_BOOTSTRAP "bootstrap")

		if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
			 CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
			set(BOOST_TOOLSET clang)
		elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
			set(BOOST_TOOLSET gcc)
		endif()	# MSVC is assumed and set above
		
		if(UNIX)
			set(BOOST_BOOTSTRAP "./bootstrap.sh")
		endif()
		
		set(BOOST_STATIC_RUNTIME)
		if(NOT BUILD_SHARED_LIBS AND MINGW)
			set(BOOST_STATIC_RUNTIME runtime-link=static)
		endif()
		
		set(BOOST_MSVC_SHARED)
		if(MSVC AND BUILD_SHARED_LIBS)
		 set(BOOST_MSVC_SHARED variant=debug)
		endif()
		
		ExternalProject_Add(
			boost-extern
			PREFIX ${EXTERN}
			URL https://archives.boost.io/release/1.86.0/source/boost_1_86_0.7z
			URL_HASH SHA256=413ee9d5754d0ac5994a3bf70c3b5606b10f33824fdd56cf04d425f2fc6bb8ce
			DOWNLOAD_DIR ${DOWNLOAD_DIR}
			CONFIGURE_COMMAND ""
			BUILD_COMMAND ""  # All steps are done below because of working directory
			INSTALL_COMMAND ""
			UPDATE_COMMAND ""  # Don't rebuild on main project recompilation
		)
				
		## Consider switching to an in source tree build. This below is tedious.
		# Boost needs the cwd to be its source dir but ExernelProject_Add() uses
		# <BINARY_DIR>. For out of source tree builds, we have to add extra steps.
		ExternalProject_Add_Step(boost-extern bootstrap
			DEPENDEES configure
			DEPENDERS build
			COMMAND ${BOOST_BOOTSTRAP} ${BOOST_TOOLSET}
			WORKING_DIRECTORY <SOURCE_DIR>
		)
		
		ExternalProject_Add_Step(boost-extern b2
			DEPENDEES bootstrap
			DEPENDERS install
			COMMAND ./b2 --with-test toolset=${BOOST_TOOLSET} threading=multi link=static ${BOOST_STATIC_RUNTIME} variant=release ${BOOST_MSVC_SHARED} ${BOOST_64BIT_FLAGS} --layout=tagged --build-dir=<BINARY_DIR> --stagedir=${EXTERN}
			WORKING_DIRECTORY <SOURCE_DIR>
		)

	endif()
endif()
