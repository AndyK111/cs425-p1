#define _POSIX_C_SOURCE 200809L
#include "test-support.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

// #region Test State

test_faults_t test_faults;
static void *tracked_allocations[128];
static struct addrinfo fake_addresses[3];
static struct sockaddr_storage fake_socket_addresses[3];

// #endregion
// #region Functions

/* Private helper declarations; definitions follow the public functions. */
static size_t smaller_size(size_t first_size, size_t second_size);
static ssize_t read_scripted_bytes(void *context, void *read_buffer, size_t capacity);
static ssize_t write_scripted_bytes(void *context, const void *write_buffer, size_t byte_count);

void reset_test_support(void)
{
    memset(&test_faults, 0, sizeof(test_faults));
    memset(tracked_allocations, 0, sizeof(tracked_allocations));
    memset(fake_addresses, 0, sizeof(fake_addresses));
    memset(fake_socket_addresses, 0, sizeof(fake_socket_addresses));
    test_faults.address_count = 2;
    test_faults.read_limit = SIZE_MAX;
    test_faults.write_limit = SIZE_MAX;
    test_faults.received_bytes = "";
}

size_t finish_test_support(void)
{
    size_t leaked_allocations = test_faults.live_allocations;
    for (size_t allocation_index = 0; allocation_index < 128; allocation_index++)
    {
        free(tracked_allocations[allocation_index]);
        tracked_allocations[allocation_index] = NULL;
    }

    test_faults.live_allocations = 0;
    return leaked_allocations;
}

void *test_malloc(size_t allocation_size)
{
    test_faults.allocation_calls++;
    if (test_faults.allocation_calls == test_faults.fail_allocation_call) return NULL;

    void *allocation = malloc(allocation_size);
    if (allocation == NULL) return NULL;
    for (size_t allocation_index = 0; allocation_index < 128; allocation_index++)
    {
        if (tracked_allocations[allocation_index] == NULL)
        {
            tracked_allocations[allocation_index] = allocation;
            test_faults.live_allocations++;
            return allocation;
        }
    }

    free(allocation);
    abort();
}

void test_free(void *allocation)
{
    if (allocation == NULL) return;
    for (size_t allocation_index = 0; allocation_index < 128; allocation_index++)
    {
        if (tracked_allocations[allocation_index] == allocation)
        {
            tracked_allocations[allocation_index] = NULL;
            test_faults.live_allocations--;
            free(allocation);
            return;
        }
    }

    /* An untracked/double free is a test infrastructure error, not a leak to hide. */
    abort();
}

size_t test_strlen(const char *text)
{
    if (text == test_faults.oversized_string) return SIZE_MAX;
    return strlen(text);
}

int test_vsnprintf(char *destination, size_t capacity, const char *format, va_list arguments)
{
    test_faults.format_calls++;
    if (test_faults.format_calls == test_faults.fail_format_call) return -1;
    return vsnprintf(destination, capacity, format, arguments);
}

int test_snprintf(char *destination, size_t capacity, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int format_result = test_vsnprintf(destination, capacity, format, arguments);
    va_end(arguments);
    return format_result;
}

smtp_transport_t initialize_scripted_transport(scripted_transport_t *server,
                                              const char *incoming_bytes)
{
    memset(server, 0, sizeof(*server));
    server->input_bytes = incoming_bytes;
    server->input_length = strlen(incoming_bytes);
    server->read_limit = SIZE_MAX;
    server->write_limit = SIZE_MAX;
    server->read_failure_offset = SIZE_MAX;
    server->write_failure_offset = SIZE_MAX;
    server->read_failure_result = -1;
    server->write_failure_result = -1;
    smtp_transport_t transport =
    {
        .context = server,
        .read = read_scripted_bytes,
        .write = write_scripted_bytes
    };
    return transport;
}

int test_getaddrinfo(const char *hostname, const char *port,
                     const struct addrinfo *hints, struct addrinfo **addresses)
{
    test_faults.lookup_calls++;
    snprintf(test_faults.hostname, sizeof(test_faults.hostname), "%s", hostname);
    snprintf(test_faults.port, sizeof(test_faults.port), "%s", port);
    test_faults.address_family = hints->ai_family;
    test_faults.socket_type = hints->ai_socktype;
    *addresses = NULL;
    if (test_faults.lookup_result != 0) return test_faults.lookup_result;

    for (size_t address_index = 0; address_index < test_faults.address_count; address_index++)
    {
        fake_socket_addresses[address_index].ss_family = address_index == 0 ? AF_INET6 : AF_INET;
        fake_addresses[address_index].ai_family = fake_socket_addresses[address_index].ss_family;
        fake_addresses[address_index].ai_socktype = SOCK_STREAM;
        fake_addresses[address_index].ai_addr = (struct sockaddr *)&fake_socket_addresses[address_index];
        fake_addresses[address_index].ai_addrlen = sizeof(fake_socket_addresses[address_index]);
        fake_addresses[address_index].ai_next = address_index + 1 < test_faults.address_count
                                               ? &fake_addresses[address_index + 1] : NULL;
    }

    if (test_faults.address_count != 0) *addresses = fake_addresses;
    return 0;
}

void test_freeaddrinfo(struct addrinfo *addresses)
{
    (void)addresses;
    test_faults.free_address_calls++;
}

int test_socket(int address_family, int socket_type, int protocol)
{
    (void)address_family;
    (void)socket_type;
    (void)protocol;
    test_faults.socket_calls++;
    if (test_faults.socket_failures != 0)
    {
        test_faults.socket_failures--;
        errno = EMFILE;
        return -1;
    }

    return 40 + (int)test_faults.socket_calls;
}

