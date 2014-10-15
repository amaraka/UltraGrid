/**
 * @file   lib_common.cpp
 * @author Martin Pulec     <pulec@cesnet.cz>
 */
/*
 * Copyright (c) 2012-2014 CESNET, z. s. p. o.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of CESNET nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#include "config_unix.h"
#include "config_win32.h"
#endif // HAVE_CONFIG_H

#include <dlfcn.h>
#include <glob.h>
#include <libgen.h>

#include <iostream>
#include <map>

#include "debug.h"
#include "host.h"

#include "lib_common.h"

using namespace std;

void open_all(const char *pattern) {
#ifdef BUILD_LIBRARIES
        char path[512];
        glob_t glob_buf;

        char *tmp = strdup(uv_argv[0]);
        /* binary not from $PATH */
        if(strchr(uv_argv[0], '/') != NULL) {
                char *dir = dirname(tmp);
                snprintf(path, sizeof(path), "%s/../lib/ultragrid/%s", dir, pattern);
        } else {
                snprintf(path, sizeof(path), TOSTRING(LIB_DIR) "/ultragrid/%s", pattern);
        }
        free(tmp);

        glob(path, 0, NULL, &glob_buf);

        for(unsigned int i = 0; i < glob_buf.gl_pathc; ++i) {
                if(!dlopen(glob_buf.gl_pathv[i], RTLD_NOW|RTLD_GLOBAL))
                        verbose_msg("Library opening warning: %s \n", dlerror());
        }

        globfree(&glob_buf);
#else
        UNUSED(pattern);
#endif
}

void *open_library(const char *name)
{
        void *handle = NULL;
        struct stat buf;
        char kLibName[128];
        char path[512];
        char *dir;
        char *tmp;

        snprintf(kLibName, sizeof(kLibName), "ultragrid/%s", name);

        if (uv_argv) {
                /* firstly expect we are opening from a build */
                tmp = strdup(uv_argv[0]);
                /* binary not from $PATH */
                if(strchr(tmp, '/') != NULL) {
                        dir = dirname(tmp);
                        snprintf(path, sizeof(path), "%s/../lib/%s", dir, kLibName);
                        if(!handle && stat(path, &buf) == 0) {
                                handle = dlopen(path, RTLD_NOW|RTLD_GLOBAL);
                                if(!handle)
                                        verbose_msg("Library opening warning: %s \n", dlerror());
                        }
                }
                free(tmp);
        } else {
                fprintf(stderr, "Warning: unable to locate executable path!");
        }

        /* next try $LIB_DIR/ultragrid */
        snprintf(path, sizeof(path), TOSTRING(LIB_DIR) "/%s", kLibName);
        if(!handle && stat(path, &buf) == 0) {
                handle = dlopen(path, RTLD_NOW|RTLD_GLOBAL);
                if(!handle)
                        verbose_msg("Library opening warning: %s \n", dlerror());
        }

        if(!handle) {
                fprintf(stderr, "Unable to load %s library.\n", kLibName);
        }

        return handle;
}

static map<enum library_class, map<string, pair<void *, int>>> *modules = nullptr;

/**
 * The purpose of this initializor instead of ordinary static initialization is that register_video_capture_filter()
 * may be called before static members are initialized (it is __attribute__((constructor)))
 */
struct init_modules {
        init_modules() {
                if (modules == nullptr) {
                        modules = new map<enum library_class, map<string, pair<void *, int>>>();
                }
        }
};

static struct init_modules loader;

void register_module(string const & name, void *data, enum library_class cls, int abi_version)
{
        struct init_modules loader;
        (*modules)[cls][name] = make_pair(data, abi_version);
}

void *load_module(std::string const & name, enum library_class cls, int abi_version)
{
        if (modules->find(cls) != modules->end()) {
                auto it_cls = modules->find(cls)->second;
                auto it_module = it_cls.find(name);
                if (it_module != it_cls.end()) {
                        auto mod_pair = it_cls.find(name)->second;
                        if (mod_pair.second == abi_version) {
                                return mod_pair.first;
                        } else {
                                std::cerr << "Module " << name << " API version mismatch (required " <<
                                        abi_version << ", have " << mod_pair.second << ")\n";
                                return NULL;
                        }
                } else {
                        return NULL;
                }
        } else {
                return NULL;
        }
}

map<string, void *> get_modules_for_class(enum library_class cls, int abi_version)
{
        map<string, void *> ret;
        auto it = modules->find(cls);
        if (it != modules->end()) {
                for (auto && item : it->second) {
                        if (abi_version == item.second.second) {
                                ret[item.first] = item.second.first;
                        } else {
                                std::cerr << "Module " << item.first << " API version mismatch (required " <<
                                        abi_version << ", have " << item.second.second << ")\n";

                        }
                }
        }

        return ret;
}
