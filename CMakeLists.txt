cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

##############################################################################################################

project(fty-nut VERSION 1.0.0 LANGUAGES CXX)

########################################################################################################################

find_package(fty-cmake PATHS ${CMAKE_BINARY_DIR})

########################################################################################################################

add_subdirectory(lib)
add_subdirectory(agent)
add_subdirectory(fty-nut-command)
add_subdirectory(fty-nut-configurator)

## agent configuration
## https://cmake.org/cmake/help/v3.0/module/GNUInstallDirs.html

# user for all services
set(AGENT_USER discovery-monitoring-daemon)
# mapping.conf installed in usr/share, coming from fty-common-nut
set(AGENT_MAPPING_FILE ${CMAKE_INSTALL_FULL_DATAROOTDIR}/fty-common-nut/mapping.conf)

set(res "resources")

## fty_10_agent-nut -> etc/sudoers.d
install(FILES "${PROJECT_SOURCE_DIR}/${res}/fty_10_agent-nut" DESTINATION "${CMAKE_INSTALL_FULL_SYSCONFDIR}/sudoers.d/")

## (fty-nutconfig, fty-dmf) -> usr/bin
install(FILES "${PROJECT_SOURCE_DIR}/${res}/fty-nutconfig" DESTINATION "${CMAKE_INSTALL_FULL_BINDIR}")
install(FILES "${PROJECT_SOURCE_DIR}/${res}/fty-dmf" DESTINATION "${CMAKE_INSTALL_FULL_BINDIR}")

## .conf file -> lib/tmpfiles.d/
configure_file("${PROJECT_SOURCE_DIR}/${res}/${PROJECT_NAME}.conf.in" "${PROJECT_BINARY_DIR}/${res}/${PROJECT_NAME}.conf" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/${res}/${PROJECT_NAME}.conf" DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/tmpfiles.d/")

## fty-nut (cfg, service)
set(agent "fty-nut")
configure_file("${PROJECT_SOURCE_DIR}/${res}/${agent}.cfg.in" "${PROJECT_BINARY_DIR}/${res}/${agent}.cfg" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/${res}/${agent}.cfg" DESTINATION "${CMAKE_INSTALL_FULL_SYSCONFDIR}/${PROJECT_NAME}")
configure_file("${PROJECT_SOURCE_DIR}/${res}/${agent}.service.in" "${PROJECT_BINARY_DIR}/${res}/${agent}.service" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/${res}/${agent}.service" DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/systemd/system")

## fty-nut-command (cfg, service)
set(agent "fty-nut-command")
configure_file("${PROJECT_SOURCE_DIR}/${res}/${agent}.cfg.in" "${PROJECT_BINARY_DIR}/${res}/${agent}.cfg" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/${res}/${agent}.cfg" DESTINATION "${CMAKE_INSTALL_FULL_SYSCONFDIR}/${PROJECT_NAME}")
configure_file("${PROJECT_SOURCE_DIR}/${res}/${agent}.service.in" "${PROJECT_BINARY_DIR}/${res}/${agent}.service" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/${res}/${agent}.service" DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/systemd/system")

## fty-nut-configurator (cfg, service)
set(agent "fty-nut-configurator")
configure_file("${PROJECT_SOURCE_DIR}/${res}/${agent}.cfg.in" "${PROJECT_BINARY_DIR}/${res}/${agent}.cfg" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/${res}/${agent}.cfg" DESTINATION "${CMAKE_INSTALL_FULL_SYSCONFDIR}/${PROJECT_NAME}")
configure_file("${PROJECT_SOURCE_DIR}/${res}/${agent}.service.in" "${PROJECT_BINARY_DIR}/${res}/${agent}.service" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/${res}/${agent}.service" DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/systemd/system")
