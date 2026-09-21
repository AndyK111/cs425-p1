#include "harness/unity.h"
#include "test-support.h"
#include <stdio.h>
#include <string.h>

//#region Test fixtures

static const char expected_payload[] =
    "From: <sender@example.com>\r\nTo: <recipient@example.com>\r\n"
    "Subject: Hello\r\n\r\n..first\r\n..\r\nlast\r\n.\r\n";
static const char successful_replies[] =
    "220 ready\r\n250 hello\r\n250 sender\r\n250 recipient\r\n"
    "354 data\r\n250 queued\r\n221 bye\r\n";
static const char expected_transcript[] =
    "HELO client.example\r\nMAIL FROM:<sender@example.com>\r\n"
    "RCPT TO:<recipient@example.com>\r\nDATA\r\n"
    "From: <sender@example.com>\r\nTo: <recipient@example.com>\r\n"
    "Subject: Hello\r\n\r\n..first\r\n..\r\nlast\r\n.\r\nQUIT\r\n";
static const scripted_exchange_t successful_exchanges[] =
{
    {
        "", "220 ready\r\n"
    },
    {
        "HELO client.example\r\n", "250 hello\r\n"
    },
    {
        "MAIL FROM:<sender@example.com>\r\n", "250 sender\r\n"
    },
    {
        "RCPT TO:<recipient@example.com>\r\n", "250 recipient\r\n"
    },
    {
        "DATA\r\n", "354 data\r\n"
    },
    {
        expected_payload, "250 queued\r\n"
    },
    {
        "QUIT\r\n", "221 bye\r\n"
    }
};

static const smtp_message_t message =
{
    .sender_address = "sender@example.com",
    .recipient_address = "recipient@example.com",
    .subject = "Hello",
    .body = ".first\n.\r\nlast"
};


static scripted_transport_t scripted_server;
static smtp_session_t session;

//#endregion

//#region Test declarations

static void test_session_initialization_clears_state(void);
static void test_session_initialization_rejects_missing_callbacks(void);
static void test_line_reader_handles_every_fragment_size(void);
static void test_line_reader_keeps_coalesced_replies_without_extra_reads(void);
static void test_line_reader_refills_at_internal_buffer_boundary(void);
static void test_line_reader_exact_capacity_and_overflow_canary(void);
static void test_line_reader_rejects_invalid_arguments(void);
static void test_line_reader_reports_eof_and_read_failure(void);
static void test_line_reader_rejects_invalid_transport_byte_count(void);
static void test_line_reader_rejects_bare_newlines_and_nul(void);
static void test_reply_reader_consumes_all_continuations(void);
static void test_reply_reader_drains_unexpected_multiline_reply(void);
static void test_reply_reader_rejects_malformed_and_inconsistent_replies(void);
static void test_reply_reader_rejects_oversized_reply(void);
static void test_writer_retries_every_partial_write_size(void);
static void test_writer_empty_input_and_invalid_arguments(void);
static void test_writer_reports_zero_negative_and_impossible_counts(void);
static void test_command_sends_bytes_and_checks_response(void);
static void test_command_failure_never_reads_after_failed_write(void);
static void test_command_rejects_invalid_input_without_io(void);
static void test_complete_session_obeys_request_reply_order(void);
static void test_complete_session_fragmented_reads_and_writes(void);
static void test_complete_session_handles_coalesced_replies(void);
static void test_complete_session_multiline_reply_at_every_stage(void);
static void test_complete_session_stops_on_wrong_code_at_every_stage(void);
static void test_complete_session_disconnect_at_every_reply_byte(void);
static void test_complete_session_read_error_at_every_reply_byte(void);
static void test_complete_session_write_failure_at_every_output_byte(void);
static void test_complete_session_allocation_failure_at_every_allocation(void);
static void test_complete_session_formatting_failure_at_every_format_call(void);
static void test_complete_session_rejects_invalid_message_and_hostname(void);
static void prepare_session(const char *server_replies);
static void prepare_ordered_session(const scripted_exchange_t *exchanges);
static void assert_transcript_prefix(size_t expected_length);

//#endregion

//#region Test runner

