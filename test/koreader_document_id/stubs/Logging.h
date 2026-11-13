#pragma once
inline void koDocTestLog(const char*, const char*, ...) {}
#define LOG_ERR(...) koDocTestLog(__VA_ARGS__)
#define LOG_DBG(...) koDocTestLog(__VA_ARGS__)
