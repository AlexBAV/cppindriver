//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Sample function driver
// 
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include <drv/ctl_code.h>
#include <drv/guid.h>

namespace function
{
	using namespace drv::literals;
	constexpr const auto GUID_DEVINTERFACE_MY_FUNCTION = "{df4c41f9-5548-4189-b3c0-0108f5ce388e}"_guid;
}

