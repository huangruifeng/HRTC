set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 11)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# custom platform macro
if(MSVC)
    set(HRTC_WINDOWS ON)

    set(CMAKE_CXX_FLAGS " -DUNICODE -D_UNICODE /DHRTC_WINDOWS /std:c++14 /permissive- /wd5208 /wd4101  ${CMAKE_CXX_FLAGS}  ")
    if("${CMAKE_GENERATOR_PLATFORM}" STREQUAL "x64")
        set(MAXME_WINDOWS_64 ON)
        set(CMAKE_CXX_FLAGS " /DHRTC_WINDOWS_64 ${CMAKE_CXX_FLAGS} ")
    endif()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi /MP")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}  /MP /bigobj")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
	set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /DEBUG /OPT:REF /OPT:ICF /bigobj")
    set(CMAKE_C_FLAGS                "-Wall ${CMAKE_C_FLAGS}")

    if(WITH_ASAN AND ${MSVC_VERSION} GREATER 1919)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address ")
    endif()

elseif(APPLE)
    set(HRTC_APPLE ON)
    set(CMAKE_CXX_FLAGS "-std=c++14 -g ${CMAKE_CXX_FLAGS}")
    if(IOS)
        set(HRTC_IOS ON)
        set(CMAKE_CXX_FLAGS " -DHRTC_IOS -DHRTC_APPLE ${CMAKE_CXX_FLAGS} ")
    else()
        set(MAXME_MACOS ON)
        set(CMAKE_CXX_FLAGS " -DHRTC_MACOS -DHRTC_APPLE ${CMAKE_CXX_FLAGS} ")
    endif()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG=3")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG=4")
    set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
    set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
    set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_WEAK YES)
elseif(ANDROID)
    set(HRTC_ANDROID ON)
	string(REPLACE "-fno-rtti" "" FIXED_C_FLAGS ${CMAKE_C_FLAGS})
	string(REPLACE "-fno-exceptions" "" FIXED_C_FLAGS ${FIXED_C_FLAGS})
	if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7-a")
		string(REPLACE "-mfpu=vfpv3-d16" "" FIXED_C_FLAGS ${FIXED_C_FLAGS})
	endif()
	string(REPLACE "-fno-rtti" "" FIXED_CXX_FLAGS ${CMAKE_CXX_FLAGS})
	string(REPLACE "-fno-exceptions" "" FIXED_CXX_FLAGS ${FIXED_CXX_FLAGS})
	if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7-a")
		string(REPLACE "-mfpu=vfpv3-d16" "" FIXED_CXX_FLAGS ${FIXED_CXX_FLAGS})
	endif()

    set(CMAKE_C_FLAGS "${FIXED_C_FLAGS} -D__ANDROID_API__=21 -DMAXME_ANDROID ")
    set(CMAKE_CXX_FLAGS "${FIXED_CXX_FLAGS} -std=c++14  -D__ANDROID_API__=21 -DMAXME_ANDROID ")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-Bsymbolic ")

elseif(HRTC_LINUX)
    set(CMAKE_C_FLAGS                "-Wall ${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS_DEBUG          "-g")
    set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
    set(CMAKE_C_FLAGS_RELEASE        "-O4 -DNDEBUG")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")
    set(CMAKE_CXX_FLAGS                "-Wall -DHRTC_LINUX ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_DEBUG          "-g")
    set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE        "-O4 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
endif()


if(APPLE OR ANDROID)
    set(IGNORE_WARNINGS "-Wno-unused-private-field -Wno-unknown-warning-option -Wno-unused-but-set-variable -Wno-deprecated-declarations")
    set(IGNORE_WARNINGS "${IGNORE_WARNINGS} -Wno-unused-function -Wno-unused-value -Wno-unused-argument -Wno-unused-variable -Wno-macro-redefined -Wno-defaulted-function-deleted ")
    set(IGNORE_WARNINGS "${IGNORE_WARNINGS} -Wno-unused-lambda-capture -Wno-implicit-const-int-float-conversion -Wno-gnu-zero-variadic-macro-arguments")
                          
    #todo(hrf): 修复音频模块中的精度丢失问题，然后移除-Wno-shorten-64-to-32 
    #todo(ojj): LOG_SEVERITY_PRECONDITION 有隐患--> -Wbitwise-conditional-parentheses
    set(IGNORE_WARNINGS " ${IGNORE_WARNINGS} -Wno-shorten-64-to-32 -Wno-bitwise-conditional-parentheses")

    set(CMAKE_CXX_FLAGS " -Wall ${IGNORE_WARNINGS} -pedantic -Werror ${CMAKE_CXX_FLAGS} ")

    if(WITH_ASAN)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
		set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
	endif(WITH_ASAN)	
endif()

string(REPLACE "-std=c++11" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})