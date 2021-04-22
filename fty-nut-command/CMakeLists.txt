cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

#Create the target
etn_target(exe fty-nut-command
    SOURCES
        src/*.cc
    USES
        czmq
        fty_common_logging
        fty_common_mlm
        fty_common_db
    USES_PRIVATE
        ${PROJECT_NAME}-lib
)
