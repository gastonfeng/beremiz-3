#if defined(USE_LWIP) || defined(windows_x86) || defined(SYLIXOS)
#ifdef windows_x86
#include <ws2tcpip.h>
#define MSG_DONTWAIT 0x0
#endif
#ifdef RTE_APP
#include "plc_rte.h"
#endif
#include "socketFirmata.h"
#include "rtos.h"
#include"logger_rte.h"
#ifdef USE_LWIP

#include "lwip/tcpip.h"
#include "lwip/sockets.h"

#endif


#ifdef SYLIXOS

#include "lwip/tcpip.h"
#include "lwip/sockets.h"

#define closesocket close
#endif

#include <csignal>

#ifdef windows_x86
#include <ws2tcpip.h>
#define MSG_DONTWAIT 0x0
#endif
#define MAX_CLIENTS 3

#define DATA_MAXSIZE 512
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif
#ifndef windows_x86
extern "C" const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt);
#endif
typedef struct
{
    int socket{};
    struct sockaddr_in addres;

    /* The same for the receiving message. */
    char receiving_buffer[DATA_MAXSIZE] {};
    size_t current_receiving_byte{};
} peer_t;
peer_t connection_list[MAX_CLIENTS] {};
peer_t *cur_peer{};
u16 rxinx{};
int listen_sock{};

char *peer_get_addres_str(peer_t *peer)
{
    static char ret[INET_ADDRSTRLEN + 10];
#ifndef windows_x86
    char peer_ipv4_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer->addres.sin_addr, peer_ipv4_str, INET_ADDRSTRLEN);
    sprintf(ret, "%s:%d", peer_ipv4_str, peer->addres.sin_port);
#endif
    return ret;
}

int create_peer(peer_t *peer)
{
    peer->current_receiving_byte = 0;

    return 0;
}

int socketFirmata::begin(mFirmata *fm)
{
    firm = fm;

    rtos::create_thread_run("socketFirmata", 768, PriorityNormal, (void *) &socketFirmata::thread, this);
    return 0;
}

#undef write
#undef read

size_t socketFirmata::write(u8 c)
{
    txbuf.push_back(c);
    return 1;
}

int socketFirmata::available()
{
    if (cur_peer)
        return cur_peer->current_receiving_byte;
    return 0;
}

int socketFirmata::read()
{
    if (cur_peer)
    {
        cur_peer->current_receiving_byte--;
        return cur_peer->receiving_buffer[rxinx++];
    }
    return 0;
}

int socketFirmata::peek()
{
    if (cur_peer)
        return cur_peer->receiving_buffer[rxinx];
    return 0;
}

int socketFirmata::receive_from_peer(void *p)
{
    //    logger.debug("Ready for recv() from %s.\n", peer_get_addres_str(peer));
    peer_t *peer = (peer_t *) p;
    size_t len_to_receive;
    ssize_t received_count;
    size_t received_total = 0;
    len_to_receive = sizeof(peer->receiving_buffer) - peer->current_receiving_byte;

    // logger.error("Let's try to recv() %zd bytes... ", len_to_receive);
    received_count = recv(peer->socket, (char *)&peer->receiving_buffer + peer->current_receiving_byte,
                          len_to_receive, MSG_DONTWAIT);
    if (received_count < 0)
    {
        logger.error("Failed recv %d %d,%s", received_count, errno, strerror(errno));
    }
    // If recv() returns 0, it means that peer gracefully shutdown. Shutdown client.
    else if (received_count == 0)
    {
        logger.error("recv() 0 bytes. Peer gracefully shutdown.\n");
        return -1;
    }
    else if (received_count > 0)
    {
        peer->current_receiving_byte += received_count;
        received_total += received_count;
        // logger.debug("recv() %zd bytes\n", received_count);
        rxinx = 0;
        cur_peer = peer;
        socketFirmata::handle_received_message();
        peer->current_receiving_byte = 0;
    }
    return 0;
}

/* Start listening socket sock. */
int socketFirmata::start_listen_socket(int *sock)
{
    // Obtain a file descriptor for our "listening" socket.
    *sock = socket(AF_INET, SOCK_STREAM, 0);
    if (*sock < 0)
    {
        logger.error("socket %d", errno);
        return -1;
    }
#ifndef ARDUINO
    int reuse = 1;
    if (setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) != 0)
    {
        logger.error("setsockopt");
        return -1;
    }
#endif
    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
#ifdef RTE_APP
    my_addr.sin_port = plc_var.info.debug_port;
#else
    my_addr.sin_port = 0;
#endif
    if (bind(*sock, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) != 0)
    {
logger.error("bind %d", errno);
        return -1;
    }
    socklen_t len = sizeof(my_addr);
    if (getsockname(*sock, (struct sockaddr *)&my_addr, &len) == -1)
    {
logger.error("getsockname");
        return -2;
    }
#ifdef RTE_APP
    plc_var.info.debug_port = ntohs(my_addr.sin_port);
