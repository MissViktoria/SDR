# cmake_minimum_required(VERSION 3.16)
# project(PlutoSDR C)
# set(CMAKE_BUILD_TYPE Debug)
# find_package(SoapySDR REQUIRED)
# add_executable(main main2.c)
# target_link_libraries(main ${SoapySDR_LIBRARIES})

cmake_minimum_required(VERSION 3.10)
project(sdr C)

set(CMAKE_C_STANDARD 11)

find_library(SOAPY_SDR_LIB SoapySDR REQUIRED)
find_path(SOAPY_SDR_INCLUDE SoapySDR/Device.h REQUIRED)

# translating
add_executable(test2 test2.c)
target_include_directories(test2 PRIVATE ${SOAPY_SDR_INCLUDE})
target_link_libraries(test2 PRIVATE ${SOAPY_SDR_LIB} m)

# recieving
add_executable(rx1 rx1.c)
target_include_directories(rx1 PRIVATE ${SOAPY_SDR_INCLUDE})
target_link_libraries(rx1 PRIVATE ${SOAPY_SDR_LIB})
