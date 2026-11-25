cmake_minimum_required(VERSION 3.20)

project(aquarium-client CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

include(FetchContent)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ftxui)

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(GTest QUIET)
if(NOT GTest_FOUND)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(googletest)
endif()

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_FILES
    "${PROTO_DIR}/TaskStoreService.proto" "${PROTO_DIR}/UserStoreService.proto"
    "${PROTO_DIR}/UtilsService.proto")

set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

if(TARGET protobuf::protoc)
  set(PROTOC_BIN protobuf::protoc)
else()
  set(PROTOC_BIN ${Protobuf_PROTOC_EXECUTABLE})
endif()

if(TARGET gRPC::grpc_cpp_plugin)
  set(GRPC_CPP_PLUGIN $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
else()
  find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
endif()

set(PROTO_SRCS)
set(PROTO_HDRS)

foreach(PROTO_FILE ${PROTO_FILES})
  get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
  set(PB_CC "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.cc")
  set(PB_H "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.h")
  set(GRPC_PB_CC "${PROTO_GEN_DIR}/${PROTO_NAME}.grpc.pb.cc")
  set(GRPC_PB_H "${PROTO_GEN_DIR}/${PROTO_NAME}.grpc.pb.h")

  list(APPEND PROTO_SRCS "${PB_CC}" "${GRPC_PB_CC}")
  list(APPEND PROTO_HDRS "${PB_H}" "${GRPC_PB_H}")

  add_custom_command(
    OUTPUT "${PB_CC}" "${PB_H}" "${GRPC_PB_CC}" "${GRPC_PB_H}"
    COMMAND
      ${PROTOC_BIN} ARGS --proto_path "${PROTO_DIR}" --cpp_out
      "${PROTO_GEN_DIR}" --grpc_out "${PROTO_GEN_DIR}"
      --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN} "${PROTO_FILE}"
    DEPENDS "${PROTO_FILE}"
    COMMENT "Generating C++ and gRPC sources for ${PROTO_NAME}.proto"
    VERBATIM)
endforeach()

add_library(aquarium-client-proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(aquarium-client-proto SYSTEM
                           PUBLIC "${PROTO_GEN_DIR}")
if(NOT MSVC)
  target_compile_options(aquarium-client-proto PRIVATE -Wno-pedantic
                                                       -Wno-unused-parameter)
endif()
target_link_libraries(aquarium-client-proto PUBLIC protobuf::libprotobuf
                                                   gRPC::grpc++)

add_library(
  aquarium-client-core STATIC
  src/Config/ConfigManager.cpp
  src/Grpc/BackendClient.cpp
  src/Grpc/HealthChecker.cpp
  src/Grpc/EventSubscriber.cpp
  src/State/Reducer.cpp
  src/UI/Components/StatusBar.cpp
  src/UI/Components/TaskDetailsPane.cpp
  src/UI/Components/RemarksPane.cpp
  src/UI/NavigationSidebar.cpp
  src/UI/Views/TaskBoardView.cpp
  src/UI/Views/ReviewView.cpp
  src/UI/Views/CreateTaskView.cpp
  src/UI/Views/AuthModal.cpp
  src/UI/Views/ProjectModal.cpp
  src/UI/MainScreen.cpp)

target_include_directories(aquarium-client-core
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(
  aquarium-client-core
  PUBLIC aquarium-client-proto
         ftxui::screen
         ftxui::dom
         ftxui::component
         nlohmann_json::nlohmann_json
         gRPC::grpc++
         protobuf::libprotobuf)

add_executable(aquarium-client src/main.cpp)

target_include_directories(aquarium-client
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(aquarium-client PRIVATE aquarium-client-core)

enable_testing()

add_executable(
  aquarium-client-unittest tests/AppStateTest.cpp tests/ConfigTest.cpp
                           tests/GrpcMockTest.cpp tests/UiComponentsTest.cpp)

target_include_directories(aquarium-client-unittest
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(
  aquarium-client-unittest PRIVATE aquarium-client-core GTest::gtest
                                   GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(aquarium-client-unittest)