void run_session_tests(void)
{
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_session_initialization_clears_state);
    RUN_TEST(test_line_reader_handles_every_fragment_size);
    RUN_TEST(test_line_reader_keeps_coalesced_replies_without_extra_reads);
    RUN_TEST(test_line_reader_refills_at_internal_buffer_boundary);
    RUN_TEST(test_reply_reader_consumes_all_continuations);
    RUN_TEST(test_writer_retries_every_partial_write_size);
    RUN_TEST(test_command_sends_bytes_and_checks_response);
    RUN_TEST(test_complete_session_obeys_request_reply_order);
    RUN_TEST(test_complete_session_fragmented_reads_and_writes);
    RUN_TEST(test_complete_session_handles_coalesced_replies);
    RUN_TEST(test_complete_session_multiline_reply_at_every_stage);
    RUN_TEST(test_session_initialization_rejects_missing_callbacks);
    RUN_TEST(test_line_reader_exact_capacity_and_overflow_canary);
    RUN_TEST(test_line_reader_rejects_invalid_arguments);
    RUN_TEST(test_line_reader_reports_eof_and_read_failure);
    RUN_TEST(test_line_reader_rejects_invalid_transport_byte_count);
    RUN_TEST(test_line_reader_rejects_bare_newlines_and_nul);
    RUN_TEST(test_reply_reader_rejects_malformed_and_inconsistent_replies);
    RUN_TEST(test_reply_reader_rejects_oversized_reply);
    RUN_TEST(test_writer_empty_input_and_invalid_arguments);
    RUN_TEST(test_command_failure_never_reads_after_failed_write);
    RUN_TEST(test_command_rejects_invalid_input_without_io);
    RUN_TEST(test_complete_session_stops_on_wrong_code_at_every_stage);
    RUN_TEST(test_complete_session_disconnect_at_every_reply_byte);
    RUN_TEST(test_complete_session_read_error_at_every_reply_byte);
    RUN_TEST(test_complete_session_write_failure_at_every_output_byte);
    RUN_TEST(test_complete_session_allocation_failure_at_every_allocation);
    RUN_TEST(test_complete_session_formatting_failure_at_every_format_call);
    RUN_TEST(test_complete_session_rejects_invalid_message_and_hostname);
    RUN_TEST(test_reply_reader_drains_unexpected_multiline_reply);
    RUN_TEST(test_writer_reports_zero_negative_and_impossible_counts);
}

//#endregion

//#region initialize_smtp_session testing

static void test_session_initialization_clears_state(void)
{
    smtp_transport_t transport = initialize_scripted_transport(&scripted_server, "");
    memset(&session, 0xa5, sizeof(session));
    TEST_ASSERT_EQUAL_INT(0, initialize_smtp_session(&session, transport));
    TEST_ASSERT_EQUAL_PTR(&scripted_server, session.transport.context);
    TEST_ASSERT_TRUE(session.transport.read == transport.read);
    TEST_ASSERT_TRUE(session.transport.write == transport.write);
    TEST_ASSERT_EQUAL_UINT64(0, session.read_offset);
    TEST_ASSERT_EQUAL_UINT64(0, session.read_length);
    TEST_ASSERT_EQUAL_STRING("", session.error_message);
    transport.context = NULL;
    TEST_ASSERT_EQUAL_INT(0, initialize_smtp_session(&session, transport));
}

//#endregion

//#region read_smtp_line testing

static void test_line_reader_handles_every_fragment_size(void)
{
    const char *reply_line = "250 complete reply\r\n";
    for (size_t fragment_size = 1; fragment_size <= strlen(reply_line); fragment_size++)
    {
        prepare_session(reply_line);
        scripted_server.read_limit = fragment_size;
        char output_line[64];
        TEST_ASSERT_EQUAL_INT(0, read_smtp_line(&session, output_line, sizeof(output_line)));
        TEST_ASSERT_EQUAL_STRING(reply_line, output_line);
        TEST_ASSERT_EQUAL_UINT64((strlen(reply_line) + fragment_size - 1) / fragment_size, scripted_server.read_calls);
    }
}

