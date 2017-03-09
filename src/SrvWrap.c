/*
 * Copyright (c) 2016, 2017, Ronald DeSantis
 *
 *	Licensed under the Apache License, Version 2.0 (the "License");
 *	you may not use this file except in compliance with the License.
 *	You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 *	Unless required by applicable law or agreed to in writing, software
 *	distributed under the License is distributed on an "AS IS" BASIS,
 *	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *	See the License for the specific language governing permissions and
 *	limitations under the License.
 */

/**
 * SrvWrap - wrap a console program as a Windows service.
 *
 * Starting the service invokes the program, which may be cmd.exe to start a Windows batch file.
 * If the program terminates itself, the service changes its status to Stopped.
 * Manually stopping the service sends CTRL + C signal to the program, which must respond by terminating.
 * If the program does not terminate in a timely way, it is forcibly killed.
 *
 * It is expected that the service will be installed using SC.exe invoked from a Windows .bat file as follows.
 * Note the extravagant use of quotes.  This is not a typo; .bat files are insane about quotes.
 * The triple double quotes can be omitted if the indicated value does not contain embedded blanks.
 *
 *		sc create %SVC_NAME% binPath="%WRAPPER_EXE% %SVC_NAME% """%SVC_CONFIG%""""
 *
 * where:
 *
 *	%SVC_NAME%		is the service name.
 *
 *	%WRAPPER_EXE%	is the path to this wrapper executable ending with SrvWrap.exe.
 * 					If directory of the wrapper executable is not defined on the PATH,
 * 					then %WRAPPER_EXE% must be fully-qualified with the directory.
 *
 * 	%SVC_CONFIG%	is the path to a text file containing configuration details for the service.
 * 					This file must contain a name=value configuration parameter on each line.
 *					The configuration parameters are as follows.
 *
 *		ApplicationName
 *					optionally is the path to the executable to be wrapped ending with an
 *					explicit file extension, typically .exe or .cmd.  The PATH is not searched
 *					when launching this executable.  Therefore, ApplicationName must be
 * 					fully-qualified with a directory unless the executable is located in
 * 					the directory where the Service Control Manager starts new services,
 * 					typically %WinDir%\System32 or %WinDir%\SysWOW64.
 *
 * 					This value can be omitted.  See CommandLine for details.
 *
 * 		CommandLine
 * 					is the command line to be wrapped including any arguments to the wrapped
 * 					executable.
 *
 * 					If ApplicationName was omitted, the first token of the command line
 * 					must be the executable to be wrapped.  In this case, the normal Windows
 * 					rules for locating an executable are following, including searching the
 * 					PATH and in some cases providing an implicit file extension if one
 * 					was omitted.
 *
 * 					If ApplicationName was provided, the first token of the command line can
 * 					be any arbitrary value.  Arguments always start at the second token.
 *
 * 					To wrap a Windows batch file, set CommandLine to "cmd.exe /c" plus
 * 					the name of the batch file followed by any arguments.
 *
 *		CurrentDirectory
 *					is the full path to the current directory for the service.
 *					If omitted, this defaults to the current directory of the
 *					Service Control Manager when it starts the new service.
 *
 *		Environment
 *					specifies how to construct the environment block for the service.
 *					It must be a string in the following format:
 *
 *						source[:path]
 *
 *					where:
 *
 *					source	is one of the following:
 *
 *							default		The environment is inherited unchanged from the
 *										Service Control Manager
 *
 *							file		A set of name=value environment variables
 *										contained in the file specified by the
 *										path argument is used to update the environment.
 *
 *							inline		The remainder of this configuration file contains
 *										name=value environment variables used to
 *										update the environment.
 *
 *					path
 *							must be specified when source is file.
 *
 *					If Environment is omitted, default mode is used.
 *
 * The configuration parameters specify arguments to be passed to the Windows API
 * CreateProcess() when launching the wrapped program.  See
 * https://msdn.microsoft.com/en-us/library/windows/desktop/ms682425(v=vs.85).aspx
 * for more details.
 *
 * Any fatal error encountered by the service writes an event to the Windows Application event log with
 * the source set to SrvWrap.  The first string reported with the event is typically the service name.
 *
 * Make sure that the PATH in effect when the service starts includes any DLLs needed to run
 * the wrapped executable.  If it does not, CreateProcess() will succeed but the attempt
 * to start the executable will fail and the process exit code will be -1073741515, hex 0xC0000135.
 *
 * See https://msdn.microsoft.com/en-us/library/windows/desktop/bb540475(v=vs.85).aspx for
 * basic information about developing a Windows service.  The framework for this code is adapted
 * from there.
 *
 * See https://support.microsoft.com/en-us/kb/251192 for information on using SC.exe to install a service.
 */

