#ifndef LAB_H
#define LAB_H

#include "protocol.h"
#include "session.h"
#include "socket_transport.h"

// #region Functions

/* (const char *) -> char *
 *
 * Builds the starter greeting retained for the existing template test.
 *
 * Parameters:
 * const char *name : Name to include in the greeting.
 *
 * Returns: An allocated greeting, or NULL for null input, formatting failure,
 * or allocation failure.
 *
 * NOTE: The caller must free the returned string. This helper is not used
 * by the SMTP client and can be removed when the template test is replaced.
 */
char *get_greeting(const char *restrict name);

// #endregion

#endif
