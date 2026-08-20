#include "StdAfx.h"
#include "crashtool.h"

namespace CRASHREPORT
{
    namespace
    {
        LONG WINAPI UnhandledExceptionFilter(PEXCEPTION_POINTERS)
        {
            // This callback runs in the faulting thread, potentially while the
            // heap, loader, MFC, or a project lock is already compromised.
            // Do not write a dump, symbolise, log, allocate memory, display
            // UI, or call DbgHelp here. Those operations can re-enter a held
            // loader/heap lock and turn a reportable crash into a permanent
            // process hang.
            //
            // Returning CONTINUE_SEARCH hands the exception to Windows Error
            // Reporting/the debugger. Application diagnostics remain the
            // normal, pre-crash logs; reliable post-crash dump generation
            // requires a separately running helper process.
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    void StartCrashReport()
    {
        // Register during normal startup only. The filter itself deliberately
        // performs no work that could acquire a crash-time lock.
        ::SetUnhandledExceptionFilter(UnhandledExceptionFilter);
    }
}