#include <windows.h>

#include <tchar.h>
#include <stdio.h>

#include "SrvConfig.h"

static const char eventSourceName[] = "SrvWrap";
static const DWORD waitSecondsBeforeKill = 30;

static LPSTR lpServiceName = NULL;
static LPSTR lpConfigName = NULL;

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  ghSvcStopEvent = NULL;

VOID WINAPI SvcCtrlHandler( DWORD );
VOID WINAPI SvcMain(DWORD, LPTSTR*);

static void ReportSvcStatus(DWORD, DWORD, DWORD);

static void LogArgs(int, char*[]);
static void LogInfo(LPTSTR);
static void LogError(LPTSTR, BOOL);

/**
 * Entry point for the process; invoked when the service is started
 *
 * argc and argv must be set as follows, which will be the case if the above service installation
 * convention is observed:
 *
 * 	argc			must be 3.
 *
 * 	argv[1]			is the service name used when the service was installed.
 *
 * 	argv[2]			is the path to a text file containing configuration details for the service.
 */
int main(int argc, char* argv[])
{
	// Validate the arguments.

	lpServiceName = (2 <= argc) ? argv[1] : "[name omitted]";

	LogArgs(argc, argv);

	if (argc != 3) {
		SetLastError(ERROR_BAD_ARGUMENTS);
		LogError(TEXT("main"), FALSE);
		return EXIT_FAILURE;
	}

	lpConfigName = argv[2];

	// Hand off to the service control dispatcher.
	// It returns when the service has stopped.

	SERVICE_TABLE_ENTRY DispatchTable[] =
	{
		{ lpServiceName, (LPSERVICE_MAIN_FUNCTION) SvcMain },
		{ NULL, NULL }
	};

	if (!StartServiceCtrlDispatcher( DispatchTable ))
	{
		LogError(TEXT("StartServiceCtrlDispatcher"), FALSE);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

//
// Purpose:
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
//
// Return value:
//   None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR *lpszArgv)
{
    BOOL bSuccess;

    // Register the handler function for the service.

	gSvcStatusHandle = RegisterServiceCtrlHandler(
			lpServiceName,
			SvcCtrlHandler);

	if (!gSvcStatusHandle) {
		LogError(TEXT("RegisterServiceCtrlHandler"), FALSE);
		return;
	}

	gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	gSvcStatus.dwServiceSpecificExitCode = 0;

	// Report initial status to the SCM.
	// If startup is slow, call ReportSvcStatus() periodically with SERVICE_START_PENDING.
	// If initialization fails, call ReportSvcStatus with SERVICE_STOPPED.

	ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

	// Create an event. The control handler function, SvcCtrlHandler,
	// signals this event when it receives the stop control code.

	ghSvcStopEvent = CreateEvent(
							NULL,    // default security attributes
							TRUE,    // manual reset event
							FALSE,   // not signaled
							NULL);   // no name

	if (ghSvcStopEvent == NULL) {
		LogError(TEXT("CreateEvent"), TRUE);
		return;
	}

	// Because this is a service, it was started without a console.
	// Allocate a console so that CTRL_C_EVENT can be sent to
	// signal the child process to terminate cleanly.

    bSuccess = AllocConsole();

    if (!bSuccess) {
		LogError(TEXT("AllocConsole"), TRUE);
		return;
    }

    // Get the service configuration.

    SRV_CONFIG srvConfig;

    bSuccess = GetSrvConfig(lpConfigName, &srvConfig);

    if (!bSuccess) {
		LogError(TEXT("GetSrvConfig"), TRUE);
		return;
    }

    // Launch the wrapped executable.

    DWORD dwCreationFlags = 0;		// Do NOT use CREATE_NO_WINDOW; that suppresses the ability to send console signals

    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    bSuccess = CreateProcess(
    				srvConfig.lpApplicationName,
					srvConfig.lpCommandLine,
					NULL,						// lpProcessAttributes
					NULL,						// lpThreadAttributes
					TRUE,						// bInheritHandles
					dwCreationFlags,			// dwCreationFlags
					srvConfig.lpEnvironment,
					srvConfig.lpCurrentDirectory,
					&si,						// lpStartupInfo
					&pi);						// lpProcessInformation

    if (!bSuccess) {
		LogError(TEXT("CreateProcess"), TRUE);
		return;
    }

	// Report running status when initialization is complete.

	ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

	// Wait until: the service is signaled to stop; or, the child process terminates.

    HANDLE waitForHandles[] = {ghSvcStopEvent, pi.hProcess};

	DWORD waitResult = WaitForMultipleObjects(
							2,					// nCount
							waitForHandles,		// lpHandles
							FALSE,				// bWaitAll
							INFINITE);			// dwMilliseconds

	if (waitResult == WAIT_OBJECT_0) {

		LogInfo(TEXT("Service signaled to stop"));

		// The service was signaled to stop; terminate the child process.
		// Try sending CTRL + C signal.  The signal affects not only child processes
		// but also this parent process.  So first disable the signal for this parent.

		bSuccess = SetConsoleCtrlHandler(NULL, TRUE);

		if (!bSuccess) {
			LogError(TEXT("SetConsoleCtrlHandler"), TRUE);
			return;
		}

		bSuccess = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);

		if (!bSuccess) {
			LogError(TEXT("GenerateConsoleCtrlEvent"), TRUE);
			return;
		}

		// Wait for the child process to terminate.

		waitResult = WaitForSingleObject(pi.hProcess, waitSecondsBeforeKill * 1000);

		if (waitResult == WAIT_OBJECT_0) {
			// Normal termination.
		}
		else if (waitResult == WAIT_TIMEOUT) {

			LogInfo(TEXT("Killing child process"));

			// The child process did not terminate itself in a timely way.
			// Kill it.

			UINT uExitCode = WAIT_TIMEOUT;

			bSuccess = TerminateProcess(pi.hProcess, uExitCode);

			if (!bSuccess) {
				LogError(TEXT("TerminateProcess"), TRUE);
				return;
			}
		}
		else {
			LogError(TEXT("WaitForSingleObject"), TRUE);
			return;
		}
	}
	else if (waitResult == (WAIT_OBJECT_0 + 1)) {

		LogInfo(TEXT("Child process terminated"));

		// The child process terminated; report that the service will stop.

		ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

		 // If the child process terminated with an error code, report it.

		DWORD dwExitCode;

		bSuccess = GetExitCodeProcess(pi.hProcess, &dwExitCode);

		if (!bSuccess) {
			LogError(TEXT("GetExitCodeProcess"), TRUE);
			return;
		}

		if (dwExitCode != 0) {
			SetLastError(dwExitCode);
			LogError(TEXT("Child process"), TRUE);
			return;
		}
	}
	else {
		LogError(TEXT("WaitForMultipleObjects"), TRUE);
		return;
	}

	// Close the handles to child process information
	// and report service stopped normally.

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
	return;
}

//
// Purpose:
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation,
//     in milliseconds
//
// Return value:
//   None
//
static void ReportSvcStatus(
		DWORD dwCurrentState,
		DWORD dwWin32ExitCode,
		DWORD dwWaitHint)
{
	static DWORD dwCheckPoint = 1;

	// Fill in the SERVICE_STATUS structure.

	gSvcStatus.dwCurrentState = dwCurrentState;
	gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
	gSvcStatus.dwWaitHint = dwWaitHint;

	if (dwCurrentState == SERVICE_START_PENDING) {
		gSvcStatus.dwControlsAccepted = 0;
	}
	else {
		gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	}

	if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED)) {
		gSvcStatus.dwCheckPoint = 0;
	}
	else {
		gSvcStatus.dwCheckPoint = dwCheckPoint++;
	}

	// Report the status of the service to the SCM.
	SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//
