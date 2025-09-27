//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include "allocator.h"

constexpr const auto Tag = 'HHDS';

[[nodiscard]]
inline void *pool_alloc(size_t size, pool_type type) noexcept
{
	const POOL_TYPE pt = type == pool_type::NonPaged ? NonPagedPoolNx : PagedPool;
#pragma warning(suppress: 4996)	// The following function is deprecated, but replacement might not be available on target OS
	return ::ExAllocatePoolWithTag(pt, size, Tag);
}

inline void pool_free(void *p)
{
	if (p)
		::ExFreePoolWithTag(p, Tag);
}

[[nodiscard]]
void *__cdecl operator new(size_t size)
{
	return pool_alloc(size, pool_type::NonPaged);
}

[[nodiscard]] 
void *__cdecl operator new[](size_t size)
{
	return pool_alloc(size, pool_type::NonPaged);
}

[[nodiscard]]
void *__cdecl operator new(size_t size, pool_type pool)
{
	return pool_alloc(size, pool);
}

[[nodiscard]]
void *__cdecl operator new[](size_t size, pool_type pool)
{
	return pool_alloc(size, pool);
}

[[nodiscard]]
void *__cdecl operator new(size_t size, std::align_val_t)
{
	return pool_alloc(size, pool_type::NonPaged);
}

//

void operator delete(void *ptr) noexcept
{
	pool_free(ptr);
}

void operator delete[](void *ptr) noexcept
{
	pool_free(ptr);
}

void operator delete(void *ptr, size_t) noexcept
{
	pool_free(ptr);
}

void operator delete(void *ptr, size_t, std::align_val_t) noexcept
{
	pool_free(ptr);
}

void operator delete[](void *ptr, size_t) noexcept
{
	pool_free(ptr);
}
