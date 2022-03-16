# SPDX-FileCopyrightText: © 2022 Daniel Just <justibus@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only

# Some options for find_package()
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

if(USE_SYSTEM_LIBS) # We always link boost statically

	find_package(Boost REQUIRED	COMPONENTS test_exec_monitor unit_test_framework)
	
else(USE_SYSTEM_LIBS) # Local static build

	# Instead of manually searching for the library files, we let find_package() do it.
	# Set search directory hint
	if(EXISTS ${EXTERN}/lib/cmake/Boost-1.78.0)

		set(BOOST_ROOT ${EXTERN})
		find_package(Boost REQUIRED COMPONENTS test_exec_monitor unit_test_framework)	
	
	else() # Boost has not been built yet. Configure for build.
	
		message(STATUS "Boost has not been fully built yet. "
							"After the first build without errors, just rerun the cmake configuration and "
							"generation steps and it should find Boost and build fine.")

		set(BOOST_64BIT_FLAGS "")
		if(CMAKE_SIZEOF_VOID_P EQUAL 8)
			list(APPEND BOOST_64BIT_FLAGS "address-model=64")
		endif()
		
		set(BOOST_TOOLSET "")
		if(${CMAKE_GENERATOR} STREQUAL "NMake Makefiles")
			set(BOOST_TOOLSET "msvc")
		elseif(	${CMAKE_GENERATOR} STREQUAL "Ninja" OR
					${CMAKE_GENERATOR} STREQUAL "MinGW Makefiles" OR
					${CMAKE_GENERATOR} STREQUAL "Unix Makefiles" OR
					${CMAKE_GENERATOR} STREQUAL "MSYS Makefiles")
			set(BOOST_TOOLSET "gcc")
		endif()
		
		# Since at least boost 1.76 and maybe earlier does not pass the toolset from the
		# bootstrap to the b2 build makefile. So it uses msvc by default.
		# See: https://github.com/boostorg/boost/issues/506
		# Build fails if you use mingw in a non-standard location (e.g. not in c:\Mingw).
		# Fix: line 15 in bootstrap.bat: call .\build.bat %1
		ExternalProject_Add(
			boost-extern
			PREFIX ${EXTERN}
			URL https://boostorg.jfrog.io/artifactory/main/release/1.78.0/source/boost_1_78_0.7z
			URL_HASH SHA256=090cefea470bca990fa3f3ed793d865389426915b37a2a3258524a7258f0790c
			# Fix for comment above
			PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${EXTERN}/src/patches/boost-extern/bootstrap.bat <SOURCE_DIR>/bootstrap.bat
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
			COMMAND bootstrap ${BOOST_TOOLSET}
			WORKING_DIRECTORY <SOURCE_DIR>
		)
		
		ExternalProject_Add_Step(boost-extern b2
			DEPENDEES bootstrap
			DEPENDERS install
			COMMAND b2 --with-test toolset=${BOOST_TOOLSET} threading=multi link=static runtime-link=static variant=release ${BOOST_64BIT_FLAGS} --build-dir=<BINARY_DIR> --stagedir=${EXTERN}
			WORKING_DIRECTORY <SOURCE_DIR>
		)

	endif()
endif(USE_SYSTEM_LIBS)

set(LIB_BOOST Boost_LIBRARIES)
list(APPEND ALL_EXTERN_INC_DIRS ${Boost_INCLUDE_DIRS})
add_definitions(-DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION)