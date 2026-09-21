#ifndef SOCKET_TRANSPORT_H
#define SOCKET_TRANSPORT_H

#include "session.h"

// #region Type Definitions

/* (int) -> smtp_socket_t
 *
 * Stores the descriptor owned by a socket transport.
 *
 * Parameters:
 * int socket_descriptor : Connected TCP socket, or -1 when disconnected.
 *
 * Returns: Socket state passed as the context of transport callbacks.
 *
 * NOTE: Initialize socket_descriptor to -1. Close a successful connection
 * with close_smtp_socket before reconnecting or discarding this state.
 */
typedef struct smtp_socket
{
    int socket_descriptor;
} smtp_socket_t;

// #endregion
// #region Functions

/* (smtp_socket_t *, const char *, const char *, char *, size_t) -> int
 *
 * Resolves a hostname and connects to the first usable TCP address.
 *
 * Parameters:
 * smtp_socket_t *socket_state : Disconnected socket state to initialize.
 * const char *server_hostname : DNS hostname or numeric server address.
 * const char *server_port     : Numeric port or service name.
 * char *error_message        : Receives a useful diagnostic on failure.
 * size_t error_capacity      : Capacity of the diagnostic buffer.
 *
 * Returns:
 * 0 : No errors, a connected socket is ready for use.
 * 1 : Invalid arguments, name resolution failure, or connection failure.
 *
 * NOTE: The caller owns the connected socket. Failed connection attempts are
 * closed internally. No mail is sent by this function.
 */
int connect_smtp_socket(smtp_socket_t *socket_state, const char *server_hostname,
                        const char *server_port, char *error_message,
                        size_t error_capacity);

/* (void *, void *, size_t) -> ssize_t
 *
 * Reads bytes from the socket, retrying interrupted receives.
 *
 * Parameters:
 * void *context          : Pointer to an initialized smtp_socket_t.
 * void *read_buffer      : Destination for received bytes.
 * size_t buffer_capacity : Maximum number of bytes to receive.
 *
 * Returns: Positive bytes read, zero for EOF, or -1 for invalid input or a
 * socket error. A timeout is reported as a socket error.
 *
 * NOTE: This callback does not append a null terminator or close the socket.
 */
ssize_t read_smtp_socket(void *context, void *read_buffer, size_t buffer_capacity);

/* (void *, const void *, size_t) -> ssize_t
 *
 * Writes bytes to the socket, retrying interrupted sends.
 *
 * Parameters:
 * void *context            : Pointer to an initialized smtp_socket_t.
 * const void *write_buffer : Bytes to send.
 * size_t byte_count        : Maximum number of bytes to send.
 *
 * Returns: Bytes written, or -1 for invalid input or a socket error.
 *
 * NOTE: Partial writes are allowed. SIGPIPE is suppressed so a disconnected
 * server cannot terminate the client unexpectedly.
 */
ssize_t write_smtp_socket(void *context, const void *write_buffer, size_t byte_count);

/* (smtp_socket_t *) -> void
 *
 * Closes an owned socket and marks its state disconnected.
 *
 * Parameters:
 * smtp_socket_t *socket_state : Socket state to close, or NULL for no action.
 *
 * Returns: Nothing.
 *
 * NOTE: Calling this on an already disconnected state is safe.
 */
void close_smtp_socket(smtp_socket_t *socket_state);

// #endregion

#endif
