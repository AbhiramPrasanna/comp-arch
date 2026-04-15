#pragma once

#include "measure/measure.hpp"

namespace sockets {

class SocketConnection;

// This `SocketServerConnection` is a client-server socket architecture for server.
// .-----.
// Use the listen port as a parameter of `.server_init()` to bind to a port.
// Each time using `server_listen()` and returning successfully,
// the SocketServerConnection will create a new SocketConnection,
// and you can get the new SocketConnection by using get_latest_socket_connetion()
// Remember to use `.disconnect()` when you finish.

class SocketServerConnection {

public:
    SocketServerConnection() = default;
    ~SocketServerConnection() = default;

    // Parameter examples: "9898", "0.0.0.0::9898", "anything::9898" (data before : will be ignored).
    // This will **return false** if failed.
    bool server_init(const std::string& this_addr_port);
    // Parameter example: 9898.
    // This will **return false** if failed.
    bool server_init(int this_port);
    // This will **be blocked** to wait for the client.
    // This will **return false** if got a client but failed to connect with.
    bool server_listen();
    // This may **throw error** if there's no socket connection
    SocketConnection& get_latest_socket_connetion();
    // This may **throw error** if num is out of range connection
    SocketConnection& get_socket_connetion(const int num);
    // This will **return false** if failed.
    bool disconnect();

private:
    int sock = -1;
    bool inited = false;

public:
    vec<SocketConnection> sock_list;

};

// This `SocketConnection` is a client-server socket architecture for clients.
// .-----.
// Use the server's `addr:port` (listen port) as a parameter of `.client_connect()` to connet to a server.
// Use `.sock_sync_data()` to sync data between server and client.
// Remember to use `.disconnect()` when you finish.

class SocketConnection {

public:
    SocketConnection() = default;
    ~SocketConnection() = default;

    // Parameter example: "192.168.0.5:9898".
    // This will **return false** if failed, including there's no server
    bool client_connect(const std::string& remote_addr_port);
    // Parameter example: ("192.168.0.5", "9898).
    // This will **return false** if failed, including there's no server
    bool client_connect(const std::string& remote_addr, int remote_port);
    // This will **return false** if failed.
    bool sock_send_data(int send_size, const char* data);
    // This will **return false** if failed.
    bool sock_read_data(int read_size, char* data);
    // This will **return false** if failed.
    bool sock_send_u32(const uint32_t u32_data);
    // This will **return false** if failed.
    bool sock_send_u64(const uint64_t u64_data);
    // This will **return false** if failed.
    bool sock_send_double(const double double_data);
    // This will **return false** if failed.
    bool sock_read_u32(uint32_t& u32_data);
    // This will **return false** if failed.
    bool sock_read_u64(uint64_t& u64_data);
    // This will **return false** if failed.
    bool sock_read_double(double& double_data);
    // This will **return false** if failed.
    bool sock_sync_data(int xfer_size, const char* local_data, char* remote_data);
    // This will **return false** if failed.
    bool disconnect();

private:
    bool from_server_listen(int sock);
    friend bool SocketServerConnection::server_listen();
    int sock = -1;
    bool inited = false;
};

}