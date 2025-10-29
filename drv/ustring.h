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

#include "allocator.h"

#undef min
#undef max

// new UNICODE_STRING wrappers
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
		template<class Base = UNICODE_STRING, pool_type pool = pool_type::NonPaged>
		class pool_allocation_strategy : public Base
		{
		protected:
			using char_type = std::decay_t<decltype(*std::declval<Base>().Buffer)>;
			using string_type = Base;

			[[nodiscard]]
			constexpr std::basic_string_view<char_type> get_view() const noexcept
			{
				return { this->data(), this->size() };
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

		private:
			char_type *allocate(size_t length) noexcept
			{
				if (length + 1 > from_bytes<char_type>(this->MaximumLength))
				{
					// For compatibility with os support for UNICODE strings, we need to allocate one more character
					// and make it zero
					delete[]this->Buffer;
					this->MaximumLength = to_bytes<char_type>((USHORT)(length + 1));
					this->Buffer = new (pool) char_type[length + 1];
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
				this->Length = to_bytes<char_type>((USHORT)string.size());

				return *this;
			}

			constexpr auto &operator =(const Base &v) noexcept
			{
				const auto length = from_bytes<char_type>(v.Length);
				sr::copy(std::wstring_view{ v.Buffer, length }, this->allocate(length));
				this->Length = v.Length;
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

		public:
			pool_allocation_strategy(size_t preallocated_chars) noexcept :
				Base{
					.Length = to_bytes<char_type>((USHORT) preallocated_chars),
					.MaximumLength = to_bytes<char_type>((USHORT) (preallocated_chars + 1)),
					.Buffer = new (pool) char_type[preallocated_chars + 1]
				}
			{
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
			using string_type = Base;

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
			using string_type = Base;

			constexpr static_allocation_strategy() noexcept :
				Base{}
			{
			}

			constexpr static_allocation_strategy(const static_allocation_strategy &o) = default;
			constexpr static_allocation_strategy &operator =(const static_allocation_strategy &o) = default;

			constexpr static_allocation_strategy(std::basic_string_view<char_type> string) noexcept :
				Base{
					.Length = to_bytes<char_type>((USHORT)string.size()),
					.MaximumLength = to_bytes<char_type>((USHORT)string.size()),
					.Buffer = const_cast<char_type *>(string.data()),
			}
			{
			}

			constexpr static_allocation_strategy &operator =(std::basic_string_view<char_type> string) noexcept
			{
				this->Length = this->MaximumLength = (USHORT)to_bytes<char_type>(string.size());
				this->Buffer = string.data();

				return *this;
			}
		};

		template<class Base = UNICODE_STRING>
		class external_allocation_strategy : public Base
		{
		public:
			using char_type = std::decay_t<decltype(*std::declval<Base>().Buffer)>;
			using string_type = Base;

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

		template<class AllocationStrategy>
		class string_t : public AllocationStrategy
		{
		public:
			using strategy_t = AllocationStrategy;
			using string_type = typename strategy_t::string_type;
			using char_type = typename strategy_t::char_type;

			// reuse base class's constructors and assignment operators
			using strategy_t::strategy_t;
			using strategy_t::operator =;

			constexpr string_t() = default;

			constexpr bool operator ==(std::basic_string_view<char_type> v) const noexcept
			{
				return this->get_view() == v;
			}

			template<class OtherStrategy>
			[[nodiscard]]
			constexpr bool operator ==(const string_t<OtherStrategy> &o) const noexcept
			{
				return this->get_view() == o.get_view();
			}

			template<class OtherStrategy>
			[[nodiscard]]
			bool equal_case_insensitive(const string_t<OtherStrategy> &o) const noexcept
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

		template<class Strategy>
		struct get_pool_type
		{
			static constexpr const auto value = pool_type::NonPaged;
		};

		template<class String, pool_type pool>
		struct get_pool_type<pool_allocation_strategy<String, pool>>
		{
			static constexpr const auto value = pool;
		};

		template<class Strategy>
		static constexpr const auto get_pool_type_v = get_pool_type<Strategy>::value;

		inline consteval auto get_common_pool_type(pool_type left, pool_type right)
		{
			return (pool_type)std::min((int)left, (int)right);
		}

		template<class Strategy1, class Strategy2>
		inline auto operator +(const details::string_t<Strategy1> &left, const details::string_t<Strategy2> &right) noexcept
		{
			static_assert(std::same_as<typename Strategy1::string_type, typename Strategy2::string_type>, "You cannot mix ANSI_STRING and UNICODE_STRING in operator +");
			using String = typename Strategy1::string_type;
			constexpr auto pool = get_common_pool_type(get_pool_type_v<Strategy1>, get_pool_type_v<Strategy2>);
			using result_t = string_t<pool_allocation_strategy<String, pool>>;

			result_t result{ (size_t) (left.size() + right.size()) };
			auto it = sr::copy(left.get_view(), result.data()).out;
			sr::copy(right.get_view(), it);
			return result;
		}
	}
	using details::string_t;
	using details::operator+;

	/// <summary>
	/// The string is backed up by paged or non-paged pool (freed using ExFreePool)
	/// </summary>
	template<pool_type pool = pool_type::NonPaged>
	using unicode_string_t = details::string_t<details::pool_allocation_strategy<UNICODE_STRING, pool>>;

	/// <summary>
	/// The string is freed using RtlFreeUnicodeString/RtlFreeAnsiString
	/// </summary>
	using sys_unicode_string_t = details::string_t<details::sys_allocation_strategy<UNICODE_STRING>>;
	
	/// <summary>
	/// The string is backed up by a static storage and is never freed
	/// </summary>
	using static_unicode_string_t = details::string_t<details::static_allocation_strategy<UNICODE_STRING>>;

	/// <summary>
	/// This string is backed up by external memory buffer
	/// </summary>
	using external_unicode_string_t = details::string_t<details::external_allocation_strategy<UNICODE_STRING>>;
}
