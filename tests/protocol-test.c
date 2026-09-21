#include "harness/unity.h"
#include "test-support.h"
#include <string.h>

//#region Test declarations

static void test_reply_parses_status_and_final_flag(void);
static void test_reply_rejects_malformed_lines_without_changing_output(void);
static void test_reply_enforces_512_byte_limit(void);
static void test_commands_have_exact_wire_format(void);
static void test_commands_reject_invalid_arguments_and_injection(void);
static void test_command_length_boundaries(void);
static void test_command_allocation_failure(void);
static void test_command_size_calculation_failure(void);
static void test_command_output_formatting_failure_releases_memory(void);
static void test_dot_stuffing_normalizes_all_line_endings(void);
static void test_dot_stuffing_preserves_message_content(void);
static void test_dot_stuffing_large_body(void);
static void test_dot_stuffing_invalid_input_and_allocation_failure(void);
static void test_dot_stuffing_prevents_allocation_size_overflow(void);
static void test_payload_exact_headers_and_data_terminator(void);
static void test_payload_empty_subject_and_body(void);
static void test_payload_rejects_null_fields(void);
static void test_payload_rejects_header_injection(void);
static void test_payload_header_length_boundaries(void);
static void test_payload_body_length_boundary_excludes_stuffed_dot(void);
static void test_payload_allocation_failures_release_intermediate_body(void);
static void test_payload_formatting_failures_release_intermediate_body(void);
static void test_starter_greeting_remains_compatible(void);
static void test_starter_greeting_allocation_failure(void);
static void test_starter_greeting_formatting_failures(void);
static void assert_owned_string(const char *expected_text, char *actual_text);
static smtp_message_t example_message(void);

//#endregion

//#region Test runner

void run_protocol_tests(void)
{
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_reply_parses_status_and_final_flag);
    RUN_TEST(test_reply_enforces_512_byte_limit);
    RUN_TEST(test_commands_have_exact_wire_format);
    RUN_TEST(test_command_length_boundaries);
    RUN_TEST(test_dot_stuffing_normalizes_all_line_endings);
    RUN_TEST(test_dot_stuffing_preserves_message_content);
    RUN_TEST(test_dot_stuffing_large_body);
    RUN_TEST(test_payload_exact_headers_and_data_terminator);
    RUN_TEST(test_payload_empty_subject_and_body);
    RUN_TEST(test_payload_header_length_boundaries);
    RUN_TEST(test_payload_body_length_boundary_excludes_stuffed_dot);
    RUN_TEST(test_starter_greeting_remains_compatible);
    RUN_TEST(test_reply_rejects_malformed_lines_without_changing_output);
    RUN_TEST(test_commands_reject_invalid_arguments_and_injection);
    RUN_TEST(test_command_allocation_failure);
    RUN_TEST(test_command_size_calculation_failure);
    RUN_TEST(test_command_output_formatting_failure_releases_memory);
    RUN_TEST(test_dot_stuffing_invalid_input_and_allocation_failure);
    RUN_TEST(test_dot_stuffing_prevents_allocation_size_overflow);
    RUN_TEST(test_payload_rejects_null_fields);
    RUN_TEST(test_payload_rejects_header_injection);
    RUN_TEST(test_payload_allocation_failures_release_intermediate_body);
    RUN_TEST(test_payload_formatting_failures_release_intermediate_body);
    RUN_TEST(test_starter_greeting_allocation_failure);
    RUN_TEST(test_starter_greeting_formatting_failures);
}

//#endregion

//#region parse_smtp_reply testing

static void test_reply_parses_status_and_final_flag(void)
{
    const char *reply_lines[] =
    {
        "220 Ready\r\n", "250-First line\r\n", "250 Last line\r\n",
        "354 Continue\r\n", "421 Busy\r\n", "550 Rejected\r\n",
        "221\r\n", "250 \r\n", "250-\r\n"
    };
    const int expected_codes[] =
    {
        220, 250, 250, 354, 421, 550, 221, 250, 250
    };
    const int expected_final_flags[] =
    {
        1, 0, 1, 1, 1, 1, 1, 1, 0
    };
    for (size_t reply_index = 0; reply_index < 9; reply_index++)
    {
        smtp_reply_t reply;
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, parse_smtp_reply(reply_lines[reply_index], &reply), reply_lines[reply_index]);
        TEST_ASSERT_EQUAL_INT(expected_codes[reply_index], reply.status_code);
        TEST_ASSERT_EQUAL_INT(expected_final_flags[reply_index], reply.is_final);
    }
}

