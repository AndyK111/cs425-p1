#define _POSIX_C_SOURCE 200809L
#include "harness/unity.h"
#include "test-support.h"
#include <errno.h>
#include <string.h>

//#region Test declarations

static void test_connection_resolves_hostname_and_service(void);
static void test_connection_rejects_invalid_arguments_without_system_calls(void);
static void test_connection_preserves_existing_descriptor(void);
static void test_connection_reports_resolver_error(void);
static void test_connection_tries_next_address_after_socket_failure(void);
static void test_connection_closes_failed_address_before_retry(void);
static void test_connection_cleans_up_each_socket_option_failure(void);
static void test_connection_reports_when_all_addresses_fail(void);
static void test_connection_reports_socket_creation_failure(void);
static void test_connection_handles_empty_resolution_and_small_error_buffer(void);
static void test_socket_reader_returns_bytes_without_adding_terminator(void);
static void test_socket_reader_retries_interrupted_receive(void);
static void test_socket_reader_reports_errors_and_invalid_arguments(void);
static void test_socket_writer_preserves_partial_write_semantics(void);
static void test_socket_writer_retries_interrupted_send(void);
static void test_socket_writer_reports_errors_and_invalid_arguments(void);
static void test_socket_close_is_idempotent_and_null_safe(void);

//#endregion

//#region Test runner

void run_socket_tests(void)
{
    UnitySetTestFile(__FILE__);
    RUN_TEST(test_connection_resolves_hostname_and_service);
    RUN_TEST(test_connection_preserves_existing_descriptor);
    RUN_TEST(test_socket_reader_returns_bytes_without_adding_terminator);
    RUN_TEST(test_socket_writer_preserves_partial_write_semantics);
    RUN_TEST(test_socket_close_is_idempotent_and_null_safe);
    RUN_TEST(test_connection_rejects_invalid_arguments_without_system_calls);
    RUN_TEST(test_connection_reports_resolver_error);
    RUN_TEST(test_connection_tries_next_address_after_socket_failure);
    RUN_TEST(test_connection_closes_failed_address_before_retry);
    RUN_TEST(test_connection_cleans_up_each_socket_option_failure);
    RUN_TEST(test_connection_reports_when_all_addresses_fail);
    RUN_TEST(test_connection_reports_socket_creation_failure);
    RUN_TEST(test_socket_reader_retries_interrupted_receive);
    RUN_TEST(test_socket_reader_reports_errors_and_invalid_arguments);
    RUN_TEST(test_socket_writer_retries_interrupted_send);
    RUN_TEST(test_socket_writer_reports_errors_and_invalid_arguments);
    RUN_TEST(test_connection_handles_empty_resolution_and_small_error_buffer);
}

//#endregion

//#region connect_smtp_socket testing

static void test_connection_resolves_hostname_and_service(void)
{
    smtp_socket_t socket_state =
    {
        -1
    };
    char error_message[256];
    TEST_ASSERT_EQUAL_INT(0, connect_smtp_socket(&socket_state, "smtp.example.invalid", "smtp", error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_STRING("smtp.example.invalid", test_faults.hostname);
    TEST_ASSERT_EQUAL_STRING("smtp", test_faults.port);
    TEST_ASSERT_EQUAL_INT(AF_UNSPEC, test_faults.address_family);
    TEST_ASSERT_EQUAL_INT(SOCK_STREAM, test_faults.socket_type);
    TEST_ASSERT_EQUAL_INT(41, socket_state.socket_descriptor);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.lookup_calls);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.free_address_calls);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.connect_calls);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.close_calls);
    TEST_ASSERT_TRUE(test_faults.receive_timeout > 0);
    TEST_ASSERT_TRUE(test_faults.send_timeout > 0);
    TEST_ASSERT_EQUAL_STRING("", error_message);
    close_smtp_socket(&socket_state);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.close_calls);
}

static void test_connection_preserves_existing_descriptor(void)
{
    smtp_socket_t socket_state =
    {
        73
    };
    char error_message[256];
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "host", "25", error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_INT(73, socket_state.socket_descriptor);
    TEST_ASSERT_NOT_NULL(strstr(error_message, "existing socket"));
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.lookup_calls);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.close_calls);
}


