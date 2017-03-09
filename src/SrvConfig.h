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

#ifndef SRVCONFIG_H_
#define SRVCONFIG_H_

#include <windows.h>

typedef struct tagSRV_CONFIG {
	LPCTSTR lpApplicationName;
	LPTSTR lpCommandLine;
	LPVOID lpEnvironment;
	LPCTSTR lpCurrentDirectory;
} SRV_CONFIG,*LPSRV_CONFIG;

/**
 * Allocate a service configuration block
 * and initialize it from the service configuration file.
 *
 * Returns pointer to the block.
 */
LPSRV_CONFIG GetSrvConfig(LPSTR lpConfigName);

/**
 * Release the service configuration block
 * allocated by GetSrvConfig().
 *
 * Always returns NULL.
 */
LPSRV_CONFIG ReleaseSrvConfig(LPSRV_CONFIG lpSrvConfig);

#endif /* SRVCONFIG_H_ */