static void test_reply_enforces_512_byte_limit(void)
{
    char reply_line[514];
    memset(reply_line, 'x', sizeof(reply_line));
    memcpy(reply_line, "250 ", 4);
    memcpy(reply_line + 510, "\r\n", 3);
    smtp_reply_t reply;
    TEST_ASSERT_EQUAL_INT(0, parse_smtp_reply(reply_line, &reply));
    reply_line[510] = 'x';
    memcpy(reply_line + 511, "\r\n", 3);
    TEST_ASSERT_EQUAL_INT(1, parse_smtp_reply(reply_line, &reply));
}

//#endregion

//#region build_smtp_command testing

static void test_commands_have_exact_wire_format(void)
{
    assert_owned_string("HELO client.example\r\n", build_smtp_command(SMTP_COMMAND_HELO, "client.example"));
    assert_owned_string("MAIL FROM:<from@example.com>\r\n", build_smtp_command(SMTP_COMMAND_MAIL_FROM, "from@example.com"));
    assert_owned_string("RCPT TO:<to@example.com>\r\n", build_smtp_command(SMTP_COMMAND_RCPT_TO, "to@example.com"));
    assert_owned_string("DATA\r\n", build_smtp_command(SMTP_COMMAND_DATA, NULL));
    assert_owned_string("QUIT\r\n", build_smtp_command(SMTP_COMMAND_QUIT, NULL));
}

static void test_command_length_boundaries(void)
{
    const smtp_command_t commands[] =
    {
        SMTP_COMMAND_HELO, SMTP_COMMAND_MAIL_FROM, SMTP_COMMAND_RCPT_TO
    };
    const size_t maximum_lengths[] =
    {
        505, 498, 500
    };
    char argument[507];
    for (size_t command_index = 0; command_index < 3; command_index++)
    {
        memset(argument, 'a', sizeof(argument));
        argument[maximum_lengths[command_index]] = '\0';
        char *command_text = build_smtp_command(commands[command_index], argument);
        TEST_ASSERT_NOT_NULL(command_text);
        TEST_ASSERT_EQUAL_UINT64(512, strlen(command_text));
        TEST_ASSERT_EQUAL_STRING("\r\n", command_text + 510);
        test_free(command_text);
        argument[maximum_lengths[command_index]] = 'a';
        argument[maximum_lengths[command_index] + 1] = '\0';
        TEST_ASSERT_NULL(build_smtp_command(commands[command_index], argument));
    }
}

//#endregion

//#region stuff_smtp_body testing

static void test_dot_stuffing_normalizes_all_line_endings(void)
{
    const char *bodies[] =
    {
        "", "plain", "plain\n", "plain\r", "plain\r\n", "\n", "\r\n\r\n", "a\nb\rc\r\nd"
    };
    const char *expected_bodies[] =
    {
        "", "plain\r\n", "plain\r\n", "plain\r\n", "plain\r\n", "\r\n", "\r\n\r\n", "a\r\nb\r\nc\r\nd\r\n"
    };
    for (size_t body_index = 0; body_index < 8; body_index++)
    {
        assert_owned_string(expected_bodies[body_index], stuff_smtp_body(bodies[body_index]));
    }
}

static void test_dot_stuffing_preserves_message_content(void)
{
    char body[] = ".\n..\n.leading\r.next\r\nlast.dot\n\n";
    char original_body[sizeof(body)];
    memcpy(original_body, body, sizeof(body));
    assert_owned_string("..\r\n...\r\n..leading\r\n..next\r\nlast.dot\r\n\r\n", stuff_smtp_body(body));
    TEST_ASSERT_EQUAL_MEMORY(original_body, body, sizeof(body));
    assert_owned_string("..\r\n", stuff_smtp_body("."));
    assert_owned_string("...\r\n", stuff_smtp_body(".."));
}

static void test_dot_stuffing_large_body(void)
{
    char body[6001];
    char expected_body[12001];
    for (size_t line_index = 0; line_index < 3000; line_index++)
    {
        memcpy(body + line_index * 2, ".\n", 2);
        memcpy(expected_body + line_index * 4, "..\r\n", 4);
    }

    body[6000] = '\0';
    expected_body[12000] = '\0';
    assert_owned_string(expected_body, stuff_smtp_body(body));
}

//#endregion

//#region build_smtp_payload testing

static void test_payload_exact_headers_and_data_terminator(void)
{
    smtp_message_t message = example_message();
    assert_owned_string("From: <sender@example.com>\r\nTo: <recipient@example.com>\r\nSubject: A subject\r\n\r\nHello\r\n..leading\r\n..\r\nlast\r\n.\r\n", build_smtp_payload(&message));
    TEST_ASSERT_EQUAL_STRING("Hello\n.leading\n.\nlast", message.body);
}

