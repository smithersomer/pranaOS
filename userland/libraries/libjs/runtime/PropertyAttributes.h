/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <base/Format.h>
#include <base/Types.h>

namespace JS {

struct Attribute {
    enum {
        Writable = 1 << 0,
        Enumerable = 1 << 1,
        Configurable = 1 << 2,
    };
};

// 6.1.7.1 Property Attributes, https://tc39.es/ecma262/#sec-property-attributes
class PropertyAttributes {
public:
    PropertyAttributes(u8 bits = 0)
        : m_bits(bits)
    {
    }

    [[nodiscard]] bool is_writable() const { return m_bits & Attribute::Writable; }
    [[nodiscard]] bool is_enumerable() const { return m_bits & Attribute::Enumerable; }
    [[nodiscard]] bool is_configurable() const { return m_bits & Attribute::Configurable; }

    void set_writable(bool writable = true)
    {
        if (writable)
            m_bits |= Attribute::Writable;
        else
            m_bits &= ~Attribute::Writable;
    }

    void set_enumerable(bool enumerable = true)
    {
        if (enumerable)
            m_bits |= Attribute::Enumerable;
        else
            m_bits &= ~Attribute::Enumerable;
    }

    void set_configurable(bool configurable = true)
    {
        if (configurable)
            m_bits |= Attribute::Configurable;
        else
            m_bits &= ~Attribute::Configurable;
    }

    bool operator==(const PropertyAttributes& other) const { return m_bits == other.m_bits; }
    bool operator!=(const PropertyAttributes& other) const { return m_bits != other.m_bits; }

    [[nodiscard]] u8 bits() const { return m_bits; }

private:
    u8 m_bits;
};

PropertyAttributes const default_attributes = Attribute::Configurable | Attribute::Writable | Attribute::Enumerable;

}

namespace Base {

template<>
struct Formatter<JS::PropertyAttributes> : Formatter<StringView> {
    void format(FormatBuilder& builder, JS::PropertyAttributes const& property_attributes)
    {
        Vector<String> parts;
        parts.append(String::formatted("[[Writable]]: {}", property_attributes.is_writable()));
        parts.append(String::formatted("[[Enumerable]]: {}", property_attributes.is_enumerable()));
        parts.append(String::formatted("[[Configurable]]: {}", property_attributes.is_configurable()));
        Formatter<StringView>::format(builder, String::formatted("PropertyAttributes {{ {} }}", String::join(", ", parts)));
    }
};

}