static void test_line_reader_keeps_coalesced_replies_without_extra_reads(void)
{
    prepare_session("220 greeting\r\n250 hello\r\n221 goodbye\r\n");
    char output_line[64];
    TEST_ASSERT_EQUAL_INT(0, read_smtp_line(&session, output_line, sizeof(output_line)));
    TEST_ASSERT_EQUAL_STRING("220 greeting\r\n", output_line);
    TEST_ASSERT_EQUAL_INT(0, read_smtp_line(&session, output_line, sizeof(output_line)));
    TEST_ASSERT_EQUAL_STRING("250 hello\r\n", output_line);
    TEST_ASSERT_EQUAL_INT(0, read_smtp_line(&session, output_line, sizeof(output_line)));
    TEST_ASSERT_EQUAL_STRING("221 goodbye\r\n", output_line);
    TEST_ASSERT_EQUAL_UINT64(1, scripted_server.read_calls);
}

static void test_line_reader_refills_at_internal_buffer_boundary(void)
{
    char incoming_bytes[1601];
    for (size_t line_index = 0; line_index < 200; line_index++) memcpy(incoming_bytes + line_index * 8, "250 OK\r\n", 8);
    incoming_bytes[1600] = '\0';
    prepare_session(incoming_bytes);
    char output_line[9];
    for (size_t line_index = 0; line_index < 200; line_index++)
    {
        TEST_ASSERT_EQUAL_INT(0, read_smtp_line(&session, output_line, sizeof(output_line)));
        TEST_ASSERT_EQUAL_STRING("250 OK\r\n", output_line);
    }

    TEST_ASSERT_EQUAL_UINT64(2, scripted_server.read_calls);
}

//#endregion

//#region read_smtp_reply testing

static void test_reply_reader_consumes_all_continuations(void)
{
    const char *multiline_reply = "250-first\r\n250-second\r\n250 final\r\n";
    prepare_session("250-first\r\n250-second\r\n250 final\r\n221 next\r\n");
    TEST_ASSERT_EQUAL_INT(0, read_smtp_reply(&session, 250));
    TEST_ASSERT_EQUAL_UINT64(strlen(multiline_reply), session.read_offset);
    TEST_ASSERT_EQUAL_INT(0, read_smtp_reply(&session, 221));
    TEST_ASSERT_EQUAL_UINT64(1, scripted_server.read_calls);
}


//#endregion

//#region write_smtp_text testing

static void test_writer_retries_every_partial_write_size(void)
{
    const char *output_text = "HELO client.example\r\n";
    for (size_t fragment_size = 1; fragment_size <= strlen(output_text); fragment_size++)
    {
        prepare_session("");
        scripted_server.write_limit = fragment_size;
        TEST_ASSERT_EQUAL_INT(0, write_smtp_text(&session, output_text));
        TEST_ASSERT_EQUAL_STRING(output_text, scripted_server.output_bytes);
        TEST_ASSERT_EQUAL_UINT64((strlen(output_text) + fragment_size - 1) / fragment_size, scripted_server.write_calls);
    }
}


//#endregion

//#region send_smtp_command testing

static void test_command_sends_bytes_and_checks_response(void)
{
    prepare_session("250-first\r\n250 hello\r\n");
    scripted_server.write_limit = 2;
    scripted_server.read_limit = 3;
    TEST_ASSERT_EQUAL_INT(0, send_smtp_command(&session, SMTP_COMMAND_HELO, "localhost", 250));
    TEST_ASSERT_EQUAL_STRING("HELO localhost\r\n", scripted_server.output_bytes);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
}

//#endregion

//#region run_smtp_session testing

static void test_complete_session_obeys_request_reply_order(void)
{
    prepare_ordered_session(successful_exchanges);
    strcpy(session.error_message, "old failure");
    TEST_ASSERT_EQUAL_INT(0, run_smtp_session(&session, "client.example", &message));
    TEST_ASSERT_EQUAL_STRING(expected_transcript, scripted_server.output_bytes);
    TEST_ASSERT_EQUAL_UINT64(7, scripted_server.exchange_index);
    TEST_ASSERT_EQUAL_INT(0, scripted_server.protocol_violation);
    TEST_ASSERT_EQUAL_STRING("", session.error_message);
}

