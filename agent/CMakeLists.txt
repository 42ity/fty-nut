cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

########################################################################################################################

#Create the target
etn_target(exe ${PROJECT_NAME}
    SOURCES
        src/*.cc
    USES
        curl
        xml2
        czmq
        fty_common_logging
    USES_PRIVATE
        ${PROJECT_NAME}-lib
)

# set path to xml2 headers
#target_include_directories(${PROJECT_NAME} PRIVATE AFTER "/usr/include/libxml2")
