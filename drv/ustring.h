//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include <string_view>
#include <ranges>

namespace drv
{
	namespace details
	{
		namespace sr = std::ranges;
		namespace rv = std::views;

		template<class Char, class T>
		inline constexpr T to_bytes(T chars) noexcept
		{
			return chars * sizeof(Char);
		}

		template<class Char, class T>
		inline constexpr T from_bytes(T bytes) noexcept
		{
			return bytes / sizeof(Char);
		}

		template<class Char>
		inline constexpr bool compare_safe_equal(std::basic_string_view<Char> l, std::basic_string_view<Char> r) noexcept
		{
			auto f = [](Char c) constexpr noexcept
			{
				return ('A' <= c && c <= 'Z') ? c + 'a' - 'A' : c;
			};

			return sr::equal(l, r, {}, f, f);
		}

		// TODO: consider adding support for specifying pool type
		template<class Base = UNICODE_STRING>
		class pool_allocation_strategy : public Base
		{
		protected:
			using char_type = std::decay_t<decltype(*std::declval<Base>().Buffer)>;

		private:
			char_type *allocate(size_t length) noexcept
			{
				if (length + 1 > from_bytes<char_type>(this->MaximumLength))
				{
					// For compatibility with os support for UNICODE strings, we need to allocate one more character
					// and make it zero
					delete[]this->Buffer;
					this->MaximumLength = to_bytes<char_type>((USHORT)(length + 1));
					this->Buffer = new char_type[length + 1];
					this->Buffer[length] = 0;
				}

				return this->Buffer;
			}
		protected:

			pool_allocation_strategy() noexcept :
				Base{}
			{
			}

			pool_allocation_strategy(const pool_allocation_strategy &o) noexcept
			{
				sr::copy(o.get_view(), this->allocate(o.size()));
				this->Length = this->MaximumLength;
			}

			pool_allocation_strategy &operator =(const pool_allocation_strategy &o) noexcept
			{
				sr::copy(o.get_view(), this->allocate(o.size()));
				this->Length = to_bytes<char_type>(o.size());

				return *this;
			}

			pool_allocation_strategy(pool_allocation_strategy &&o) noexcept :
				Base{ std::move(o) }
			{
				static_cast<Base &>(o) = {};
			}

			pool_allocation_strategy &operator =(pool_allocation_strategy &&o) noexcept
			{
				std::swap(static_cast<Base &>(*this), static_cast<Base &>(o));
				return *this;
			}

			pool_allocation_strategy(std::basic_string_view<char_type> string) noexcept
			{
				sr::copy(string, this->allocate(string.size()));
				this->Length = this->MaximumLength;
			}

			pool_allocation_strategy &operator =(std::basic_string_view<char_type> string) noexcept
			{
				sr::copy(string, this->allocate(string.size()));
				this->Length = to_bytes<char_type>((USHORT) string.size());

				return *this;
			}

			~pool_allocation_strategy()
			{
				delete[]this->Buffer;
			}

			void free() noexcept
			{
				delete[]this->Buffer;
				static_cast<Base &>(*this) = {};
			}
		};

		template<class Base = UNICODE_STRING>
		class sys_allocation_strategy : public Base
		{
			size_t allocated_length() const noexcept
			{
				return this->MaximumLength / sizeof(char_type);
			}
		protected:
			using char_type = std::decay_t<decltype(*std::declval<Base>().Buffer)>;

			sys_allocation_strategy() noexcept :
				Base{}
			{
			}

			sys_allocation_strategy(const sys_allocation_strategy &o) = delete;
			sys_allocation_strategy &operator =(const sys_allocation_strategy &o) = delete;

			sys_allocation_strategy(sys_allocation_strategy &&o) noexcept :
				Base{ std::move(o) }
			{
				static_cast<Base &>(o) = {};
			}

			sys_allocation_strategy &operator =(sys_allocation_strategy &&o) noexcept
			{
				std::swap(static_cast<Base &>(*this), static_cast<Base &>(o));
				return *this;
			}

			~sys_allocation_strategy()
			{
				free_impl();
			}

			void free_impl()
			{
				if (this->Buffer)
				{
					if constexpr (std::same_as<UNICODE_STRING, Base>)
						RtlFreeUnicodeString(this);
					else if constexpr (std::same_as<ANSI_STRING, Base>)
						RtlFreeAnsiString(this);
					else
						static_assert(false, "Unsupported base type");
				}
			}

