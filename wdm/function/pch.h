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

// Declarations
#include <drv/decl.h>

// WDM
#include <ntifs.h>
#include <wdm.h>

#undef min
#undef max

// STL
#include <string_view>
#include <utility>
#include <memory>
#include <atomic>
#include <algorithm>
#include <ranges>
#include <tuple>
#include <optional>
#include <expected>
#include <coroutine>

// drv
#include <drv/allocator.h>
#include <drv/ntstatus.h>
#include <drv/device.h>
#include <drv/ustring.h>
#include <drv/guid.h>
#include <drv/onexit.h>

// wil
#include <wil/resource.h>

namespace sr = std::ranges;
namespace rv = std::views;

using namespace std::literals;
