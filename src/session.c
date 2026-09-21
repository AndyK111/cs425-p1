#include "session.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST
#include "../tests/test-hooks.h"
#endif

// #region Functions

/* Private helper declarations; definitions follow the public functions. */
static int report_session_error(smtp_session_t *session, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

int initialize_smtp_session(smtp_session_t *session, smtp_transport_t transport)
{
    if (session == NULL) return 1;

    memset(session, 0, sizeof(*session));
    if (transport.read == NULL || transport.write == NULL) return report_session_error(session, "Transport requires both read and write callbacks.");

    session->transport = transport;
    return 0;
}

int read_smtp_line(smtp_session_t *session, char *reply_line, size_t line_capacity)
{
    if (session == NULL) return 1;
    if (reply_line == NULL || line_capacity < 3 || session->transport.read == NULL) return report_session_error(session, "Invalid reply buffer or read callback.");

    size_t line_length = 0;
    reply_line[0] = '\0';
    for (;;)
    {
        if (session->read_offset == session->read_length)
        {
            ssize_t received_length = session->transport.read(
                session->transport.context, session->read_buffer,
                sizeof(session->read_buffer));
            if (received_length < 0) return report_session_error(session, "Failed to read the server reply.");
            if (received_length == 0) return report_session_error(session, "Server disconnected before completing its reply.");
            if ((size_t)received_length > sizeof(session->read_buffer)) return report_session_error(session, "Read callback returned more bytes than requested.");

            session->read_offset = 0;
            session->read_length = (size_t)received_length;
        }

        if (line_length == line_capacity - 1) return report_session_error(session, "Server reply exceeds the reply buffer capacity (%zu bytes).", line_capacity);

        char current_character = session->read_buffer[session->read_offset++];
        if (current_character == '\0') return report_session_error(session, "Server reply contains a NUL byte.");
        if (line_length > 0 && reply_line[line_length - 1] == '\r' && current_character != '\n') return report_session_error(session, "Server reply contains a bare CR.");

        reply_line[line_length++] = current_character;
        reply_line[line_length] = '\0';
        if (current_character == '\n')
        {
            if (line_length < 2 || reply_line[line_length - 2] != '\r') return report_session_error(session, "Server reply must end with CRLF.");
            return 0;
        }
    }
}

int read_smtp_reply(smtp_session_t *session, int expected_code)
{
    if (session == NULL) return 1;

    char reply_line[SMTP_REPLY_LINE_CAPACITY];
    char first_reply_line[SMTP_REPLY_LINE_CAPACITY];
    smtp_reply_t parsed_reply;
    int first_status_code = 0;
    do
    {
        if (read_smtp_line(session, reply_line, sizeof(reply_line)) != 0) return 1;
        if (parse_smtp_reply(reply_line, &parsed_reply) != 0) return report_session_error(session, "Malformed server reply: %s", reply_line);

        if (first_status_code == 0)
        {
            first_status_code = parsed_reply.status_code;
            memcpy(first_reply_line, reply_line, strlen(reply_line) + 1);
        }
        else if (parsed_reply.status_code != first_status_code) return report_session_error(session, "Inconsistent continuation: first code %d, received %s", first_status_code, reply_line);
    }
    while (!parsed_reply.is_final);

    if (first_status_code != expected_code) return report_session_error(session, "Expected SMTP %d, received %s", expected_code, first_reply_line);

    return 0;
}

int write_smtp_text(smtp_session_t *session, const char *message)
{
    if (session == NULL) return 1;
    if (message == NULL || session->transport.write == NULL) return report_session_error(session, "Invalid message or write callback.");

    size_t message_length = strlen(message);
    size_t written_length = 0;
    while (written_length < message_length)
    {
        size_t remaining_length = message_length - written_length;
        ssize_t sent_length = session->transport.write(session->transport.context,
            message + written_length, remaining_length);
        if (sent_length <= 0) return report_session_error(session, "Failed to write to the server; connection closed or transport error.");
        if ((size_t)sent_length > remaining_length) return report_session_error(session, "Write callback returned more bytes than requested.");

        written_length += (size_t)sent_length;
    }

    return 0;
}

int send_smtp_command(smtp_session_t *session, smtp_command_t command,
                      const char *argument, int expected_code)
{
    if (session == NULL) return 1;

    char *command_text = build_smtp_command(command, argument);
    if (command_text == NULL) return report_session_error(session, "Cannot construct SMTP command: invalid argument, excessive length, or allocation failure.");

    int command_result = write_smtp_text(session, command_text);
    free(command_text);
    if (command_result != 0) return command_result;

    return read_smtp_reply(session, expected_code);
}

int run_smtp_session(smtp_session_t *session, const char *helo_hostname,
                     const smtp_message_t *message)
{
    if (session == NULL) return 1;

    session->error_message[0] = '\0';
    char *message_payload = build_smtp_payload(message);
    if (message_payload == NULL) return report_session_error(session, "Cannot construct message: invalid fields, excessive line length, or allocation failure.");

    int session_result = 1;
    if (read_smtp_reply(session, 220) != 0) goto cleanup;
    if (send_smtp_command(session, SMTP_COMMAND_HELO, helo_hostname, 250) != 0) goto cleanup;
    if (send_smtp_command(session, SMTP_COMMAND_MAIL_FROM, message->sender_address, 250) != 0) goto cleanup;
    if (send_smtp_command(session, SMTP_COMMAND_RCPT_TO, message->recipient_address, 250) != 0) goto cleanup;
    if (send_smtp_command(session, SMTP_COMMAND_DATA, NULL, 354) != 0) goto cleanup;
    if (write_smtp_text(session, message_payload) != 0) goto cleanup;
    if (read_smtp_reply(session, 250) != 0) goto cleanup;
    if (send_smtp_command(session, SMTP_COMMAND_QUIT, NULL, 221) != 0) goto cleanup;

    session_result = 0;

cleanup:
    free(message_payload);
    return session_result;
}

// #endregion
// #region Helpers

static int report_session_error(smtp_session_t *session, const char *format, ...)
{
    /* Each public entry point rejects a NULL session before reaching here. */
    va_list format_arguments;
    va_start(format_arguments, format);
    vsnprintf(session->error_message, sizeof(session->error_message),
              format, format_arguments);
    va_end(format_arguments);

    return 1;
}

// #endregion
