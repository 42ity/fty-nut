/*
    fty-nut - NUT (Network UPS Tools) daemon wrapper/proxy

    Copyright (C) 2017 - 2020 Eaton.

    This software is confidential and licensed under Eaton Proprietary License
    (EPL or EULA).
    This software is not authorized to be used, duplicated or disclosed to
    anyone without the prior written permission of Eaton.
    Limitations, restrictions and exclusions of the Eaton applicable standard
    terms and conditions, such as its EPL and EULA, apply.

    NOTE : This Jenkins pipeline script only handles the self-testing of your
    project. If you also want the successful codebase published or deployed,
    you can define a helper job - see the reference implementation skeleton at
    https://github.com/zeromq/zproject/blob/master/Jenkinsfile-deploy.example

*/

@Library('etn-ipm2-jenkins') _

import params.CmakePipelineParams
CmakePipelineParams parameters = new CmakePipelineParams()
parameters.debugBuildRunTests = false
parameters.debugBuildRunMemcheck = false
etn_ipm2_build_and_tests_pipeline_cmake(parameters)