			void free() noexcept
			{
				free_impl();
				static_cast<Base &>(*this) = {};
			}
		};

		template<class Base = UNICODE_STRING>
		class static_allocation_strategy : public Base
		{
		public:
			using char_type = std::decay_t<decltype(*std::declval<Base>().Buffer)>;

			constexpr static_allocation_strategy() noexcept :
				Base{}
			{
			}

			constexpr static_allocation_strategy(const static_allocation_strategy &o) = default;
			constexpr static_allocation_strategy &operator =(const static_allocation_strategy &o) = default;

			constexpr static_allocation_strategy(std::basic_string_view<char_type> string) noexcept :
				Base{
					.Length = to_bytes<char_type>((USHORT) string.size()),
					.MaximumLength = to_bytes<char_type>((USHORT)string.size()),
					.Buffer = const_cast<char_type *>(string.data()),
				}
			{
			}

			constexpr static_allocation_strategy &operator =(std::basic_string_view<char_type> string) noexcept
			{
				this->Length = this->MaximumLength = (USHORT) to_bytes<char_type>(string.size());
				this->Buffer = string.data();

				return *this;
			}
		};

		template<class Base = UNICODE_STRING>
		class external_allocation_strategy : public Base
		{
		public:
			using char_type = std::decay_t<decltype(*std::declval<Base>().Buffer)>;

			constexpr external_allocation_strategy() noexcept :
				Base{}
			{
			}

			constexpr external_allocation_strategy(const external_allocation_strategy &o) = default;
			constexpr external_allocation_strategy &operator =(const external_allocation_strategy &o) = default;

			constexpr external_allocation_strategy(Base v) noexcept :
				Base{ v }
			{
			}

			constexpr external_allocation_strategy &operator =(Base v) noexcept
			{
				*static_cast<Base *>(this) = v;
				return *this;
			}

		};

		template<class Base = UNICODE_STRING, template<class> class AllocationStrategy = pool_allocation_strategy>
		class string_t : public AllocationStrategy<Base>
		{
			using strategy_t = AllocationStrategy<Base>;
			using char_type = typename strategy_t::char_type;

		public:			
			// reuse base class's constructors and assignment operators
			using strategy_t::strategy_t;
			using strategy_t::operator =;

			constexpr string_t() = default;

			constexpr bool operator ==(std::basic_string_view<char_type> v) const noexcept
			{
				return this->get_view() == v;
			}

			template<template<class> class OtherStrategy>
			[[nodiscard]]
			constexpr bool operator ==(const string_t<Base, OtherStrategy> &o) const noexcept
			{
				return this->get_view() == o.get_view();
			}

			template<template<class> class OtherStrategy>
			[[nodiscard]]
			bool equal_case_insensitive(const string_t<Base, OtherStrategy> &o) const noexcept
			{
				return compare_safe_equal(get_view(), o.get_view());
			}

			[[nodiscard]]
			bool equal_case_insensitive(std::basic_string_view<char_type> v) const noexcept
			{
				return compare_safe_equal(get_view(), v);
			}

			[[nodiscard]]
			constexpr auto *data(this auto &self) noexcept
			{
				return self.Buffer;
			}

			[[nodiscard]]
			constexpr auto size() const noexcept
			{
				return from_bytes<char_type>(this->Length);
			}

			[[nodiscard]]
			constexpr std::basic_string_view<char_type> get_view() const noexcept
			{
				return { this->data(), this->size() };
			}

			[[nodiscard]]
			constexpr auto *begin(this auto &self) noexcept
			{
				return self.data();
			}

			[[nodiscard]]
			constexpr auto *end(this auto &self) noexcept
			{
				return self.data() + self.size();
			}

			[[nodiscard]]
			constexpr operator std::basic_string_view<char_type>() const noexcept
			{
				return this->get_view();
			}

			[[nodiscard]]
			constexpr bool empty() const noexcept
			{
				return this->size() == 0;
			}

			constexpr void clear(bool free_storage = true) noexcept
			{
				if (free_storage)
					this->free();
				else
					this->Length = 0;
			}
		};
	}

	using details::string_t;
	using unicode_string_t = details::string_t<UNICODE_STRING>;
	using sys_unicode_string_t = details::string_t<UNICODE_STRING, details::sys_allocation_strategy>;
	using static_unicode_string_t = details::string_t<UNICODE_STRING, details::static_allocation_strategy>;
	using external_unicode_string_t = details::string_t<UNICODE_STRING, details::external_allocation_strategy>;
}