// Purpose:
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
//
// Return value:
//   None
//
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
   // Handle the requested control code.

   switch(dwCtrl) {

	case SERVICE_CONTROL_STOP:
		ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);

		// Signal the service to stop.

		SetEvent(ghSvcStopEvent);
		ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);

		return;

	case SERVICE_CONTROL_INTERROGATE:
		break;

	default:
		break;
   }
}

/**
 * Reporting functions
 *
 * See Event Identifiers at https://msdn.microsoft.com/en-us/library/windows/desktop/aa363651(v=vs.85).aspx
 */
#define SVC_INFORMATION ((DWORD)0x00000000L)
#define SVC_ERROR ((DWORD)0xC0020001L)

/**
 * Report service startup arguments to the event log
 */
static void LogArgs(int argc, char* argv[])
{
    HANDLE hEventSource = RegisterEventSource(NULL, eventSourceName);

    if (NULL != hEventSource) {

            ReportEvent(
            		hEventSource,				// event log handle
    				EVENTLOG_INFORMATION_TYPE,	// event type
    				0,							// event category
    				SVC_INFORMATION,			// event identifier
    				NULL,						// no security identifier
    				argc,						// size of lpszStrings array
    				0,							// no binary data
    				(LPCSTR*)argv,				// array of strings
    				NULL);						// no binary data

        DeregisterEventSource(hEventSource);
    }
}

