#include "protocol.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST
#include "../tests/test-hooks.h"
#endif

// #region Functions

/* Private helper declarations; definitions follow the public functions. */
static char *allocate_formatted_text(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
static int is_valid_header_value(const char *header_value);
static int is_valid_address(const char *mail_address);
static int body_lines_fit(const char *message_body);

int parse_smtp_reply(const char *reply_line, smtp_reply_t *reply)
{
    if (reply_line == NULL || reply == NULL) return 1;

    size_t line_length = strlen(reply_line);
    if (line_length < 5 || line_length > 512) return 1;
    if (reply_line[line_length - 2] != '\r' || reply_line[line_length - 1] != '\n') return 1;
    if (reply_line[0] < '2' || reply_line[0] > '5') return 1;
    if (reply_line[1] < '0' || reply_line[1] > '9') return 1;
    if (reply_line[2] < '0' || reply_line[2] > '9') return 1;
    if (line_length != 5 && reply_line[3] != ' ' && reply_line[3] != '-') return 1;

    for (size_t character_index = 3; character_index < line_length - 2; character_index++)
    {
        if (reply_line[character_index] == '\r' || reply_line[character_index] == '\n') return 1;
    }

    reply->status_code = (reply_line[0] - '0') * 100
                        + (reply_line[1] - '0') * 10 + reply_line[2] - '0';
    reply->is_final = reply_line[3] != '-';
    return 0;
}

char *build_smtp_command(smtp_command_t command, const char *argument)
{
    char *command_text = NULL;
    switch (command)
    {
        case SMTP_COMMAND_HELO:
            if (!is_valid_header_value(argument) || argument[0] == '\0') return NULL;
            if (strpbrk(argument, " \t") != NULL) return NULL;
            command_text = allocate_formatted_text("HELO %s\r\n", argument);
            break;
        case SMTP_COMMAND_MAIL_FROM:
            if (!is_valid_address(argument)) return NULL;
            command_text = allocate_formatted_text("MAIL FROM:<%s>\r\n", argument);
            break;
        case SMTP_COMMAND_RCPT_TO:
            if (!is_valid_address(argument)) return NULL;
            command_text = allocate_formatted_text("RCPT TO:<%s>\r\n", argument);
            break;
        case SMTP_COMMAND_DATA:
            if (argument != NULL) return NULL;
            command_text = allocate_formatted_text("DATA\r\n");
            break;
        case SMTP_COMMAND_QUIT:
            if (argument != NULL) return NULL;
            command_text = allocate_formatted_text("QUIT\r\n");
            break;
        default:
            return NULL;
    }

    if (command_text != NULL && strlen(command_text) > 512)
    {
        free(command_text);
        return NULL;
    }

    return command_text;
}

char *stuff_smtp_body(const char *message_body)
{
    if (message_body == NULL) return NULL;

    size_t body_length = strlen(message_body);
    if (body_length > (SIZE_MAX - 3) / 2) return NULL;

    char *escaped_body = malloc(body_length * 2 + 3);
    if (escaped_body == NULL) return NULL;

    size_t output_length = 0;
    int at_line_start = 1;
    for (size_t character_index = 0; character_index < body_length; character_index++)
    {
        char current_character = message_body[character_index];
        if (current_character == '\r' || current_character == '\n')
        {
            if (current_character == '\r' && message_body[character_index + 1] == '\n') character_index++;
            escaped_body[output_length++] = '\r';
            escaped_body[output_length++] = '\n';
            at_line_start = 1;
            continue;
        }

        if (at_line_start && current_character == '.') escaped_body[output_length++] = '.';
        escaped_body[output_length++] = current_character;
        at_line_start = 0;
    }

    if (!at_line_start)
    {
        escaped_body[output_length++] = '\r';
        escaped_body[output_length++] = '\n';
    }

    escaped_body[output_length] = '\0';
    return escaped_body;
}

char *build_smtp_payload(const smtp_message_t *message)
{
    if (message == NULL) return NULL;
    if (!is_valid_address(message->sender_address) || !is_valid_address(message->recipient_address)) return NULL;
    if (!is_valid_header_value(message->subject) || message->body == NULL) return NULL;
    if (strlen(message->sender_address) > 990 || strlen(message->recipient_address) > 992) return NULL;
    if (strlen(message->subject) > 989 || !body_lines_fit(message->body)) return NULL;

    char *escaped_body = stuff_smtp_body(message->body);
    if (escaped_body == NULL) return NULL;

    char *message_payload = allocate_formatted_text(
        "From: <%s>\r\nTo: <%s>\r\nSubject: %s\r\n\r\n%s.\r\n",
        message->sender_address, message->recipient_address, message->subject,
        escaped_body);
    free(escaped_body);
    return message_payload;
}

// #endregion
// #region Helpers

static char *allocate_formatted_text(const char *format, ...)
{
    va_list format_arguments;
    va_list copied_arguments;
    va_start(format_arguments, format);
    va_copy(copied_arguments, format_arguments);
    int text_length = vsnprintf(NULL, 0, format, copied_arguments);
    va_end(copied_arguments);

    if (text_length < 0)
    {
        va_end(format_arguments);
        return NULL;
    }

    char *formatted_text = malloc((size_t)text_length + 1);
    if (formatted_text == NULL)
    {
        va_end(format_arguments);
        return NULL;
    }

    int written_length = vsnprintf(formatted_text, (size_t)text_length + 1,
                                    format, format_arguments);
    va_end(format_arguments);
    if (written_length != text_length)
    {
        free(formatted_text);
        return NULL;
    }

    return formatted_text;
}

static int is_valid_header_value(const char *header_value)
{
    return header_value != NULL && strpbrk(header_value, "\r\n") == NULL;
}

static int is_valid_address(const char *mail_address)
{
    return is_valid_header_value(mail_address) && mail_address[0] != '\0'
           && strpbrk(mail_address, "<>") == NULL;
}

static int body_lines_fit(const char *message_body)
{
    size_t line_length = 0;
    for (size_t character_index = 0; message_body[character_index] != '\0';
         character_index++)
    {
        char current_character = message_body[character_index];
        if (current_character == '\r' || current_character == '\n')
        {
            line_length = 0;
            continue;
        }

        line_length++;
        if (line_length > 998) return 0;
    }

    return 1;
}

// #endregion
