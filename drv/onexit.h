//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Based on Andrei Alexandrescu talk
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

namespace drv::details
{
	template<class F>
	class scope_exit
	{
		F f;
	public:
		explicit constexpr scope_exit(const F &f) noexcept :
			f{ f }
		{
		}

		explicit constexpr scope_exit(F &&f) noexcept :
			f{ std::move(f) }
		{
		}

		constexpr ~scope_exit() noexcept
		{
			f();
		}
	};

	template<class F>
	class scope_exit_cancellable
	{
		F f;
		bool cancelled{ false };
	public:
		explicit constexpr scope_exit_cancellable(const F &f) noexcept :
			f{ f }
		{
		}

		explicit constexpr scope_exit_cancellable(F &&f) noexcept :
			f{ std::move(f) }
		{
		}

		constexpr void cancel() noexcept
		{
			cancelled = true;
		}

		constexpr ~scope_exit_cancellable() noexcept
		{
			if (!cancelled)
				f();
		}
	};

	enum class ScopeGuardOnExit {};

	template<class F>
	inline scope_exit<std::decay_t<F>> operator +(ScopeGuardOnExit, F &&f) noexcept
	{
		return scope_exit<std::decay_t<F>>(std::forward<F>(f));
	}

	enum class ScopeGuardOnExitCancellable {};

	template<class F>
	inline scope_exit_cancellable<std::decay_t<F>> operator +(ScopeGuardOnExitCancellable, F &&f) noexcept
	{
		return scope_exit_cancellable<std::decay_t<F>>(std::forward<F>(f));
	}
}

#define BELT_CAT_I(a, b) a ## b
#define BELT_CAT(a, b) BELT_CAT_I(a, b)

// Alexandrescu
#define ANONYMOUS_VARIABLE(x) BELT_CAT(x,__COUNTER__)

#define SCOPE_EXIT \
auto ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE) \
= ::drv::details::ScopeGuardOnExit() + [&]() noexcept \
// end of macro

#define SCOPE_EXIT_CANCELLABLE(name) \
auto name\
= ::drv::details::ScopeGuardOnExitCancellable() + [&]() noexcept \
// end of macro
