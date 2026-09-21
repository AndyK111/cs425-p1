#ifndef PROTOCOL_H
#define PROTOCOL_H

// #region Constants

/* (enum) -> smtp_command_t
 *
 * Identifies a command supported by the basic SMTP session.
 *
 * Parameters:
 * SMTP_COMMAND_HELO      : Introduce the client hostname.
 * SMTP_COMMAND_MAIL_FROM : Set the envelope sender.
 * SMTP_COMMAND_RCPT_TO   : Set the envelope recipient.
 * SMTP_COMMAND_DATA      : Request permission to send message data.
 * SMTP_COMMAND_QUIT      : End the session.
 *
 * Returns: A command identifier for build_smtp_command.
 *
 * NOTE: No ESMTP extensions, authentication, or TLS commands are supported.
 */
typedef enum smtp_command
{
    SMTP_COMMAND_HELO,
    SMTP_COMMAND_MAIL_FROM,
    SMTP_COMMAND_RCPT_TO,
    SMTP_COMMAND_DATA,
    SMTP_COMMAND_QUIT
} smtp_command_t;

// #endregion
// #region Type Definitions

/* (int, int) -> smtp_reply_t
 *
 * Stores the status code and continuation state of one SMTP reply line.
 *
 * Parameters:
 * int status_code : Three digit status code sent by the server.
 * int is_final    : One for the final line, zero for a continuation.
 *
 * Returns: A value describing a parsed reply line.
 *
 * NOTE: This type owns no allocated memory.
 */
typedef struct smtp_reply
{
    int status_code;
    int is_final;
} smtp_reply_t;

/* (const char *, const char *, const char *, const char *) -> smtp_message_t
 *
 * Groups the envelope addresses, subject, and body of one outgoing message.
 *
 * Parameters:
 * const char *sender_address    : Envelope sender and From header value.
 * const char *recipient_address : Envelope recipient and To header value.
 * const char *subject           : Subject header value, possibly empty.
 * const char *body              : Message body, possibly empty.
 *
 * Returns: A message description that borrows the supplied strings.
 *
 * NOTE: All strings must remain valid during use. Addresses and subject must
 * not contain CR or LF. The caller retains ownership of every string.
 */
typedef struct smtp_message
{
    const char *sender_address;
    const char *recipient_address;
    const char *subject;
    const char *body;
} smtp_message_t;

// #endregion
// #region Functions

/* (const char *, smtp_reply_t *) -> int
 *
 * Parses a complete CRLF terminated reply and identifies its final line.
 *
 * Parameters:
 * const char *reply_line : A single reply line including its CRLF ending.
 * smtp_reply_t *reply    : Receives the parsed status and continuation flag.
 *
 * Returns:
 * 0 : No errors, reply parsed successfully.
 * 1 : Null argument or malformed reply line.
 *
 * NOTE: The output is only changed on success. No memory is allocated.
 */
int parse_smtp_reply(const char *reply_line, smtp_reply_t *reply);

/* (smtp_command_t, const char *) -> char *
 *
 * Builds a command with its required spacing, address brackets, and CRLF.
 *
 * Parameters:
 * smtp_command_t command : The command to construct.
 * const char *argument   : Hostname or address; NULL for DATA and QUIT.
 *
 * Returns: An allocated command, or NULL for invalid input, an overlong
 * command, or allocation failure.
 *
 * NOTE: The caller must free the returned string. Arguments must not contain
 * CR or LF. Addresses must not contain angle brackets supplied by the caller.
 */
char *build_smtp_command(smtp_command_t command, const char *argument);

/* (const char *) -> char *
 *
 * Normalizes body line endings to CRLF and doubles each leading period.
 *
 * Parameters:
 * const char *message_body : Body text with LF, CRLF, or CR line endings.
 *
 * Returns: An allocated escaped body, or NULL for null input or allocation
 * failure. Every nonempty body ends with CRLF.
 *
 * NOTE: The caller must free the returned string. This does not append the
 * single period line that terminates DATA. Input must be a C string.
 */
char *stuff_smtp_body(const char *message_body);

/* (const smtp_message_t *) -> char *
 *
 * Builds From, To, and Subject headers, a blank line, the escaped body, and
 * the single period line that terminates DATA.
 *
 * Parameters:
 * const smtp_message_t *message : Borrowed message fields to serialize.
 *
 * Returns: An allocated DATA payload, or NULL for invalid fields, overlong
 * header/body lines, or allocation failure.
 *
 * NOTE: The caller must free the returned string. The input is not modified.
 */
char *build_smtp_payload(const smtp_message_t *message);

// #endregion

#endif
