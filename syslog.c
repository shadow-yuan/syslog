#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "syslog.h"

static void* dont_use_it = NULL;
struct init_once_parameter {
    void (*init_function)(void);
};

static inline intptr_t AtomicAdd(intptr_t ptr, long long count) {
#ifdef _WIN64
    return (intptr_t)InterlockedExchangeAdd64((LONG64 volatile *)ptr, (LONG64)count);
#else
    return (intptr_t)InterlockedExchangeAdd((LONG volatile *)ptr, (LONG)count);
#endif
}

#define AtomicGetRelaxed(var,dstvar) do { \
    dstvar = AtomicAdd((intptr_t)&var, 0); \
} while(0)

static inline intptr_t AtomicSetRelaxed(intptr_t ptr, long long value) {
#ifdef _WIN64
    return (intptr_t)InterlockedExchange64((LONG64 volatile *)ptr, (LONG64)value);
#else
    return (intptr_t)InterlockedExchange((LONG volatile *)ptr, (LONG)value);
#endif
}

static BOOL CALLBACK __InitOnceCallback(INIT_ONCE* io, void* Parameter, void** ptr) {
    struct init_once_parameter* p = (struct init_once_parameter*)Parameter;
    p->init_function();
    return TRUE;
}

static void CallOnceInit(INIT_ONCE* once, void (*init_function)(void)) {
    struct init_once_parameter parameter;
    parameter.init_function = init_function;
    InitOnceExecuteOnce(once, __InitOnceCallback, &parameter, &dont_use_it);
}

// ----------------------------------------------
#define UNSET_VALUE 0
#define INIT_DONE 1
#define CLEANUP 2

static int is_initialized = UNSET_VALUE;
static int syslog_mask = ~0;
static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION cs;
static char g_ident[128];
static char g_pid[32];
static SOCKET g_s = INVALID_SOCKET;
static SOCKADDR_IN g_host;

static char* month[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static int CheckInitialized() {
    int v = 0;
    AtomicGetRelaxed(is_initialized, v);
    if (v == UNSET_VALUE) {
        return 0;
    }
    return 1;
}

static void ExitCleanup() {
    AtomicSetRelaxed((intptr_t)&is_initialized, CLEANUP);
    // WSACleanup();
    DeleteCriticalSection(&cs);
}

static void InternalInitialize() {

    WSADATA wd;
    int r = WSAStartup(MAKEWORD(2,0), &wd);
    assert(r == 0);

    AtomicSetRelaxed((intptr_t)&is_initialized, INIT_DONE);
    InitializeCriticalSection(&cs);
    atexit(ExitCleanup);
}

static void InitLoggerAddress() {
    struct hostent * host = NULL;
    memset(&g_host, 0, sizeof(g_host));


}
/* Close descriptor used to write to system logger.

   This function is a possible cancellation point and therefore not
   marked with __THROW.  */
void closelog (void) {
    if (!CheckInitialized()) {
        return;
    }

    EnterCriticalSection(&cs);
    if (g_s != INVALID_SOCKET) {
        closesocket(g_s);
        g_s = INVALID_SOCKET;
    }
    LeaveCriticalSection(&cs);
}


/* Open connection to system logger.

   This function is a possible cancellation point and therefore not
   marked with __THROW.  */
void openlog (const char *__ident, int __option, int __facility) {
    if (!CheckInitialized()) {
        CallOnceInit(&init_once, InternalInitialize);
    }

    assert(is_initialized == INIT_DONE);

    EnterCriticalSection(&cs);
    if (g_s != INVALID_SOCKET) {
        LeaveCriticalSection(&cs);
        return;
    }
    LeaveCriticalSection(&cs);


    if (__option & LOG_PID) {
        snprintf(g_pid, sizeof(g_pid), "%lu", GetCurrentProcessId());
    } else {
        g_pid[0] = 0;
    }

    if (__ident) {
        strcpy_s(g_ident, sizeof(g_ident), __ident);
    } else {
        g_ident[0] = 0;
    }
}


/* Set the log mask level.  */
int setlogmask (int __mask) {
    return AtomicSetRelaxed((intptr_t)&syslog_mask, __mask);
}

/* Generate a log message using FMT string and option arguments.

   This function is a possible cancellation point and therefore not
   marked with __THROW.  */
void syslog (int __pri, const char *__fmt, ...) {
    va_list ap;

    if (!CheckInitialized()) {
        CallOnceInit(&init_once, InternalInitialize);
    }

    assert(is_initialized == INIT_DONE);

    va_start(ap, __fmt);
    vsyslog(__pri, __fmt, ap);
    va_end(ap);
}


/* Generate a log message using FMT and using arguments pointed to by AP.

   This function is not part of POSIX and therefore no official
   cancellation point.  But due to similarity with an POSIX interface
   or due to the implementation it is a cancellation point and
   therefore not marked with __THROW.  */
void vsyslog (int __pri, const char *__fmt, va_list __ap) {
    char buff[4096];
    SYSTEMTIME st;
    int n;
    DWORD bytes;

    if(!(LOG_MASK( LOG_PRI( __pri )) & syslog_mask)) {
        return;
    }

    if (!CheckInitialized()) {
        CallOnceInit(&init_once, InternalInitialize);
    }

    assert(is_initialized == INIT_DONE);

    EnterCriticalSection(&cs);

    LeaveCriticalSection(&cs);

    GetLocalTime(&st);
    n = snprintf(buff, sizeof(buff),
        "<%d> %s %2d %02d:%02d:%02d %s %s\n",
        __pri, month[st.wMonth-1], st.wDay,
        st.wHour, st.wMinute, st.wSecond,
        g_ident, g_pid
        );

    if (n > 0) {

    }
}