#endif
    // start accept client connections
    if (listen(*sock, 10) != 0)
    {
logger.error("listen");
        return -1;
    }
#ifdef RTE_APP
logger.info("Accepting connections on port %d.\n", (int)plc_var.info.debug_port);
#endif
    return 0;
}

void socketFirmata::shutdown_properly(int code)
{

    closesocket(listen_sock);

    for (auto &i : connection_list)
        if (i.socket != -1)
            closesocket(i.socket);

    logger.error("Shutdown server properly.\n");
}

int build_fd_sets(fd_set *read_fds, fd_set *write_fds, fd_set *except_fds)
{
    int i;

    memset(read_fds, 0, sizeof(fd_set));
    FD_SET(listen_sock, read_fds);
    for (i = 0; i < MAX_CLIENTS; ++i)
        if (connection_list[i].socket != -1)
            FD_SET(connection_list[i].socket, read_fds);

    memset(write_fds, 0, sizeof(fd_set));
    memset(except_fds, 0, sizeof(fd_set));
    FD_SET(listen_sock, except_fds);
    for (i = 0; i < MAX_CLIENTS; ++i)
        if (connection_list[i].socket != -1)
            FD_SET(connection_list[i].socket, except_fds);

    return 0;
}

int socketFirmata::handle_new_connection()
{
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t client_len = sizeof(client_addr);
    int new_client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
    if (new_client_sock < 0)
    {
        logger.error("accept error=%d", errno);
        return -1;
    }
    int optval = 1;
#ifndef windows_x86
    setsockopt(new_client_sock, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    char client_ipv4_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ipv4_str, INET_ADDRSTRLEN);

    logger.debug("Incoming connection from %s:%d.\n", client_ipv4_str, client_addr.sin_port);
#endif

    for (auto &i : connection_list)
    {
        if (i.socket == -1)
        {
            i.socket = new_client_sock;
            i.addres = client_addr;
            i.current_receiving_byte = 0;
            return 0;
        }
    }
#ifndef windows_x86
    logger.error("There is too much connections. Close new connection %s:%d.\n", client_ipv4_str,
                 client_addr.sin_port);
#endif
    closesocket(new_client_sock);
    return -1;
}

int close_client_connection(peer_t *client)
{
    logger.debug("Close client socket for %s.\n", peer_get_addres_str(client));
    closesocket(client->socket);
    client->socket = -1;
    client->current_receiving_byte = 0;
    return 0;
}

int socketFirmata::handle_received_message()
{
    while (available())
        firm->processInput(this);
    return 0;
}

int socketFirmata::loop()
{
    fd_set read_fds;
    fd_set write_fds;
    fd_set except_fds;
#ifdef windows_x86
    WSADATA wsaData;

    int iResult;

    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        logger.error("WSAStartup failed: %d\n", iResult);
        return -1;
    }
#endif
    if (start_listen_socket(&listen_sock) != 0)
        return -2;

    for (auto &i : connection_list)
    {
        i.socket = -1;
        create_peer(&i);
    }
    logger.debug("Waiting for incoming connections.\n");
    while (true)
    {

        int high_sock = listen_sock;
        build_fd_sets(&read_fds, &write_fds, &except_fds);

        high_sock = listen_sock;
        for (auto &i : connection_list)
        {
            if (i.socket > high_sock)
                high_sock = i.socket;
        }

        int activity = select(high_sock + 1, &read_fds, &write_fds, &except_fds, nullptr);

        switch (activity)
        {
        case -1:
logger.error("select()");
            shutdown_properly(EXIT_FAILURE);
            break;
        case 0:
            // you should never get here
            logger.error("select() returns 0.\n");
            shutdown_properly(EXIT_FAILURE);
            break;
        default:

            if (FD_ISSET(listen_sock, &read_fds))
            {
                handle_new_connection();
            }

            if (FD_ISSET(listen_sock, &except_fds))
            {
                logger.error("Exception listen socket fd.\n");
                shutdown_properly(EXIT_FAILURE);
            }

            for (auto &i : connection_list)
            {
                if (i.socket != -1 && FD_ISSET(i.socket, &read_fds) && receive_from_peer(&i) != 0)
                {
                    close_client_connection(&i);
                    continue;
                }

                if (i.socket != -1 && FD_ISSET(i.socket, &except_fds))
                {
                    logger.error("Exception client fd.\n");
                    close_client_connection(&i);
                    continue;
                }
            }
        }
    }
}

void socketFirmata::flush()
{
    if (cur_peer)
        send(cur_peer->socket, (const char *)txbuf.data(), txbuf.size(), 0);
    txbuf.clear();
}
#if defined(RTE_APP) || defined(PLC)
void socketFirmata::report()
{
    firm->report(this);
}
#endif
int socketFirmata::begin(u32 tick)
{
    return begin(&ifirmata);
}

int socketFirmata::run(u32 tick)
{
    return 0;
}

int socketFirmata::diag(u32 tick)
{
    return 0;
}
#endif
