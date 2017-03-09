/*
 * Copyright (c) 2017, Ronald DeSantis
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

#include <windows.h>

#include <tchar.h>
#include <stdio.h>

#include "SrvConfig.h"

static BOOL GetSrvEnvironment(
		char* pSource,
		FILE* inlineFile,
		LPVOID* plpEnvironment);

/**
 * Read the service configuration file.
 */
BOOL GetSrvConfig(
		LPSTR lpConfigName,
		LPSRV_CONFIG lpSrvConfig) {

	lpSrvConfig->lpApplicationName = NULL;
	lpSrvConfig->lpCommandLine = NULL;
	lpSrvConfig->lpCurrentDirectory = NULL;
	lpSrvConfig->lpEnvironment = NULL;

	// Open the file and loop over it line by line.

	FILE* file = fopen(lpConfigName, "r");
	if (file == NULL) {
		SetLastError(ERROR_FILE_NOT_FOUND);
		return FALSE;
	}

	char line[256];
    while (fgets(line, sizeof(line), file)) {

    	// Expecting line with "keyword=value\n\0".
    	// Replace "=" and "\n" with 0 terminators.
    	// Ignore empty lines.

    	char* pNewline = strchr(line, '\n');
    	if (pNewline != NULL) {
    		*pNewline = 0;
    	}

    	if (line[0] == 0) {
    		continue;
    	}

    	char* pEquals = strchr(line, '=');
    	if (pEquals == NULL) {
			SetLastError(ERROR_BAD_FORMAT);
			return FALSE;
    	}
    	*pEquals = 0;

    	char* pKeyword = line;
    	char* pValue = pEquals + 1;

    	// Check for keyword with simple value.

    	LPTSTR* pField = NULL;
    	if (strcmp(pKeyword, "ApplicationName") == 0) {
    		pField = (LPTSTR*)&lpSrvConfig->lpApplicationName;
    	}
    	else if (strcmp(pKeyword, "CommandLine") == 0) {
    		pField = &lpSrvConfig->lpCommandLine;
    	}
    	else if (strcmp(pKeyword, "CurrentDirectory") == 0) {
    		pField = (LPTSTR*)&lpSrvConfig->lpCurrentDirectory;
    	}

    	if (pField != NULL) {

    		*pField = malloc(256);
    		if (*pField == NULL) {
    			SetLastError(ERROR_OUTOFMEMORY);
    			return FALSE;
    		}

    		strcpy(*pField, pValue);
    	}
    	else if (strcmp(pKeyword, "Environment") == 0) {

    		// Environment keyword requires complex handling.

    		BOOL bSuccess = GetSrvEnvironment(pValue, file, &lpSrvConfig->lpEnvironment);

    		if (!bSuccess) {
    			return FALSE;
    		}
    	}
    	else {
			SetLastError(ERROR_BAD_FORMAT);
			return FALSE;
    	}
    }

    if (feof(file) == 0) {
		SetLastError(ERROR_READ_FAULT);
		return FALSE;
    }

    fclose(file);
	return TRUE;
}

/**
 * Construct the environment.
 *
 *	pSource
 *			must point to a string in form "source[:path]"
 *			where source is default, inline, or file.
 *
 *	inlineFile
 *			is the file from which to read environment variables
 *			if source is "inline".
 *
 *	plpEnvironment
 *			points to a variable to receive the environment block
 *			suitable for passing to CreateProcess().
 *
 * For ease of implementation, this function simply updates the current environment
 * and returns NULL for plpEnvironment for the caller to pass to CreateProcess()
 * to indicate that the current environment should be inherited by the child process.
 */
static BOOL GetSrvEnvironment(
		char* pSource,
		FILE* inlineFile,
		LPVOID* plpEnvironment) {

	*plpEnvironment = NULL;

	FILE* file = NULL;
	if (strcmp(pSource, "default") == 0) {
		return TRUE;
	}
	else if (strcmp(pSource, "inline") == 0) {
		file = inlineFile;
	}
	else {
    	char* pColon = strchr(pSource, ':');
    	if (pColon == NULL) {
			SetLastError(ERROR_BAD_FORMAT);
			return FALSE;
    	}
    	*pColon = 0;

    	char* pKeyword = pSource;
    	char* pPath = pColon + 1;

    	if (strcmp(pKeyword, "file") == 0) {

    		file = fopen(pPath, "r");
    		if (file == NULL) {
    			SetLastError(ERROR_FILE_NOT_FOUND);
    			return FALSE;
    		}
    	}
    	else {
			SetLastError(ERROR_BAD_FORMAT);
			return FALSE;
    	}
	}

	// Loop reading and setting environment variables.

	BOOL bSuccess = TRUE;

	char line[256];
    while (fgets(line, sizeof(line), file)) {

    	// Expecting line with "keyword=value\n\0".
    	// Replace "=" and "\n" with 0 terminators.
    	// Ignore empty lines.

    	char* pNewline = strchr(line, '\n');
    	if (pNewline != NULL) {
    		*pNewline = 0;
    	}

    	if (line[0] == 0) {
    		continue;
    	}

    	char* pEquals = strchr(line, '=');
    	if (pEquals == NULL) {
			SetLastError(ERROR_BAD_FORMAT);
			return FALSE;
    	}
    	*pEquals = 0;

    	char* pName = line;
    	char* pValue = pEquals + 1;

    	// Set environment variable.

    	bSuccess = SetEnvironmentVariable(pName, pValue);
    	if (!bSuccess) {
    		break;
    	}
    }

    if (feof(file) == 0) {
		SetLastError(ERROR_READ_FAULT);
		return FALSE;
    }

	if (file != inlineFile) {
		fclose(file);
	}

    return bSuccess;
}
