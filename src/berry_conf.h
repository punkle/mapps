/********************************************************************
 * Berry configuration for mapps
 * Based on firmware's berry_conf.h — all settings wrapped in #ifndef
 * guards so consumers can override via -D flags.
 ********************************************************************/
#ifndef BERRY_CONF_H
#define BERRY_CONF_H

#include <assert.h>

#ifndef BE_DEBUG
#define BE_DEBUG 0
#endif

/* Use 32-bit int (saves RAM vs long long) */
#ifndef BE_INTGER_TYPE
#define BE_INTGER_TYPE 0
#endif

/* Use single-precision float */
#ifndef BE_USE_SINGLE_FLOAT
#define BE_USE_SINGLE_FLOAT 1
#endif

/* Max size of bytes() object (32 kb) */
#ifndef BE_BYTES_MAX_SIZE
#define BE_BYTES_MAX_SIZE (32 * 1024)
#endif

#ifndef BE_USE_PRECOMPILED_OBJECT
#define BE_USE_PRECOMPILED_OBJECT 1
#endif

#ifndef BE_DEBUG_RUNTIME_INFO
#define BE_DEBUG_RUNTIME_INFO 1
#endif

#ifndef BE_DEBUG_VAR_INFO
#define BE_DEBUG_VAR_INFO 0
#endif

#ifndef BE_USE_PERF_COUNTERS
#define BE_USE_PERF_COUNTERS 1
#endif

#ifndef BE_VM_OBSERVABILITY_SAMPLING
#define BE_VM_OBSERVABILITY_SAMPLING 20
#endif

#ifndef BE_STACK_TOTAL_MAX
#define BE_STACK_TOTAL_MAX 2000
#endif

#ifndef BE_STACK_FREE_MIN
#define BE_STACK_FREE_MIN 10
#endif

#ifndef BE_STACK_START
#define BE_STACK_START 50
#endif

#ifndef BE_CONST_SEARCH_SIZE
#define BE_CONST_SEARCH_SIZE 50
#endif

#ifndef BE_USE_STR_HASH_CACHE
#define BE_USE_STR_HASH_CACHE 0
#endif

/* Disable filesystem in Berry — we handle file I/O ourselves */
#ifndef BE_USE_FILE_SYSTEM
#define BE_USE_FILE_SYSTEM 0
#endif

#ifndef BE_USE_SCRIPT_COMPILER
#define BE_USE_SCRIPT_COMPILER 1
#endif

#ifndef BE_USE_BYTECODE_SAVER
#define BE_USE_BYTECODE_SAVER 0
#endif

#ifndef BE_USE_BYTECODE_LOADER
#define BE_USE_BYTECODE_LOADER 0
#endif

#ifndef BE_USE_SHARED_LIB
#define BE_USE_SHARED_LIB 0
#endif

#ifndef BE_USE_OVERLOAD_HASH
#define BE_USE_OVERLOAD_HASH 1
#endif

#ifndef BE_USE_DEBUG_HOOK
#define BE_USE_DEBUG_HOOK 0
#endif

#ifndef BE_USE_DEBUG_GC
#define BE_USE_DEBUG_GC 0
#endif

#ifndef BE_USE_DEBUG_STACK
#define BE_USE_DEBUG_STACK 0
#endif

/* Modules: enable only what we need */
#ifndef BE_USE_STRING_MODULE
#define BE_USE_STRING_MODULE 1
#endif

#ifndef BE_USE_JSON_MODULE
#define BE_USE_JSON_MODULE 1
#endif

#ifndef BE_USE_MATH_MODULE
#define BE_USE_MATH_MODULE 1
#endif

#ifndef BE_USE_TIME_MODULE
#define BE_USE_TIME_MODULE 0
#endif

#ifndef BE_USE_OS_MODULE
#define BE_USE_OS_MODULE 0
#endif

#ifndef BE_USE_GLOBAL_MODULE
#define BE_USE_GLOBAL_MODULE 1
#endif

#ifndef BE_USE_SYS_MODULE
#define BE_USE_SYS_MODULE 0
#endif

#ifndef BE_USE_DEBUG_MODULE
#define BE_USE_DEBUG_MODULE 0
#endif

#ifndef BE_USE_GC_MODULE
#define BE_USE_GC_MODULE 0
#endif

#ifndef BE_USE_SOLIDIFY_MODULE
#define BE_USE_SOLIDIFY_MODULE 0
#endif

#ifndef BE_USE_INTROSPECT_MODULE
#define BE_USE_INTROSPECT_MODULE 0
#endif

#ifndef BE_USE_STRICT_MODULE
#define BE_USE_STRICT_MODULE 0
#endif

/* Memory management */
#ifndef BE_EXPLICIT_ABORT
#define BE_EXPLICIT_ABORT abort
#endif

#ifndef BE_EXPLICIT_EXIT
#define BE_EXPLICIT_EXIT exit
#endif

#if defined(BOARD_HAS_PSRAM) && defined(ARCH_ESP32)
/* Route Berry allocations to PSRAM (prefer SPIRAM, fallback to default) */
#include <esp_heap_caps.h>
static inline void *berry_psram_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)
        p = malloc(size); /* fallback to internal if PSRAM fails */
    return p;
}
static inline void *berry_psram_realloc(void *ptr, size_t size)
{
    if (!ptr)
        return berry_psram_malloc(size); /* NULL ptr = malloc */
    /* Use MALLOC_CAP_8BIT to allow realloc across heap regions (SPIRAM or internal) */
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
    if (!p && size > 0)
        p = realloc(ptr, size); /* fallback to default */
    return p;
}
#ifndef BE_EXPLICIT_MALLOC
#define BE_EXPLICIT_MALLOC berry_psram_malloc
#endif
#ifndef BE_EXPLICIT_FREE
#define BE_EXPLICIT_FREE free
#endif
#ifndef BE_EXPLICIT_REALLOC
#define BE_EXPLICIT_REALLOC berry_psram_realloc
#endif
#else
#ifndef BE_EXPLICIT_MALLOC
#define BE_EXPLICIT_MALLOC malloc
#endif
#ifndef BE_EXPLICIT_FREE
#define BE_EXPLICIT_FREE free
#endif
#ifndef BE_EXPLICIT_REALLOC
#define BE_EXPLICIT_REALLOC realloc
#endif
#endif

#define be_assert(expr) assert(expr)

#endif
