#include <catch2/catch.hpp>
#include <malamute.h>
#include <fty_proto.h>
#include "src/asset_state.h"
#include "src/state_manager.h"
#include "src/alert_device_list.h"
#include "rule_actor.h"
#include <nutclientmem.h>
#include <fty_shm.h>

TEST_CASE("alert actor test")
{
    static const char* endpoint = "inproc://fty-alert-actor-test.7afde8";
    const char* SELFTEST_DIR_RW = ".";

    const std::string ruleName = "input.L1.current@mydevice";
    std::map<std::string, std::string> rulesMap;

    fty_shm_set_test_dir(SELFTEST_DIR_RW);
    fty_shm_set_default_polling_interval(2);

    nut::MemClientStub nutClient;

    // malamute broker
    zactor_t* malamute = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    REQUIRE(malamute);
    zstr_sendx(malamute, "BIND", endpoint, NULL);

    mlm_client_t* client = mlm_client_new();
    REQUIRE(client);
    mlm_client_connect(client, endpoint, 1000, "agent-nut-alert");
    mlm_client_set_producer(client, FTY_PROTO_STREAM_ALERTS_SYS);

    // run the rule actor
    zactor_t* ruleActor{nullptr};
    {
        ruleActor = zactor_new(RuleActor, &rulesMap);
        REQUIRE(ruleActor);
        zstr_sendx(ruleActor, "CONNECT", endpoint, "fty-alert-engine", nullptr);
        zclock_sleep(500); // sync
    }

    auto checkThresolds = [&](const char *high_critical, const char *high_warning,
        const char *low_warning, const char *low_critical) -> void {
        REQUIRE(rulesMap.size() == 1);
        auto it = rulesMap.find(ruleName);
        REQUIRE(it != rulesMap.end());
        cxxtools::SerializationInfo alertSi;
        JSON::readFromString(it->second, alertSi);
        auto thresoldValues = alertSi.getMember(0).getMember("values");
        REQUIRE(thresoldValues.category() == cxxtools::SerializationInfo::Array);
        for (const auto& thresoldValue : thresoldValues) {
            REQUIRE(thresoldValue.category() == cxxtools::SerializationInfo::Object);
            std::string value;
            auto name = thresoldValue.name();
            if (name == "high_critical") {
                thresoldValue.getMember("value").getValue(value);
                REQUIRE(streq(value.c_str(), high_critical));
            }
            else if (name == "high_warning") {
                thresoldValue.getMember("value").getValue(value);
                REQUIRE(streq(value.c_str(), high_warning));
            }
            else if(name == "low_warning") {
                thresoldValue.getMember("value").getValue(value);
                REQUIRE(streq(value.c_str(), low_warning));
            }
            else if(name == "low_critical") {
                thresoldValue.getMember("value").getValue(value);
                REQUIRE(streq(value.c_str(), low_critical));
            }
        }
    };

    auto setDeviceValue = [&nutClient](const std::string& device, const std::string& name, const std::string& value) -> void
    {
        nutClient.setDeviceVariable(device, name, value);
        std::vector<std::string> list_value = nutClient.getDeviceVariableValue(device, name);
        CHECK (list_value.size() == 1);
        CHECK (list_value[0] == value);
    };

    // Create asset
    fty_proto_t* msg = fty_proto_new(FTY_PROTO_ASSET);
    REQUIRE(msg);
    fty_proto_set_name(msg, "mydevice");
    fty_proto_set_operation(msg, FTY_PROTO_ASSET_OP_CREATE);
    fty_proto_aux_insert(msg, "type", "device");
    fty_proto_aux_insert(msg, "subtype", "ups");
    fty_proto_ext_insert(msg, "ip.1", "192.0.2.1");
    std::shared_ptr<AssetState::Asset> asset(new AssetState::Asset(msg));
    fty_proto_destroy(&msg);

	// Set values on asset
    setDeviceValue("mydevice", "input.L1.current.status", "critical-high");
    setDeviceValue("mydevice", "input.L1.current", "120");
    setDeviceValue("mydevice", "input.L1.current.high.critical", "100");
    setDeviceValue("mydevice", "input.L1.current.high.warning", "80");
    setDeviceValue("mydevice", "input.L1.current.low.warning", "10");
    setDeviceValue("mydevice", "input.L1.current.low.critical", "5");

    Device dev(asset);
    StateManager manager;
    Devices devs(manager.getReader());
    devs.devices()["mydevice"] = dev;

    // Update devices
    devs.updateDeviceCapabilities(nutClient);
    devs.updateDevices(nutClient);

    // wait calculation
    sleep(1);
    {
        fty_proto_t* m;
        fty::shm::shmMetrics resultT;
        fty::shm::read_metrics("mydevice", ".*", resultT);
        REQUIRE(resultT.size() == 1);
        m = resultT.get(0);
        REQUIRE(m);
        fty_proto_print(m);
        CHECK(streq(fty_proto_value(m), "120"));
        CHECK(streq(fty_proto_type(m), "input.L1.current"));
        CHECK(fty_proto_ttl(m) == 60);
        m = nullptr;
    }

    // Add new rule
    devs.publishRules(client);

    // Check initial rule receive
    checkThresolds("100", "80", "10", "5");

    // Change value of device thresholds
    setDeviceValue("mydevice", "input.L1.current.high.critical", "140");
    setDeviceValue("mydevice", "input.L1.current.high.warning", "90");
    setDeviceValue("mydevice", "input.L1.current.low.warning", "20");
    setDeviceValue("mydevice", "input.L1.current.low.critical", "10");

    devs.updateDevices(nutClient);
    devs.updateDeviceCapabilities(nutClient);

    // Update rules
    devs.publishRules(client);

    // Check new rule receive
    checkThresolds("140", "90", "20", "10");

    zactor_destroy(&ruleActor);
    mlm_client_destroy(&client);
    zactor_destroy(&malamute);
}