static void test_payload_empty_subject_and_body(void)
{
    smtp_message_t message = example_message();
    message.subject = "";
    message.body = "";
    assert_owned_string("From: <sender@example.com>\r\nTo: <recipient@example.com>\r\nSubject: \r\n\r\n.\r\n", build_smtp_payload(&message));
}

static void test_payload_header_length_boundaries(void)
{
    char header_value[994];
    const size_t maximum_lengths[] =
    {
        990, 992, 989
    };
    for (size_t field_index = 0; field_index < 3; field_index++)
    {
        smtp_message_t message = example_message();
        memset(header_value, 'a', sizeof(header_value));
        header_value[maximum_lengths[field_index]] = '\0';
        if (field_index == 0) message.sender_address = header_value;
        if (field_index == 1) message.recipient_address = header_value;
        if (field_index == 2) message.subject = header_value;
        char *payload = build_smtp_payload(&message);
        TEST_ASSERT_NOT_NULL(payload);
        test_free(payload);
        header_value[maximum_lengths[field_index]] = 'a';
        header_value[maximum_lengths[field_index] + 1] = '\0';
        TEST_ASSERT_NULL(build_smtp_payload(&message));
    }
}

static void test_payload_body_length_boundary_excludes_stuffed_dot(void)
{
    char body[1000];
    memset(body, 'a', sizeof(body));
    body[0] = '.';
    body[998] = '\0';
    smtp_message_t message = example_message();
    message.body = body;
    char *payload = build_smtp_payload(&message);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_NOT_NULL(strstr(payload, "\r\n\r\n..aaaa"));
    test_free(payload);
    body[998] = 'a';
    body[999] = '\0';
    TEST_ASSERT_NULL(build_smtp_payload(&message));
}

//#endregion

//#region get_greeting testing

static void test_starter_greeting_remains_compatible(void)
{
    assert_owned_string("Hello, Alice!", get_greeting("Alice"));
    assert_owned_string("Hello, !", get_greeting(""));
    TEST_ASSERT_NULL(get_greeting(NULL));
}

//#endregion

//#region Helpers

static void assert_owned_string(const char *expected_text, char *actual_text)
{
    TEST_ASSERT_NOT_NULL(actual_text);
    TEST_ASSERT_EQUAL_STRING(expected_text, actual_text);
    test_free(actual_text);
}

static smtp_message_t example_message(void)
{
    smtp_message_t message =
    {
        .sender_address = "sender@example.com",
        .recipient_address = "recipient@example.com",
        .subject = "A subject",
        .body = "Hello\n.leading\n.\nlast"
    };
    return message;
}

//#endregion

//#region Exception testing

//#region parse_smtp_reply testing

static void test_reply_rejects_malformed_lines_without_changing_output(void)
{
    const char *invalid_lines[] =
    {
        "", "2", "250", "250\n", "250 OK\n", "250 OK\rX", "250 OKX\n",
        "150 OK\r\n", "650 OK\r\n", "2/0 OK\r\n", "2:0 OK\r\n",
        "25/ OK\r\n", "25: OK\r\n", "250X\r\n", "250\tOK\r\n",
        "250 OK\rinside\r\n", "250 OK\ninside\r\n", "250 OK\r\n250 next\r\n"
    };
    for (size_t line_index = 0; line_index < sizeof(invalid_lines) / sizeof(invalid_lines[0]); line_index++)
    {
        smtp_reply_t reply =
        {
            777, 7
        };
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, parse_smtp_reply(invalid_lines[line_index], &reply), invalid_lines[line_index]);
        TEST_ASSERT_EQUAL_INT(777, reply.status_code);
        TEST_ASSERT_EQUAL_INT(7, reply.is_final);
    }

    smtp_reply_t reply;
    TEST_ASSERT_EQUAL_INT(1, parse_smtp_reply(NULL, &reply));
    TEST_ASSERT_EQUAL_INT(1, parse_smtp_reply("250 OK\r\n", NULL));
}

//#endregion

//#region build_smtp_command testing

