#ifndef BUILTIN_HEADERS_H
#define BUILTIN_HEADERS_H

/* Write bundled freestanding headers to a temporary directory.
 * Returns the path to the directory containing the headers, or NULL on failure.
 * The caller is responsible for cleaning up the returned path. */
const char *builtin_headers_write_temp_dir(void);

#endif /* BUILTIN_HEADERS_H */
