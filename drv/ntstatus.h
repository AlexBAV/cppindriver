#pragma once
//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------


[[nodiscard, msvc::forceinline]]
inline constexpr bool nt_success(NTSTATUS Status) noexcept
{
	return Status >= 0;
}

[[nodiscard, msvc::forceinline]]
inline constexpr bool nt_error(NTSTATUS Status) noexcept
{
	return (Status >> 30) == 3;
}

#if defined(__USB_H__)
[[nodiscard, msvc::forceinline]]
inline constexpr bool usbd_success(USBD_STATUS Status) noexcept
{
	return Status >= 0;
}
#endif

[[nodiscard, msvc::forceinline]]
inline constexpr NTSTATUS nt_from_hresult(HRESULT hr) noexcept
{
	constexpr const unsigned facility_nt_bit = 0x10000000;
	return hr & ~facility_nt_bit;
}

[[nodiscard, msvc::forceinline]]
inline constexpr HRESULT hresult_from_nt(NTSTATUS status) noexcept
{
	constexpr const unsigned facility_nt_bit = 0x10000000;
	return (HRESULT)(status | facility_nt_bit);
}
