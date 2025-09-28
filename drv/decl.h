//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

// Define supported OS version
#define DECLSPEC_DEPRECATED_DDK
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#define NTDDI_VERSION NTDDI_WIN10_RS3
#include <sdkddkver.h>

// Disable all debug STL machinery
// This will allow us to safely compile debug versions of our driver
#if _MSC_VER >= 1944
#define _MSVC_STL_HARDENING 0
#define _MSVC_STL_DOOM_FUNCTION(expr)
#else
#define _CONTAINER_DEBUG_LEVEL 0
#define _ITERATOR_DEBUG_LEVEL 0
#define _STL_CRT_SECURE_INVALID_PARAMETER(expr) 
#endif

// Disable call to invalid_parameter runtime function, used in Debug
#define _CRT_SECURE_INVALID_PARAMETER(expr)

// Prevent atomic from referencing CRT's invalid_parameter function
#define _INVALID_MEMORY_ORDER

// Use assert from wdm.h header
#define assert NT_ASSERT

// If you are using boost, the following may also be required
#define BOOST_DISABLE_ASSERTS
#define BOOST_NO_EXCEPTIONS

// Define architecture type as required by WDM
#if defined(_M_AMD64)
#define _AMD64_
#elif defined(_M_ARM64)
#define _ARM64_
#endif
