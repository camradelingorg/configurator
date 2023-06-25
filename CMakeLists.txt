cmake_minimum_required(VERSION 3.6)
project(configurator)
option(DEBUG_BUILD "Option description" OFF)
set(CMAKE_CXX_STANDARD 17)
set(BIN_DIR ${CMAKE_BINARY_DIR}/Binaries)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BIN_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BIN_DIR})
set(PROJECT_NAME configurator)
link_directories(${BIN_DIR})
message(STATUS "CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")
if(DEBUG_BUILD)
    message(STATUS "DEBUG_BUILD = ON")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fno-omit-frame-pointer -fasynchronous-unwind-tables")
else()
    message(STATUS "DEBUG_BUILD = OFF")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Os -s")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s")
    set(CMAKE_INSTALL_RPATH "/")
endif()

set(Sources
        main.cpp
        modbuslib/modbus_client.cpp
        modbuslib/modbus_crc_tables.c
        mpoller.cpp
        fupdater.cpp
        )

set(PollerSources
        poller.cpp
	modbuslib/modbus_client.cpp
        modbuslib/modbus_crc_tables.c
        mpoller.cpp
		) 

set(Configurator_test_Sources
        configurator_test.cpp
        parameters_map.cpp
        )
set(ModbusConfigurator_Sources
        modbuslib/modbus_client.cpp
        modbuslib/modbus_crc_tables.c
        mpoller.cpp
        configurator.cpp
        )


message(STATUS "CMAKE_SOURCE_DIR = ${CMAKE_SOURCE_DIR}")
message("path to modbus_config.h ${MODBUS_CONFIG_PATH}")
include_directories(channellib)
include_directories(threadlib)
include_directories(modbuslib)
include_directories(${MODBUS_CONFIG_PATH})

add_subdirectory(channellib)
add_subdirectory(threadlib)
add_custom_target(static_libs)
add_dependencies(static_libs ProgramThread_static ChannelLib_static)

add_library(ModbusConfigurator SHARED ${ModbusConfigurator_Sources})
add_library(ModbusConfigurator_static STATIC ${ModbusConfigurator_Sources})

target_link_libraries(ModbusConfigurator ChannelLib )
target_link_libraries(ModbusConfigurator ProgramThread )
target_link_libraries(ModbusConfigurator pthread )
target_link_libraries(ModbusConfigurator mxml)

target_link_libraries(ModbusConfigurator_static libChannelLib_static.a )
target_link_libraries(ModbusConfigurator_static libProgramThread_static.a )
target_link_libraries(ModbusConfigurator_static pthread )
target_link_libraries(ModbusConfigurator_static mxml)
add_dependencies(ModbusConfigurator static_libs)
add_dependencies(ModbusConfigurator_static static_libs)

add_custom_target(full_static_libs)
add_dependencies(full_static_libs static_libs ModbusConfigurator_static)

add_executable(configurator_test ${Configurator_test_Sources})
target_link_libraries(configurator_test libModbusConfigurator_static.a)
target_link_libraries(configurator_test libChannelLib_static.a )
target_link_libraries(configurator_test libProgramThread_static.a )
target_link_libraries(configurator_test pthread )
target_link_libraries(configurator_test mxml)
add_dependencies(configurator_test full_static_libs)

#add_executable(poller ${PollerSources})
#target_link_libraries(poller ChannelLib )
#target_link_libraries(poller ProgramThread )
#target_link_libraries(poller pthread)