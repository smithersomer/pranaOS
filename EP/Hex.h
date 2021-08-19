/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <EP/ByteBuffer.h>
#include <EP/Optional.h>
#include <EP/String.h>
#include <EP/StringView.h>

namespace EP {

constexpr u8 decode_hex_digit(char digit)
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return 10 + (digit - 'a');
    if (digit >= 'A' && digit <= 'F')
        return 10 + (digit - 'A');
    return 255;
}

Optional<ByteBuffer> decode_hex(const StringView&);

String encode_hex(ReadonlyBytes);

}

using EP::decode_hex;
using EP::decode_hex_digit;
using EP::encode_hex;