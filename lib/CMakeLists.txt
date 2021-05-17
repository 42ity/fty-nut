cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

etn_target(static ${PROJECT_NAME}-lib
    SOURCES
        src/actor_commands.cc
        src/actor_commands.h
        src/alert_actor.cc
        src/alert_device.cc
        src/alert_device.h
        src/alert_device_list.cc
        src/alert_device_list.h
        src/asset_state.cc
        src/asset_state.h
        src/cidr.cc
        src/cidr.h
        src/fty_nut_command_server.cc
        src/fty_nut_command_server_helper.h
        src/fty_nut_configurator_server.cc
        src/fty_nut_server.cc
        src/nut_agent.cc
        src/nut_agent.h
        src/nut_configurator.cc
        src/nut_configurator.h
        src/nut_device.cc
        src/nut_device.h
        src/nut_mlm.h
        src/sensor_actor.cc
        src/sensor_device.cc
        src/sensor_device.h
        src/sensor_list.cc
        src/sensor_list.h
        src/state_manager.cc
        src/state_manager.h
        src/ups_status.cc
        src/ups_status.h
    PUBLIC_INCLUDE_DIR
        include
    PUBLIC
        alert_actor.h
        fty_nut_command_server.h
        fty_nut_configurator_server.h
        fty_nut.h
        fty_nut_server.h
        sensor_actor.h
    USES_PRIVATE
        cidr
        nutclient
        czmq
        mlm
        cxxtools
        tntdb
        fty_proto
        fty_shm
        fty-utils
        fty_security_wallet
        fty_common
        fty_common_logging
        fty_common_db
        fty_common_mlm
        fty_common_socket
        fty_common_nut
        fty_common_messagebus
        fty_common_dto
        fty-asset-accessor # ZZZ
    PRIVATE
)

########################################################################################################################

etn_test_target(${PROJECT_NAME}-lib
    CONFIGS
        tests/selftest-ro/mapping.conf
        SOURCES
        tests/actor_commands.cpp
        tests/alert_actor.cpp
        tests/alert_device.cpp
        tests/main.cpp
        tests/nut_command_server.cpp
        tests/nut_configurator_server.cpp
        tests/nut_device.cpp
        tests/sensors.cpp
        tests/sensor_actor.cpp
        tests/sensor_device.cpp
        tests/state_manager.cpp
        tests/ups_status.cpp
        INCLUDE_DIR
            include
        PREPROCESSOR -DCATCH_CONFIG_FAST_COMPILE
        USES
            stdc++fs
        pthread
)

########################################################################################################################
