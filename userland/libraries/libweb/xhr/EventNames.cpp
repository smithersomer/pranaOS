/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <libweb/xhr/EventNames.h>

namespace Web::XHR::EventNames {

#define __ENUMERATE_XHR_EVENT(name) FlyString name;
ENUMERATE_XHR_EVENTS
#undef __ENUMERATE_XHR_EVENT

[[gnu::constructor]] static void initialize()
{
    static bool s_initialized = false;
    if (s_initialized)
        return;

#define __ENUMERATE_XHR_EVENT(name) \
    name = #name;
    ENUMERATE_XHR_EVENTS
#undef __ENUMERATE_XHR_EVENT

    s_initialized = true;
}

}
