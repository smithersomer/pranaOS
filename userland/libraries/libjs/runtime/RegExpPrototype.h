/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <libjs/runtime/RegExpObject.h>
#include <libjs/runtime/Utf16String.h>

namespace JS {

Value regexp_exec(GlobalObject& global_object, Object& regexp_object, Utf16String string);
size_t advance_string_index(Utf16View const& string, size_t index, bool unicode);

class RegExpPrototype final : public Object {
    JS_OBJECT(RegExpPrototype, Object);

public:
    explicit RegExpPrototype(GlobalObject&);
    virtual void initialize(GlobalObject&) override;
    virtual ~RegExpPrototype() override;

private:
    JS_DECLARE_NATIVE_GETTER(flags);
    JS_DECLARE_NATIVE_GETTER(source);

    JS_DECLARE_NATIVE_FUNCTION(exec);
    JS_DECLARE_NATIVE_FUNCTION(test);
    JS_DECLARE_NATIVE_FUNCTION(to_string);
    JS_DECLARE_NATIVE_FUNCTION(symbol_match);
    JS_DECLARE_NATIVE_FUNCTION(symbol_match_all);
    JS_DECLARE_NATIVE_FUNCTION(symbol_replace);
    JS_DECLARE_NATIVE_FUNCTION(symbol_search);
    JS_DECLARE_NATIVE_FUNCTION(symbol_split);

#define __JS_ENUMERATE(_, flag_name, ...) \
    JS_DECLARE_NATIVE_GETTER(flag_name);
    JS_ENUMERATE_REGEXP_FLAGS
#undef __JS_ENUMERATE
};

}