#include "tc_catch2.h"
#include "ClusterAuth.h"
#include <string>
#include <vector>

TEST_CASE("ClusterAuth seal/open round trip", "[ClusterAuth]")
{
    std::string const key(32, 'k');
    REQUIRE(ClusterAuth::Init(key));
    REQUIRE(ClusterAuth::IsInitialised());

    std::vector<uint8> const payload = { 1, 2, 3, 4, 5 };
    std::vector<uint8> frame = ClusterAuth::Seal(3, 0x29, payload.data(), payload.size());
    REQUIRE(frame.size() > payload.size());

    uint8 srcNode = 0, msgType = 0;
    std::vector<uint8> opened;
    REQUIRE(ClusterAuth::Open(frame.data(), frame.size(), srcNode, msgType, opened));
    REQUIRE(srcNode == 3);
    REQUIRE(msgType == 0x29);
    REQUIRE(opened == payload);

    SECTION("replay of the same frame is rejected")
    {
        std::vector<uint8> again;
        REQUIRE_FALSE(ClusterAuth::Open(frame.data(), frame.size(), srcNode, msgType, again));
    }

    SECTION("tampered frame is rejected")
    {
        std::vector<uint8> bad = ClusterAuth::Seal(3, 0x29, payload.data(), payload.size());
        bad.back() ^= 0xFF;
        std::vector<uint8> out;
        REQUIRE_FALSE(ClusterAuth::Open(bad.data(), bad.size(), srcNode, msgType, out));
    }
}

TEST_CASE("ClusterAuth refuses a short key", "[ClusterAuth]")
{
    REQUIRE_FALSE(ClusterAuth::Init("short"));
}
