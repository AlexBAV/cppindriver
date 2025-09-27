//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

enum class pool_type
{
	NonPaged,
	Paged,
};

extern void *__cdecl operator new(size_t size);
extern void *__cdecl operator new[](size_t size);
extern void *__cdecl operator new(size_t size, pool_type pool);
extern void *__cdecl operator new[](size_t size, pool_type pool);
extern void *__cdecl operator new(size_t size, std::align_val_t);

extern void operator delete(void *ptr) noexcept;
extern void operator delete[](void *ptr) noexcept;
extern void operator delete(void *ptr, size_t) noexcept;
extern void operator delete(void *ptr, size_t, std::align_val_t) noexcept;
extern void operator delete[](void *ptr, size_t) noexcept;