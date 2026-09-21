#ifndef TEST_HOOKS_H
#define TEST_HOOKS_H

/* Included after system headers only in TEST builds of application files.
 * The test runner and Unity retain the real library/system functions.
 */
#include "test-support.h"

// #region Constants

/* Darwin's fortified stdio headers may already define these as macros. */
#undef snprintf
#undef vsnprintf

#define malloc test_malloc
#define free test_free
#define strlen test_strlen
#define snprintf test_snprintf
#define vsnprintf test_vsnprintf
#define getaddrinfo test_getaddrinfo
#define freeaddrinfo test_freeaddrinfo
#define socket test_socket
#define setsockopt test_setsockopt
#define connect test_connect
#define recv test_recv
#define send test_send
#define close test_close

// #endregion

#endif