/**
 * Report information to the event log
 */
static void LogInfo(LPTSTR info) {

    HANDLE hEventSource = RegisterEventSource(NULL, eventSourceName);

    if (NULL != hEventSource) {

        LPCTSTR lpszStrings[2];
        lpszStrings[0] = lpServiceName;
        lpszStrings[1] = info;

        ReportEvent(
				hEventSource,				// event log handle
				EVENTLOG_INFORMATION_TYPE,	// event type
				0,							// event category
				SVC_INFORMATION,			// event identifier
				NULL,						// no security identifier
				2,							// size of lpszStrings array
				0,							// no binary data
				lpszStrings,				// array of strings
				NULL);						// no binary data

        DeregisterEventSource(hEventSource);
    }
}

/**
 * Report error to the event log
 *
 *	szFunction		is the name of function that failed.
 *
 *	bReportStopping	if TRUE sets the service status to SERVICE_STOP_PENDING
 *					after reporting the error.
 */
static void LogError(LPTSTR szFunction, BOOL bReportStopping)
{
    DWORD dwLastError = GetLastError();

    HANDLE hEventSource = RegisterEventSource(NULL, eventSourceName);

    if (NULL != hEventSource) {

    	TCHAR message[80];
        sprintf_s(message, 80, TEXT("%s failed with error %d hex %#X"), szFunction, dwLastError, dwLastError);

        LPCTSTR lpszStrings[2];
        lpszStrings[0] = lpServiceName;
        lpszStrings[1] = message;

        ReportEvent(
        		hEventSource,				// event log handle
				EVENTLOG_ERROR_TYPE,		// event type
				0,							// event category
				SVC_ERROR,					// event identifier
				NULL,						// no security identifier
				2,							// size of lpszStrings array
				0,							// no binary data
				lpszStrings,				// array of strings
				NULL);						// no binary data

        DeregisterEventSource(hEventSource);
    }

    if (bReportStopping) {
		ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
    }
}
