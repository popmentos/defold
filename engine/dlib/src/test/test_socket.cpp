// Copyright 2020 The Defold Foundation
// Licensed under the Defold License version 1.0 (the "License"); you may not use
// this file except in compliance with the License.
// 
// You may obtain a copy of the License, together with FAQs at
// https://www.defold.com/license
// 
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include <stdint.h>
#include <stdlib.h> // free
#include <string.h>
#include "../dlib/dstrings.h"
#include "../dlib/socket.h"
#include "../dlib/thread.h"
#include "../dlib/time.h"
#if defined(__linux__) || defined(__MACH__) || defined(ANDROID) || defined(__EMSCRIPTEN__)
#include <arpa/inet.h>
#endif

#include "../dlib/network_constants.h"

#define JC_TEST_IMPLEMENTATION
#include <jc_test/jc_test.h>

template <> char* jc_test_print_value(char* buffer, size_t buffer_len, dmSocket::Result r) {
    return buffer + dmSnPrintf(buffer, buffer_len, "%s", dmSocket::ResultToString(r));
}

const uint16_t CONST_TEST_PORT = 8008;

struct ServerThreadInfo
{

    ServerThreadInfo()
    {
        port = 0;
        domain = dmSocket::DOMAIN_MISSING;
        listening = false;
        sent = false;
    }

    uint16_t port;
    dmSocket::Domain domain;
    bool listening;
    bool sent;
};

void WaitForBool(bool* lock)
{
    const uint32_t maximum_wait = 5000; // milliseconds
    const uint32_t wait_timeout = 100;  // milliseconds
    uint32_t wait_count = 0;
    for (uint32_t i = 0; i < (maximum_wait / wait_timeout); ++i)
    {
        if (!(*lock))
        {
            wait_count += 1;
            dmTime::Sleep(wait_timeout * 1000); // nanoseconds
        }
    }

    printf("Waited for %d/%d counts (%d ms each)\n", wait_count, (maximum_wait / wait_timeout), wait_timeout);
    ASSERT_TRUE(*lock);
    dmTime::Sleep(5 * wait_timeout * 1000); // nanoseconds
}

