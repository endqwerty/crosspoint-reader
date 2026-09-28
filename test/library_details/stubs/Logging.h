#pragma once
inline int errors = 0;
template <class... T>
void logError(T&&...) {
  ++errors;
}
#define LOG_ERR(...) logError(__VA_ARGS__)