int test_setsockopt(int descriptor, int level, int option, const void *value,
                    socklen_t value_length)
{
    (void)level;
    (void)value_length;
    test_faults.last_descriptor = descriptor;
    test_faults.option_calls++;
    if (test_faults.option_calls == test_faults.option_failure_call)
    {
        errno = EINVAL;
        return -1;
    }

    if (option == SO_RCVTIMEO) test_faults.receive_timeout = ((const struct timeval *)value)->tv_sec;
    if (option == SO_SNDTIMEO) test_faults.send_timeout = ((const struct timeval *)value)->tv_sec;
    return 0;
}

int test_connect(int descriptor, const struct sockaddr *address, socklen_t address_length)
{
    (void)address;
    (void)address_length;
    test_faults.last_descriptor = descriptor;
    test_faults.connect_calls++;
    if (test_faults.connect_failures != 0)
    {
        test_faults.connect_failures--;
        errno = ECONNREFUSED;
        return -1;
    }

    return 0;
}

ssize_t test_recv(int descriptor, void *buffer, size_t length, int flags)
{
    test_faults.last_descriptor = descriptor;
    test_faults.last_flags = flags;
    test_faults.read_calls++;
    if (test_faults.read_interrupts != 0)
    {
        test_faults.read_interrupts--;
        errno = EINTR;
        return -1;
    }

    if (test_faults.read_error != 0)
    {
        errno = test_faults.read_error;
        return -1;
    }

    size_t available_length = strlen(test_faults.received_bytes) - test_faults.received_offset;
    size_t received_length = smaller_size(smaller_size(length, available_length), test_faults.read_limit);
    memcpy(buffer, test_faults.received_bytes + test_faults.received_offset, received_length);
    test_faults.received_offset += received_length;
    return (ssize_t)received_length;
}

ssize_t test_send(int descriptor, const void *buffer, size_t length, int flags)
{
    test_faults.last_descriptor = descriptor;
    test_faults.last_flags = flags;
    test_faults.write_calls++;
    if (test_faults.write_interrupts != 0)
    {
        test_faults.write_interrupts--;
        errno = EINTR;
        return -1;
    }

    if (test_faults.write_error != 0)
    {
        errno = test_faults.write_error;
        return -1;
    }

    size_t written_length = smaller_size(length, test_faults.write_limit);
    written_length = smaller_size(written_length, sizeof(test_faults.written_bytes) - test_faults.written_length - 1);
    memcpy(test_faults.written_bytes + test_faults.written_length, buffer, written_length);
    test_faults.written_length += written_length;
    test_faults.written_bytes[test_faults.written_length] = '\0';
    return (ssize_t)written_length;
}

int test_close(int descriptor)
{
    if (test_faults.close_calls >= 16) abort();
    test_faults.closed_descriptors[test_faults.close_calls++] = descriptor;
    return 0;
}

// #endregion
// #region Helpers

static size_t smaller_size(size_t first_size, size_t second_size)
{
    return first_size < second_size ? first_size : second_size;
}

static ssize_t read_scripted_bytes(void *context, void *read_buffer, size_t capacity)
{
    scripted_transport_t *server = context;
    server->read_calls++;
    if (server->invalid_read_count) return (ssize_t)(capacity + 1);
    if (server->input_offset >= server->read_failure_offset) return server->read_failure_result;

    const char *input_bytes = server->input_bytes;
    size_t available_length = server->input_length - server->input_offset;
    size_t source_offset = server->input_offset;
    if (server->exchanges != NULL)
    {
        if (server->exchange_index == server->exchange_count) return 0;
        const scripted_exchange_t *exchange = &server->exchanges[server->exchange_index];
        if (server->request_offset != strlen(exchange->request))
        {
            server->protocol_violation = 1;
            return -1;
        }

        input_bytes = exchange->reply;
        source_offset = server->reply_offset;
        available_length = strlen(input_bytes) - source_offset;
    }

    size_t received_length = smaller_size(available_length, capacity);
    received_length = smaller_size(received_length, server->read_limit);
    received_length = smaller_size(received_length, server->read_failure_offset - server->input_offset);
    memcpy(read_buffer, input_bytes + source_offset, received_length);
    server->input_offset += received_length;
    if (server->exchanges != NULL)
    {
        server->reply_offset += received_length;
        if (server->reply_offset == strlen(input_bytes))
        {
            server->exchange_index++;
            server->request_offset = 0;
            server->reply_offset = 0;
        }
    }

    return (ssize_t)received_length;
}

static ssize_t write_scripted_bytes(void *context, const void *write_buffer, size_t byte_count)
{
    scripted_transport_t *server = context;
    server->write_calls++;
    if (server->invalid_write_count) return (ssize_t)(byte_count + 1);
    if (server->output_length >= server->write_failure_offset) return server->write_failure_result;

    size_t written_length = smaller_size(byte_count, server->write_limit);
    written_length = smaller_size(written_length, server->write_failure_offset - server->output_length);
    if (written_length >= sizeof(server->output_bytes) - server->output_length) return -1;
    if (server->exchanges != NULL)
    {
        if (server->exchange_index == server->exchange_count)
        {
            server->protocol_violation = 1;
            return -1;
        }

        const char *expected_request = server->exchanges[server->exchange_index].request;
        size_t remaining_length = strlen(expected_request) - server->request_offset;
        if (server->reply_offset != 0 || written_length > remaining_length
            || memcmp(expected_request + server->request_offset, write_buffer, written_length) != 0)
        {
            server->protocol_violation = 1;
            return -1;
        }

        server->request_offset += written_length;
    }

    memcpy(server->output_bytes + server->output_length, write_buffer, written_length);
    server->output_length += written_length;
    server->output_bytes[server->output_length] = '\0';
    return (ssize_t)written_length;
}

// #endregion
