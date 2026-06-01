cmake_minimum_required(VERSION 3.23)

project(
  lemon
  VERSION 0.1.0
  LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Boost REQUIRED)

add_library(lemon INTERFACE)
add_library(lemon::lemon ALIAS lemon)

target_compile_features(lemon INTERFACE cxx_std_20)

target_include_directories(
  lemon
  INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(
  lemon
  INTERFACE
    Boost::headers
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
