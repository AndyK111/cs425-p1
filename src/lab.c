#include "lab.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef TEST
#include "../tests/test-hooks.h"
#endif

// #region Functions

char *get_greeting(const char *restrict name)
{
    if (name == NULL) return NULL;

    int greeting_length = snprintf(NULL, 0, "Hello, %s!", name);
    if (greeting_length < 0) return NULL;

    size_t allocation_size = (size_t)greeting_length + 1;
    char *greeting_message = malloc(allocation_size);
    if (greeting_message == NULL) return NULL;

    int written_length = snprintf(greeting_message, allocation_size, "Hello, %s!", name);
    if (written_length != greeting_length)
    {
        free(greeting_message);
        return NULL;
    }

    return greeting_message;
}

// #endregion
