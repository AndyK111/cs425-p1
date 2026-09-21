#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include "../src/lab.h"
#include <netdb.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/socket.h>

// #region Constants

#define TEST_TRANSCRIPT_CAPACITY 32768

// #endregion
// #region Type Definitions

/* (const char *, const char *) -> scripted_exchange_t
 *
 * Describes one request followed by its server reply.
 *
 * Parameters:
 * const char *request : Exact bytes required before the reply becomes readable.
 * const char *reply   : Server bytes returned after the request is complete.
 *
 * Returns: One borrowed step in a deterministic SMTP conversation.
 *
 * NOTE: An empty request represents the initial greeting. Strings must outlive
 * the exchange. Reading early or sending different bytes fails the transport.
 */
typedef struct scripted_exchange
{
    const char *request;
    const char *reply;
} scripted_exchange_t;

/* (byte stream, limits, failure offsets, exchange script) -> scripted_transport_t
 *
 * Holds an in-memory server with controllable reads, writes, and failures.
 *
 * Parameters:
 * input_bytes/input_length/input_offset : Incoming bytes and read position.
 * output_bytes/output_length           : Captured client bytes and length.
 * read_limit/write_limit               : Maximum bytes per callback.
 * read_failure_offset/write_failure_offset : Byte offsets that trigger failure.
 * read_failure_result/write_failure_result : Zero for EOF or -1 for I/O error.
 * invalid_read_count/invalid_write_count   : Return an impossible byte count.
 * read_calls/write_calls                  : Callback invocation counters.
 * exchanges/exchange_count/exchange_index : Optional ordered conversation.
 * request_offset/reply_offset            : Progress within that conversation.
 * protocol_violation                     : Records out-of-order or wrong I/O.
 *
 * Returns: State configured by initialize_scripted_transport.
 *
 * NOTE: No real I/O or allocations. SIZE_MAX disables a failure offset.
 */
typedef struct scripted_transport
{
    const char *input_bytes;
    size_t input_length;
    size_t input_offset;
    char output_bytes[TEST_TRANSCRIPT_CAPACITY];
    size_t output_length;
    size_t read_limit;
    size_t write_limit;
    size_t read_failure_offset;
    size_t write_failure_offset;
    ssize_t read_failure_result;
    ssize_t write_failure_result;
    int invalid_read_count;
    int invalid_write_count;
    size_t read_calls;
    size_t write_calls;
    const scripted_exchange_t *exchanges;
    size_t exchange_count;
    size_t exchange_index;
    size_t request_offset;
    size_t reply_offset;
    int protocol_violation;
} scripted_transport_t;

/* (counters, failure selectors, recorded socket arguments) -> test_faults_t
 *
 * Controls failures in library/socket calls made by the application under TEST.
 *
 * Parameters:
 * allocation_calls/fail_allocation_call/live_allocations : Allocation tracking.
 * format_calls/fail_format_call : One-based formatting failure selection.
 * oversized_string             : String whose measured size becomes SIZE_MAX.
 * lookup_result/address_count  : DNS result and number of fake addresses.
 * socket_failures/connect_failures/option_failure_call : Socket failure controls.
 * lookup_calls/socket_calls/connect_calls/option_calls : Recorded call counts.
 * free_address_calls/close_calls/closed_descriptors    : Resource cleanup record.
 * hostname/port/address_family/socket_type             : Resolver arguments.
 * read_interrupts/write_interrupts : EINTR results before normal socket I/O.
 * read_error/write_error          : errno for a persistent socket I/O failure.
 * read_calls/write_calls/read_limit/write_limit : Socket callback accounting.
 * received_bytes/received_offset/written_bytes/written_length : Fake socket data.
 * last_descriptor/last_flags/receive_timeout/send_timeout : Socket arguments.
 *
 * Returns: Global test controls reset before each Unity test.
 *
 * NOTE: Descriptor numbers are synthetic. None of these operations use DNS
 * or operating-system sockets. A zero failure selector disables that fault.
 */