//#endregion

//#region read_smtp_socket testing

static void test_socket_reader_returns_bytes_without_adding_terminator(void)
{
    smtp_socket_t socket_state =
    {
        41
    };
    char read_buffer[8];
    memset(read_buffer, '!', sizeof(read_buffer));
    test_faults.received_bytes = "abcdef";
    test_faults.read_limit = 3;
    TEST_ASSERT_EQUAL_INT64(3, read_smtp_socket(&socket_state, read_buffer, sizeof(read_buffer)));
    TEST_ASSERT_EQUAL_MEMORY("abc!", read_buffer, 4);
    TEST_ASSERT_EQUAL_INT(41, test_faults.last_descriptor);
    TEST_ASSERT_EQUAL_INT(0, test_faults.last_flags);
    TEST_ASSERT_EQUAL_INT64(3, read_smtp_socket(&socket_state, read_buffer, sizeof(read_buffer)));
    TEST_ASSERT_EQUAL_MEMORY("def!", read_buffer, 4);
    TEST_ASSERT_EQUAL_INT64(0, read_smtp_socket(&socket_state, read_buffer, sizeof(read_buffer)));
}

//#endregion

//#region write_smtp_socket testing

static void test_socket_writer_preserves_partial_write_semantics(void)
{
    smtp_socket_t socket_state =
    {
        41
    };
    test_faults.write_limit = 3;
    TEST_ASSERT_EQUAL_INT64(3, write_smtp_socket(&socket_state, "abcdef", 6));
    TEST_ASSERT_EQUAL_STRING("abc", test_faults.written_bytes);
    TEST_ASSERT_EQUAL_INT(41, test_faults.last_descriptor);
#ifdef MSG_NOSIGNAL
    TEST_ASSERT_EQUAL_INT(MSG_NOSIGNAL, test_faults.last_flags & MSG_NOSIGNAL);
#endif
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.write_calls);
    TEST_ASSERT_EQUAL_INT64(0, write_smtp_socket(&socket_state, "", 0));
    TEST_ASSERT_EQUAL_STRING("abc", test_faults.written_bytes);
}

//#endregion

//#region close_smtp_socket testing

static void test_socket_close_is_idempotent_and_null_safe(void)
{
    smtp_socket_t socket_state =
    {
        41
    };
    close_smtp_socket(NULL);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.close_calls);
    close_smtp_socket(&socket_state);
    TEST_ASSERT_EQUAL_INT(-1, socket_state.socket_descriptor);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.close_calls);
    TEST_ASSERT_EQUAL_INT(41, test_faults.closed_descriptors[0]);
    close_smtp_socket(&socket_state);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.close_calls);
}

//#endregion

//#region Exception testing

//#region connect_smtp_socket testing

static void test_connection_rejects_invalid_arguments_without_system_calls(void)
{
    smtp_socket_t socket_state =
    {
        -1
    };
    char error_message[256] = "unchanged";
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "host", "25", NULL, 5));
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "host", "25", error_message, 0));
    TEST_ASSERT_EQUAL_STRING("unchanged", error_message);
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(NULL, "host", "25", error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, NULL, "25", error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "host", NULL, error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "", "25", error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "host", "", error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.lookup_calls);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.socket_calls);
    TEST_ASSERT_EQUAL_INT(-1, socket_state.socket_descriptor);
}

static void test_connection_reports_resolver_error(void)
{
    smtp_socket_t socket_state =
    {
        -1
    };
    char error_message[256];
    test_faults.lookup_result = EAI_NONAME;
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "missing.invalid", "2525", error_message, sizeof(error_message)));
    TEST_ASSERT_NOT_NULL(strstr(error_message, "missing.invalid:2525"));
    TEST_ASSERT_NOT_NULL(strstr(error_message, gai_strerror(EAI_NONAME)));
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.socket_calls);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.free_address_calls);
    TEST_ASSERT_EQUAL_INT(-1, socket_state.socket_descriptor);
}

