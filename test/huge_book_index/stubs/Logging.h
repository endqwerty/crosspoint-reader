#pragma once
inline void cacheTestLog(const char*, const char*, ...) {}
#define LOG_ERR(...) cacheTestLog(__VA_ARGS__)
#define LOG_DBG(...) cacheTestLog(__VA_ARGS__)
#define LOG_INF(...) cacheTestLog(__VA_ARGS__)