typedef struct test_faults
{
    size_t allocation_calls;
    size_t fail_allocation_call;
    size_t live_allocations;
    size_t format_calls;
    size_t fail_format_call;
    const char *oversized_string;
    int lookup_result;
    size_t address_count;
    size_t socket_failures;
    size_t connect_failures;
    size_t option_failure_call;
    size_t lookup_calls;
    size_t socket_calls;
    size_t connect_calls;
    size_t option_calls;
    size_t free_address_calls;
    size_t close_calls;
    int closed_descriptors[16];
    char hostname[256];
    char port[32];
    int address_family;
    int socket_type;
    size_t read_interrupts;
    size_t write_interrupts;
    int read_error;
    int write_error;
    size_t read_calls;
    size_t write_calls;
    size_t read_limit;
    size_t write_limit;
    const char *received_bytes;
    size_t received_offset;
    char written_bytes[1024];
    size_t written_length;
    int last_descriptor;
    int last_flags;
    long receive_timeout;
    long send_timeout;
} test_faults_t;

// #endregion
// #region Test State

extern test_faults_t test_faults;

// #endregion
// #region Functions

/* (void) -> void
 *
 * Resets fault controls and fake socket state before a test.
 *
 * Parameters: None.
 * Returns: Nothing.
 * NOTE: Call finish_test_support after each test to release tracked memory.
 */
void reset_test_support(void);

/* (void) -> size_t
 *
 * Frees outstanding tracked allocations and reports how many were leaked.
 *
 * Parameters: None.
 * Returns: Number of live application allocations at the end of the test.
 * NOTE: Cleanup runs even if a Unity assertion interrupted the test.
 */
size_t finish_test_support(void);

/* (scripted_transport_t *, const char *) -> smtp_transport_t
 *
 * Initializes an in-memory byte stream and returns its transport callbacks.
 *
 * Parameters:
 * scripted_transport_t *server : State to initialize.
 * const char *incoming_bytes   : Borrowed incoming stream, possibly empty.
 *
 * Returns: Read/write callbacks with server as their context.
 * NOTE: The caller keeps server and incoming_bytes alive during the session.
 */
smtp_transport_t initialize_scripted_transport(scripted_transport_t *server,
                                              const char *incoming_bytes);

/* (size_t) -> void *
 *
 * Allocates tracked memory or injects a selected allocation failure.
 *
 * Parameters:
 * size_t allocation_size : Requested byte count.
 *
 * Returns: Allocated memory, or NULL on the configured failure or real failure.
 * NOTE: Release with test_free; test teardown detects any missing release.
 */
void *test_malloc(size_t allocation_size);

/* (void *) -> void
 *
 * Releases memory allocated through test_malloc.
 *
 * Parameters:
 * void *allocation : Allocation to free, or NULL for no action.
 *
 * Returns: Nothing.
 * NOTE: Also removes the allocation from the per-test ownership registry.
 */
void test_free(void *allocation);

/* (const char *) -> size_t
 *
 * Measures a string, with a selectable SIZE_MAX result for overflow checking.
 *
 * Parameters:
 * const char *text : Null terminated string to measure.
 *
 * Returns: strlen(text), or SIZE_MAX for test_faults.oversized_string.
 * NOTE: The substituted size does not cause an oversized allocation.
 */
size_t test_strlen(const char *text);

/* (char *, size_t, const char *, va_list) -> int
 *
 * Formats text, or returns -1 on the selected formatting call.
 *
 * Parameters:
 * char *destination   : Output buffer, possibly NULL when capacity is zero.
 * size_t capacity     : Output buffer capacity.
 * const char *format  : printf-compatible format string.
 * va_list arguments   : Formatting arguments.
 *
 * Returns: The real vsnprintf result, or -1 for an injected library error.
 * NOTE: No failure is injected into the test runner or Unity itself.
 */
int test_vsnprintf(char *destination, size_t capacity, const char *format, va_list arguments)
    __attribute__((format(printf, 3, 0)));

/* (char *, size_t, const char *, ...) -> int
 *
 * Provides the variadic form of the controllable formatting operation.
 *
 * Parameters:
 * char *destination  : Output buffer.
 * size_t capacity    : Output buffer capacity.
 * const char *format : Format string followed by matching arguments.
 *
 * Returns: The real snprintf-compatible result, or -1 on the selected call.
 * NOTE: Shares the formatting failure counter with test_vsnprintf.
 */