static void test_commands_reject_invalid_arguments_and_injection(void)
{
    const char *invalid_arguments[] =
    {
        NULL, "", "bad\rhost", "bad\nhost", "bad\r\nQUIT"
    };
    for (size_t argument_index = 0; argument_index < 5; argument_index++)
    {
        TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_HELO, invalid_arguments[argument_index]));
        TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_MAIL_FROM, invalid_arguments[argument_index]));
        TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_RCPT_TO, invalid_arguments[argument_index]));
    }

    TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_HELO, "two hosts"));
    TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_HELO, "two\thosts"));
    TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_MAIL_FROM, "<sender@example.com>"));
    TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_RCPT_TO, "recipient>example.com"));
    TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_DATA, "unexpected"));
    TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_QUIT, ""));
    TEST_ASSERT_NULL(build_smtp_command((smtp_command_t)-1, NULL));
    TEST_ASSERT_NULL(build_smtp_command((smtp_command_t)99, "argument"));
}

static void test_command_allocation_failure(void)
{
    test_faults.fail_allocation_call = 1;
    TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_HELO, "localhost"));
}

static void test_command_size_calculation_failure(void)
{
    test_faults.fail_format_call = 1;
    TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_HELO, "localhost"));
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.allocation_calls);
}

static void test_command_output_formatting_failure_releases_memory(void)
{
    test_faults.fail_format_call = 2;
    TEST_ASSERT_NULL(build_smtp_command(SMTP_COMMAND_HELO, "localhost"));
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
}

//#endregion

//#region stuff_smtp_body testing

static void test_dot_stuffing_invalid_input_and_allocation_failure(void)
{
    TEST_ASSERT_NULL(stuff_smtp_body(NULL));
    test_faults.fail_allocation_call = 1;
    TEST_ASSERT_NULL(stuff_smtp_body("body"));
}

static void test_dot_stuffing_prevents_allocation_size_overflow(void)
{
    const char body[] = "simulated huge string";
    test_faults.oversized_string = body;
    TEST_ASSERT_NULL(stuff_smtp_body(body));
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.allocation_calls);
}

//#endregion

//#region build_smtp_payload testing

static void test_payload_rejects_null_fields(void)
{
    smtp_message_t message = example_message();
    TEST_ASSERT_NULL(build_smtp_payload(NULL));
    message.sender_address = NULL;
    TEST_ASSERT_NULL(build_smtp_payload(&message));
    message = example_message();
    message.recipient_address = NULL;
    TEST_ASSERT_NULL(build_smtp_payload(&message));
    message = example_message();
    message.subject = NULL;
    TEST_ASSERT_NULL(build_smtp_payload(&message));
    message = example_message();
    message.body = NULL;
    TEST_ASSERT_NULL(build_smtp_payload(&message));
}

static void test_payload_rejects_header_injection(void)
{
    const char *invalid_fields[] =
    {
        "", "x\rInjected", "x\nInjected", "x\r\nBcc: someone", "<x>", "x>"
    };
    for (size_t field_index = 0; field_index < 6; field_index++)
    {
        smtp_message_t message = example_message();
        message.sender_address = invalid_fields[field_index];
        TEST_ASSERT_NULL(build_smtp_payload(&message));
        message = example_message();
        message.recipient_address = invalid_fields[field_index];
        TEST_ASSERT_NULL(build_smtp_payload(&message));
    }

    smtp_message_t message = example_message();
    message.subject = "subject\rInjected";
    TEST_ASSERT_NULL(build_smtp_payload(&message));
    message.subject = "subject\nInjected";
    TEST_ASSERT_NULL(build_smtp_payload(&message));
}

static void test_payload_allocation_failures_release_intermediate_body(void)
{
    smtp_message_t message = example_message();
    for (size_t failure_index = 1; failure_index <= 2; failure_index++)
    {
        test_faults.allocation_calls = 0;
        test_faults.fail_allocation_call = failure_index;
        TEST_ASSERT_NULL(build_smtp_payload(&message));
        TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
    }
}

static void test_payload_formatting_failures_release_intermediate_body(void)
{
    smtp_message_t message = example_message();
    for (size_t failure_index = 1; failure_index <= 2; failure_index++)
    {
        test_faults.format_calls = 0;
        test_faults.fail_format_call = failure_index;
        TEST_ASSERT_NULL(build_smtp_payload(&message));
        TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
    }
}

//#endregion

//#region get_greeting testing

static void test_starter_greeting_allocation_failure(void)
{
    test_faults.fail_allocation_call = 1;
    TEST_ASSERT_NULL(get_greeting("Alice"));
}

static void test_starter_greeting_formatting_failures(void)
{
    for (size_t failure_index = 1; failure_index <= 2; failure_index++)
    {
        test_faults.format_calls = 0;
        test_faults.fail_format_call = failure_index;
        TEST_ASSERT_NULL(get_greeting("Alice"));
        TEST_ASSERT_EQUAL_UINT64(0, test_faults.live_allocations);
    }
}

//#endregion

//#endregion
