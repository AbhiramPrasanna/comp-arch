#pragma once

#include <cstdint>
#include <byteswap.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

namespace sockets {

// return socket of the client
int sock_client_connect(const char* servername, int port);
// return socket of the server
int sock_server_init(int port);
// return socket of the new listen connection in server
int sock_server_listen(int server_socket);
// return true (succeeded) or false (failed)
bool close_socket(int socket);
// return true (succeeded) or false (failed)
bool sock_send_data(int sock, int send_size, const char* data);
// return true (succeeded) or false (failed)
bool sock_read_data(int sock, int read_size, char* data);
// return true (succeeded) or false (failed)
bool sock_sync_data(int sock, int xfer_size, const char *local_data, char *remote_data);

}