int test_snprintf(char *destination, size_t capacity, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

/* (const char *, const char *, const struct addrinfo *, struct addrinfo **) -> int
 *
 * Supplies a deterministic address list without performing name resolution.
 *
 * Parameters:
 * const char *hostname           : Recorded server hostname.
 * const char *port               : Recorded port or service name.
 * const struct addrinfo *hints   : Recorded address-family and socket hints.
 * struct addrinfo **addresses    : Receives the static fake address list.
 *
 * Returns: test_faults.lookup_result, where zero means success.
 * NOTE: The returned list must be released through test_freeaddrinfo.
 */
int test_getaddrinfo(const char *hostname, const char *port,
                     const struct addrinfo *hints, struct addrinfo **addresses);

/* (struct addrinfo *) -> void
 *
 * Records release of the fake resolver list.
 *
 * Parameters:
 * struct addrinfo *addresses : List returned by test_getaddrinfo.
 *
 * Returns: Nothing.
 * NOTE: The list uses static storage, so no system allocator is called.
 */
void test_freeaddrinfo(struct addrinfo *addresses);

/* (int, int, int) -> int
 *
 * Returns a synthetic descriptor or a configured socket creation error.
 *
 * Parameters:
 * int address_family : Requested address family.
 * int socket_type    : Requested socket type.
 * int protocol       : Requested protocol.
 *
 * Returns: A synthetic descriptor, or -1 with errno set to EMFILE.
 * NOTE: Never opens an operating-system socket.
 */
int test_socket(int address_family, int socket_type, int protocol);

/* (int, int, int, const void *, socklen_t) -> int
 *
 * Records socket options and optionally injects an option-setting failure.
 *
 * Parameters:
 * int descriptor         : Synthetic socket descriptor.
 * int level              : Socket option level.
 * int option             : Option being set.
 * const void *value      : Option value.
 * socklen_t value_length : Size of the value.
 *
 * Returns: Zero on success, or -1 with errno set to EINVAL.
 * NOTE: Timeout values are copied into test_faults for assertions.
 */
int test_setsockopt(int descriptor, int level, int option, const void *value,
                    socklen_t value_length);

/* (int, const struct sockaddr *, socklen_t) -> int
 *
 * Simulates connection success or a refused connection.
 *
 * Parameters:
 * int descriptor                : Synthetic socket descriptor.
 * const struct sockaddr *address : Address supplied by the fake resolver.
 * socklen_t address_length       : Size of the address.
 *
 * Returns: Zero on success, or -1 with errno set to ECONNREFUSED.
 * NOTE: Never contacts any server.
 */
int test_connect(int descriptor, const struct sockaddr *address, socklen_t address_length);

/* (int, void *, size_t, int) -> ssize_t
 *
 * Simulates receive data, EOF, EINTR, or a persistent socket error.
 *
 * Parameters:
 * int descriptor : Recorded synthetic descriptor.
 * void *buffer   : Destination for fake incoming bytes.
 * size_t length  : Maximum byte count.
 * int flags      : Recorded receive flags.
 *
 * Returns: Bytes copied, zero for EOF, or -1 with configured errno.
 * NOTE: Data is copied from test_faults.received_bytes.
 */
ssize_t test_recv(int descriptor, void *buffer, size_t length, int flags);

/* (int, const void *, size_t, int) -> ssize_t
 *
 * Captures socket output, with optional partial writes, EINTR, or errors.
 *
 * Parameters:
 * int descriptor     : Recorded synthetic descriptor.
 * const void *buffer : Bytes supplied by the client.
 * size_t length      : Requested byte count.
 * int flags          : Recorded send flags.
 *
 * Returns: Bytes captured, or -1 with configured errno.
 * NOTE: Never sends bytes outside this process.
 */
ssize_t test_send(int descriptor, const void *buffer, size_t length, int flags);

/* (int) -> int
 *
 * Records closure of a synthetic descriptor.
 *
 * Parameters:
 * int descriptor : Descriptor to record in closed_descriptors.
 *
 * Returns: Zero for success.
 * NOTE: Does not intercept close calls outside the application's socket file.
 */
int test_close(int descriptor);

/* (void) -> void
 * Runs the protocol helper tests through Unity.
 * Parameters: None.
 * Returns: Nothing.
 * NOTE: Called once by the shared test runner.
 */
void run_protocol_tests(void);

/* (void) -> void
 * Runs the in-memory session tests through Unity.
 * Parameters: None.
 * Returns: Nothing.
 * NOTE: Called once by the shared test runner.
 */
void run_session_tests(void);

/* (void) -> void
 * Runs the socket adapter tests against deterministic system-call fakes.
 * Parameters: None.
 * Returns: Nothing.
 * NOTE: Called once by the shared test runner; no network is used.
 */
void run_socket_tests(void);

// #endregion

#endif
