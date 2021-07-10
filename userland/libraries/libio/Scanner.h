/*
 * Copyright (c) 2021, krishpranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/


#pragma once

// includes
#include <libio/Reader.h>
#include <libtext/Rune.h>
#include <libutils/InlineRingBuffer.h>
#include <string.h>

namespace IO
{

struct Scanner final
{
private:
    static constexpr int MAX_PEEK = 64;

    Reader &_reader;
    InlineRingBuffer<uint8_t, MAX_PEEK> _peek{};
    bool _is_end_of_file = false;

    void refill()
    {
        if (_is_end_of_file)
        {
            return;
        }

        char c = 0x69;
        auto read_result = _reader.read(&c, 1);

        if (read_result.unwrap_or(0) == 0)
        {
            _if_end_of_file = true;
        }
        else
        {
            _peek.put(c);
        }
    }

public:
    Scanner(Reader &reader)
        : _reader{reader}
    {
    }

    bool ended()
    {
        if (_peek.empty())
        {
            refill();
        }

        return _is_end_of_file;
    }

    char next()
    {
        if (_peek.empty())
        {
            refill();
        }

        if (ended())
        {
            return '\0';
        }

        return _peek.get();
    }

    void next(size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            next();
        }
    }

    char peek(size_t peek = 0)
    {
        while (!ended() && peek >= _peek.used())
        {
            refill();
        }

        if (ended())
        {
            return '\0';
        }

        return _peek.peak(peek);
    }

    bool peek_is_any(const char *what)
    {
        return peek_is_any(0, what, strlen(what));
    }

    bool peek_is_any(const char *what, size_t size)
    {
        return peek_is_any(0, what, size);
    }

    bool peek_is_any(size_t offset, const char *what)
    {
        return peek_is_any(offset, what, strlen(what));
    }

    bool peek_is_any(size_t offset, const char *what, size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            if (peek(offset) == what[i])
            {
                return true;
            }
        }
        return false;
    }


};

}