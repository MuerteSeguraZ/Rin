#ifndef RIN_DIAG_H
#define RIN_DIAG_H

void ReportError(int Line, const char *Format, ...);
int HadError(void);

#endif /* RIN_DIAG_H */