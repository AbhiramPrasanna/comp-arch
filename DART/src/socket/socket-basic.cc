#include <netdb.h>
#include <unistd.h>

#include "log/log.hpp"
#include "measure/measure.hpp"

#include "socket/socket-basic.hpp"

namespace sockets {

int sock_client_connect(const char* servername, int port) {

    int iresult = -1;
    char service[6];
    int sockfd = -1;
    struct addrinfo* iterator = nullptr;
    struct addrinfo* resolved_addr = nullptr;
    struct addrinfo hints = {
        .ai_flags = AI_PASSIVE, .ai_family = AF_INET, .ai_socktype = SOCK_STREAM
    };

    if (strlen(servername) == 0)
        goto failed;

    iresult = sprintf(service, "%d", port);
    if (iresult < 0)
        goto failed;

    /* Resolve DNS address, use sockfd as temp storage */
    sockfd = getaddrinfo(servername, service, &hints, &resolved_addr);
    if (sockfd < 0) {
        log_error << gai_strerror(sockfd) << " for " << servername << ":" << port << std::endl;
        goto failed;
    }

    /* Search through results and find the one we want */
    for (iterator = resolved_addr; iterator; iterator = iterator->ai_next) {
        sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
        if (sockfd >= 0) {
            iresult = connect(sockfd, iterator->ai_addr, iterator->ai_addrlen);
            if (iresult < 0) {
                log_error << "failed connect" << std::endl;
                close(sockfd);
                sockfd = -1;
            }
        }
    }
    if (resolved_addr) freeaddrinfo(resolved_addr);
    if (sockfd < 0) goto failed;

    return sockfd;

failed:
    log_error << "couldn't connect to " << servername << ":" << port << std::endl;
    return sockfd;
}


int sock_server_init(int port) {

    int iresult = -1;
    char service[6];
    int sockfd = -1;
    struct addrinfo* iterator = nullptr;
    struct addrinfo* resolved_addr = nullptr;
    struct addrinfo hints = {
        .ai_flags = AI_PASSIVE, .ai_family = AF_INET, .ai_socktype = SOCK_STREAM
    };

    iresult = sprintf(service, "%d", port);
    if (iresult < 0)
        goto failed;

    /* Resolve DNS address, use sockfd as temp storage */
    sockfd = getaddrinfo("0.0.0.0", service, &hints, &resolved_addr);
    if (sockfd < 0) {
        log_error << gai_strerror(sockfd) << " for port " << port << std::endl;
        goto failed;
    }

    /* Search through results and find the one we want */
    for (iterator = resolved_addr; iterator; iterator = iterator->ai_next) {
        sockfd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
        if (sockfd >= 0) {
            iresult = bind(sockfd, iterator->ai_addr, iterator->ai_addrlen);
            if (iresult < 0) {
                log_error << "failed connect" << std::endl;
                close(sockfd);
                sockfd = -1;
            }
        }
    }
    if (resolved_addr) freeaddrinfo(resolved_addr);
    if (sockfd < 0) goto failed;

    return sockfd;

failed:
    perror("sock_server_init");
    log_error << "failed" << std::endl;
    return sockfd;
}


int sock_server_listen(int server_socket) {

    int receivefd = -1;
    int iresult;

    iresult = listen(server_socket, 1);
    if (iresult)
        goto failed;

    receivefd = accept(server_socket, NULL, 0);
    if (receivefd < 0)
        goto failed;

    return receivefd;

failed:
    perror("sock_server_listen_again");
    log_error << "failed" << std::endl;
    return receivefd;
}


bool close_socket(int socket) {
    if (socket >= 0) {
        close(socket);
        return true;
    }
    return false;
}

bool sock_send_data(int sock, int send_size, const char* data) {
    int rc;
    rc = write(sock, data, send_size);
    if (rc < send_size) return false;
    return true;
}

bool sock_read_data(int sock, int read_size, char* data) {
    int rc = 0;
    int read_bytes = 0;
    int total_read_bytes = 0;
    while (!rc && total_read_bytes < read_size) {
        read_bytes = read(sock, data, read_size);
        if (read_bytes > 0)
            total_read_bytes += read_bytes;
        else
            rc = read_bytes;
    }
    if (rc != 0) return false;
    return true;
}

bool sock_sync_data(int sock, int xfer_size, const char* local_data, char* remote_data) {
    int rc;
    int read_bytes = 0;
    int total_read_bytes = 0;
    rc = write(sock, local_data, xfer_size);
    if (rc < xfer_size)
        log_error << "failed writing data" << std::endl;
    else
        rc = 0;
    while (!rc && total_read_bytes < xfer_size) {
        read_bytes = read(sock, remote_data, xfer_size);
        if (read_bytes > 0)
            total_read_bytes += read_bytes;
        else
            rc = read_bytes;
    }
    if (rc != 0) return false;
    return true;
}

}