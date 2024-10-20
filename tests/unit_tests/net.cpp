// Copyright (c) 2018, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <algorithm>
#include <atomic>
#include <boost/archive/portable_binary_oarchive.hpp>
#include <boost/archive/portable_binary_iarchive.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/range/adaptor/sliced.hpp>
#include <boost/range/combine.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <type_traits>

#include "crypto/crypto.h"
#include "net/dandelionpp.h"
#include "net/error.h"
#include "net/i2p_address.h"
#include "epee/net/net_utils_base.h"
#include "net/parse.h"
#include "net/tor_address.h"
#include "p2p/net_peerlist_boost_serialization.h"
#include "epee/serialization/keyvalue_serialization.h"
#include "epee/storages/portable_storage.h"

namespace
{
    static constexpr const char v2_onion[] =
        "xmrto2bturnore26.onion";
    static constexpr const char v3_onion[] =
        "vww6ybal4bd7szmgncyruucpgfkqahzddi37ktceo3ah7ngmcopnpyyd.onion";
}

using namespace std::literals;

TEST(tor_address, constants)
{
    static_assert(!net::tor_address::is_local(), "bad is_local() response");
    static_assert(!net::tor_address::is_loopback(), "bad is_loopback() response");
    static_assert(net::tor_address::get_type_id() == epee::net_utils::address_type::tor, "bad get_type_id() response");

    EXPECT_FALSE(net::tor_address::is_local());
    EXPECT_FALSE(net::tor_address::is_loopback());
    EXPECT_EQ(epee::net_utils::address_type::tor, net::tor_address::get_type_id());
    EXPECT_EQ(epee::net_utils::address_type::tor, net::tor_address::get_type_id());
}

TEST(tor_address, invalid)
{
    EXPECT_TRUE(net::tor_address::make("").has_error());
    EXPECT_TRUE(net::tor_address::make(":").has_error());
    EXPECT_TRUE(net::tor_address::make(".onion").has_error());
    EXPECT_TRUE(net::tor_address::make(".onion:").has_error());
    EXPECT_TRUE(net::tor_address::make(v2_onion + 1).has_error());
    EXPECT_TRUE(net::tor_address::make(v3_onion + 1).has_error());
    EXPECT_TRUE(net::tor_address::make(std::string_view{v2_onion, sizeof(v2_onion) - 2}).has_error());
    EXPECT_TRUE(net::tor_address::make(std::string_view{v3_onion, sizeof(v3_onion) - 2}).has_error());
    EXPECT_TRUE(net::tor_address::make(std::string{v2_onion} + ":-").has_error());
    EXPECT_TRUE(net::tor_address::make(std::string{v2_onion} + ":900a").has_error());
    EXPECT_TRUE(net::tor_address::make(std::string{v3_onion} + ":65536").has_error());
    EXPECT_TRUE(net::tor_address::make(std::string{v3_onion} + ":-1").has_error());

    std::string onion{v3_onion};
    onion.at(10) = 1;
    EXPECT_TRUE(net::tor_address::make(onion).has_error());
}

TEST(tor_address, unblockable_types)
{
    net::tor_address tor{};

    ASSERT_NE(nullptr, tor.host_str());
    EXPECT_STREQ("<unknown tor host>", tor.host_str());
    EXPECT_STREQ("<unknown tor host>", tor.str().c_str());
    EXPECT_EQ(0u, tor.port());
    EXPECT_TRUE(tor.is_unknown());
    EXPECT_FALSE(tor.is_local());
    EXPECT_FALSE(tor.is_loopback());
    EXPECT_EQ(epee::net_utils::address_type::tor, tor.get_type_id());
    EXPECT_EQ(epee::net_utils::zone::tor, tor.get_zone());

    tor = net::tor_address::unknown();
    ASSERT_NE(nullptr, tor.host_str());
    EXPECT_STREQ("<unknown tor host>", tor.host_str());
    EXPECT_STREQ("<unknown tor host>", tor.str().c_str());
    EXPECT_EQ(0u, tor.port());
    EXPECT_TRUE(tor.is_unknown());
    EXPECT_FALSE(tor.is_local());
    EXPECT_FALSE(tor.is_loopback());
    EXPECT_EQ(epee::net_utils::address_type::tor, tor.get_type_id());
    EXPECT_EQ(epee::net_utils::zone::tor, tor.get_zone());

    EXPECT_EQ(net::tor_address{}, net::tor_address::unknown());
}

TEST(tor_address, valid)
{
    const auto address1 = net::tor_address::make(v3_onion);

    ASSERT_TRUE(address1.has_value());
    EXPECT_EQ(0u, address1->port());
    EXPECT_STREQ(v3_onion, address1->host_str());
    EXPECT_STREQ(v3_onion, address1->str().c_str());
    EXPECT_TRUE(address1->is_blockable());

    net::tor_address address2{*address1};

    EXPECT_EQ(0u, address2.port());
    EXPECT_STREQ(v3_onion, address2.host_str());
    EXPECT_STREQ(v3_onion, address2.str().c_str());
    EXPECT_TRUE(address2.is_blockable());
    EXPECT_TRUE(address2.equal(*address1));
    EXPECT_TRUE(address1->equal(address2));
    EXPECT_TRUE(address2 == *address1);
    EXPECT_TRUE(*address1 == address2);
    EXPECT_FALSE(address2 != *address1);
    EXPECT_FALSE(*address1 != address2);
    EXPECT_TRUE(address2.is_same_host(*address1));
    EXPECT_TRUE(address1->is_same_host(address2));
    EXPECT_FALSE(address2.less(*address1));
    EXPECT_FALSE(address1->less(address2));

    address2 = MONERO_UNWRAP(net::tor_address::make(std::string{v2_onion} + ":6545"));

    EXPECT_EQ(6545, address2.port());
    EXPECT_STREQ(v2_onion, address2.host_str());
    EXPECT_EQ(std::string{v2_onion} + ":6545", address2.str().c_str());
    EXPECT_TRUE(address2.is_blockable());
    EXPECT_FALSE(address2.equal(*address1));
    EXPECT_FALSE(address1->equal(address2));
    EXPECT_FALSE(address2 == *address1);
    EXPECT_FALSE(*address1 == address2);
    EXPECT_TRUE(address2 != *address1);
    EXPECT_TRUE(*address1 != address2);
    EXPECT_FALSE(address2.is_same_host(*address1));
    EXPECT_FALSE(address1->is_same_host(address2));
    EXPECT_FALSE(address2.less(*address1));
    EXPECT_TRUE(address1->less(address2));

    net::tor_address address3 = MONERO_UNWRAP(net::tor_address::make(std::string{v3_onion} + ":", 65535));

    EXPECT_EQ(65535, address3.port());
    EXPECT_STREQ(v3_onion, address3.host_str());
    EXPECT_EQ(std::string{v3_onion} + ":65535", address3.str().c_str());
    EXPECT_TRUE(address3.is_blockable());
    EXPECT_FALSE(address3.equal(*address1));
    EXPECT_FALSE(address1->equal(address3));
    EXPECT_FALSE(address3 == *address1);
    EXPECT_FALSE(*address1 == address3);
    EXPECT_TRUE(address3 != *address1);
    EXPECT_TRUE(*address1 != address3);
    EXPECT_TRUE(address3.is_same_host(*address1));
    EXPECT_TRUE(address1->is_same_host(address3));
    EXPECT_FALSE(address3.less(*address1));
    EXPECT_TRUE(address1->less(address3));

    EXPECT_FALSE(address3.equal(address2));
    EXPECT_FALSE(address2.equal(address3));
    EXPECT_FALSE(address3 == address2);
    EXPECT_FALSE(address2 == address3);
    EXPECT_TRUE(address3 != address2);
    EXPECT_TRUE(address2 != address3);
    EXPECT_FALSE(address3.is_same_host(address2));
    EXPECT_FALSE(address2.is_same_host(address3));
    EXPECT_TRUE(address3.less(address2));
    EXPECT_FALSE(address2.less(address3));
}

