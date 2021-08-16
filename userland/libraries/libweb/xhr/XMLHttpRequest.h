/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/ByteBuffer.h>
#include <base/RefCounted.h>
#include <base/URL.h>
#include <base/Weakable.h>
#include <libweb/bindings/WindowObject.h>
#include <libweb/bindings/Wrappable.h>
#include <libweb/dom/EventTarget.h>
#include <libweb/dom/ExceptionOr.h>
#include <libweb/xhr/XMLHttpRequestEventTarget.h>

namespace Web::XHR {

class XMLHttpRequest final
    : public RefCounted<XMLHttpRequest>
    , public Weakable<XMLHttpRequest>
    , public XMLHttpRequestEventTarget {
public:
    enum class ReadyState : u16 {
        Unsent = 0,
        Opened = 1,
        HeadersReceived = 2,
        Loading = 3,
        Done = 4,
    };

    using WrapperType = Bindings::XMLHttpRequestWrapper;

    static NonnullRefPtr<XMLHttpRequest> create(DOM::Window& window)
    {
        return adopt_ref(*new XMLHttpRequest(window));
    }
    static NonnullRefPtr<XMLHttpRequest> create_with_global_object(Bindings::WindowObject& window)
    {
        return XMLHttpRequest::create(window.impl());
    }

    virtual ~XMLHttpRequest() override;

    using RefCounted::ref;
    using RefCounted::unref;

    ReadyState ready_state() const { return m_ready_state; };
    unsigned status() const { return m_status; };
    String response_text() const;

    DOM::ExceptionOr<void> open(const String& method, const String& url);
    DOM::ExceptionOr<void> send();

    DOM::ExceptionOr<void> set_request_header(const String& header, const String& value);

    String get_response_header(const String& name) { return m_response_headers.get(name).value_or({}); }

private:
    virtual void ref_event_target() override { ref(); }
    virtual void unref_event_target() override { unref(); }
    virtual bool dispatch_event(NonnullRefPtr<DOM::Event>) override;
    virtual JS::Object* create_wrapper(JS::GlobalObject&) override;

    void set_ready_state(ReadyState);
    void set_status(unsigned status) { m_status = status; }
    void fire_progress_event(const String&, u64, u64);

    explicit XMLHttpRequest(DOM::Window&);

    NonnullRefPtr<DOM::Window> m_window;

    ReadyState m_ready_state { ReadyState::Unsent };
    unsigned m_status { 0 };
    bool m_send { false };

    String m_method;
    URL m_url;

    HashMap<String, String, CaseInsensitiveStringTraits> m_request_headers;
    HashMap<String, String, CaseInsensitiveStringTraits> m_response_headers;

    bool m_synchronous { false };
    bool m_upload_complete { false };
    bool m_upload_listener { false };
    bool m_timed_out { false };

    ByteBuffer m_response_object;
};

}