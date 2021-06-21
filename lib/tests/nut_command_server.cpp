#include "src/fty_nut_command_server_helper.h"
#include <catch2/catch.hpp>


TEST_CASE("nut command server test")
{
    /**
     * Test setup:
     *  - epdu-1 : standalone EPDU with two outlets.
     *  - epdu-2 : daisy-chain host EPDU with three outlets.
     *  - epdu-3 : daisy-chain device 1 EPDU with three outlets.
     *  - server-4: server with one power source connected to epdu-1.
     *  - server-5: server with two power sources connected to epdu-2 and epdu-3.
     *
     * Tests consist of calling the functions with a set of input data and
     * checking that the results are as expected.
     */

    /**
     * Callables defining our mock data-center without having to instanciate
     * a full 42ity environment.
     */
    auto generateEpduCommands = [](int outlets, int devices) -> std::set<std::string> {
        const static std::vector<std::string> commandList = {
            ".load.cycle", ".load.cycle.delay", ".load.off", ".load.off.delay", ".load.on", ".load.on.delay"};

        std::set<std::string> commands;
        for (int i = 1; i <= devices; i++) {
            const std::string prefix = (devices == 1) ? "" : "device." + std::to_string(i) + ".";

            for (int j = 1; j <= outlets; j++) {
                for (const auto& command : commandList) {
                    commands.insert(prefix + "outlet." + std::to_string(j) + command);
                }
            }
        }
        return commands;
    };

    ftynut::DeviceCommandRequester deviceCommandRequester = [&generateEpduCommands](
                                                                const std::string& asset) -> std::set<std::string> {
        const std::map<std::string, std::set<std::string>> assetCommands = {{"epdu-1", generateEpduCommands(2, 1)},
            {"epdu-2", generateEpduCommands(3, 2)}, {"epdu-3", {}}, {"server-4", {}}, {"server-5", {}}};

        return assetCommands.at(asset);
    };

    ftynut::DaisyChainRequester daisyChainRequester = [](const std::string& asset) -> std::map<int, std::string> {
        const static std::map<int, std::string> doubleDaisyChain = {{1, "epdu-2"}, {2, "epdu-3"}};

        const static std::map<std::string, std::map<int, std::string>> daisyChains = {{"epdu-1", {}},
            {"epdu-2", doubleDaisyChain}, {"epdu-3", doubleDaisyChain}, {"server-4", {}}, {"server-5", {}}};

        return daisyChains.at(asset);
    };

    ftynut::TopologyRequester topologyRequester =
        [](const std::string& asset) -> std::vector<std::pair<std::string, int>> {
        const static std::map<std::string, std::vector<std::pair<std::string, int>>> topologies = {{"epdu-1", {}},
            {"epdu-2", {}}, {"epdu-3", {}}, {"server-4", {{"epdu-1", 2}}},
            {"server-5", {{"epdu-2", 3}, {"epdu-3", 1}}}};

        return topologies.at(asset);
    };

    {
        const static std::map<std::string, dto::commands::CommandDescriptions> expectedAssetCommands = {
            {"epdu-1",
                {dto::commands::CommandDescription({"epdu-1", "load.cycle", "", {"outlet.1", "outlet.2"}}),
                    dto::commands::CommandDescription({"epdu-1", "load.cycle.delay", "", {"outlet.1", "outlet.2"}}),
                    dto::commands::CommandDescription({"epdu-1", "load.off", "", {"outlet.1", "outlet.2"}}),
                    dto::commands::CommandDescription({"epdu-1", "load.off.delay", "", {"outlet.1", "outlet.2"}}),
                    dto::commands::CommandDescription({"epdu-1", "load.on", "", {"outlet.1", "outlet.2"}}),
                    dto::commands::CommandDescription({"epdu-1", "load.on.delay", "", {"outlet.1", "outlet.2"}})}},
            {"epdu-2",
                {dto::commands::CommandDescription({"epdu-2", "load.cycle", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription(
                        {"epdu-2", "load.cycle.delay", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription({"epdu-2", "load.off", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription(
                        {"epdu-2", "load.off.delay", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription({"epdu-2", "load.on", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription(
                        {"epdu-2", "load.on.delay", "", {"outlet.1", "outlet.2", "outlet.3"}})}},
            {"epdu-3",
                {dto::commands::CommandDescription({"epdu-3", "load.cycle", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription(
                        {"epdu-3", "load.cycle.delay", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription({"epdu-3", "load.off", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription(
                        {"epdu-3", "load.off.delay", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription({"epdu-3", "load.on", "", {"outlet.1", "outlet.2", "outlet.3"}}),
                    dto::commands::CommandDescription(
                        {"epdu-3", "load.on.delay", "", {"outlet.1", "outlet.2", "outlet.3"}})}}};

        for (const auto& expectedAssetCommand : expectedAssetCommands) {
            const auto& assetName     = expectedAssetCommand.first;
            const auto& assetCommands = expectedAssetCommand.second;

            auto commandDescriptions =
                ftynut::queryNativePowerCommands(deviceCommandRequester, daisyChainRequester, assetName);

            CHECK(assetCommands.size() == commandDescriptions.size());
            for (size_t i = 0; i < assetCommands.size(); i++) {
                CHECK(assetCommands[i].asset == commandDescriptions[i].asset);
                CHECK(assetCommands[i].command == commandDescriptions[i].command);

                CHECK(assetCommands[i].targets.size() == commandDescriptions[i].targets.size());
                for (size_t j = 0; j < assetCommands[i].targets.size(); j++) {
                    CHECK(assetCommands[i].targets[j] == commandDescriptions[i].targets[j]);
                }
            }
        }
    }

    {
        const static dto::commands::Commands commands = {dto::commands::Command({"epdu-1", "load.off", "outlet.1", ""}),
            dto::commands::Command({"epdu-1", "load.on.delay", "outlet.2", "3"}),
            dto::commands::Command({"epdu-2", "load.cycle", "outlet.1", ""}),
            dto::commands::Command({"epdu-3", "load.cycle", "outlet.3", ""}),
            dto::commands::Command({"server-4", "powersource.off", "", ""}),
            dto::commands::Command({"server-5", "powersource.cycle", "", ""}),
            dto::commands::Command({"server-5", "powersource.off.delay", "", "3"}),
            dto::commands::Command({"server-5", "powersource.on.stagger", "", "3"})};

        const static std::vector<dto::commands::Commands> expectedResults = {
            {dto::commands::Command({"epdu-1", "load.off", "outlet.1", ""})},
            {dto::commands::Command({"epdu-1", "load.on.delay", "outlet.2", "3"})},
            {dto::commands::Command({"epdu-2", "load.cycle", "device.1.outlet.1", ""})},
            {dto::commands::Command({"epdu-2", "load.cycle", "device.2.outlet.3", ""})},
            {dto::commands::Command({"epdu-1", "load.off", "outlet.2", ""})},
            {dto::commands::Command({"epdu-2", "load.cycle", "device.1.outlet.3", ""}),
                dto::commands::Command({"epdu-2", "load.cycle", "device.2.outlet.1", ""})},
            {dto::commands::Command({"epdu-2", "load.off.delay", "device.1.outlet.3", "3"}),
                dto::commands::Command({"epdu-2", "load.off.delay", "device.2.outlet.1", "3"})},
            {dto::commands::Command({"epdu-2", "load.on.delay", "device.1.outlet.3", "3"}),
                dto::commands::Command({"epdu-2", "load.on.delay", "device.2.outlet.1", "6"})}};

        CHECK(commands.size() == expectedResults.size());

        for (size_t i = 0; i < commands.size(); i++) {
            auto result = ftynut::computePowerCommands(daisyChainRequester, topologyRequester, {commands[i]});

            CHECK(result.size() == expectedResults[i].size());
            for (size_t j = 0; j < expectedResults[i].size(); j++) {
                CHECK(result[j].asset == expectedResults[i][j].asset);
                CHECK(result[j].command == expectedResults[i][j].command);
                CHECK(result[j].target == expectedResults[i][j].target);
                CHECK(result[j].argument == expectedResults[i][j].argument);
            }
        }
    }
}
