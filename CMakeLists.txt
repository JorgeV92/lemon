cmake_minimum_required(VERSION 3.23)

project(
  lemon
  VERSION 0.1.0
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Boost REQUIRED COMPONENTS json)
find_package(OpenSSL REQUIRED)

add_library(
  lemon
  STATIC
    src/lemon/entry.cpp
    src/lemon/level.cpp
    src/lemon/orderBook.cpp
)
add_library(lemon::lemon ALIAS lemon)

target_compile_features(lemon PUBLIC cxx_std_20)

target_include_directories(
  lemon
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  lemon
  PUBLIC
    Boost::headers
    Boost::json
    OpenSSL::Crypto
)

include(CTest)

if(BUILD_TESTING)
  add_executable(
    lemon_price_level_snapshot_test
    tests/price_level_snapshot_test.cpp
  )

  target_link_libraries(
    lemon_price_level_snapshot_test
    PRIVATE
      lemon::lemon
  )

  add_test(
    NAME price_level_snapshot
    COMMAND lemon_price_level_snapshot_test
  )

  add_executable(
    lemon_order_queue_test
    tests/order_queue_test.cpp
  )

  target_link_libraries(
    lemon_order_queue_test
    PRIVATE
      lemon::lemon
  )

  add_test(
    NAME order_queue
    COMMAND lemon_order_queue_test
  )
endif()

add_executable(
  lemon_price_level_snapshot_example
  examples/price_level_snapshot.cpp
)

target_link_libraries(
  lemon_price_level_snapshot_example
  PRIVATE
    lemon::lemon
)

install(
  TARGETS lemon
  EXPORT lemonTargets
)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  PATTERN ".DS_Store" EXCLUDE
)

install(
  EXPORT lemonTargets
  FILE lemonTargets.cmake
  NAMESPACE lemon::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lemon
)

configure_package_config_file(
  cmake/lemonConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/lemonConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lemon
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/lemonConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lemonConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/lemonConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lemon
)