TEST(tor_address, generic_network_address)
{
    const epee::net_utils::network_address tor1{MONERO_UNWRAP(net::tor_address::make(v3_onion, 8080))};
    const epee::net_utils::network_address tor2{MONERO_UNWRAP(net::tor_address::make(v3_onion, 8080))};
    const epee::net_utils::network_address ip{epee::net_utils::ipv4_network_address{100, 200}};

    EXPECT_EQ(tor1, tor2);
    EXPECT_NE(ip, tor1);
    EXPECT_LT(ip, tor1);

    EXPECT_STREQ(v3_onion, tor1.host_str().c_str());
    EXPECT_EQ(std::string{v3_onion} + ":8080", tor1.str());
    EXPECT_EQ(epee::net_utils::address_type::tor, tor1.get_type_id());
    EXPECT_EQ(epee::net_utils::address_type::tor, tor2.get_type_id());
    EXPECT_EQ(epee::net_utils::address_type::ipv4, ip.get_type_id());
    EXPECT_EQ(epee::net_utils::zone::tor, tor1.get_zone());
    EXPECT_EQ(epee::net_utils::zone::tor, tor2.get_zone());
    EXPECT_EQ(epee::net_utils::zone::public_, ip.get_zone());
    EXPECT_TRUE(tor1.is_blockable());
    EXPECT_TRUE(tor2.is_blockable());
    EXPECT_TRUE(ip.is_blockable());
}

namespace
{
    struct test_command_tor
    {
        net::tor_address tor;

        BEGIN_KV_SERIALIZE_MAP()
            KV_SERIALIZE(tor);
        END_KV_SERIALIZE_MAP()
    };
}

