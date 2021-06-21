#include <catch2/catch.hpp>
#include "fty_nut.h"
#include <malamute.h>

TEST_CASE("nut configurator server test")
{
    static const char* endpoint = "inproc://fty_nut_configurator_server-test";
    zactor_t*          mlm      = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    REQUIRE(mlm);
    zstr_sendx(mlm, "BIND", endpoint, NULL);
    zactor_t* self = zactor_new(fty_nut_configurator_server, const_cast<char*>(endpoint));
    REQUIRE(self);
    zactor_destroy(&self);
    zactor_destroy(&mlm);
}