static void test_connection_tries_next_address_after_socket_failure(void)
{
    smtp_socket_t socket_state =
    {
        -1
    };
    char error_message[256];
    test_faults.socket_failures = 1;
    TEST_ASSERT_EQUAL_INT(0, connect_smtp_socket(&socket_state, "host", "2525", error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_INT(42, socket_state.socket_descriptor);
    TEST_ASSERT_EQUAL_UINT64(2, test_faults.socket_calls);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.connect_calls);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.close_calls);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.free_address_calls);
    close_smtp_socket(&socket_state);
}

static void test_connection_closes_failed_address_before_retry(void)
{
    smtp_socket_t socket_state =
    {
        -1
    };
    char error_message[256];
    test_faults.connect_failures = 1;
    TEST_ASSERT_EQUAL_INT(0, connect_smtp_socket(&socket_state, "host", "2525", error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_INT(42, socket_state.socket_descriptor);
    TEST_ASSERT_EQUAL_UINT64(2, test_faults.connect_calls);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.close_calls);
    TEST_ASSERT_EQUAL_INT(41, test_faults.closed_descriptors[0]);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.free_address_calls);
    close_smtp_socket(&socket_state);
    TEST_ASSERT_EQUAL_INT(42, test_faults.closed_descriptors[1]);
}

static void test_connection_cleans_up_each_socket_option_failure(void)
{
    size_t option_count = 2;
#ifdef SO_NOSIGPIPE
    option_count = 3;
#endif
    for (size_t option_index = 1; option_index <= option_count; option_index++)
    {
        reset_test_support();
        smtp_socket_t socket_state =
        {
            -1
        };
        char error_message[256];
        test_faults.option_failure_call = option_index;
        TEST_ASSERT_EQUAL_INT(0, connect_smtp_socket(&socket_state, "host", "25", error_message, sizeof(error_message)));
        TEST_ASSERT_EQUAL_INT(42, socket_state.socket_descriptor);
        TEST_ASSERT_EQUAL_UINT64(1, test_faults.close_calls);
        TEST_ASSERT_EQUAL_INT(41, test_faults.closed_descriptors[0]);
        TEST_ASSERT_EQUAL_UINT64(1, test_faults.connect_calls);
        TEST_ASSERT_EQUAL_UINT64(1, test_faults.free_address_calls);
        close_smtp_socket(&socket_state);
    }
}

static void test_connection_reports_when_all_addresses_fail(void)
{
    smtp_socket_t socket_state =
    {
        -1
    };
    char error_message[256];
    test_faults.connect_failures = 2;
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "host", "25", error_message, sizeof(error_message)));
    TEST_ASSERT_EQUAL_INT(-1, socket_state.socket_descriptor);
    TEST_ASSERT_EQUAL_UINT64(2, test_faults.close_calls);
    TEST_ASSERT_EQUAL_INT(41, test_faults.closed_descriptors[0]);
    TEST_ASSERT_EQUAL_INT(42, test_faults.closed_descriptors[1]);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.free_address_calls);
    TEST_ASSERT_NOT_NULL(strstr(error_message, "host:25"));
    TEST_ASSERT_NOT_NULL(strstr(error_message, strerror(ECONNREFUSED)));
    close_smtp_socket(&socket_state);
    TEST_ASSERT_EQUAL_UINT64(2, test_faults.close_calls);
}

static void test_connection_reports_socket_creation_failure(void)
{
    smtp_socket_t socket_state =
    {
        -1
    };
    char error_message[256];
    test_faults.socket_failures = 2;
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "host", "25", error_message, sizeof(error_message)));
    TEST_ASSERT_NOT_NULL(strstr(error_message, strerror(EMFILE)));
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.close_calls);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.connect_calls);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.free_address_calls);
}

