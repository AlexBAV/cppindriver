//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include "intdefs.h"

namespace drv::ctl
{
    enum class Method
    {
        Buffered,
        DirectIn,
        DirectOut,
        Neither,
    };

    enum class Access
    {
        Any,
        Special = Any,
        Read,
        Write,
    };

    inline constexpr auto code(u16 device_type, u16 function, Method method, Access access) noexcept
    {
        return (static_cast<u32>(device_type) << 16) | (static_cast<u32>(access) << 14) | (static_cast<u32>(function) << 2) | static_cast<u32>(method);
    }
}
