//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

#if defined(_DEBUG)
int __cdecl _CrtDbgReport(
	[[maybe_unused]] int         _ReportType,
	[[maybe_unused]] char const *_FileName,
	[[maybe_unused]] int         _Linenumber,
	[[maybe_unused]] char const *_ModuleName,
	[[maybe_unused]] char const *_Format,
	...)
	{
	return 0;
	}

int __cdecl _CrtDbgReportW(
	[[maybe_unused]] int            _ReportType,
	[[maybe_unused]] wchar_t const *_FileName,
	[[maybe_unused]] int            _LineNumber,
	[[maybe_unused]] wchar_t const *_ModuleName,
	[[maybe_unused]] wchar_t const *_Format,
	...)
	{
	return 0;
	}

extern "C" void __cdecl __security_init_cookie(void)
	{
	}

void __cdecl _wassert(
	[[maybe_unused]] wchar_t const *_Message,
	[[maybe_unused]] wchar_t const *_File,
	[[maybe_unused]] unsigned       _Line
)
{
}

#endif