static void ServerThread(void* arg)
{
    struct ServerThreadInfo* info = (struct ServerThreadInfo *) arg;
    dmSocket::Result result = dmSocket::RESULT_OK;
    dmSocket::Socket server_sock = -1;
    dmSocket::Address server_addr;
    dmSocket::Socket client_sock = -1;
    dmSocket::Address client_addr;

    // Setup server socket and listen for a client to connect
    result = dmSocket::New(info->domain, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &server_sock);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::SetReuseAddress(server_sock, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    const char* hostname = dmSocket::IsSocketIPv4(server_sock) ? DM_LOOPBACK_ADDRESS_IPV4 : DM_LOOPBACK_ADDRESS_IPV6;
    result = dmSocket::GetHostByName(hostname, &server_addr, dmSocket::IsSocketIPv4(server_sock), dmSocket::IsSocketIPv6(server_sock));
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::Bind(server_sock, server_addr, info->port);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::Listen(server_sock, 1000); // Backlog = 1000
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Wait for a client to connect
    info->listening = true;
    result = dmSocket::Accept(server_sock, &client_addr, &client_sock);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Send data to the client for verification
    int value = 0x00def01d;
    int written = 0;

    result = dmSocket::Send(client_sock, &value, sizeof(value), &written);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_EQ((int) sizeof(value), written);

    info->sent = true;

    // Teardown
    result = dmSocket::Delete(client_sock);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::Delete(server_sock);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

inline dmSocket::Socket GetSocket(dmSocket::Domain domain)
{
    dmSocket::Socket instance = 0;
    uint64_t timeout = 3000; // milliseconds
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::New(domain, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &instance);
    if (result != dmSocket::RESULT_OK) return -1;

    result = dmSocket::SetSendTimeout(instance, timeout);
    if (result != dmSocket::RESULT_OK) return -1;

    result = dmSocket::SetReceiveTimeout(instance, timeout);
    if (result != dmSocket::RESULT_OK) return -1;

    result = dmSocket::SetNoDelay(instance, true);
    if (result != dmSocket::RESULT_OK) return -1;

    return instance;
}

TEST(Socket, BitDifference_Difference)
{
    dmSocket::Address instance1;
    dmSocket::Address instance2;

    instance1.m_address[3] = 0x4e;
    instance2.m_address[3] = 0xe6;

    ASSERT_EQ(3u, dmSocket::BitDifference(instance1, instance2));
}

TEST(Socket, BitDifference_Equal)
{
    dmSocket::Address instance1;
    dmSocket::Address instance2;

    instance1.m_address[3] = 0xe6;
    instance2.m_address[3] = 0xe6;

    ASSERT_EQ(0U, dmSocket::BitDifference(instance1, instance2));
}

TEST(Socket, NetworkOrder)
{
    dmSocket::Address address;
    const char* hostname = DM_LOOPBACK_ADDRESS_IPV4;
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::GetHostByName(hostname, &address, true, false);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // This checks so our format is in network order.
    ASSERT_EQ(inet_addr(DM_LOOPBACK_ADDRESS_IPV4), address.m_address[3]);
}

TEST(Socket, IPv4)
{
    dmSocket::Address instance;
    instance.m_family = dmSocket::DOMAIN_IPV4;
    ASSERT_EQ(&instance.m_address[3], dmSocket::IPv4(&instance));
}

TEST(Socket, IPv6)
{
    dmSocket::Address instance;
    instance.m_family = dmSocket::DOMAIN_IPV6;
    ASSERT_EQ(&instance.m_address[0], dmSocket::IPv6(&instance));
}

TEST(Socket, New_IPv4)
{
    dmSocket::Socket instance = 0;
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::New(dmSocket::DOMAIN_IPV4, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_NE(-1, instance);
    ASSERT_TRUE(dmSocket::IsSocketIPv4(instance));
    ASSERT_FALSE(dmSocket::IsSocketIPv6(instance));

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, New_IPv6)
{
    dmSocket::Socket instance = 0;
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::New(dmSocket::DOMAIN_IPV6, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_NE(-1, instance);
    ASSERT_TRUE(dmSocket::IsSocketIPv6(instance));
    ASSERT_FALSE(dmSocket::IsSocketIPv4(instance));

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, New_InvalidDomain)
{
    dmSocket::Socket instance = 0;
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::New(dmSocket::DOMAIN_UNKNOWN, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &instance);
    ASSERT_EQ(dmSocket::RESULT_AFNOSUPPORT, result);
    ASSERT_EQ(-1, instance);
    ASSERT_FALSE(dmSocket::IsSocketIPv6(instance));
    ASSERT_FALSE(dmSocket::IsSocketIPv4(instance));

    // Teardown
    result = dmSocket::Delete(instance);
#if defined(_WIN32)
    ASSERT_EQ(dmSocket::RESULT_NOTSOCK, result);
#else
    ASSERT_EQ(dmSocket::RESULT_BADF, result);
#endif
}

TEST(Socket, SetReuseAddress_IPv4)
{
    dmSocket::Result result = dmSocket::RESULT_OK;
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV4);
    ASSERT_NE(-1, instance);

    result = dmSocket::SetReuseAddress(instance, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, SetReuseAddress_IPv6)
{
    dmSocket::Result result = dmSocket::RESULT_OK;
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV6);
    ASSERT_NE(-1, instance);

    result = dmSocket::SetReuseAddress(instance, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, AddMembership_IPv4)
{
    dmSocket::Result result = dmSocket::RESULT_OK;
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV4);
    ASSERT_NE(-1, instance);

    // ASSERT_FALSE("This test has not been implemented yet");

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, AddMembership_IPv6)
{
    dmSocket::Result result = dmSocket::RESULT_OK;
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV6);
    ASSERT_NE(-1, instance);

    // ASSERT_FALSE("This test has not been implemented yet");

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, SetMulticastIf_IPv4)
{
    dmSocket::Result result = dmSocket::RESULT_OK;
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV4);
    ASSERT_NE(-1, instance);

    uint32_t tests = 0;
    uint32_t count = 0;
    const uint32_t max_count = 16;
    dmSocket::IfAddr addresses[max_count];
    dmSocket::GetIfAddresses(addresses, max_count, &count);

    // Test has been disabled since we can't test multicast interfaces without
    // multiple interfaces. Neither the build infrastructure nor the
    // development environment at King currently supports this, thus this
    // functionality has to be tested manually.
    printf("[   INFO   ] Test for SetMulticastIf is disabled.\n");

    for (uint32_t i = 0; i < count; ++i)
    {
        dmSocket::Address address = addresses[i].m_Address;
        if (address.m_family == dmSocket::DOMAIN_IPV4)
        {
            tests += 1;
            result = dmSocket::SetMulticastIf(instance, address);
            // ASSERT_EQ(dmSocket::RESULT_OK, result);
        }
    }

    // ASSERT_GT(tests, 0);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, SetMulticastIf_IPv6)
{
    dmSocket::Result result = dmSocket::RESULT_OK;
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV6);
    ASSERT_NE(-1, instance);

    uint32_t tests = 0;
    uint32_t count = 0;
    const uint32_t max_count = 16;
    dmSocket::IfAddr addresses[max_count];
    dmSocket::GetIfAddresses(addresses, max_count, &count);

    // Test has been disabled since we can't test multicast interfaces without
    // multiple interfaces. Neither the build infrastructure nor the
    // development environment at King currently supports this, thus this
    // functionality has to be tested manually.
    printf("[   INFO   ] Test for SetMulticastIf is disabled.\n");

    for (uint32_t i = 0; i < count; ++i)
    {
        dmSocket::Address address = addresses[i].m_Address;
        if (address.m_family == dmSocket::DOMAIN_IPV6)
        {
            tests += 1;
            result = dmSocket::SetMulticastIf(instance, address);
            // ASSERT_EQ(dmSocket::RESULT_OK, result);
        }
    }

    // ASSERT_GT(tests, 0);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, Delete_IPv4)
{
    dmSocket::Result result = dmSocket::RESULT_OK;
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV4);
    ASSERT_NE(-1, instance);

    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, Delete_IPv6)
{
    dmSocket::Result result = dmSocket::RESULT_OK;
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV6);
    ASSERT_NE(-1, instance);

    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, Delete_InvalidSocket)
{
    dmSocket::Socket instance = -1;
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::Delete(instance);
#if defined(_WIN32)
    ASSERT_EQ(dmSocket::RESULT_NOTSOCK, result);
#else
    ASSERT_EQ(dmSocket::RESULT_BADF, result);
#endif
}

