cmake_minimum_required(VERSION 3.10...999.999)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_ENABLE_EXPORTS TRUE)

project(prheart)
add_definitions(-std=c++20)
# add_compile_options(-g)

find_package(Boost REQUIRED COMPONENTS context coroutine)

include_directories(./include)
include_directories(./src-impl)

set(gflags_dir ${CMAKE_SOURCE_DIR}/gflags)
add_subdirectory(${gflags_dir})
include_directories(${gflags_dir}/include)

set(magic_enum_dir ${CMAKE_SOURCE_DIR}/magic_enum)
add_subdirectory(${magic_enum_dir})
include_directories(${magic_enum_dir}/include)


file(GLOB src_file ${CMAKE_SOURCE_DIR}/src/*/*.cc ${CMAKE_SOURCE_DIR}/src/*.cc)

foreach(main_exec monitor compute memory)
    list(REMOVE_ITEM src_file ${CMAKE_SOURCE_DIR}/src/main/${main_exec}.cc)
endforeach()

foreach(main_exec monitor compute memory)

    add_executable(${main_exec} ${CMAKE_SOURCE_DIR}/src/main/${main_exec}.cc ${src_file})
    target_link_libraries(${main_exec} gflags ibverbs Boost::context Boost::coroutine)

    # 幽默，这好像是我之前还没上 RDMA 的时候在 windows 上用的
    if (WIN32 OR MSVC OR MSYS)
        add_custom_command(TARGET ${main_exec} POST_BUILD
            COMMAND (if not exist "\"${CMAKE_SOURCE_DIR}/bin\"" mkdir "\"${CMAKE_SOURCE_DIR}/bin\"")
            COMMAND move ${main_exec}.exe "\"${CMAKE_SOURCE_DIR}/bin/\""
        )
    endif()
    if (UNIX)
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -g")
        add_custom_command(TARGET ${main_exec} POST_BUILD
            COMMAND mkdir -p ${CMAKE_SOURCE_DIR}/bin
            COMMAND mv ${main_exec} ${CMAKE_SOURCE_DIR}/bin/
        )
    endif()

endforeach()
