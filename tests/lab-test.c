#include "harness/unity.h"
#include "test-support.h"

// #region Functions

void setUp(void)
{
    reset_test_support();
}

void tearDown(void)
{
    size_t leaked_allocations = finish_test_support();
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, leaked_allocations, "Application allocations leaked during this test.");
}

int main(void)
{
    UNITY_BEGIN();
    run_protocol_tests();
    run_session_tests();
    run_socket_tests();
    return UNITY_END();
}

// #endregion
