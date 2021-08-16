/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/StdLibExtras.h>
#include <libweb/bindings/Wrappable.h>

namespace Web::NavigationTiming {

class PerformanceTiming final : public Bindings::Wrappable {
public:
    using WrapperType = Bindings::PerformanceTimingWrapper;
    using AllowOwnPtr = TrueType;

    explicit PerformanceTiming(DOM::Window&);
    ~PerformanceTiming();

    void ref();
    void unref();

    u32 navigation_start() { return 0; }
    u32 unload_event_start() { return 0; }
    u32 unload_event_end() { return 0; }
    u32 redirect_start() { return 0; }
    u32 redirect_end() { return 0; }
    u32 fetch_start() { return 0; }
    u32 domain_lookup_start() { return 0; }
    u32 domain_lookup_end() { return 0; }
    u32 connect_start() { return 0; }
    u32 connect_end() { return 0; }
    u32 secure_connection_start() { return 0; }
    u32 request_start() { return 0; }
    u32 response_start() { return 0; }
    u32 response_end() { return 0; }
    u32 dom_loading() { return 0; }
    u32 dom_interactive() { return 0; }
    u32 dom_content_loaded_event_start() { return 0; }
    u32 dom_content_loaded_event_end() { return 0; }
    u32 dom_complete() { return 0; }
    u32 load_event_start() { return 0; }
    u32 load_event_end() { return 0; }

private:
    DOM::Window& m_window;
};

}