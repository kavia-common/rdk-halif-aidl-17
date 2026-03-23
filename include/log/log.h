#pragma once
/**
 * Minimal compatibility shim for Android's liblog headers.
 *
 * Some binder/liblog header sets (often staged under build/usr/include/...)
 * expect LOG_PRI() to be provided by <log/log.h>. In reduced header bundles,
 * this can be missing, causing build failures like:
 *   error: ‘LOG_PRI’ was not declared in this scope
 *
 * This header provides the minimal types/macros needed by <log/log_main.h>
 * and ALOG*/__android_log_print style macros.
 *
 * The definitions are only provided when absent to avoid conflicts with a
 * full Android sysroot.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Android log priorities (subset; values match AOSP). */
#ifndef ANDROID_LOG_UNKNOWN
typedef enum android_LogPriority {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT = 1,
  ANDROID_LOG_VERBOSE = 2,
  ANDROID_LOG_DEBUG = 3,
  ANDROID_LOG_INFO = 4,
  ANDROID_LOG_WARN = 5,
  ANDROID_LOG_ERROR = 6,
  ANDROID_LOG_FATAL = 7,
  ANDROID_LOG_SILENT = 8
} android_LogPriority;
#endif

/**
 * LOG_PRI(priority, tag)
 *
 * AOSP defines this macro to combine log priority and (optionally) tag.
 * Many call sites only require that it compiles and produces an integer.
 *
 * If a full implementation is present, we do not override it.
 */
#ifndef LOG_PRI
#define LOG_PRI(priority, tag) (priority)
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif
