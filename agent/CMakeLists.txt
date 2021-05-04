cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

#Create the target
etn_target(exe ${PROJECT_NAME}
    SOURCES
        src/*.cc
    USES
        czmq
        fty_common_logging
        fty_common_mlm
    USES_PRIVATE
        ${PROJECT_NAME}-lib
)
