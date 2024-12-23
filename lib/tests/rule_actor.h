#include <czmq.h>
#include <malamute.h>
#include <iostream>
#include <functional>
#include <fty_common_json.h>

// czmq/mlm simple rule actor
static void RuleActor(zsock_t* pipe, void* args)
{
    // zmsg_t* pop std::string
    std::function<std::string(zmsg_t*)> zmsg_popstdstr = [](zmsg_t* msg)
    {
        char* s = msg ? zmsg_popstr(msg) : nullptr;
        std::string ret{s ? s : ""};
        zstr_free(&s);
        return ret;
    };

    auto rulesMap = static_cast<std::map<std::string, std::string> *>(args);
    if (!rulesMap) {
        std::cout << "Error rule maps null" << std::endl;
        return;
    }

    mlm_client_t* client = mlm_client_new();
    REQUIRE(client);
    zpoller_t* poller = zpoller_new(pipe, mlm_client_msgpipe(client), NULL);
    REQUIRE(poller);

    zsock_signal(pipe, 0);

    std::cout << "== rule actor started" << std::endl;

    while (!zsys_interrupted) {
        void* which = zpoller_wait(poller, 1000);
        if (!which) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                break;
            }
        }
        else if (which == pipe) {
            zmsg_t* msg = zmsg_recv(pipe);
            std::string cmd = zmsg_popstdstr(msg);
            bool term = (cmd == "$TERM");
            if (cmd == "CONNECT") {
                std::string endpoint = zmsg_popstdstr(msg);
                std::string address = zmsg_popstdstr(msg);
                std::cout << "== rule actor " << cmd << " (endpoint: " << endpoint << ", address: " << address << ")" << std::endl;
                int r = mlm_client_connect(client, endpoint.c_str(), 5000, address.c_str());
                REQUIRE(r == 0);
            }
            zmsg_destroy(&msg);
            if (term) {
                break;
            }
        }
        else if (which == mlm_client_msgpipe(client)) {
            zmsg_t* msg = mlm_client_recv(client);
            const char* sender = mlm_client_sender(client);
            const char* subject = mlm_client_subject(client);
            const char* cmd = mlm_client_command(client);
            std::cout << "== rule actor msg: sender: '" << sender << "', subject: '" << subject << "', cmd: '" << cmd << "'" << std::endl;
            if (streq(cmd, "MAILBOX DELIVER")) {
                std::string reqCmd = zmsg_popstdstr(msg);
                std::cout << "== reqCmd: '" << reqCmd << "'" << std::endl;                                
                if (reqCmd == "ADD") {
                    std::string ruleJson = zmsg_popstdstr(msg);
                    zmsg_t* reply = zmsg_new();
                    REQUIRE(reply);
                    if (zmsg_size(msg) == 0) {
                        // Add new rule
                        std::cout << "== rule actor request add rule" << std::endl;
                        std::string ruleName;
                        try {                            
                            cxxtools::SerializationInfo alertSi;                                        
                            JSON::readFromString(ruleJson, alertSi);                            
                            alertSi.getMember(0).getMember("rule_name").getValue(ruleName);                                                    
                        } catch (const std::exception& e) {
                            std::cout << "Error parsing json: " << e.what() << std::endl;                            
                        }
                        if (ruleName.empty()) {
                            zmsg_addstr(reply, "ERROR");
                            zmsg_addstr(reply, "rule name not defined");
                            std::cout << "== rule actor request add rule ERROR: rule name not defined" << std::endl;
                        }
                        if (rulesMap->find(ruleName) == rulesMap->end()) {
                            (*rulesMap)[ruleName] = ruleJson;
                            zmsg_addstr(reply, "OK");
                            std::cout << "== rule actor request add rule OK: " << ruleName << std::endl;
                        }
                        else {
                            zmsg_addstr(reply, "ERROR");
                            zmsg_addstr(reply, "rule already exists");
                            std::cout << "== rule actor request add rule ERROR: rule already exists" << std::endl;
                        }
                    }
                    else {
                        // Update rule                        
                        std::string ruleName = zmsg_popstdstr(msg);
                        std::cout << "== rule actor request update rule: " << ruleName <<  std::endl;
                        if (rulesMap->find(ruleName) != rulesMap->end()) {
                            (*rulesMap)[ruleName] = ruleJson;
                            zmsg_addstr(reply, "OK");                            
                            std::cout << "== actor request update rule OK" << std::endl;
                        }
                        else {
                            zmsg_addstr(reply, "ERROR");
                            zmsg_addstr(reply, "rule not found");
                            std::cout << "== rule actor request update rule ERROR: rule not found" << std::endl;
                        }                        
                    }
                    std::cout << "== rule actor reply" << std::endl;                    
                    int r = mlm_client_sendto(client, sender, subject, NULL, 1000, &reply);
                    zmsg_destroy(&reply);
                    REQUIRE(r == 0);
                }
                else if (reqCmd == "GET") {
                    std::string ruleName = zmsg_popstdstr(msg);
                    std::cout << "== rule actor request '" << reqCmd << "' rule name: " << ruleName << std::endl;                    
                    zmsg_t* reply = zmsg_new();
                    REQUIRE(reply);
                    //std::map<std::string, std::string> rulesMap;
                    if (rulesMap->find(ruleName) != rulesMap->end()) {
                        zmsg_addstr(reply, "OK");
                        zmsg_addstr(reply, (*rulesMap)[ruleName].c_str());
                    }
                    else {
                        zmsg_addstr(reply, "ERROR");
                        zmsg_addstr(reply, "rule not found");
                        std::cout << "== actor request get rule ERROR: rule not found" << std::endl;
                    }                    
                    std::cout << "== rule actor reply" << std::endl;
                    int r = mlm_client_sendto(client, sender, subject, NULL, 1000, &reply);
                    zmsg_destroy(&reply);
                    REQUIRE(r == 0);
                }
                else { // no reply
                    std::cout << "== rule actor request '" << reqCmd << "' not handled" << std::endl;
                    REQUIRE(0);
                }
            }
            zmsg_destroy(&msg);
        }
    }

    std::cout << "== rule actor ended" << std::endl;

    zpoller_destroy(&poller);
    mlm_client_destroy(&client);
}