// Accept

// Bind

TEST(Socket, Connect_IPv4_ThreadServer)
{
    // Setup server thread
    struct ServerThreadInfo info;
    info.listening = false;
    info.port = CONST_TEST_PORT;
    info.domain = dmSocket::DOMAIN_IPV4;
    const char* hostname = DM_LOOPBACK_ADDRESS_IPV4;
    dmThread::Thread thread = dmThread::New(&ServerThread, 0x80000, (void *) &info, "server");

    // Setup client
    dmSocket::Socket socket = GetSocket(dmSocket::DOMAIN_IPV4);
    ASSERT_NE(-1, socket);
    dmSocket::Address address;
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::GetHostByName(hostname, &address, dmSocket::IsSocketIPv4(socket), dmSocket::IsSocketIPv6(socket));
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    WaitForBool(&info.listening);

    result = dmSocket::Connect(socket, address, CONST_TEST_PORT);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    WaitForBool(&info.sent);

    // Receive data from the server
    int value = 0;
    int read = 0;

    result = dmSocket::Receive(socket, &value, sizeof(value), &read);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_EQ(0x00def01d, value);

    result = dmSocket::Delete(socket);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    dmThread::Join(thread);
}

TEST(Socket, Connect_IPv6_ThreadServer)
{
    // Setup server thread
    struct ServerThreadInfo info;
    info.listening = false;
    info.port = CONST_TEST_PORT;
    info.domain = dmSocket::DOMAIN_IPV6;
    dmThread::Thread thread = dmThread::New(&ServerThread, 0x80000, (void *) &info, "server");
    const char* hostname = DM_LOOPBACK_ADDRESS_IPV6;

    // Setup client
    dmSocket::Socket socket = GetSocket(dmSocket::DOMAIN_IPV6);
    ASSERT_NE(-1, socket);
    dmSocket::Address address;
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::GetHostByName(hostname, &address, dmSocket::IsSocketIPv4(socket), dmSocket::IsSocketIPv6(socket));
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    WaitForBool(&info.listening);

    result = dmSocket::Connect(socket, address, CONST_TEST_PORT);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    WaitForBool(&info.sent);

    // Receive data from the server
    int value = 0;
    int read = 0;

    result = dmSocket::Receive(socket, &value, sizeof(value), &read);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_EQ(0x00def01d, value);

    result = dmSocket::Delete(socket);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    dmThread::Join(thread);
}

