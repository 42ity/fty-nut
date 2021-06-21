#pragma once
#include <set>
#include <map>
#include <functional>
#include <string>
#include <fty_common_dto.h>

namespace ftynut {

/**
 * Function helpers.
 *
 * These isolate the side-effects of power command requests so that the
 * power command compute mechanism can be tested without a fully-blown
 * 42ity setup. In short, these interfaces are mockable.
 */
using DeviceCommandRequester = std::function<std::set<std::string>(const std::string&)>;
using DaisyChainRequester    = std::function<std::map<int, std::string>(const std::string&)>;
using TopologyRequester      = std::function<std::vector<std::pair<std::string, int>>(const std::string&)>;

dto::commands::CommandDescriptions queryNativePowerCommands(
    DeviceCommandRequester deviceCommandRequester, DaisyChainRequester daisyChainRequester, const std::string& asset);

dto::commands::Commands computePowerCommands(
    DaisyChainRequester daisyChainRequester, TopologyRequester topologyRequester, const dto::commands::Commands& jobs);

} // namespace ftynut


