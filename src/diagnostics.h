#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

extern const char *diagnostics_filepath;

void diagnostics_error(int line, int col, const char *msg);
void diagnostics_fatal(const char *msg);

#endif // DIAGNOSTICS_H
