/*

  Copyright (c) 2009 Samuel Lidén Borell <samuel@slbdata.se>
 
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <stdbool.h>
#include <stdint.h>
#include <libp11.h>

/* Initialization */
void platform_init(int *argc, char ***argv);

/* Random number generation */
void platform_seedRandom();
void platform_makeRandomString(char *buff, int length);

/* Pipe I/O */
typedef void (PlatformPipeFunction) ();
void platform_setupPipe(PlatformPipeFunction *pipeFunction);

/* File IO */
bool platform_readFile(const char *filename, char **data, int *length);
bool platform_deleteFile(const char *filename);
bool platform_deleteDir(const char *filename);

typedef struct PlatformDirIter PlatformDirIter;
PlatformDirIter *platform_openDir(const char *pathname);
bool platform_iterateDir(PlatformDirIter *iter);
char *platform_currentName(PlatformDirIter *iter);
char *platform_currentPath(PlatformDirIter *iter);
void platform_closeDir(PlatformDirIter *iter);

void platform_keyDirs(char*** path, int* len);
PlatformDirIter *platform_openKeysDir();
char *platform_makeMemTempDir();

/* Configuration */
char *platform_getConfigPath(const char *appname);

typedef struct PlatformConfig PlatformConfig;
PlatformConfig *platform_openConfig(const char *appname,
                                    const char *configname);
bool platform_saveConfig(PlatformConfig *config);
void platform_freeConfig(PlatformConfig *config);

#define PLATFORM_CFGPARAMS PlatformConfig *config, \
                           const char *section, const char *name
bool platform_getConfigInteger(const PLATFORM_CFGPARAMS, long *value);
bool platform_getConfigBool(const PLATFORM_CFGPARAMS, bool *value);
bool platform_getConfigString(const PLATFORM_CFGPARAMS, char **value);

void platform_setConfigInteger(PLATFORM_CFGPARAMS, long value);
void platform_setConfigBool(PLATFORM_CFGPARAMS, bool value);
void platform_setConfigString(PLATFORM_CFGPARAMS, const char *value);

/* Asynchronous calls / threads */
typedef void (AsyncCallFunction) (void *);
void platform_asyncCall(AsyncCallFunction *function, void *param);

/* Network */
uint32_t platform_lookupTypeARecord(const char *hostname);

/* User interface */

// This value has to match the value in the window system
// (for example, None on X11)
#define PLATFORM_NO_WINDOW 0

void platform_mainloop();
void platform_leaveMainloop();

void platform_startSign(const char *url, const char *hostname, const char *ip,
                        const char *subjectFilter, unsigned long parentWindowId);
void platform_endSign();
void platform_setMessage(const char *message);
bool platform_sign(PKCS11_SLOT **slot, char **signature, int *siglen, char **person,
                   char *password, int password_maxlen);

void platform_signError();
void platform_versionExpiredError();

#endif

