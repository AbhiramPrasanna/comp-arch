#include <netdb.h>
#include <unistd.h>

#include "log/log.hpp"
#include "measure/measure.hpp"

#include "socket/socket-basic.hpp"
#include "socket/socket-connection.hpp"

namespace sockets {

bool SocketConnection::client_connect(const std::string& remote_addr_port){
    if (this->inited) return false;
    std::string server_name = remote_addr_port.substr(0, remote_addr_port.find(":"));
    std::string remote_port = remote_addr_port.substr(remote_addr_port.find(":") + 1);
    return this->client_connect(server_name, std::stoi(remote_port));
}

bool SocketConnection::client_connect(const std::string& remote_addr, int remote_port){
    if (this->inited) return false;

    log_debug << "connect to " << remote_addr << ":" << remote_port << std::endl;
    this->sock = sock_client_connect(remote_addr.c_str(), remote_port);
    if (this->sock < 0) {
        log_error << "failed to establish TCP connection" << std::endl;
        return false;
    }
    log_debug << "init (connect) " << this->sock << std::endl;
    this->inited = true;

    return true;
}

bool SocketConnection::sock_send_data(int send_size, const char* data) {
    if (!this->inited) return false;
    return sockets::sock_send_data(this->sock, send_size, data);
}

bool SocketConnection::sock_read_data(int read_size, char* data) {
    if (!this->inited) return false;
    return sockets::sock_read_data(this->sock, read_size, data);
}

bool SocketConnection::sock_send_u32(const uint32_t u32_data) {

    if (!this->inited) return false;

    int iresult;
    bool result;

    uint32_t tcp_send_u32_data = htonl(u32_data);
    result = this->sock_send_data(sizeof(uint32_t), (char*)&tcp_send_u32_data);

    return result;
}

bool SocketConnection::sock_send_u64(const uint64_t u64_data) {

    if (!this->inited) return false;

    int iresult;
    bool result;

    uint64_t tcp_send_u64_data = htonll(u64_data);
    result = this->sock_send_data(sizeof(uint64_t), (char*)&tcp_send_u64_data);

    return result;
}

bool SocketConnection::sock_send_double(const double double_data) {

    if (!this->inited) return false;

    int iresult;
    bool result;

    uint64_t u64_data = *((uint64_t*)&double_data);
    uint64_t tcp_send_u64_data = htonll(u64_data);
    result = this->sock_send_data(sizeof(uint64_t), (char*)&tcp_send_u64_data);

    return result;
}

bool SocketConnection::sock_read_u32(uint32_t& u32_data) {

    if (!this->inited) return false;

    int iresult;
    bool result;

    uint32_t tcp_read_u32_data;
    result = this->sock_read_data(sizeof(uint32_t), (char*)&tcp_read_u32_data);
    u32_data = ntohl(tcp_read_u32_data);

    return result;
}

bool SocketConnection::sock_read_u64(uint64_t& u64_data) {

    if (!this->inited) return false;

    int iresult;
    bool result;

    uint64_t tcp_read_u64_data;
    result = this->sock_read_data(sizeof(uint64_t), (char*)&tcp_read_u64_data);
    u64_data = ntohll(tcp_read_u64_data);

    return result;
}

bool SocketConnection::sock_read_double(double& double_data) {

    if (!this->inited) return false;

    int iresult;
    bool result;

    uint64_t tcp_read_u64_data;
    result = this->sock_read_data(sizeof(uint64_t), (char*)&tcp_read_u64_data);
    uint64_t u64_data = ntohll(tcp_read_u64_data);
    double_data = *((double*)&u64_data);

    return result;
}

bool SocketConnection::sock_sync_data(int xfer_size, const char* local_data, char* remote_data) {
    if (!this->inited) return false;
    return sockets::sock_sync_data(this->sock, xfer_size, local_data, remote_data);
}

bool SocketConnection::disconnect() {
    if (!this->inited) return false;
    int iresult = 0;

    log_debug << "disconnect (connet/listen) " << this->sock << std::endl;
    iresult = close(this->sock);
    if (iresult != 0) {
        log_error << "error: " << iresult << std::endl;
        return false;
    }
    this->inited = false;

    return true;
}

bool SocketConnection::from_server_listen(int sock) {
    if (this->inited) return false;

    this->sock = sock;
    this->inited = true;

    return true;
}



bool SocketServerConnection::server_init(const std::string& this_addr_port) {
    if (this->inited) return true;
    if (this_addr_port.find(":") == str::npos)
        return this->server_init(std::stoi(this_addr_port));
    std::string port = this_addr_port.substr(this_addr_port.find(":") + 1);
    return this->server_init(std::stoi(port));
}

bool SocketServerConnection::server_init(int this_port) {
    if (this->inited) return true;

    log_debug << "init at port " << this_port << std::endl;
    this->sock = sock_server_init(this_port);
    if (this->sock < 0) {
        log_error << "failed to establish TCP binding" << std::endl;
        return false;
    }
    log_debug << "init (bind) " << this->sock << std::endl;
    this->inited = true;

    return true;
}

bool SocketServerConnection::server_listen() {
    if (!this->inited) return false;

    log_debug << "listening..." << std::endl;
    int recv_sock = sock_server_listen(this->sock);
    if (recv_sock < 0) {
        log_error << "failed to establish TCP listening" << std::endl;
        return false;
    }
    log_debug << "init (listen) " << recv_sock << std::endl;
    // this may call the ~SocketConnection() of all previous member (for vector resize)
    sock_list.emplace_back();
    sock_list.back().from_server_listen(recv_sock);
    log_debug << "now listen list num = " << sock_list.size() << std::endl;

    return true;
}

SocketConnection& SocketServerConnection::get_latest_socket_connetion() {
    return sock_list.back();
}

SocketConnection& SocketServerConnection::get_socket_connetion(const int num) {
    return sock_list[num];
}

bool SocketServerConnection::disconnect() {
    if (!this->inited) return false;
    int iresult = 0;

    log_debug << "disconnect (bind) " << this->sock << std::endl;
    iresult = close(this->sock);
    if (iresult != 0) {
        log_error << "error: " << iresult << std::endl;
        return false;
    }
    for (auto& i : this->sock_list) i.disconnect();
    this->inited = false;

    log_debug << "all done" << std::endl;
    return true;
}

}