static void RefusingServerThread(void* arg)
{
    dmSocket::Socket* server = (dmSocket::Socket*)arg;
    dmTime::Sleep(100 * 1000);
    dmSocket::Delete(*server);
}

TEST(Socket, Connect_IPv4_ConnectionRefused)
{
    dmSocket::Socket server = GetSocket(dmSocket::DOMAIN_IPV4);
    ASSERT_NE(-1, server);
    dmSocket::Socket client = GetSocket(dmSocket::DOMAIN_IPV4);
    ASSERT_NE(-1, client);
    dmSocket::Result result = dmSocket::RESULT_OK;
    result = dmSocket::SetBlocking(client, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    const char* hostname = DM_LOOPBACK_ADDRESS_IPV4;
    dmSocket::Address address;

    result = dmSocket::GetHostByName(hostname, &address, true, false);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::Bind(server, address, 0);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    uint16_t port;
    result = dmSocket::GetName(server, &address, &port);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    dmThread::Thread thread = dmThread::New(&RefusingServerThread, 0x80000, (void *) &server, "server");

    result = dmSocket::Connect(client, address, port);
    ASSERT_EQ(dmSocket::RESULT_CONNREFUSED, result);

    // Teardown
    result = dmSocket::Delete(client);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    dmThread::Join(thread);
}

TEST(Socket, Connect_IPv6_ConnectionRefused)
{
    dmSocket::Socket server = GetSocket(dmSocket::DOMAIN_IPV6);
    ASSERT_NE(-1, server);
    dmSocket::Socket client = GetSocket(dmSocket::DOMAIN_IPV6);
    ASSERT_NE(-1, client);
    dmSocket::Result result = dmSocket::RESULT_OK;
    result = dmSocket::SetBlocking(client, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    const char* hostname = DM_LOOPBACK_ADDRESS_IPV6;
    dmSocket::Address address;

    result = dmSocket::GetHostByName(hostname, &address, false, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::Bind(server, address, 0);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    uint16_t port;
    result = dmSocket::GetName(server, &address, &port);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    dmThread::Thread thread = dmThread::New(&RefusingServerThread, 0x80000, (void *) &server, "server");

    result = dmSocket::Connect(client, address, port);
    ASSERT_EQ(dmSocket::RESULT_CONNREFUSED, result);

    // Teardown
    result = dmSocket::Delete(client);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    dmThread::Join(thread);
}

// Listen

// Shutdown

TEST(Socket, GetName_IPv4_Connected)
{
    // Setup server thread
    struct ServerThreadInfo info;
    info.listening = false;
    info.port = CONST_TEST_PORT;
    info.domain = dmSocket::DOMAIN_IPV4;
    dmThread::Thread thread = dmThread::New(&ServerThread, 0x80000, (void *) &info, "server");

    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV4);

    ASSERT_NE(-1, instance);
    dmSocket::Result result = dmSocket::RESULT_OK;
    const char* hostname = DM_LOOPBACK_ADDRESS_IPV4;
    dmSocket::Address address;
    uint16_t port = CONST_TEST_PORT;

    result = dmSocket::GetHostByName(hostname, &address, dmSocket::IsSocketIPv4(instance), dmSocket::IsSocketIPv6(instance));
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    for (int i = 0; i < 20; ++i) // Wait up to 2 seconds for the Server thread to kick in
    {
        if (!info.listening)
            dmTime::Sleep(100000); // 100 ms
    }

    dmTime::Sleep(500000);
    ASSERT_TRUE(info.listening);

    result = dmSocket::Connect(instance, address, port);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    uint16_t actual_port = 0;
    dmSocket::Address actual_address;
    result = dmSocket::GetName(instance, &actual_address, &actual_port);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_EQ(address.m_family, actual_address.m_family);
    ASSERT_EQ(*dmSocket::IPv4(&address), *dmSocket::IPv4(&actual_address));

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    dmThread::Join(thread);
}

TEST(Socket, GetName_IPv6_Connected)
{
    // Setup server thread
    struct ServerThreadInfo info;
    info.listening = false;
    info.port = CONST_TEST_PORT;
    info.domain = dmSocket::DOMAIN_IPV6;
    dmThread::Thread thread = dmThread::New(&ServerThread, 0x80000, (void *) &info, "server");

    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV6);

    ASSERT_NE(-1, instance);
    dmSocket::Result result = dmSocket::RESULT_OK;
    const char* hostname = DM_LOOPBACK_ADDRESS_IPV6;
    dmSocket::Address address;
    uint16_t port = CONST_TEST_PORT;

    result = dmSocket::GetHostByName(hostname, &address, dmSocket::IsSocketIPv4(instance), dmSocket::IsSocketIPv6(instance));
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    for (int i = 0; i < 20; ++i) // Wait up to 2 seconds for the Server thread to kick in
    {
        if (!info.listening)
            dmTime::Sleep(100000); // 100 ms
    }

    dmTime::Sleep(500000);
    ASSERT_TRUE(info.listening);

    result = dmSocket::Connect(instance, address, port);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    uint16_t actual_port = 0;
    dmSocket::Address actual_address;
    result = dmSocket::GetName(instance, &actual_address, &actual_port);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_EQ(address.m_family, actual_address.m_family);
    ASSERT_EQ(address.m_address[0], actual_address.m_address[0]);
    ASSERT_EQ(address.m_address[1], actual_address.m_address[1]);
    ASSERT_EQ(address.m_address[2], actual_address.m_address[2]);
    ASSERT_EQ(address.m_address[3], actual_address.m_address[3]);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    dmThread::Join(thread);
}

TEST(Socket, SetBlocking_IPv4)
{
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV4);
    ASSERT_NE(-1, instance);
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::SetBlocking(instance, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::SetBlocking(instance, false);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, SetBlocking_IPv6)
{
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV6);
    ASSERT_NE(-1, instance);
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::SetBlocking(instance, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::SetBlocking(instance, false);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, SetNoDelay_IPv4)
{
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV4);
    ASSERT_NE(-1, instance);
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::SetNoDelay(instance, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::SetNoDelay(instance, false);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, SetNoDelay_IPv6)
{
    dmSocket::Socket instance = GetSocket(dmSocket::DOMAIN_IPV6);
    ASSERT_NE(-1, instance);
    dmSocket::Result result = dmSocket::RESULT_OK;

    result = dmSocket::SetNoDelay(instance, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::SetNoDelay(instance, false);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, SetTimeout_IPv4)
{
    dmSocket::Socket instance = 0;
    dmSocket::Result result = dmSocket::RESULT_OK;
    uint64_t timeout = 2000;

    result = dmSocket::New(dmSocket::DOMAIN_IPV4, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::SetSendTimeout(instance, timeout);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::SetReceiveTimeout(instance, timeout);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, SetTimeout_IPv6)
{
    dmSocket::Socket instance = 0;
    dmSocket::Result result = dmSocket::RESULT_OK;
    uint64_t timeout = 2000;

    result = dmSocket::New(dmSocket::DOMAIN_IPV6, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::SetSendTimeout(instance, timeout);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    result = dmSocket::SetReceiveTimeout(instance, timeout);
    ASSERT_EQ(dmSocket::RESULT_OK, result);

    // Teardown
    result = dmSocket::Delete(instance);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
}

TEST(Socket, AddressToIPString_IPv4)
{
    dmSocket::Address address;
    address.m_family = dmSocket::DOMAIN_IPV4;
    address.m_address[3] = 0x0100007f; // 127.0.0.1 in network order

    char* actual = dmSocket::AddressToIPString(address);
    ASSERT_EQ(9u, strlen(actual));
    ASSERT_EQ(0, memcmp(DM_LOOPBACK_ADDRESS_IPV4, actual, 9));

    // Teardown
    free(actual);
}

TEST(Socket, AddressToIPString_IPv6_Empty)
{
    dmSocket::Address address;
    address.m_family = dmSocket::DOMAIN_IPV6;
    address.m_address[0] = 0x00000000;
    address.m_address[1] = 0x00000000;
    address.m_address[2] = 0x00000000;
    address.m_address[3] = 0x00000000;

    char* actual = dmSocket::AddressToIPString(address);
    ASSERT_EQ(2, strlen(actual));
    ASSERT_EQ(0, memcmp(DM_UNIVERSAL_BIND_ADDRESS_IPV6, actual, 2));

    // Teardown
    free(actual);
}

TEST(Socket, AddressToIPString_IPv6_Localhost)
{
    dmSocket::Address address;
    address.m_family = dmSocket::DOMAIN_IPV6;

    address.m_address[0] = 0x00000000;
    address.m_address[1] = 0x00000000;
    address.m_address[2] = 0x00000000;
    address.m_address[3] = 0x01000000;

    char* actual = dmSocket::AddressToIPString(address);
    ASSERT_EQ(3, strlen(actual));
    ASSERT_EQ(0, memcmp(DM_LOOPBACK_ADDRESS_IPV6, actual, 3));

    // Teardown
    free(actual);
}

TEST(Socket, AddressToIPString_IPv6_FullAddress)
{
    dmSocket::Address address;
    address.m_family = dmSocket::DOMAIN_IPV6;

    address.m_address[0] = 0xd0410120;
    address.m_address[1] = 0xade80800;
    address.m_address[2] = 0x00000000;
    address.m_address[3] = 0x01000000;

    char* actual = dmSocket::AddressToIPString(address);
    ASSERT_EQ(19, strlen(actual));
    ASSERT_EQ(0, memcmp("2001:41d0:8:e8ad::1", actual, 19));

    // Teardown
    free(actual);
}

TEST(Socket, GetHostByName_IPv4_Localhost)
{
    dmSocket::Address address;
    dmSocket::Result result = dmSocket::RESULT_OK;
    const char* hostname = DM_LOOPBACK_ADDRESS_IPV4;

    result = dmSocket::GetHostByName(hostname, &address, true, false);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_EQ(dmSocket::DOMAIN_IPV4, address.m_family);
    ASSERT_EQ(0x0100007f, *dmSocket::IPv4(&address));
}

TEST(Socket, GetHostByName_IPv6_Localhost)
{
    dmSocket::Address address;
    dmSocket::Result result = dmSocket::RESULT_OK;
    const char* hostname = DM_LOOPBACK_ADDRESS_IPV6;

    result = dmSocket::GetHostByName(hostname, &address, false, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_EQ(dmSocket::DOMAIN_IPV6, address.m_family);
    ASSERT_EQ(0x00000000, address.m_address[0]);
    ASSERT_EQ(0x00000000, address.m_address[1]);
    ASSERT_EQ(0x00000000, address.m_address[2]);
    ASSERT_EQ(0x01000000, address.m_address[3]);
}

TEST(Socket, GetHostByName_IPv4_External)
{
    dmSocket::Address address;
    dmSocket::Result result = dmSocket::RESULT_OK;
    const char* hostname = "build.defold.com";

    result = dmSocket::GetHostByName(hostname, &address, true, false);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_EQ(dmSocket::DOMAIN_IPV4, address.m_family);
}

TEST(Socket, GetHostByName_IPv6_External)
{
#if !defined(_WIN32)
    dmSocket::Address address;
    dmSocket::Result result = dmSocket::RESULT_OK;
    const char* hostname = "ipv6-test.com";

    result = dmSocket::GetHostByName(hostname, &address, false, true);
    ASSERT_EQ(dmSocket::RESULT_OK, result);
    ASSERT_EQ(dmSocket::DOMAIN_IPV6, address.m_family);
#else
    printf("[   INFO   ] Test for GetHostByName/IPv6 is disabled on Windows.\n");
    ASSERT_TRUE(true);
#endif
}

TEST(Socket, GetHostByName_IPv4_Unavailable)
{
    dmSocket::Address address;
    dmSocket::Result result = dmSocket::RESULT_OK;
    const char* hostname = "localhost.invalid";

    result = dmSocket::GetHostByName(hostname, &address, true, false);
    ASSERT_EQ(dmSocket::RESULT_HOST_NOT_FOUND, result);
}

TEST(Socket, GetHostByName_IPv6_Unavailable)
{
    dmSocket::Address address;
    dmSocket::Result result = dmSocket::RESULT_OK;
    const char* hostname = "localhost.invalid";

    result = dmSocket::GetHostByName(hostname, &address, false, true);
    ASSERT_EQ(dmSocket::RESULT_HOST_NOT_FOUND, result);
}

TEST(Socket, GetHostByName_NoValidAddressFamily)
{
    dmSocket::Address address;
    dmSocket::Result result = dmSocket::RESULT_OK;
    const char* hostname = "localhost";

    result = dmSocket::GetHostByName(hostname, &address, false, false);
    ASSERT_EQ(dmSocket::RESULT_HOST_NOT_FOUND, result);
}

TEST(Socket, ServerSocketIPv4)
{
    dmSocket::Socket socket;
    dmSocket::Result r = dmSocket::New(dmSocket::DOMAIN_IPV4, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &socket);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    const int port = 9003;

    dmSocket::Address bindaddress;
    r = dmSocket::GetHostByName(DM_UNIVERSAL_BIND_ADDRESS_IPV4, &bindaddress, true, false);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Bind(socket, bindaddress, port);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Listen(socket, 1000);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Delete(socket);
    ASSERT_EQ(dmSocket::RESULT_OK, r);
}

TEST(Socket, ServerSocketIPv6)
{
    dmSocket::Socket socket;
    dmSocket::Result r = dmSocket::New(dmSocket::DOMAIN_IPV6, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &socket);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    const int port = 9003;

    dmSocket::Address bindaddress;
    r = dmSocket::GetHostByName(DM_UNIVERSAL_BIND_ADDRESS_IPV6, &bindaddress, false, true);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Bind(socket, bindaddress, port);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Listen(socket, 1000);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Delete(socket);
    ASSERT_EQ(dmSocket::RESULT_OK, r);
}

TEST(Socket, ServerSocketIPv4_MultipleBind)
{
    dmSocket::Socket socket1, socket2;
    dmSocket::Result r;
    r = dmSocket::New(dmSocket::DOMAIN_IPV4, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &socket1);
    ASSERT_EQ(dmSocket::RESULT_OK, r);
    r = dmSocket::New(dmSocket::DOMAIN_IPV4, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &socket2);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    const int port = 9003;

    dmSocket::Address bindaddress1;
    r = dmSocket::GetHostByName(DM_UNIVERSAL_BIND_ADDRESS_IPV4, &bindaddress1, true, false);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    dmSocket::Address bindaddress2;
    r = dmSocket::GetHostByName(DM_UNIVERSAL_BIND_ADDRESS_IPV4, &bindaddress2, true, false);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Bind(socket1, bindaddress1, port);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Bind(socket2, bindaddress2, port);
    ASSERT_EQ(dmSocket::RESULT_ADDRINUSE, r);

    r = dmSocket::Delete(socket1);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Delete(socket2);
    ASSERT_EQ(dmSocket::RESULT_OK, r);
    ASSERT_EQ(dmSocket::RESULT_OK, r);
}

TEST(Socket, ServerSocketIPv6_MultipleBind)
{
    dmSocket::Socket socket1, socket2;
    dmSocket::Result r;
    r = dmSocket::New(dmSocket::DOMAIN_IPV6, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &socket1);
    ASSERT_EQ(dmSocket::RESULT_OK, r);
    r = dmSocket::New(dmSocket::DOMAIN_IPV6, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &socket2);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    const int port = 9003;

    dmSocket::Address bindaddress1;
    r = dmSocket::GetHostByName(DM_UNIVERSAL_BIND_ADDRESS_IPV6, &bindaddress1, false, true);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    dmSocket::Address bindaddress2;
    r = dmSocket::GetHostByName(DM_UNIVERSAL_BIND_ADDRESS_IPV6, &bindaddress2, false, true);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Bind(socket1, bindaddress1, port);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Bind(socket2, bindaddress2, port);
    ASSERT_EQ(dmSocket::RESULT_ADDRINUSE, r);

    r = dmSocket::Delete(socket1);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Delete(socket2);
    ASSERT_EQ(dmSocket::RESULT_OK, r);
}

TEST(Socket, ServerSocketIPv4_Accept)
{
    dmSocket::Socket socket;
    dmSocket::Result r = dmSocket::New(dmSocket::DOMAIN_IPV4, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &socket);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::SetBlocking(socket, false);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    const int port = 9003;

    dmSocket::Address bindaddress;
    r = dmSocket::GetHostByName(DM_UNIVERSAL_BIND_ADDRESS_IPV4, &bindaddress, true, false);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Bind(socket, bindaddress, port);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Listen(socket, 1000);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    dmSocket::Address address;
    dmSocket::Socket client_socket;
    r = dmSocket::Accept(socket, &address, &client_socket);
    ASSERT_EQ(dmSocket::RESULT_WOULDBLOCK, r);

    r = dmSocket::Delete(socket);
    ASSERT_EQ(dmSocket::RESULT_OK, r);
}

TEST(Socket, ServerSocketIPv6_Accept)
{
    dmSocket::Socket socket;
    dmSocket::Result r = dmSocket::New(dmSocket::DOMAIN_IPV6, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &socket);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::SetBlocking(socket, false);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    const int port = 9003;

    dmSocket::Address bindaddress;
    r = dmSocket::GetHostByName(DM_UNIVERSAL_BIND_ADDRESS_IPV6, &bindaddress, false, true);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Bind(socket, bindaddress, port);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Listen(socket, 1000);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    dmSocket::Address address;
    dmSocket::Socket client_socket;
    r = dmSocket::Accept(socket, &address, &client_socket);
    ASSERT_EQ(dmSocket::RESULT_WOULDBLOCK, r);

    r = dmSocket::Delete(socket);
    ASSERT_EQ(dmSocket::RESULT_OK, r);
}

static void PrintFlags(uint32_t f) {
    if (f & dmSocket::FLAGS_UP) {
        printf("UP ");
    }
    if (f & dmSocket::FLAGS_RUNNING) {
        printf("RUNNING ");
    }
}

TEST(Socket, GetIfAddrs)
{
    uint32_t count;
    dmSocket::GetIfAddresses(0, 0, &count);
    ASSERT_EQ(0U, count);

    dmSocket::IfAddr as[16];
    dmSocket::GetIfAddresses(as, 16, &count);

    for (uint32_t i = 0; i < count; ++i) {
        const dmSocket::IfAddr& a = as[i];
        printf("%s ", a.m_Name);

        if (a.m_Flags & dmSocket::FLAGS_LINK) {
            printf("LINK %02x:%02x:%02x:%02x:%02x:%02x ",
                    a.m_MacAddress[0],a.m_MacAddress[1],a.m_MacAddress[2],a.m_MacAddress[3],a.m_MacAddress[4],a.m_MacAddress[5]);
        }

        if (a.m_Flags & dmSocket::FLAGS_INET) {
            char* straddr = dmSocket::AddressToIPString(a.m_Address);
            printf("INET %s ", straddr);
            free((void*) straddr);
        }

        PrintFlags(a.m_Flags);
        printf("\n");
    }
}

TEST(Socket, Timeout)
{
    const uint64_t timeout = 50 * 1000;
    dmSocket::Socket server_socket;
    dmSocket::Result r = dmSocket::New(dmSocket::DOMAIN_IPV6, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &server_socket); // This has to be rewritten
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::SetReuseAddress(server_socket, true);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    dmSocket::Address bindaddress;
    r = dmSocket::GetHostByName(DM_UNIVERSAL_BIND_ADDRESS_IPV6, &bindaddress, false, true);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

	r = dmSocket::Bind(server_socket, bindaddress, 0);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Listen(server_socket, 1000);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    uint16_t port;
    dmSocket::Address address;
    r = dmSocket::GetName(server_socket, &address, &port); // We do this to get the port
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::GetHostByName(DM_LOOPBACK_ADDRESS_IPV6, &address, false, true); // We do this to get the address
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    dmSocket::Socket client_socket;
    r = dmSocket::New(dmSocket::DOMAIN_IPV6, dmSocket::TYPE_STREAM, dmSocket::PROTOCOL_TCP, &client_socket); // This has to be rewritten
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::SetReceiveTimeout(client_socket, timeout);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::SetSendTimeout(client_socket, timeout);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    r = dmSocket::Connect(client_socket, address, port);
    ASSERT_EQ(dmSocket::RESULT_OK, r);

    int received;
    char buf[4096];
    memset(buf, 0, sizeof(buf));

    for (int i = 0; i < 10; ++i) {
        uint64_t start = dmTime::GetTime();
        r = dmSocket::Receive(client_socket, buf, sizeof(buf), &received);
        uint64_t end = dmTime::GetTime();
        ASSERT_EQ(dmSocket::RESULT_WOULDBLOCK, r);
        ASSERT_GE(end - start, timeout - 2500); // NOTE: Margin of 2500. Required on Linux
    }

    for (int i = 0; i < 10; ++i) {
        uint64_t start = dmTime::GetTime();
        for (int j = 0; j < 10000; ++j) {
            // Loop to ensure that we fill send buffers
            r = dmSocket::Send(client_socket, buf, sizeof(buf), &received);
            if (r != dmSocket::RESULT_OK) {
                break;
            }
        }
        uint64_t end = dmTime::GetTime();
        ASSERT_EQ(dmSocket::RESULT_WOULDBLOCK, r);
        ASSERT_GE(end - start, timeout - 2500); // NOTE: Margin of 2500. Required on Linux
    }

    dmSocket::Delete(server_socket);
    dmSocket::Delete(client_socket);
}

int main(int argc, char **argv)
{
    dmSocket::Initialize();
    jc_test_init(&argc, argv);
    int ret = jc_test_run_all();
    dmSocket::Finalize();
    return ret;
}
