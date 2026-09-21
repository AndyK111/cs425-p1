#ifndef SESSION_H
#define SESSION_H

#include "protocol.h"
#include <stddef.h>
#include <sys/types.h>

// #region Constants

#define SMTP_READ_BUFFER_CAPACITY 1024
#define SMTP_REPLY_LINE_CAPACITY 513
#define SMTP_ERROR_MESSAGE_CAPACITY 640

// #endregion
// #region Type Definitions

/* (void *, void *, size_t) -> ssize_t
 *
 * Defines a transport callback that reads available bytes into a buffer.
 *
 * Parameters:
 * void *context          : Transport state owned by the caller.
 * void *read_buffer      : Destination for received bytes.
 * size_t buffer_capacity : Maximum number of bytes to copy.
 *
 * Returns: Positive bytes read, zero for end of stream, or -1 for an error.
 *
 * NOTE: The callback may return fewer bytes than requested, but never more.
 * It must handle interrupted system calls before returning.
 */
typedef ssize_t (*smtp_read_callback_t)(void *context, void *read_buffer,
                                       size_t buffer_capacity);

/* (void *, const void *, size_t) -> ssize_t
 *
 * Defines a transport callback that writes some or all supplied bytes.
 *
 * Parameters:
 * void *context            : Transport state owned by the caller.
 * const void *write_buffer : Bytes to send.
 * size_t byte_count        : Maximum number of bytes to send.
 *
 * Returns: Positive bytes written, zero for a closed transport, or -1 for an
 * error. A positive result may be smaller than byte_count.
 *
 * NOTE: The callback must not return more than byte_count. It must handle
 * interrupted system calls and prevent SIGPIPE from terminating the process.
 */
typedef ssize_t (*smtp_write_callback_t)(void *context, const void *write_buffer,
                                        size_t byte_count);

/* (void *, smtp_read_callback_t, smtp_write_callback_t) -> smtp_transport_t
 *
 * Groups a replaceable byte transport with its borrowed state.
 *
 * Parameters:
 * void *context                : Socket state or scripted test server state.
 * smtp_read_callback_t read     : Callback that receives bytes.
 * smtp_write_callback_t write   : Callback that transmits bytes.
 *
 * Returns: A transport description usable by the session layer.
 *
 * NOTE: The session does not free context or close the transport.
 */
typedef struct smtp_transport
{
    void *context;
    smtp_read_callback_t read;
    smtp_write_callback_t write;
} smtp_transport_t;

/* (smtp_transport_t, char[], size_t, size_t, char[]) -> smtp_session_t
 *
 * Holds the transport, unread bytes, and diagnostic text for one session.
 *
 * Parameters:
 * smtp_transport_t transport : Borrowed callbacks and transport context.
 * char read_buffer[]        : Bytes retained between reads of reply lines.
 * size_t read_offset        : Index of the next unread byte.
 * size_t read_length        : Number of valid bytes in read_buffer.
 * char error_message[]      : Description of the most recent failure.
 *
 * Returns: Session state initialized by initialize_smtp_session.
 *
 * NOTE: Initialize before use. This type contains no heap allocations and
 * must not be shared by concurrent sessions.
 */
typedef struct smtp_session
{
    smtp_transport_t transport;
    char read_buffer[SMTP_READ_BUFFER_CAPACITY];
    size_t read_offset;
    size_t read_length;
    char error_message[SMTP_ERROR_MESSAGE_CAPACITY];
} smtp_session_t;

// #endregion
// #region Functions

/* (smtp_session_t *, smtp_transport_t) -> int
 *
 * Initializes a session with empty buffers and a replaceable transport.
 *
 * Parameters:
 * smtp_session_t *session   : Receives initialized session state.
 * smtp_transport_t transport : Read/write callbacks and borrowed context.
 *
 * Returns:
 * 0 : No errors, session initialized successfully.
 * 1 : Null session or missing transport callback.
 *
 * NOTE: The context may be NULL if the callbacks support it.
 */
int initialize_smtp_session(smtp_session_t *session, smtp_transport_t transport);

/* (smtp_session_t *, char *, size_t) -> int
 *
 * Reads one complete CRLF terminated line, retaining subsequent bytes.
 *
 * Parameters:
 * smtp_session_t *session : Initialized session with a readable transport.
 * char *reply_line       : Receives a null terminated line including CRLF.
 * size_t line_capacity   : Destination size, including the null terminator.
 *
 * Returns:
 * 0 : No errors, one complete line received.
 * 1 : Invalid arguments, oversized line, malformed ending, EOF, or I/O error.
 *
 * NOTE: On failure, error_message explains the problem when session is
 * non-NULL. Stop the session after a failure; partial input is not recoverable.
 */
int read_smtp_line(smtp_session_t *session, char *reply_line, size_t line_capacity);

/* (smtp_session_t *, int) -> int
 *
 * Consumes a complete reply, including continuation lines, and checks its code.
 *
 * Parameters:
 * smtp_session_t *session : Initialized session with a readable transport.
 * int expected_code      : Status code required at this point in the session.
 *
 * Returns:
 * 0 : No errors, all reply lines match the expected status code.
 * 1 : Invalid input, read failure, malformed reply, inconsistent continuation
 *     codes, or an unexpected status code.
 *
 * NOTE: An unexpected status is reported only after its whole reply is read.
 */
int read_smtp_reply(smtp_session_t *session, int expected_code);

/* (smtp_session_t *, const char *) -> int
 *
 * Writes an entire string, retrying when the callback writes only a prefix.
 *
 * Parameters:
 * smtp_session_t *session : Initialized session with a writable transport.
 * const char *message    : Null terminated text to send unchanged.
 *
 * Returns:
 * 0 : No errors, every byte was sent.
 * 1 : Invalid arguments, closed transport, or write failure.
 *
 * NOTE: An empty string succeeds without calling the transport.
 */
int write_smtp_text(smtp_session_t *session, const char *message);

/* (smtp_session_t *, smtp_command_t, const char *, int) -> int
 *
 * Builds and sends one SMTP command, then validates the complete reply.
 *
 * Parameters:
 * smtp_session_t *session : Initialized session to use.
 * smtp_command_t command : Command to construct and send.
 * const char *argument   : Command argument, or NULL for DATA and QUIT.
 * int expected_code      : Required status code for this command.
 *
 * Returns:
 * 0 : No errors, command sent and expected reply received.
 * 1 : Command construction, write, or reply validation failed.
 *
 * NOTE: Temporary command memory is freed before returning, even on failure.
 */
int send_smtp_command(smtp_session_t *session, smtp_command_t command,
                      const char *argument, int expected_code);

/* (smtp_session_t *, const char *, const smtp_message_t *) -> int
 *
 * Runs the greeting, HELO, MAIL FROM, RCPT TO, DATA, and QUIT exchange.
 *
 * Parameters:
 * smtp_session_t *session       : Initialized session with an open transport.
 * const char *helo_hostname     : Client hostname supplied to HELO.
 * const smtp_message_t *message : Borrowed envelope and message fields.
 *
 * Returns:
 * 0 : No errors, the message was queued and QUIT received its expected reply.
 * 1 : Invalid message, allocation failure, or a failed SMTP exchange.
 *
 * NOTE: Stops at the first error. The caller closes the transport. All
 * temporary allocations are freed on both success and failure.
 */
int run_smtp_session(smtp_session_t *session, const char *helo_hostname,
                     const smtp_message_t *message);

// #endregion

#endif
