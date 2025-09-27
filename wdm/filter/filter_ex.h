//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Sample filter driver
// 
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include <drv/ctl_code.h>
#include <drv/guid.h>

namespace filter
{
	using namespace drv::literals;

	struct version_info
	{
		int current_version;
		int requested_count;
	};

	constexpr const auto CurrentVersion = 1;
	constexpr const u16 FilterDriver = 0x1234;
	constexpr const auto IOCTL_GET_VERSION = drv::ctl::code(FilterDriver, 0x1, drv::ctl::Method::Buffered, drv::ctl::Access::Read);

	constexpr const auto GUID_DEVINTERFACE_MY_FILTER = "{cd87ec5b-5ac2-4e58-9d9e-0e92e7d5f09f}"_guid;
}