static void test_complete_session_fragmented_reads_and_writes(void)
{
    for (size_t fragment_size = 1; fragment_size <= 17; fragment_size++)
    {
        prepare_ordered_session(successful_exchanges);
        scripted_server.read_limit = fragment_size;
        scripted_server.write_limit = 18 - fragment_size;
        TEST_ASSERT_EQUAL_INT(0, run_smtp_session(&session, "client.example", &message));
        assert_transcript_prefix(strlen(expected_transcript));
        TEST_ASSERT_EQUAL_UINT64(7, scripted_server.exchange_index);
    }
}

static void test_complete_session_handles_coalesced_replies(void)
{
    prepare_session(successful_replies);
    TEST_ASSERT_EQUAL_INT(0, run_smtp_session(&session, "client.example", &message));
    TEST_ASSERT_EQUAL_UINT64(1, scripted_server.read_calls);
    assert_transcript_prefix(strlen(expected_transcript));
}

static void test_complete_session_multiline_reply_at_every_stage(void)
{
    const int expected_codes[] =
    {
        220, 250, 250, 250, 354, 250, 221
    };
    for (size_t stage_index = 0; stage_index < 7; stage_index++)
    {
        scripted_exchange_t exchanges[7];
        memcpy(exchanges, successful_exchanges, sizeof(exchanges));
        char multiline_reply[128];
        snprintf(multiline_reply, sizeof(multiline_reply), "%d-first\r\n%d-second\r\n%d final\r\n",
                 expected_codes[stage_index], expected_codes[stage_index], expected_codes[stage_index]);
        exchanges[stage_index].reply = multiline_reply;
        prepare_ordered_session(exchanges);
        scripted_server.read_limit = 2;
        TEST_ASSERT_EQUAL_INT(0, run_smtp_session(&session, "client.example", &message));
        assert_transcript_prefix(strlen(expected_transcript));
    }
}

//#endregion

//#region Helpers

static void prepare_session(const char *server_replies)
{
    smtp_transport_t transport = initialize_scripted_transport(&scripted_server, server_replies);
    TEST_ASSERT_EQUAL_INT(0, initialize_smtp_session(&session, transport));
}

static void prepare_ordered_session(const scripted_exchange_t *exchanges)
{
    prepare_session("");
    scripted_server.exchanges = exchanges;
    scripted_server.exchange_count = 7;
}

static void assert_transcript_prefix(size_t expected_length)
{
    TEST_ASSERT_EQUAL_UINT64(expected_length, scripted_server.output_length);
    if (expected_length != 0) TEST_ASSERT_EQUAL_MEMORY(expected_transcript, scripted_server.output_bytes, expected_length);
    TEST_ASSERT_EQUAL_INT(0, scripted_server.protocol_violation);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
}

//#endregion

//#region Exception testing

//#region initialize_smtp_session testing

static void test_session_initialization_rejects_missing_callbacks(void)
{
    smtp_transport_t transport = initialize_scripted_transport(&scripted_server, "");
    TEST_ASSERT_EQUAL_INT(1, initialize_smtp_session(NULL, transport));
    transport.read = NULL;
    TEST_ASSERT_EQUAL_INT(1, initialize_smtp_session(&session, transport));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "callbacks"));
    transport = initialize_scripted_transport(&scripted_server, "");
    transport.write = NULL;
    TEST_ASSERT_EQUAL_INT(1, initialize_smtp_session(&session, transport));
}

//#endregion

//#region read_smtp_line testing

static void test_line_reader_exact_capacity_and_overflow_canary(void)
{
    char output_buffer[10];
    memset(output_buffer, '!', sizeof(output_buffer));
    prepare_session("250 OK\r\n");
    TEST_ASSERT_EQUAL_INT(0, read_smtp_line(&session, output_buffer, 9));
    TEST_ASSERT_EQUAL_CHAR('!', output_buffer[9]);
    prepare_session("250 OK\r\n");
    TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, output_buffer, 8));
    TEST_ASSERT_EQUAL_CHAR('!', output_buffer[9]);
    TEST_ASSERT_EQUAL_CHAR('\0', output_buffer[7]);
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "capacity"));
}

