#define _POSIX_C_SOURCE 200809L
#include "lab.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef TEST
#define main main_exclude
#endif

// #region Type Definitions

/* (smtp_message_t, const char *, const char *, const char *) -> client_options_t
 *
 * Groups parsed message fields and connection options for the application.
 *
 * Parameters:
 * smtp_message_t message      : Sender, recipient, subject, and optional body.
 * const char *server_hostname : Positional server hostname or address.
 * const char *server_port     : Port number or service name, defaulting to 25.
 * const char *helo_hostname   : Client hostname, defaulting to localhost.
 *
 * Returns: Options populated from the command line.
 *
 * NOTE: Strings borrow argv storage or literals. A NULL message body means
 * stdin must be read; main separately owns and frees that allocated body.
 */
typedef struct client_options
{
    smtp_message_t message;
    const char *server_hostname;
    const char *server_port;
    const char *helo_hostname;
} client_options_t;

// #endregion
// #region Functions

/* Private helper declarations; definitions follow the public functions. */
static void print_usage(FILE *output_stream);
static int validate_client_options(const client_options_t *client_options);
static int parse_client_options(int argument_count, char **argument_values,
                                client_options_t *client_options);
static int read_standard_input(char **message_body);

int main(int argument_count, char **argument_values)
{
    if (argument_count == 1)
    {
        print_usage(stdout);
        return 0;
    }

    client_options_t client_options;
    if (parse_client_options(argument_count, argument_values, &client_options) != 0)
    {
        print_usage(stderr);
        return 1;
    }

    char *allocated_body = NULL;
    if (client_options.message.body == NULL)
    {
        int input_result = read_standard_input(&allocated_body);
        if (input_result != 0) return input_result;
        client_options.message.body = allocated_body;
    }

    smtp_socket_t socket_state =
    {
        .socket_descriptor = -1
    };
    char connection_error[SMTP_ERROR_MESSAGE_CAPACITY];
    if (connect_smtp_socket(&socket_state, client_options.server_hostname,
        client_options.server_port, connection_error, sizeof(connection_error)) != 0)
    {
        fprintf(stderr, "%s\n", connection_error);
        free(allocated_body);
        return 2;
    }

    smtp_transport_t socket_transport =
    {
        .context = &socket_state,
        .read = read_smtp_socket,
        .write = write_smtp_socket
    };
    smtp_session_t smtp_session;
    int session_result = initialize_smtp_session(&smtp_session, socket_transport);
    if (session_result == 0) session_result = run_smtp_session(&smtp_session, client_options.helo_hostname, &client_options.message);

    if (session_result != 0) fprintf(stderr, "%s\n", smtp_session.error_message);
    close_smtp_socket(&socket_state);
    free(allocated_body);
    return session_result == 0 ? 0 : 2;
}

// #endregion
// #region Helpers

static void print_usage(FILE *output_stream)
{
    fprintf(output_stream,
        "Usage: myapp -f <from> -t <to> [-s subject] [-b body] [-p port]\n"
        "             [-H helo-host] <server>\n\n"
        "  -f <from>       Envelope sender\n"
        "  -t <to>         Envelope recipient\n"
        "  -s <subject>    Subject line (default: empty)\n"
        "  -b <body>       Message body (default: read from stdin)\n"
        "  -p <port>       Port or service name (default: 25)\n"
        "  -H <helo-host>  Hostname sent with HELO (default: localhost)\n"
        "  <server>        Mail server hostname or address\n");
}

