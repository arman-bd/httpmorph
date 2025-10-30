/**
 * windows_compat.h - Windows compatibility header for POSIX types
 *
 * This header is force-included on Windows to provide POSIX types
 * that are missing from MSVC, particularly ssize_t which is used
 * extensively by nghttp2 and other libraries.
 */

#ifndef HTTPMORPH_WINDOWS_COMPAT_H
#define HTTPMORPH_WINDOWS_COMPAT_H

#ifdef _WIN32
    /* Only apply these definitions on Windows */

    /* Prevent Windows crypto headers from conflicting with BoringSSL */
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOCRYPT
        #define NOCRYPT
    #endif

    /* Include BaseTsd.h to get SSIZE_T definition (signed size_t) */
    #ifndef _BASETSD_H_
        #include <BaseTsd.h>
    #endif

    /* Define ssize_t as SSIZE_T for Windows if not already defined */
    /* Use typedef for both C and C++ to avoid macro expansion issues */
    #ifndef _SSIZE_T_DEFINED
        #define _SSIZE_T_DEFINED
        typedef SSIZE_T ssize_t;
    #endif

    /* strings.h compatibility */
    #ifndef strcasecmp
        #define strcasecmp _stricmp
    #endif
    #ifndef strncasecmp
        #define strncasecmp _strnicmp
    #endif

    /* pthread compatibility for Windows */
    #ifndef _PTHREAD_COMPAT_DEFINED
        #define _PTHREAD_COMPAT_DEFINED

        #include <stdlib.h>
        #include <stdint.h>
        #include <time.h>
        #include <windows.h>
        #include <process.h>

        /* Sleep function compatibility */
        static inline int usleep(unsigned int usec) {
            Sleep(usec / 1000); /* Sleep takes milliseconds */
            return 0;
        }

        static inline int nanosleep(const struct timespec *req, struct timespec *rem) {
            DWORD ms = (DWORD)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
            (void)rem; /* Windows Sleep doesn't support remaining time */
            Sleep(ms);
            return 0;
        }

        /* clock_gettime support */
        #ifndef CLOCK_REALTIME
            #define CLOCK_REALTIME 0
        #endif

        #ifndef CLOCK_MONOTONIC
            #define CLOCK_MONOTONIC 1
        #endif

        static inline int clock_gettime(int clk_id, struct timespec *tp) {
            FILETIME ft;
            ULARGE_INTEGER uli;
            uint64_t ticks;

            (void)clk_id; /* Ignore clock type, use system time for both */
            GetSystemTimeAsFileTime(&ft);

            uli.LowPart = ft.dwLowDateTime;
            uli.HighPart = ft.dwHighDateTime;

            /* Convert to Unix epoch (FILETIME is 100ns intervals since 1601-01-01) */
            ticks = uli.QuadPart - 116444736000000000ULL;

            tp->tv_sec = (long)(ticks / 10000000ULL);
            tp->tv_nsec = (long)((ticks % 10000000ULL) * 100);

            return 0;
        }

        /* Thread types */
        typedef HANDLE pthread_t;
        typedef DWORD pthread_attr_t;

        /* Mutex types */
        typedef CRITICAL_SECTION pthread_mutex_t;
        typedef DWORD pthread_mutexattr_t;

        /* Condition variable types */
        typedef CONDITION_VARIABLE pthread_cond_t;
        typedef DWORD pthread_condattr_t;

        /* Mutex functions */
        static inline int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
            (void)attr;
            InitializeCriticalSection(mutex);
            return 0;
        }

        static inline int pthread_mutex_destroy(pthread_mutex_t *mutex) {
            DeleteCriticalSection(mutex);
            return 0;
        }

        static inline int pthread_mutex_lock(pthread_mutex_t *mutex) {
            EnterCriticalSection(mutex);
            return 0;
        }

        static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) {
            LeaveCriticalSection(mutex);
            return 0;
        }

        /* Condition variable functions */
        static inline int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
            (void)attr;
            InitializeConditionVariable(cond);
            return 0;
        }

        static inline int pthread_cond_destroy(pthread_cond_t *cond) {
            (void)cond;
            /* No cleanup needed for Windows condition variables */
            return 0;
        }

        static inline int pthread_cond_signal(pthread_cond_t *cond) {
            WakeConditionVariable(cond);
            return 0;
        }

        static inline int pthread_cond_broadcast(pthread_cond_t *cond) {
            WakeAllConditionVariable(cond);
            return 0;
        }

        static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
            if (!SleepConditionVariableCS(cond, mutex, INFINITE)) {
                return -1;
            }
            return 0;
        }

        static inline int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                                const struct timespec *abstime) {
            /* Calculate timeout in milliseconds */
            struct timespec now;
            timespec_get(&now, TIME_UTC);

            long long timeout_ms = (abstime->tv_sec - now.tv_sec) * 1000LL +
                                   (abstime->tv_nsec - now.tv_nsec) / 1000000LL;

            if (timeout_ms < 0) timeout_ms = 0;
            if (timeout_ms > INFINITE - 1) timeout_ms = INFINITE - 1;

            if (!SleepConditionVariableCS(cond, mutex, (DWORD)timeout_ms)) {
                DWORD err = GetLastError();
                if (err == ERROR_TIMEOUT) {
                    return 110; /* ETIMEDOUT */
                }
                return -1;
            }
            return 0;
        }

        /* Thread functions */
        typedef struct {
            void *(*start_routine)(void *);
            void *arg;
        } pthread_start_info_t;

        static unsigned __stdcall pthread_start_wrapper(void *arg) {
            pthread_start_info_t *info = (pthread_start_info_t *)arg;
            void *(*start_routine)(void *) = info->start_routine;
            void *thread_arg = info->arg;
            free(info);
            start_routine(thread_arg);
            return 0;
        }

        static inline int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                        void *(*start_routine)(void *), void *arg) {
            (void)attr;
            pthread_start_info_t *info = (pthread_start_info_t *)malloc(sizeof(pthread_start_info_t));
            if (!info) return -1;

            info->start_routine = start_routine;
            info->arg = arg;

            *thread = (HANDLE)_beginthreadex(NULL, 0, pthread_start_wrapper, info, 0, NULL);
            return (*thread == NULL) ? -1 : 0;
        }

        static inline int pthread_join(pthread_t thread, void **retval) {
            (void)retval;
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
            return 0;
        }

        static inline int pthread_detach(pthread_t thread) {
            CloseHandle(thread);
            return 0;
        }

        static inline pthread_t pthread_self(void) {
            return GetCurrentThread();
        }

    #endif /* _PTHREAD_COMPAT_DEFINED */

#endif /* _WIN32 */

#endif /* HTTPMORPH_WINDOWS_COMPAT_H */
