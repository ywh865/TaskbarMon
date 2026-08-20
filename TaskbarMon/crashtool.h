#pragma once

namespace CRASHREPORT
{
    /**@brief
        Registers a fail-closed unhandled-exception filter.
        The filter does not create an in-process dump, log, symbolise, allocate,
        or display UI; it returns EXCEPTION_CONTINUE_SEARCH so Windows Error
        Reporting/the debugger can handle the fault. Reliable post-crash dumps
        require a separate helper process.
    */
    void StartCrashReport();
}