static void test_connection_handles_empty_resolution_and_small_error_buffer(void)
{
    smtp_socket_t socket_state =
    {
        -1
    };
    char error_buffer[3] =
    {
        '!', '!', '!'
    };
    test_faults.address_count = 0;
    TEST_ASSERT_EQUAL_INT(1, connect_smtp_socket(&socket_state, "host", "25", error_buffer, 1));
    TEST_ASSERT_EQUAL_CHAR('\0', error_buffer[0]);
    TEST_ASSERT_EQUAL_CHAR('!', error_buffer[1]);
    TEST_ASSERT_EQUAL_CHAR('!', error_buffer[2]);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.socket_calls);
    TEST_ASSERT_EQUAL_UINT64(1, test_faults.free_address_calls);
}

//#endregion

//#region read_smtp_socket testing

static void test_socket_reader_retries_interrupted_receive(void)
{
    smtp_socket_t socket_state =
    {
        41
    };
    char read_buffer[8];
    test_faults.received_bytes = "reply";
    test_faults.read_interrupts = 3;
    TEST_ASSERT_EQUAL_INT64(5, read_smtp_socket(&socket_state, read_buffer, sizeof(read_buffer)));
    TEST_ASSERT_EQUAL_MEMORY("reply", read_buffer, 5);
    TEST_ASSERT_EQUAL_UINT64(4, test_faults.read_calls);
}

static void test_socket_reader_reports_errors_and_invalid_arguments(void)
{
    smtp_socket_t socket_state =
    {
        41
    };
    char read_buffer[8];
    TEST_ASSERT_EQUAL_INT64(-1, read_smtp_socket(NULL, read_buffer, sizeof(read_buffer)));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_INT64(-1, read_smtp_socket(&socket_state, NULL, sizeof(read_buffer)));
    socket_state.socket_descriptor = -1;
    TEST_ASSERT_EQUAL_INT64(-1, read_smtp_socket(&socket_state, read_buffer, sizeof(read_buffer)));
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.read_calls);
    socket_state.socket_descriptor = 41;
    test_faults.read_error = ECONNRESET;
    TEST_ASSERT_EQUAL_INT64(-1, read_smtp_socket(&socket_state, read_buffer, sizeof(read_buffer)));
    TEST_ASSERT_EQUAL_INT(ECONNRESET, errno);
    test_faults.read_error = EAGAIN;
    TEST_ASSERT_EQUAL_INT64(-1, read_smtp_socket(&socket_state, read_buffer, sizeof(read_buffer)));
    TEST_ASSERT_EQUAL_INT(EAGAIN, errno);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.close_calls);
}

//#endregion

//#region write_smtp_socket testing

static void test_socket_writer_retries_interrupted_send(void)
{
    smtp_socket_t socket_state =
    {
        41
    };
    test_faults.write_interrupts = 3;
    TEST_ASSERT_EQUAL_INT64(6, write_smtp_socket(&socket_state, "QUIT\r\n", 6));
    TEST_ASSERT_EQUAL_STRING("QUIT\r\n", test_faults.written_bytes);
    TEST_ASSERT_EQUAL_UINT64(4, test_faults.write_calls);
}

static void test_socket_writer_reports_errors_and_invalid_arguments(void)
{
    smtp_socket_t socket_state =
    {
        41
    };
    TEST_ASSERT_EQUAL_INT64(-1, write_smtp_socket(NULL, "x", 1));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_INT64(-1, write_smtp_socket(&socket_state, NULL, 1));
    socket_state.socket_descriptor = -1;
    TEST_ASSERT_EQUAL_INT64(-1, write_smtp_socket(&socket_state, "x", 1));
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.write_calls);
    socket_state.socket_descriptor = 41;
    test_faults.write_error = EPIPE;
    TEST_ASSERT_EQUAL_INT64(-1, write_smtp_socket(&socket_state, "x", 1));
    TEST_ASSERT_EQUAL_INT(EPIPE, errno);
    test_faults.write_error = EAGAIN;
    TEST_ASSERT_EQUAL_INT64(-1, write_smtp_socket(&socket_state, "x", 1));
    TEST_ASSERT_EQUAL_INT(EAGAIN, errno);
    TEST_ASSERT_EQUAL_UINT64(0, test_faults.close_calls);
}

//#endregion

//#endregion
