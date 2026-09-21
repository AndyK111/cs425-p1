#define _POSIX_C_SOURCE 200809L
#include "socket_transport.h"
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef TEST
#include "../tests/test-hooks.h"
#endif

// #region Functions

/* Private helper declarations; definitions follow the public functions. */
static int configure_smtp_socket(int socket_descriptor);

int connect_smtp_socket(smtp_socket_t *socket_state, const char *server_hostname,
                        const char *server_port, char *error_message,
                        size_t error_capacity)
{
    if (error_message == NULL || error_capacity == 0) return 1;
    error_message[0] = '\0';
    if (socket_state == NULL || server_hostname == NULL || server_port == NULL
        || server_hostname[0] == '\0' || server_port[0] == '\0')
    {
        snprintf(error_message, error_capacity, "A socket, server hostname, and port are required.");
        return 1;
    }

    if (socket_state->socket_descriptor != -1)
    {
        snprintf(error_message, error_capacity, "Close the existing socket before reconnecting.");
        return 1;
    }

    struct addrinfo address_hints;
    memset(&address_hints, 0, sizeof(address_hints));
    address_hints.ai_family = AF_UNSPEC;
    address_hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *server_addresses = NULL;
    int lookup_result = getaddrinfo(server_hostname, server_port, &address_hints,
                                    &server_addresses);
    if (lookup_result != 0)
    {
        snprintf(error_message, error_capacity, "Cannot resolve %s:%s: %s",
                 server_hostname, server_port, gai_strerror(lookup_result));
        return 1;
    }

    int connection_error = ECONNREFUSED;
    for (struct addrinfo *server_address = server_addresses; server_address != NULL;
         server_address = server_address->ai_next)
    {
        int socket_descriptor = socket(server_address->ai_family,
                                       server_address->ai_socktype,
                                       server_address->ai_protocol);
        if (socket_descriptor < 0)
        {
            connection_error = errno;
            continue;
        }

        if (configure_smtp_socket(socket_descriptor) != 0
            || connect(socket_descriptor, server_address->ai_addr,
                       server_address->ai_addrlen) != 0)
        {
            connection_error = errno;
            close(socket_descriptor);
            continue;
        }

        socket_state->socket_descriptor = socket_descriptor;
        break;
    }

    freeaddrinfo(server_addresses);
    if (socket_state->socket_descriptor == -1)
    {
        snprintf(error_message, error_capacity, "Cannot connect to %s:%s: %s",
                 server_hostname, server_port, strerror(connection_error));
        return 1;
    }

    return 0;
}

ssize_t read_smtp_socket(void *context, void *read_buffer, size_t buffer_capacity)
{
    smtp_socket_t *socket_state = context;
    if (socket_state == NULL || read_buffer == NULL || socket_state->socket_descriptor < 0)
    {
        errno = EINVAL;
        return -1;
    }

    ssize_t received_length;
    do
    {
        received_length = recv(socket_state->socket_descriptor, read_buffer,
                               buffer_capacity, 0);
    }
    while (received_length < 0 && errno == EINTR);

    return received_length;
}

ssize_t write_smtp_socket(void *context, const void *write_buffer, size_t byte_count)
{
    smtp_socket_t *socket_state = context;
    if (socket_state == NULL || write_buffer == NULL || socket_state->socket_descriptor < 0)
    {
        errno = EINVAL;
        return -1;
    }

    int send_flags = 0;
#ifdef MSG_NOSIGNAL
    send_flags = MSG_NOSIGNAL;
#endif

    ssize_t sent_length;
    do
    {
        sent_length = send(socket_state->socket_descriptor, write_buffer,
                           byte_count, send_flags);
    }
    while (sent_length < 0 && errno == EINTR);

    return sent_length;
}

void close_smtp_socket(smtp_socket_t *socket_state)
{
    if (socket_state == NULL || socket_state->socket_descriptor < 0) return;

    close(socket_state->socket_descriptor);
    socket_state->socket_descriptor = -1;
}

// #endregion
// #region Helpers

static int configure_smtp_socket(int socket_descriptor)
{
    struct timeval socket_timeout =
    {
        .tv_sec = 30,
        .tv_usec = 0
    };
    if (setsockopt(socket_descriptor, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout, sizeof(socket_timeout)) != 0) return 1;
    if (setsockopt(socket_descriptor, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout)) != 0) return 1;

#ifdef SO_NOSIGPIPE
    int suppress_sigpipe = 1;
    if (setsockopt(socket_descriptor, SOL_SOCKET, SO_NOSIGPIPE, &suppress_sigpipe, sizeof(suppress_sigpipe)) != 0) return 1;
#endif

    return 0;
}

// #endregion