static void test_line_reader_rejects_invalid_arguments(void)
{
    char output_line[8];
    prepare_session("250 OK\r\n");
    TEST_ASSERT_EQUAL_INT(1, read_smtp_line(NULL, output_line, sizeof(output_line)));
    TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, NULL, 8));
    TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, output_line, 0));
    TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, output_line, 2));
    session.transport.read = NULL;
    TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, output_line, sizeof(output_line)));
    TEST_ASSERT_EQUAL_UINT64(0, scripted_server.read_calls);
}

static void test_line_reader_reports_eof_and_read_failure(void)
{
    char output_line[32];
    const char *truncated_lines[] =
    {
        "", "250", "250 incomplete\r"
    };
    for (size_t line_index = 0; line_index < 3; line_index++)
    {
        prepare_session(truncated_lines[line_index]);
        TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, output_line, sizeof(output_line)));
        TEST_ASSERT_NOT_NULL(strstr(session.error_message, "disconnected"));
    }

    prepare_session("250 reply\r\n");
    scripted_server.read_failure_offset = 4;
    TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, output_line, sizeof(output_line)));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "Failed to read"));
}

static void test_line_reader_rejects_invalid_transport_byte_count(void)
{
    prepare_session("");
    scripted_server.invalid_read_count = 1;
    char output_line[32];
    TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, output_line, sizeof(output_line)));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "more bytes"));
}

static void test_line_reader_rejects_bare_newlines_and_nul(void)
{
    const char *invalid_lines[] =
    {
        "\n", "250 reply\n", "250 bare\rX\r\n"
    };
    char output_line[64];
    for (size_t line_index = 0; line_index < 3; line_index++)
    {
        prepare_session(invalid_lines[line_index]);
        TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, output_line, sizeof(output_line)));
        TEST_ASSERT_NOT_EQUAL('\0', session.error_message[0]);
    }

    const char embedded_nul[] = "250 bad\0hidden\r\n";
    prepare_session(embedded_nul);
    scripted_server.input_length = sizeof(embedded_nul) - 1;
    TEST_ASSERT_EQUAL_INT(1, read_smtp_line(&session, output_line, sizeof(output_line)));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "NUL"));
}

//#endregion

//#region read_smtp_reply testing

static void test_reply_reader_rejects_malformed_and_inconsistent_replies(void)
{
    prepare_session("hello server\r\n");
    TEST_ASSERT_EQUAL_INT(1, read_smtp_reply(&session, 220));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "hello server"));
    prepare_session("250-first\r\n550 changed code\r\n");
    TEST_ASSERT_EQUAL_INT(1, read_smtp_reply(&session, 250));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "Inconsistent continuation"));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "550 changed code"));
    prepare_session("250-incomplete\r\n");
    TEST_ASSERT_EQUAL_INT(1, read_smtp_reply(&session, 250));
    TEST_ASSERT_EQUAL_INT(1, read_smtp_reply(NULL, 250));
}

static void test_reply_reader_rejects_oversized_reply(void)
{
    char reply_line[600];
    memset(reply_line, 'x', sizeof(reply_line));
    memcpy(reply_line, "250 ", 4);
    memcpy(reply_line + sizeof(reply_line) - 3, "\r\n", 3);
    prepare_session(reply_line);
    TEST_ASSERT_EQUAL_INT(1, read_smtp_reply(&session, 250));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "capacity"));
}

static void test_reply_reader_drains_unexpected_multiline_reply(void)
{
    const char *multiline_reply = "550-first reason\r\n550-more detail\r\n550 final reason\r\n";
    prepare_session(multiline_reply);
    scripted_server.read_limit = 1;
    TEST_ASSERT_EQUAL_INT(1, read_smtp_reply(&session, 250));
    TEST_ASSERT_EQUAL_UINT64(strlen(multiline_reply), scripted_server.input_offset);
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "Expected SMTP 250"));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "550-first reason"));
}

//#endregion

//#region write_smtp_text testing