static int validate_client_options(const client_options_t *client_options)
{
    const smtp_message_t *message = &client_options->message;
    if (message->sender_address == NULL || message->recipient_address == NULL)
    {
        fprintf(stderr, "Both -f <from> and -t <to> are required.\n");
        return 1;
    }

    if (message->sender_address[0] == '\0' || message->recipient_address[0] == '\0'
        || strpbrk(message->sender_address, "\r\n<>") != NULL
        || strpbrk(message->recipient_address, "\r\n<>") != NULL)
    {
        fprintf(stderr, "Addresses must be nonempty and must not contain CR, LF, or angle brackets.\n");
        return 1;
    }

    if (strpbrk(message->subject, "\r\n") != NULL)
    {
        fprintf(stderr, "The subject must not contain CR or LF.\n");
        return 1;
    }

    if (client_options->helo_hostname[0] == '\0'
        || strpbrk(client_options->helo_hostname, "\r\n \t") != NULL)
    {
        fprintf(stderr, "The HELO hostname must be nonempty and contain no whitespace.\n");
        return 1;
    }

    if (strlen(message->sender_address) > 498 || strlen(message->recipient_address) > 500
        || strlen(client_options->helo_hostname) > 505 || strlen(message->subject) > 989)
    {
        fprintf(stderr, "An address, HELO hostname, or subject exceeds the SMTP line length limit.\n");
        return 1;
    }

    if (client_options->server_hostname[0] == '\0'
        || strpbrk(client_options->server_hostname, "\r\n \t") != NULL)
    {
        fprintf(stderr, "The server hostname must be nonempty and contain no whitespace.\n");
        return 1;
    }

    const char *server_port = client_options->server_port;
    if (server_port[0] == '\0' || strpbrk(server_port, "\r\n \t+") != NULL || server_port[0] == '-')
    {
        fprintf(stderr, "The port must be a number from 1 through 65535 or a service name.\n");
        return 1;
    }

    if (strspn(server_port, "0123456789") == strlen(server_port))
    {
        errno = 0;
        unsigned long port_number = strtoul(server_port, NULL, 10);
        if (errno != 0 || port_number == 0 || port_number > 65535)
        {
            fprintf(stderr, "Numeric ports must be between 1 and 65535.\n");
            return 1;
        }
    }

    return 0;
}

static int parse_client_options(int argument_count, char **argument_values,
                                client_options_t *client_options)
{
    *client_options = (client_options_t)
    {
        .message =
        {
            .subject = ""
        },
        .server_port = "25",
        .helo_hostname = "localhost"
    };

    opterr = 0;
    int selected_option;
    while ((selected_option = getopt(argument_count, argument_values, ":f:t:s:b:p:H:")) != -1)
    {
        switch (selected_option)
        {
            case 'f':
                client_options->message.sender_address = optarg;
                break;
            case 't':
                client_options->message.recipient_address = optarg;
                break;
            case 's':
                client_options->message.subject = optarg;
                break;
            case 'b':
                client_options->message.body = optarg;
                break;
            case 'p':
                client_options->server_port = optarg;
                break;
            case 'H':
                client_options->helo_hostname = optarg;
                break;
            case ':':
                fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                return 1;
            default:
                fprintf(stderr, "Unknown option: -%c.\n", optopt);
                return 1;
        }
    }

    if (argument_count - optind != 1)
    {
        fprintf(stderr, "Exactly one server hostname or address is required.\n");
        return 1;
    }

    client_options->server_hostname = argument_values[optind];
    return validate_client_options(client_options);
}

static int read_standard_input(char **message_body)
{
    size_t buffer_capacity = 4096;
    size_t body_length = 0;
    char *body_buffer = malloc(buffer_capacity);
    if (body_buffer == NULL)
    {
        fprintf(stderr, "Unable to allocate memory for the message body.\n");
        return 2;
    }

    int input_character;
    while ((input_character = fgetc(stdin)) != EOF)
    {
        if (input_character == '\0')
        {
            fprintf(stderr, "The message body must not contain NUL bytes.\n");
            free(body_buffer);
            return 1;
        }

        if (body_length == buffer_capacity - 1)
        {
            if (buffer_capacity > SIZE_MAX / 2)
            {
                fprintf(stderr, "The message body is too large to store.\n");
                free(body_buffer);
                return 2;
            }

            size_t enlarged_capacity = buffer_capacity * 2;
            char *enlarged_buffer = realloc(body_buffer, enlarged_capacity);
            if (enlarged_buffer == NULL)
            {
                fprintf(stderr, "Unable to allocate more memory for the message body.\n");
                free(body_buffer);
                return 2;
            }

            body_buffer = enlarged_buffer;
            buffer_capacity = enlarged_capacity;
        }

        body_buffer[body_length++] = (char)input_character;
    }

    if (ferror(stdin))
    {
        fprintf(stderr, "Failed to read the message body from stdin.\n");
        free(body_buffer);
        return 2;
    }

    body_buffer[body_length] = '\0';
    *message_body = body_buffer;
    return 0;
}

// #endregion
