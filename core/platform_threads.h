// core/platform_threads.h — Win32 threads on Windows (DLL self-contained under
// MinGW, no pthread link), pthreads elsewhere. Monotonic ms clock helper.
#ifndef SEEDFINDER_PLATFORM_THREADS_H_
#define SEEDFINDER_PLATFORM_THREADS_H_
#ifdef _WIN32
#include <windows.h>
typedef HANDLE CrackThread;
static inline CrackThread crackThreadCreate(void *(*fn)(void *), void *arg) {
    // double cast via void*: silencia -Wcast-function-type (MinGW) sem mudar ABI
    return CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)fn, arg, 0, NULL);
}
static inline void crackThreadJoin(CrackThread t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
static inline double nowms_s(void) { return (double)GetTickCount64(); }
#else
#include <pthread.h>
#include <time.h>
typedef pthread_t CrackThread;
static inline CrackThread crackThreadCreate(void *(*fn)(void *), void *arg) {
    pthread_t t; pthread_create(&t, NULL, fn, arg); return t;
}
static inline void crackThreadJoin(CrackThread t) { pthread_join(t, NULL); }
static inline double nowms_s(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#endif
#endif