static void test_writer_empty_input_and_invalid_arguments(void)
{
    prepare_session("");
    TEST_ASSERT_EQUAL_INT(0, write_smtp_text(&session, ""));
    TEST_ASSERT_EQUAL_UINT64(0, scripted_server.write_calls);
    TEST_ASSERT_EQUAL_INT(1, write_smtp_text(NULL, "hello"));
    TEST_ASSERT_EQUAL_INT(1, write_smtp_text(&session, NULL));
    session.transport.write = NULL;
    TEST_ASSERT_EQUAL_INT(1, write_smtp_text(&session, "hello"));
}

static void test_writer_reports_zero_negative_and_impossible_counts(void)
{
    for (ssize_t failure_result = -1; failure_result <= 0; failure_result++)
    {
        prepare_session("");
        scripted_server.write_failure_offset = 3;
        scripted_server.write_failure_result = failure_result;
        TEST_ASSERT_EQUAL_INT(1, write_smtp_text(&session, "abcdef"));
        TEST_ASSERT_EQUAL_STRING("abc", scripted_server.output_bytes);
        TEST_ASSERT_NOT_NULL(strstr(session.error_message, "Failed to write"));
    }

    prepare_session("");
    scripted_server.invalid_write_count = 1;
    TEST_ASSERT_EQUAL_INT(1, write_smtp_text(&session, "abcdef"));
    TEST_ASSERT_NOT_NULL(strstr(session.error_message, "more bytes"));
}

//#endregion

//#region send_smtp_command testing

static void test_command_failure_never_reads_after_failed_write(void)
{
    prepare_session("250 hello\r\n");
    scripted_server.write_failure_offset = 2;
    TEST_ASSERT_EQUAL_INT(1, send_smtp_command(&session, SMTP_COMMAND_HELO, "localhost", 250));
    TEST_ASSERT_EQUAL_UINT64(0, scripted_server.read_calls);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
}

static void test_command_rejects_invalid_input_without_io(void)
{
    prepare_session("250 hello\r\n");
    TEST_ASSERT_EQUAL_INT(1, send_smtp_command(NULL, SMTP_COMMAND_HELO, "localhost", 250));
    TEST_ASSERT_EQUAL_INT(1, send_smtp_command(&session, SMTP_COMMAND_HELO, "bad\r\nQUIT", 250));
    TEST_ASSERT_EQUAL_UINT64(0, scripted_server.read_calls);
    TEST_ASSERT_EQUAL_UINT64(0, scripted_server.write_calls);
}

//#endregion

//#region run_smtp_session testing

static void test_complete_session_stops_on_wrong_code_at_every_stage(void)
{
    const int expected_codes[] =
    {
        220, 250, 250, 250, 354, 250, 221
    };
    const char *wrong_replies[] =
    {
        "550 rejected by fake server\r\n", "251 unexpected success\r\n", "450 temporary failure\r\n"
    };
    for (size_t error_index = 0; error_index < 3; error_index++)
    {
        size_t expected_output_length = 0;
        for (size_t stage_index = 0; stage_index < 7; stage_index++)
        {
            scripted_exchange_t exchanges[7];
            memcpy(exchanges, successful_exchanges, sizeof(exchanges));
            exchanges[stage_index].reply = wrong_replies[error_index];
            prepare_ordered_session(exchanges);
            TEST_ASSERT_EQUAL_INT(1, run_smtp_session(&session, "client.example", &message));
            expected_output_length += strlen(exchanges[stage_index].request);
            assert_transcript_prefix(expected_output_length);
            char expected_description[32];
            snprintf(expected_description, sizeof(expected_description), "Expected SMTP %d", expected_codes[stage_index]);
            TEST_ASSERT_NOT_NULL(strstr(session.error_message, expected_description));
            TEST_ASSERT_NOT_NULL(strstr(session.error_message, wrong_replies[error_index]));
            TEST_ASSERT_EQUAL_UINT64(stage_index + 1, scripted_server.exchange_index);
        }
    }
}