TEST(tor_address, epee_serializev_v2)
{
    std::string buffer{};
    {
        test_command_tor command{MONERO_UNWRAP(net::tor_address::make(v2_onion, 10))};
        EXPECT_FALSE(command.tor.is_unknown());
        EXPECT_NE(net::tor_address{}, command.tor);
        EXPECT_STREQ(v2_onion, command.tor.host_str());
        EXPECT_EQ(10u, command.tor.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(command.store(stg));
        EXPECT_TRUE(stg.store_to_binary(buffer));
    }

    test_command_tor command{};
    {
        EXPECT_TRUE(command.tor.is_unknown());
        EXPECT_EQ(net::tor_address{}, command.tor);
        EXPECT_STREQ(net::tor_address::unknown_str(), command.tor.host_str());
        EXPECT_EQ(0u, command.tor.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(stg.load_from_binary(buffer));
        EXPECT_TRUE(command.load(stg));
    }
    EXPECT_FALSE(command.tor.is_unknown());
    EXPECT_NE(net::tor_address{}, command.tor);
    EXPECT_STREQ(v2_onion, command.tor.host_str());
    EXPECT_EQ(10u, command.tor.port());

    // make sure that exceeding max buffer doesn't destroy tor_address::_load
    {
        epee::serialization::portable_storage stg{};
        stg.load_from_binary(buffer);

        std::string host{};
        ASSERT_TRUE(stg.get_value("host", host, stg.open_section("tor", nullptr, false)));
        EXPECT_EQ(std::strlen(v2_onion), host.size());

        host.push_back('k');
        EXPECT_TRUE(stg.set_value("host", host, stg.open_section("tor", nullptr, false)));
        EXPECT_TRUE(command.load(stg)); // poor error reporting from `KV_SERIALIZE`
    }

    EXPECT_TRUE(command.tor.is_unknown());
    EXPECT_EQ(net::tor_address{}, command.tor);
    EXPECT_STREQ(net::tor_address::unknown_str(), command.tor.host_str());
    EXPECT_EQ(0u, command.tor.port());
}

TEST(tor_address, epee_serializev_v3)
{
    std::string buffer{};
    {
        test_command_tor command{MONERO_UNWRAP(net::tor_address::make(v3_onion, 10))};
        EXPECT_FALSE(command.tor.is_unknown());
        EXPECT_NE(net::tor_address{}, command.tor);
        EXPECT_STREQ(v3_onion, command.tor.host_str());
        EXPECT_EQ(10u, command.tor.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(command.store(stg));
        EXPECT_TRUE(stg.store_to_binary(buffer));
    }

    test_command_tor command{};
    {
        EXPECT_TRUE(command.tor.is_unknown());
        EXPECT_EQ(net::tor_address{}, command.tor);
        EXPECT_STREQ(net::tor_address::unknown_str(), command.tor.host_str());
        EXPECT_EQ(0u, command.tor.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(stg.load_from_binary(buffer));
        EXPECT_TRUE(command.load(stg));
    }
    EXPECT_FALSE(command.tor.is_unknown());
    EXPECT_NE(net::tor_address{}, command.tor);
    EXPECT_STREQ(v3_onion, command.tor.host_str());
    EXPECT_EQ(10u, command.tor.port());

    // make sure that exceeding max buffer doesn't destroy tor_address::_load
    {
        epee::serialization::portable_storage stg{};
        stg.load_from_binary(buffer);

        std::string host{};
        ASSERT_TRUE(stg.get_value("host", host, stg.open_section("tor", nullptr, false)));
        EXPECT_EQ(std::strlen(v3_onion), host.size());

        host.push_back('k');
        EXPECT_TRUE(stg.set_value("host", host, stg.open_section("tor", nullptr, false)));
        EXPECT_TRUE(command.load(stg)); // poor error reporting from `KV_SERIALIZE`
    }

    EXPECT_TRUE(command.tor.is_unknown());
    EXPECT_EQ(net::tor_address{}, command.tor);
    EXPECT_STRNE(v3_onion, command.tor.host_str());
    EXPECT_EQ(0u, command.tor.port());
}

TEST(tor_address, epee_serialize_unknown)
{
    std::string buffer{};
    {
        test_command_tor command{net::tor_address::unknown()};
        EXPECT_TRUE(command.tor.is_unknown());
        EXPECT_EQ(net::tor_address{}, command.tor);
        EXPECT_STREQ(net::tor_address::unknown_str(), command.tor.host_str());
        EXPECT_EQ(0u, command.tor.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(command.store(stg));
        EXPECT_TRUE(stg.store_to_binary(buffer));
    }

    test_command_tor command{};
    {
        EXPECT_TRUE(command.tor.is_unknown());
        EXPECT_EQ(net::tor_address{}, command.tor);
        EXPECT_STRNE(v3_onion, command.tor.host_str());
        EXPECT_EQ(0u, command.tor.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(stg.load_from_binary(buffer));
        EXPECT_TRUE(command.load(stg));
    }
    EXPECT_TRUE(command.tor.is_unknown());
    EXPECT_EQ(net::tor_address{}, command.tor);
    EXPECT_STREQ(net::tor_address::unknown_str(), command.tor.host_str());
    EXPECT_EQ(0u, command.tor.port());

    // make sure that exceeding max buffer doesn't destroy tor_address::_load
    {
        epee::serialization::portable_storage stg{};
        stg.load_from_binary(buffer);

        std::string host{};
        ASSERT_TRUE(stg.get_value("host", host, stg.open_section("tor", nullptr, false)));
        EXPECT_EQ(std::strlen(net::tor_address::unknown_str()), host.size());

        host.push_back('k');
        EXPECT_TRUE(stg.set_value("host", host, stg.open_section("tor", nullptr, false)));
        EXPECT_TRUE(command.load(stg)); // poor error reporting from `KV_SERIALIZE`
    }

    EXPECT_TRUE(command.tor.is_unknown());
    EXPECT_EQ(net::tor_address{}, command.tor);
    EXPECT_STRNE(v3_onion, command.tor.host_str());
    EXPECT_EQ(0u, command.tor.port());
}

TEST(tor_address, boost_serialize_v2)
{
    std::string buffer{};
    {
        const net::tor_address tor = MONERO_UNWRAP(net::tor_address::make(v2_onion, 10));
        EXPECT_FALSE(tor.is_unknown());
        EXPECT_NE(net::tor_address{}, tor);
        EXPECT_STREQ(v2_onion, tor.host_str());
        EXPECT_EQ(10u, tor.port());

        std::ostringstream stream{};
        {
            boost::archive::portable_binary_oarchive archive{stream};
            archive << tor;
        }
        buffer = stream.str();
    }

    net::tor_address tor{};
    {
        EXPECT_TRUE(tor.is_unknown());
        EXPECT_EQ(net::tor_address{}, tor);
        EXPECT_STREQ(net::tor_address::unknown_str(), tor.host_str());
        EXPECT_EQ(0u, tor.port());

        std::istringstream stream{buffer};
        boost::archive::portable_binary_iarchive archive{stream};
        archive >> tor;
    }
    EXPECT_FALSE(tor.is_unknown());
    EXPECT_NE(net::tor_address{}, tor);
    EXPECT_STREQ(v2_onion, tor.host_str());
    EXPECT_EQ(10u, tor.port());
}

TEST(tor_address, boost_serialize_v3)
{
    std::string buffer{};
    {
        const net::tor_address tor = MONERO_UNWRAP(net::tor_address::make(v3_onion, 10));
        EXPECT_FALSE(tor.is_unknown());
        EXPECT_NE(net::tor_address{}, tor);
        EXPECT_STREQ(v3_onion, tor.host_str());
        EXPECT_EQ(10u, tor.port());

        std::ostringstream stream{};
        {
            boost::archive::portable_binary_oarchive archive{stream};
            archive << tor;
        }
        buffer = stream.str();
    }

    net::tor_address tor{};
    {
        EXPECT_TRUE(tor.is_unknown());
        EXPECT_EQ(net::tor_address{}, tor);
        EXPECT_STREQ(net::tor_address::unknown_str(), tor.host_str());
        EXPECT_EQ(0u, tor.port());

        std::istringstream stream{buffer};
        boost::archive::portable_binary_iarchive archive{stream};
        archive >> tor;
    }
    EXPECT_FALSE(tor.is_unknown());
    EXPECT_NE(net::tor_address{}, tor);
    EXPECT_STREQ(v3_onion, tor.host_str());
    EXPECT_EQ(10u, tor.port());
}

TEST(tor_address, boost_serialize_unknown)
{
    std::string buffer{};
    {
        const net::tor_address tor{};
        EXPECT_TRUE(tor.is_unknown());
        EXPECT_EQ(net::tor_address::unknown(), tor);
        EXPECT_STREQ(net::tor_address::unknown_str(), tor.host_str());
        EXPECT_EQ(0u, tor.port());

        std::ostringstream stream{};
        {
            boost::archive::portable_binary_oarchive archive{stream};
            archive << tor;
        }
        buffer = stream.str();
    }

    net::tor_address tor{};
    {
        EXPECT_TRUE(tor.is_unknown());
        EXPECT_EQ(net::tor_address{}, tor);
        EXPECT_STREQ(net::tor_address::unknown_str(), tor.host_str());
        EXPECT_EQ(0u, tor.port());

        std::istringstream stream{buffer};
        boost::archive::portable_binary_iarchive archive{stream};
        archive >> tor;
    }
    EXPECT_TRUE(tor.is_unknown());
    EXPECT_EQ(net::tor_address::unknown(), tor);
    EXPECT_STREQ(net::tor_address::unknown_str(), tor.host_str());
    EXPECT_EQ(0u, tor.port());
}

TEST(get_network_address, onion)
{
    expect<epee::net_utils::network_address> address =
        net::get_network_address("onion", 0);
    EXPECT_EQ(net::error::unsupported_address, address);

    address = net::get_network_address(".onion", 0);
    EXPECT_EQ(net::error::invalid_tor_address, address);

    address = net::get_network_address(v3_onion, 1000);
    ASSERT_TRUE(bool(address));
    EXPECT_EQ(epee::net_utils::address_type::tor, address->get_type_id());
    EXPECT_STREQ(v3_onion, address->host_str().c_str());
    EXPECT_EQ(std::string{v3_onion} + ":1000", address->str());

    address = net::get_network_address(std::string{v3_onion} + ":2000", 1000);
    ASSERT_TRUE(bool(address));
    EXPECT_EQ(epee::net_utils::address_type::tor, address->get_type_id());
    EXPECT_STREQ(v3_onion, address->host_str().c_str());
    EXPECT_EQ(std::string{v3_onion} + ":2000", address->str());

    address = net::get_network_address(std::string{v3_onion} + ":65536", 1000);
    EXPECT_EQ(net::error::invalid_port, address);
}

namespace
{
    static constexpr const char b32_i2p[] =
        "vww6ybal4bd7szmgncyruucpgfkqahzddi37ktceo3ah7ngmcopn.b32.i2p";
    static constexpr const char b32_i2p_2[] =
        "xmrto2bturnore26xmrto2bturnore26xmrto2bturnore26xmr2.b32.i2p";
}

TEST(i2p_address, constants)
{
    static_assert(!net::i2p_address::is_local(), "bad is_local() response");
    static_assert(!net::i2p_address::is_loopback(), "bad is_loopback() response");
    static_assert(net::i2p_address::get_type_id() == epee::net_utils::address_type::i2p, "bad get_type_id() response");

    EXPECT_FALSE(net::i2p_address::is_local());
    EXPECT_FALSE(net::i2p_address::is_loopback());
    EXPECT_EQ(epee::net_utils::address_type::i2p, net::i2p_address::get_type_id());
    EXPECT_EQ(epee::net_utils::address_type::i2p, net::i2p_address::get_type_id());
}

TEST(i2p_address, invalid)
{
    EXPECT_TRUE(net::i2p_address::make("").has_error());
    EXPECT_TRUE(net::i2p_address::make(":").has_error());
    EXPECT_TRUE(net::i2p_address::make(".b32.i2p").has_error());
    EXPECT_TRUE(net::i2p_address::make(".b32.i2p:").has_error());
    EXPECT_TRUE(net::i2p_address::make(b32_i2p + 1).has_error());
    EXPECT_TRUE(net::i2p_address::make(std::string_view{b32_i2p, sizeof(b32_i2p) - 2}).has_error());
    EXPECT_TRUE(net::i2p_address::make(std::string{b32_i2p} + ":65536").has_error());
    EXPECT_TRUE(net::i2p_address::make(std::string{b32_i2p} + ":-1").has_error());

    std::string i2p{b32_i2p};
    i2p.at(10) = 1;
    EXPECT_TRUE(net::i2p_address::make(i2p).has_error());
}

TEST(i2p_address, unblockable_types)
{
    net::i2p_address i2p{};

    ASSERT_NE(nullptr, i2p.host_str());
    EXPECT_STREQ("<unknown i2p host>", i2p.host_str());
    EXPECT_STREQ("<unknown i2p host>", i2p.str().c_str());
    EXPECT_EQ(0u, i2p.port());
    EXPECT_TRUE(i2p.is_unknown());
    EXPECT_FALSE(i2p.is_local());
    EXPECT_FALSE(i2p.is_loopback());
    EXPECT_EQ(epee::net_utils::address_type::i2p, i2p.get_type_id());
    EXPECT_EQ(epee::net_utils::zone::i2p, i2p.get_zone());

    i2p = net::i2p_address::unknown();
    ASSERT_NE(nullptr, i2p.host_str());
    EXPECT_STREQ("<unknown i2p host>", i2p.host_str());
    EXPECT_STREQ("<unknown i2p host>", i2p.str().c_str());
    EXPECT_EQ(0u, i2p.port());
    EXPECT_TRUE(i2p.is_unknown());
    EXPECT_FALSE(i2p.is_local());
    EXPECT_FALSE(i2p.is_loopback());
    EXPECT_EQ(epee::net_utils::address_type::i2p, i2p.get_type_id());
    EXPECT_EQ(epee::net_utils::zone::i2p, i2p.get_zone());

    EXPECT_EQ(net::i2p_address{}, net::i2p_address::unknown());
}

TEST(i2p_address, valid)
{
    const auto address1 = net::i2p_address::make(b32_i2p);

    ASSERT_TRUE(address1.has_value());
    EXPECT_EQ(0u, address1->port());
    EXPECT_STREQ(b32_i2p, address1->host_str());
    EXPECT_STREQ(b32_i2p, address1->str().c_str());
    EXPECT_TRUE(address1->is_blockable());

    net::i2p_address address2{*address1};

    EXPECT_EQ(0u, address2.port());
    EXPECT_STREQ(b32_i2p, address2.host_str());
    EXPECT_STREQ(b32_i2p, address2.str().c_str());
    EXPECT_TRUE(address2.is_blockable());
    EXPECT_TRUE(address2.equal(*address1));
    EXPECT_TRUE(address1->equal(address2));
    EXPECT_TRUE(address2 == *address1);
    EXPECT_TRUE(*address1 == address2);
    EXPECT_FALSE(address2 != *address1);
    EXPECT_FALSE(*address1 != address2);
    EXPECT_TRUE(address2.is_same_host(*address1));
    EXPECT_TRUE(address1->is_same_host(address2));
    EXPECT_FALSE(address2.less(*address1));
    EXPECT_FALSE(address1->less(address2));

    address2 = MONERO_UNWRAP(net::i2p_address::make(std::string{b32_i2p_2} + ":6545"));

    EXPECT_EQ(6545, address2.port());
    EXPECT_STREQ(b32_i2p_2, address2.host_str());
    EXPECT_EQ(std::string{b32_i2p_2} + ":6545", address2.str().c_str());
    EXPECT_TRUE(address2.is_blockable());
    EXPECT_FALSE(address2.equal(*address1));
    EXPECT_FALSE(address1->equal(address2));
    EXPECT_FALSE(address2 == *address1);
    EXPECT_FALSE(*address1 == address2);
    EXPECT_TRUE(address2 != *address1);
    EXPECT_TRUE(*address1 != address2);
    EXPECT_FALSE(address2.is_same_host(*address1));
    EXPECT_FALSE(address1->is_same_host(address2));
    EXPECT_FALSE(address2.less(*address1));
    EXPECT_TRUE(address1->less(address2));

    net::i2p_address address3 = MONERO_UNWRAP(net::i2p_address::make(std::string{b32_i2p} + ":", 65535));

    EXPECT_EQ(65535, address3.port());
    EXPECT_STREQ(b32_i2p, address3.host_str());
    EXPECT_EQ(std::string{b32_i2p} + ":65535", address3.str().c_str());
    EXPECT_TRUE(address3.is_blockable());
    EXPECT_FALSE(address3.equal(*address1));
    EXPECT_FALSE(address1->equal(address3));
    EXPECT_FALSE(address3 == *address1);
    EXPECT_FALSE(*address1 == address3);
    EXPECT_TRUE(address3 != *address1);
    EXPECT_TRUE(*address1 != address3);
    EXPECT_TRUE(address3.is_same_host(*address1));
    EXPECT_TRUE(address1->is_same_host(address3));
    EXPECT_FALSE(address3.less(*address1));
    EXPECT_TRUE(address1->less(address3));

    EXPECT_FALSE(address3.equal(address2));
    EXPECT_FALSE(address2.equal(address3));
    EXPECT_FALSE(address3 == address2);
    EXPECT_FALSE(address2 == address3);
    EXPECT_TRUE(address3 != address2);
    EXPECT_TRUE(address2 != address3);
    EXPECT_FALSE(address3.is_same_host(address2));
    EXPECT_FALSE(address2.is_same_host(address3));
    EXPECT_TRUE(address3.less(address2));
    EXPECT_FALSE(address2.less(address3));
}

TEST(i2p_address, generic_network_address)
{
    const epee::net_utils::network_address i2p1{MONERO_UNWRAP(net::i2p_address::make(b32_i2p, 8080))};
    const epee::net_utils::network_address i2p2{MONERO_UNWRAP(net::i2p_address::make(b32_i2p, 8080))};
    const epee::net_utils::network_address ip{epee::net_utils::ipv4_network_address{100, 200}};

    EXPECT_EQ(i2p1, i2p2);
    EXPECT_NE(ip, i2p1);
    EXPECT_LT(ip, i2p1);

    EXPECT_STREQ(b32_i2p, i2p1.host_str().c_str());
    EXPECT_EQ(std::string{b32_i2p} + ":8080", i2p1.str());
    EXPECT_EQ(epee::net_utils::address_type::i2p, i2p1.get_type_id());
    EXPECT_EQ(epee::net_utils::address_type::i2p, i2p2.get_type_id());
    EXPECT_EQ(epee::net_utils::address_type::ipv4, ip.get_type_id());
    EXPECT_EQ(epee::net_utils::zone::i2p, i2p1.get_zone());
    EXPECT_EQ(epee::net_utils::zone::i2p, i2p2.get_zone());
    EXPECT_EQ(epee::net_utils::zone::public_, ip.get_zone());
    EXPECT_TRUE(i2p1.is_blockable());
    EXPECT_TRUE(i2p2.is_blockable());
    EXPECT_TRUE(ip.is_blockable());
}

namespace
{
    struct test_command_i2p
    {
        net::i2p_address i2p;

        BEGIN_KV_SERIALIZE_MAP()
            KV_SERIALIZE(i2p);
        END_KV_SERIALIZE_MAP()
    };
}

TEST(i2p_address, epee_serializev_b32)
{
    std::string buffer{};
    {
        test_command_i2p command{MONERO_UNWRAP(net::i2p_address::make(b32_i2p, 10))};
        EXPECT_FALSE(command.i2p.is_unknown());
        EXPECT_NE(net::i2p_address{}, command.i2p);
        EXPECT_STREQ(b32_i2p, command.i2p.host_str());
        EXPECT_EQ(10u, command.i2p.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(command.store(stg));
        EXPECT_TRUE(stg.store_to_binary(buffer));
    }

    test_command_i2p command{};
    {
        EXPECT_TRUE(command.i2p.is_unknown());
        EXPECT_EQ(net::i2p_address{}, command.i2p);
        EXPECT_STREQ(net::i2p_address::unknown_str(), command.i2p.host_str());
        EXPECT_EQ(0u, command.i2p.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(stg.load_from_binary(buffer));
        EXPECT_TRUE(command.load(stg));
    }
    EXPECT_FALSE(command.i2p.is_unknown());
    EXPECT_NE(net::i2p_address{}, command.i2p);
    EXPECT_STREQ(b32_i2p, command.i2p.host_str());
    EXPECT_EQ(10u, command.i2p.port());

    // make sure that exceeding max buffer doesn't destroy i2p_address::_load
    {
        epee::serialization::portable_storage stg{};
        stg.load_from_binary(buffer);

        std::string host{};
        ASSERT_TRUE(stg.get_value("host", host, stg.open_section("i2p", nullptr, false)));
        EXPECT_EQ(std::strlen(b32_i2p), host.size());

        host.push_back('k');
        EXPECT_TRUE(stg.set_value("host", std::string{host}, stg.open_section("i2p", nullptr, false)));
        EXPECT_TRUE(command.load(stg)); // poor error reporting from `KV_SERIALIZE`
    }

    EXPECT_TRUE(command.i2p.is_unknown());
    EXPECT_EQ(net::i2p_address{}, command.i2p);
    EXPECT_STRNE(b32_i2p, command.i2p.host_str());
    EXPECT_EQ(0u, command.i2p.port());
}

TEST(i2p_address, epee_serialize_unknown)
{
    std::string buffer{};
    {
        test_command_i2p command{net::i2p_address::unknown()};
        EXPECT_TRUE(command.i2p.is_unknown());
        EXPECT_EQ(net::i2p_address{}, command.i2p);
        EXPECT_STREQ(net::i2p_address::unknown_str(), command.i2p.host_str());
        EXPECT_EQ(0u, command.i2p.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(command.store(stg));
        EXPECT_TRUE(stg.store_to_binary(buffer));
    }

    test_command_i2p command{};
    {
        EXPECT_TRUE(command.i2p.is_unknown());
        EXPECT_EQ(net::i2p_address{}, command.i2p);
        EXPECT_STRNE(b32_i2p, command.i2p.host_str());
        EXPECT_EQ(0u, command.i2p.port());

        epee::serialization::portable_storage stg{};
        EXPECT_TRUE(stg.load_from_binary(buffer));
        EXPECT_TRUE(command.load(stg));
    }
    EXPECT_TRUE(command.i2p.is_unknown());
    EXPECT_EQ(net::i2p_address{}, command.i2p);
    EXPECT_STREQ(net::i2p_address::unknown_str(), command.i2p.host_str());
    EXPECT_EQ(0u, command.i2p.port());

    // make sure that exceeding max buffer doesn't destroy i2p_address::_load
    {
        epee::serialization::portable_storage stg{};
        stg.load_from_binary(buffer);

        std::string host{};
        ASSERT_TRUE(stg.get_value("host", host, stg.open_section("i2p", nullptr, false)));
        EXPECT_EQ(std::strlen(net::i2p_address::unknown_str()), host.size());

        host.push_back('k');
        EXPECT_TRUE(stg.set_value("host", std::string{host}, stg.open_section("i2p", nullptr, false)));
        EXPECT_TRUE(command.load(stg)); // poor error reporting from `KV_SERIALIZE`
    }

    EXPECT_TRUE(command.i2p.is_unknown());
    EXPECT_EQ(net::i2p_address{}, command.i2p);
    EXPECT_STRNE(b32_i2p, command.i2p.host_str());
    EXPECT_EQ(0u, command.i2p.port());
}

TEST(i2p_address, boost_serialize_b32)
{
    std::string buffer{};
    {
        const net::i2p_address i2p = MONERO_UNWRAP(net::i2p_address::make(b32_i2p, 10));
        EXPECT_FALSE(i2p.is_unknown());
        EXPECT_NE(net::i2p_address{}, i2p);
        EXPECT_STREQ(b32_i2p, i2p.host_str());
        EXPECT_EQ(10u, i2p.port());

        std::ostringstream stream{};
        {
            boost::archive::portable_binary_oarchive archive{stream};
            archive << i2p;
        }
        buffer = stream.str();
    }

    net::i2p_address i2p{};
    {
        EXPECT_TRUE(i2p.is_unknown());
        EXPECT_EQ(net::i2p_address{}, i2p);
        EXPECT_STREQ(net::i2p_address::unknown_str(), i2p.host_str());
        EXPECT_EQ(0u, i2p.port());

        std::istringstream stream{buffer};
        boost::archive::portable_binary_iarchive archive{stream};
        archive >> i2p;
    }
    EXPECT_FALSE(i2p.is_unknown());
    EXPECT_NE(net::i2p_address{}, i2p);
    EXPECT_STREQ(b32_i2p, i2p.host_str());
    EXPECT_EQ(10u, i2p.port());
}

TEST(i2p_address, boost_serialize_unknown)
{
    std::string buffer{};
    {
        const net::i2p_address i2p{};
        EXPECT_TRUE(i2p.is_unknown());
        EXPECT_EQ(net::i2p_address::unknown(), i2p);
        EXPECT_STREQ(net::i2p_address::unknown_str(), i2p.host_str());
        EXPECT_EQ(0u, i2p.port());

        std::ostringstream stream{};
        {
            boost::archive::portable_binary_oarchive archive{stream};
            archive << i2p;
        }
        buffer = stream.str();
    }

    net::i2p_address i2p{};
    {
        EXPECT_TRUE(i2p.is_unknown());
        EXPECT_EQ(net::i2p_address{}, i2p);
        EXPECT_STREQ(net::i2p_address::unknown_str(), i2p.host_str());
        EXPECT_EQ(0u, i2p.port());

        std::istringstream stream{buffer};
        boost::archive::portable_binary_iarchive archive{stream};
        archive >> i2p;
    }
    EXPECT_TRUE(i2p.is_unknown());
    EXPECT_EQ(net::i2p_address::unknown(), i2p);
    EXPECT_STREQ(net::i2p_address::unknown_str(), i2p.host_str());
    EXPECT_EQ(0u, i2p.port());
}

TEST(get_network_address, i2p)
{
    expect<epee::net_utils::network_address> address =
        net::get_network_address("i2p", 0);
    EXPECT_EQ(net::error::unsupported_address, address);

    address = net::get_network_address(".b32.i2p", 0);
    EXPECT_EQ(net::error::invalid_i2p_address, address);

    address = net::get_network_address(b32_i2p, 1000);
    ASSERT_TRUE(bool(address));
    EXPECT_EQ(epee::net_utils::address_type::i2p, address->get_type_id());
    EXPECT_STREQ(b32_i2p, address->host_str().c_str());
    EXPECT_EQ(std::string{b32_i2p} + ":1000", address->str());

    address = net::get_network_address(std::string{b32_i2p} + ":2000", 1000);
    ASSERT_TRUE(bool(address));
    EXPECT_EQ(epee::net_utils::address_type::i2p, address->get_type_id());
    EXPECT_STREQ(b32_i2p, address->host_str().c_str());
    EXPECT_EQ(std::string{b32_i2p} + ":2000", address->str());

    address = net::get_network_address(std::string{b32_i2p} + ":65536", 1000);
    EXPECT_EQ(net::error::invalid_port, address);
}

TEST(get_network_address, ipv4)
{
    expect<epee::net_utils::network_address> address =
        net::get_network_address("0.0.0.", 0);
    EXPECT_EQ(net::error::unsupported_address, address);

    address = net::get_network_address("0.0.0.257", 0);
    EXPECT_EQ(net::error::unsupported_address, address);

    address = net::get_network_address("0.0.0.254", 1000);
    ASSERT_TRUE(bool(address));
    EXPECT_EQ(epee::net_utils::address_type::ipv4, address->get_type_id());
    EXPECT_STREQ("0.0.0.254", address->host_str().c_str());
    EXPECT_STREQ("0.0.0.254:1000", address->str().c_str());

    address = net::get_network_address("23.0.0.254:2000", 1000);
    ASSERT_TRUE(bool(address));
    EXPECT_EQ(epee::net_utils::address_type::ipv4, address->get_type_id());
    EXPECT_STREQ("23.0.0.254", address->host_str().c_str());
    EXPECT_STREQ("23.0.0.254:2000", address->str().c_str());
}

TEST(get_network_address, ipv4subnet)
{
    expect<epee::net_utils::ipv4_network_subnet> address = net::get_ipv4_subnet_address("0.0.0.0", true);
    EXPECT_STREQ("0.0.0.0/32", address->str().c_str());

    address = net::get_ipv4_subnet_address("0.0.0.0");
    EXPECT_TRUE(!address);

    address = net::get_ipv4_subnet_address("0.0.0.0/32");
    EXPECT_STREQ("0.0.0.0/32", address->str().c_str());

    address = net::get_ipv4_subnet_address("0.0.0.0/0");
    EXPECT_STREQ("0.0.0.0/0", address->str().c_str());

    address = net::get_ipv4_subnet_address("12.34.56.78/16");
    EXPECT_STREQ("12.34.0.0/16", address->str().c_str());
}

TEST(dandelionpp_map, traits)
{
    EXPECT_TRUE(std::is_default_constructible<net::dandelionpp::connection_map>());
    EXPECT_TRUE(std::is_move_constructible<net::dandelionpp::connection_map>());
    EXPECT_TRUE(std::is_move_assignable<net::dandelionpp::connection_map>());
    EXPECT_FALSE(std::is_copy_constructible<net::dandelionpp::connection_map>());
    EXPECT_FALSE(std::is_copy_assignable<net::dandelionpp::connection_map>());
}

TEST(dandelionpp_map, empty)
{
    const net::dandelionpp::connection_map mapper{};

    EXPECT_EQ(mapper.begin(), mapper.end());
    EXPECT_EQ(0u, mapper.size());

    const net::dandelionpp::connection_map cloned = mapper.clone();
    EXPECT_EQ(cloned.begin(), cloned.end());
    EXPECT_EQ(0u, cloned.size());
}

using epee::connection_id_t;

TEST(dandelionpp_map, zero_stems)
{
    std::vector<connection_id_t> connections{6};
    for (auto &c: connections)
      c = connection_id_t::random();

    net::dandelionpp::connection_map mapper{connections, 0};
    EXPECT_EQ(mapper.begin(), mapper.end());
    EXPECT_EQ(0u, mapper.size());

    for (const connection_id_t& connection : connections)
        EXPECT_TRUE(mapper.get_stem(connection).is_nil());

    EXPECT_FALSE(mapper.update(connections));
    EXPECT_EQ(mapper.begin(), mapper.end());
    EXPECT_EQ(0u, mapper.size());

    for (const connection_id_t& connection : connections)
        EXPECT_TRUE(mapper.get_stem(connection).is_nil());

    const net::dandelionpp::connection_map cloned = mapper.clone();
    EXPECT_EQ(cloned.end(), cloned.begin());
    EXPECT_EQ(0u, cloned.size());
}

TEST(dandelionpp_map, dropped_connection)
{
    std::vector<connection_id_t> connections{6};
    for (auto &c: connections)
      c = connection_id_t::random();
    std::sort(connections.begin(), connections.end());

    // select 3 of 6 outgoing connections
    net::dandelionpp::connection_map mapper{connections, 3};
    EXPECT_EQ(3u, mapper.size());
    EXPECT_EQ(3, mapper.end() - mapper.begin());
    {
        std::set<connection_id_t> used;
        for (const connection_id_t& connection : mapper)
        {
            EXPECT_TRUE(used.insert(connection).second);
            EXPECT_TRUE(std::binary_search(connections.begin(), connections.end(), connection));
        }
    }
    {
        const net::dandelionpp::connection_map cloned = mapper.clone();
        EXPECT_EQ(3u, cloned.size());
        ASSERT_EQ(mapper.end() - mapper.begin(), cloned.end() - cloned.begin());
        for (auto elem : boost::combine(mapper, cloned))
            EXPECT_EQ(boost::get<0>(elem), boost::get<1>(elem));
    }
    EXPECT_FALSE(mapper.update(connections));
    EXPECT_EQ(3u, mapper.size());
    ASSERT_EQ(3, mapper.end() - mapper.begin());
    {
        std::set<connection_id_t> used;
        for (const connection_id_t& connection : mapper)
        {
            EXPECT_FALSE(connection.is_nil());
            EXPECT_TRUE(used.insert(connection).second);
            EXPECT_TRUE(std::binary_search(connections.begin(), connections.end(), connection));
        }
    }
    std::map<connection_id_t, connection_id_t> mapping;
    std::vector<connection_id_t> in_connections{9};
    for (auto &c: in_connections)
      c = connection_id_t::random();
    {
        std::map<connection_id_t, std::size_t> used;
        std::multimap<connection_id_t, connection_id_t> inverse_mapping;
        for (const connection_id_t& connection : in_connections)
        {
            const connection_id_t out = mapper.get_stem(connection);
            EXPECT_FALSE(out.is_nil());
            EXPECT_TRUE(mapping.emplace(connection, out).second);
            inverse_mapping.emplace(out, connection);
            used[out]++;
        }

        EXPECT_EQ(3u, used.size());
        for (const auto& entry : used)
            EXPECT_EQ(3u, entry.second);

        for (const connection_id_t& connection : in_connections)
            EXPECT_EQ(mapping[connection], mapper.get_stem(connection));

        // drop 1 connection, and select replacement from 1 of unused 3.
        const connection_id_t lost_connection = *(++mapper.begin());
        const auto elem = std::lower_bound(connections.begin(), connections.end(), lost_connection);
        ASSERT_NE(connections.end(), elem);
        ASSERT_EQ(lost_connection, *elem);
        connections.erase(elem);

        EXPECT_TRUE(mapper.update(connections));
        EXPECT_EQ(3u, mapper.size());
        ASSERT_EQ(3, mapper.end() - mapper.begin());

        for (const connection_id_t& connection : mapper)
        {
            EXPECT_FALSE(connection.is_nil());
            EXPECT_NE(lost_connection, connection);
        }

        const connection_id_t newly_mapped = *(++mapper.begin());
        EXPECT_FALSE(newly_mapped.is_nil());
        EXPECT_NE(lost_connection, newly_mapped);

        for (auto elems = inverse_mapping.equal_range(lost_connection); elems.first != elems.second; ++elems.first)
            mapping[elems.first->second] = newly_mapped;
    }
    {
        const net::dandelionpp::connection_map cloned = mapper.clone();
        EXPECT_EQ(3u, cloned.size());
        ASSERT_EQ(mapper.end() - mapper.begin(), cloned.end() - cloned.begin());
        for (auto elem : boost::combine(mapper, cloned))
            EXPECT_EQ(boost::get<0>(elem), boost::get<1>(elem));
    }
    // mappings should remain evenly distributed amongst 2, with 3 sitting in waiting
    {
        std::set<connection_id_t> used;
        for (const connection_id_t& connection : mapper)
        {
            EXPECT_FALSE(connection.is_nil());
            EXPECT_TRUE(used.insert(connection).second);
            EXPECT_TRUE(std::binary_search(connections.begin(), connections.end(), connection));
        }
    }
    {
        std::map<connection_id_t, std::size_t> used;
        for (const connection_id_t& connection : in_connections)
        {
            const connection_id_t& out = mapper.get_stem(connection);
            EXPECT_FALSE(out.is_nil());
            EXPECT_EQ(mapping[connection], out);
            used[out]++;
        }

        EXPECT_EQ(3u, used.size());
        for (const auto& entry : used)
            EXPECT_EQ(3u, entry.second);
    }
    {
        const net::dandelionpp::connection_map cloned = mapper.clone();
        EXPECT_EQ(3u, cloned.size());
        ASSERT_EQ(mapper.end() - mapper.begin(), cloned.end() - cloned.begin());
        for (auto elem : boost::combine(mapper, cloned))
            EXPECT_EQ(boost::get<0>(elem), boost::get<1>(elem));
    }
}

TEST(dandelionpp_map, dropped_connection_remapped)
{
    std::vector<connection_id_t> connections{3};
    for (auto &e: connections)
      e = connection_id_t::random();
    std::sort(connections.begin(), connections.end());

    // select 3 of 3 outgoing connections
    net::dandelionpp::connection_map mapper{connections, 3};
    EXPECT_EQ(3u, mapper.size());
    EXPECT_EQ(3, mapper.end() - mapper.begin());
    {
        std::set<connection_id_t> used;
        for (const connection_id_t& connection : mapper)
        {
            EXPECT_FALSE(connection.is_nil());
            EXPECT_TRUE(used.insert(connection).second);
            EXPECT_TRUE(std::binary_search(connections.begin(), connections.end(), connection));
        }
    }
    EXPECT_FALSE(mapper.update(connections));
    EXPECT_EQ(3u, mapper.size());
    ASSERT_EQ(3, mapper.end() - mapper.begin());
    {
        std::set<connection_id_t> used;
        for (const connection_id_t& connection : mapper)
        {
            EXPECT_FALSE(connection.is_nil());
            EXPECT_TRUE(used.insert(connection).second);
            EXPECT_TRUE(std::binary_search(connections.begin(), connections.end(), connection));
        }
    }
    std::map<connection_id_t, connection_id_t> mapping;
    std::vector<connection_id_t> in_connections{9};
    for (auto &e: in_connections)
      e = connection_id_t::random();
    {
        std::map<connection_id_t, std::size_t> used;
        std::multimap<connection_id_t, connection_id_t> inverse_mapping;
        for (const connection_id_t& connection : in_connections)
        {
            const connection_id_t out = mapper.get_stem(connection);
            EXPECT_FALSE(out.is_nil());
            EXPECT_TRUE(mapping.emplace(connection, out).second);
            inverse_mapping.emplace(out, connection);
            used[out]++;
        }

        EXPECT_EQ(3u, used.size());
        for (const auto& entry : used)
            EXPECT_EQ(3u, entry.second);

        for (const connection_id_t& connection : in_connections)
            EXPECT_EQ(mapping[connection], mapper.get_stem(connection));

        // drop 1 connection leaving "hole"
        const connection_id_t lost_connection = *(++mapper.begin());
        const auto elem = std::lower_bound(connections.begin(), connections.end(), lost_connection);
        ASSERT_NE(connections.end(), elem);
        ASSERT_EQ(lost_connection, *elem);
        connections.erase(elem);

        EXPECT_TRUE(mapper.update(connections));
        EXPECT_EQ(2u, mapper.size());
        EXPECT_EQ(3, mapper.end() - mapper.begin());

        for (auto elems = inverse_mapping.equal_range(lost_connection); elems.first != elems.second; ++elems.first)
            mapping[elems.first->second] = {};
    }
    // remap 3 connections and map 1 new connection to 2 remaining out connections
    in_connections.resize(10);
    in_connections[9] = connection_id_t::random();
    {
        std::map<connection_id_t, std::size_t> used;
        for (const connection_id_t& connection : in_connections)
        {
            const connection_id_t& out = mapper.get_stem(connection);
            EXPECT_FALSE(out.is_nil());
            used[out]++;

            connection_id_t& expected = mapping[connection];
            if (!expected.is_nil())
                EXPECT_EQ(expected, out);
            else
                expected = out;
        }

        EXPECT_EQ(2u, used.size());
        for (const auto& entry : used)
            EXPECT_EQ(5u, entry.second);
    }
    // select 3 of 3 connections but do not remap existing links
    connections.resize(3);
    connections[2] = connection_id_t::random();
    EXPECT_TRUE(mapper.update(connections));
    EXPECT_EQ(3u, mapper.size());
    EXPECT_EQ(3, mapper.end() - mapper.begin());
    {
        std::map<connection_id_t, std::size_t> used;
        for (const connection_id_t& connection : in_connections)
        {
            const connection_id_t& out = mapper.get_stem(connection);
            EXPECT_FALSE(out.is_nil());
            used[out]++;

            EXPECT_EQ(mapping[connection], out);
        }

        EXPECT_EQ(2u, used.size());
        for (const auto& entry : used)
            EXPECT_EQ(5u, entry.second);
    }
    // map 8 new incoming connections across 3 outgoing links
    in_connections.resize(18);
    for (size_t i = 10; i < in_connections.size(); ++i)
      in_connections[i] = connection_id_t::random();
    {
        std::map<connection_id_t, std::size_t> used;
        for (const connection_id_t& connection : in_connections)
        {
            const connection_id_t& out = mapper.get_stem(connection);
            EXPECT_FALSE(out.is_nil());
            used[out]++;

            connection_id_t& expected = mapping[connection];
            if (!expected.is_nil())
                EXPECT_EQ(expected, out);
            else
                expected = out;
        }

        EXPECT_EQ(3u, used.size());
        for (const auto& entry : used)
            EXPECT_EQ(6u, entry.second);
    }
}

TEST(dandelionpp_map, dropped_all_connections)
{
    std::vector<connection_id_t> connections{8};
    for (auto &e: connections)
      e = connection_id_t::random();
    std::sort(connections.begin(), connections.end());

    // select 3 of 8 outgoing connections
    net::dandelionpp::connection_map mapper{connections, 3};
    EXPECT_EQ(3u, mapper.size());
    EXPECT_EQ(3, mapper.end() - mapper.begin());
    {
        std::set<connection_id_t> used;
        for (const connection_id_t& connection : mapper)
        {
            EXPECT_FALSE(connection.is_nil());
            EXPECT_TRUE(used.insert(connection).second);
            EXPECT_TRUE(std::binary_search(connections.begin(), connections.end(), connection));
        }
    }
    EXPECT_FALSE(mapper.update(connections));
    EXPECT_EQ(3u, mapper.size());
    ASSERT_EQ(3, mapper.end() - mapper.begin());
    {
        std::set<connection_id_t> used;
        for (const connection_id_t& connection : mapper)
        {
            EXPECT_FALSE(connection.is_nil());
            EXPECT_TRUE(used.insert(connection).second);
            EXPECT_TRUE(std::binary_search(connections.begin(), connections.end(), connection));
        }
    }
    std::vector<connection_id_t> in_connections{9};
    for (auto &e: in_connections)
      e = connection_id_t::random();
    {
        std::map<connection_id_t, std::size_t> used;
        std::map<connection_id_t, connection_id_t> mapping;
        for (const connection_id_t& connection : in_connections)
        {
            const connection_id_t out = mapper.get_stem(connection);
            EXPECT_FALSE(out.is_nil());
            EXPECT_TRUE(mapping.emplace(connection, out).second);
            used[out]++;
        }

        EXPECT_EQ(3u, used.size());
        for (const auto& entry : used)
            EXPECT_EQ(3u, entry.second);

        for (const connection_id_t& connection : in_connections)
            EXPECT_EQ(mapping[connection], mapper.get_stem(connection));

        // drop all connections
        connections.clear();

        EXPECT_TRUE(mapper.update(connections));
        EXPECT_EQ(0u, mapper.size());
        EXPECT_EQ(3, mapper.end() - mapper.begin());
    }
    // remap 7 connections to nothing
    for (const connection_id_t& connection : boost::adaptors::slice(in_connections, 0, 7))
        EXPECT_TRUE(mapper.get_stem(connection).is_nil());

    // select 3 of 30 connections, only 7 should be remapped to new indexes (but all to new uuids)
    connections.resize(30);
    for (auto &e: connections)
      e = connection_id_t::random();
    EXPECT_TRUE(mapper.update(connections));
    {
        std::map<connection_id_t, std::size_t> used;
        for (const connection_id_t& connection : in_connections)
        {
            const connection_id_t& out = mapper.get_stem(connection);
            EXPECT_FALSE(out.is_nil());
            used[out]++;
        }

        EXPECT_EQ(3u, used.size());
        for (const auto& entry : used)
            EXPECT_EQ(3u, entry.second);
    }
}
