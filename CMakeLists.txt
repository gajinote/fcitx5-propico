cmake_minimum_required(VERSION 3.16)
project(fcitx5-propico VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ECM REQUIRED)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

find_package(Fcitx5Core REQUIRED)
include(GNUInstallDirs)

add_fcitx5_addon(fcitx5-propico
  src/propico_engine.cc
  src/romaji_kana.cc
  src/factory.cc
)

target_include_directories(fcitx5-propico PRIVATE src)
target_link_libraries(fcitx5-propico PRIVATE Fcitx5::Core)

install(TARGETS fcitx5-propico
  DESTINATION "${FCITX_INSTALL_ADDONDIR}")
install(FILES data/propico-addon.conf
  DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
install(FILES data/propico.conf
  DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod")