static void test_complete_session_disconnect_at_every_reply_byte(void)
{
    size_t reply_start = 0;
    size_t expected_output_length = 0;
    for (size_t stage_index = 0; stage_index < 7; stage_index++)
    {
        expected_output_length += strlen(successful_exchanges[stage_index].request);
        size_t reply_end = reply_start + strlen(successful_exchanges[stage_index].reply);
        for (size_t disconnect_offset = reply_start; disconnect_offset < reply_end; disconnect_offset++)
        {
            prepare_session(successful_replies);
            scripted_server.input_length = disconnect_offset;
            TEST_ASSERT_EQUAL_INT(1, run_smtp_session(&session, "client.example", &message));
            TEST_ASSERT_NOT_NULL(strstr(session.error_message, "disconnected"));
            assert_transcript_prefix(expected_output_length);
        }

        reply_start = reply_end;
    }
}

static void test_complete_session_read_error_at_every_reply_byte(void)
{
    for (size_t failure_offset = 0; failure_offset < strlen(successful_replies); failure_offset++)
    {
        prepare_session(successful_replies);
        scripted_server.read_failure_offset = failure_offset;
        TEST_ASSERT_EQUAL_INT(1, run_smtp_session(&session, "client.example", &message));
        TEST_ASSERT_NOT_NULL(strstr(session.error_message, "Failed to read"));
        TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
    }
}

static void test_complete_session_write_failure_at_every_output_byte(void)
{
    for (ssize_t failure_result = -1; failure_result <= 0; failure_result++)
    {
        for (size_t failure_offset = 0; failure_offset < strlen(expected_transcript); failure_offset++)
        {
            prepare_ordered_session(successful_exchanges);
            scripted_server.write_failure_offset = failure_offset;
            scripted_server.write_failure_result = failure_result;
            TEST_ASSERT_EQUAL_INT(1, run_smtp_session(&session, "client.example", &message));
            TEST_ASSERT_NOT_NULL(strstr(session.error_message, "Failed to write"));
            assert_transcript_prefix(failure_offset);
        }
    }
}

static void test_complete_session_allocation_failure_at_every_allocation(void)
{
    prepare_ordered_session(successful_exchanges);
    TEST_ASSERT_EQUAL_INT(0, run_smtp_session(&session, "client.example", &message));
    size_t allocation_count = test_faults.allocation_calls;
    TEST_ASSERT_EQUAL_UINT64(7, allocation_count);
    for (size_t failure_call = 1; failure_call <= allocation_count; failure_call++)
    {
        prepare_ordered_session(successful_exchanges);
        test_faults.allocation_calls = 0;
        test_faults.fail_allocation_call = failure_call;
        TEST_ASSERT_EQUAL_INT(1, run_smtp_session(&session, "client.example", &message));
        TEST_ASSERT_NOT_EQUAL('\0', session.error_message[0]);
        TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
        TEST_ASSERT_EQUAL_INT(0, scripted_server.protocol_violation);
    }
}

static void test_complete_session_formatting_failure_at_every_format_call(void)
{
    prepare_ordered_session(successful_exchanges);
    TEST_ASSERT_EQUAL_INT(0, run_smtp_session(&session, "client.example", &message));
    size_t format_count = test_faults.format_calls;
    TEST_ASSERT_EQUAL_UINT64(12, format_count);
    for (size_t failure_call = 1; failure_call <= format_count; failure_call++)
    {
        prepare_ordered_session(successful_exchanges);
        test_faults.format_calls = 0;
        test_faults.fail_format_call = failure_call;
        TEST_ASSERT_EQUAL_INT(1, run_smtp_session(&session, "client.example", &message));
        TEST_ASSERT_NOT_EQUAL('\0', session.error_message[0]);
        TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
    }
}

static void test_complete_session_rejects_invalid_message_and_hostname(void)
{
    prepare_ordered_session(successful_exchanges);
    TEST_ASSERT_EQUAL_INT(1, run_smtp_session(NULL, "client.example", &message));
    TEST_ASSERT_EQUAL_INT(1, run_smtp_session(&session, "client.example", NULL));
    TEST_ASSERT_EQUAL_UINT64(0, scripted_server.read_calls);
    TEST_ASSERT_EQUAL_UINT64(0, scripted_server.write_calls);
    TEST_ASSERT_EQUAL_INT(1, run_smtp_session(&session, NULL, &message));
    TEST_ASSERT_EQUAL_UINT64(0, scripted_server.write_calls);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
}

//#endregion

//#endregion
