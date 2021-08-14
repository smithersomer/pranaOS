/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <libjs/runtime/NativeFunction.h>

namespace JS {

class AggregateErrorConstructor final : public NativeFunction {
    JS_OBJECT(AggregateErrorConstructor, NativeFunction);

public:
    explicit AggregateErrorConstructor(GlobalObject&);
    virtual void initialize(GlobalObject&) override;
    virtual ~AggregateErrorConstructor() override = default;

    virtual Value call() override;
    virtual Value construct(FunctionObject& new_target) override;

private:
    virtual bool has_constructor() const override { return true; }
};

}