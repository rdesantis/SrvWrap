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
 * Read the service configuration file.
 */
BOOL GetSrvConfig(
		LPSTR lpConfigName,
		LPSRV_CONFIG lpSrvConfig);

void ReleaseSrvConfig(LPSRV_CONFIG lpSrvConfig);

#endif /* SRVCONFIG_H_ */
