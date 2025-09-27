//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include <stdexcept>
#include <string>
#include <cstdint>
#include <utility>
#include <functional>

#if !defined(GUID_DEFINED)
#define GUID_DEFINED
struct GUID {
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t Data4[8];
};
#endif

namespace drv::com
{
	namespace details
	{
		constexpr const size_t short_guid_form_length = 36;	// {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}
		constexpr const size_t long_guid_form_length = 38;	// XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX

		//
		inline consteval int parse_hex_digit(const char c)
		{
			using namespace std::string_literals;
			if ('0' <= c && c <= '9')
				return c - '0';
			else if ('a' <= c && c <= 'f')
				return 10 + c - 'a';
			else if ('A' <= c && c <= 'F')
				return 10 + c - 'A';
			else
			{
#if defined(_CPPUNWIND)
				throw std::domain_error{ "invalid character in GUID"s };
#else
				return 0;
#endif
			}
		}

		template<class T>
		inline consteval T parse_hex(const char *ptr)
		{
			constexpr size_t digits = sizeof(T) * 2;
			T result{};
			for (size_t i = 0; i < digits; ++i)
				result |= parse_hex_digit(ptr[i]) << (4 * (digits - i - 1));
			return result;
		}

		inline consteval GUID make_guid_helper(const char *begin)
		{
			GUID result{};
			result.Data1 = parse_hex<uint32_t>(begin);
			begin += 8 + 1;
			result.Data2 = parse_hex<uint16_t>(begin);
			begin += 4 + 1;
			result.Data3 = parse_hex<uint16_t>(begin);
			begin += 4 + 1;
			result.Data4[0] = parse_hex<uint8_t>(begin);
			begin += 2;
			result.Data4[1] = parse_hex<uint8_t>(begin);
			begin += 2 + 1;
			for (size_t i = 0; i < 6; ++i)
				result.Data4[i + 2] = parse_hex<uint8_t>(begin + i * 2);
			return result;
		}

		template<size_t N>
		inline consteval GUID make_guid(const char(&str)[N])
		{
			using namespace std::string_literals;
			static_assert(N == (long_guid_form_length + 1) || N == (short_guid_form_length + 1), "String GUID of the form {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} or XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX is expected");

			if constexpr(N == (long_guid_form_length + 1))
			{
				if (str[0] != '{' || str[long_guid_form_length - 1] != '}')
					throw std::domain_error{ "Missing opening or closing brace"s };
			}

			return make_guid_helper(str + (N == (long_guid_form_length + 1) ? 1 : 0));
		}
	}
	using details::make_guid;
}

namespace drv::literals
{
	inline namespace guid_literals
	{
		inline consteval GUID operator ""_guid(const char *str, size_t N)
		{
			using namespace com::details;
			using namespace std::string_literals;

#if defined(_CPPUNWIND)
			if (!(N == long_guid_form_length || N == short_guid_form_length))
				throw std::domain_error{ "String GUID of the form {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} or XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX is expected"s };
			if (N == long_guid_form_length && (str[0] != '{' || str[long_guid_form_length - 1] != '}'))
				throw std::domain_error{ "Missing opening or closing brace"s };
#endif

			return com::details::make_guid_helper(str + (N == long_guid_form_length ? 1 : 0));
		}
	}
}

//using namespace belt::com::literals;

namespace std
{
	template<>
	struct hash<GUID>
	{
		auto operator()(const GUID &val) const noexcept
		{
			// Given that GUIDs are often randomized, a good strategy would be to just take first sizeof(size_t) bytes as a hash value
			return *reinterpret_cast<const size_t *>(&val);
		}
	};
}
