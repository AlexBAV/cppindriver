//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

namespace drv::details
{
	template<class Base, class T>
	concept t_or_const = std::same_as<Base, T> || std::same_as<const Base, T>;

	template<class From, class To>
	using pointer_like = std::conditional_t<std::is_const_v<From>, const To *, To *>;

	template<class T, size_t offset>
	struct list_entry
	{
		using link_type = LIST_ENTRY;

		template<class V>
		static auto *to_link(V *ptr) noexcept requires t_or_const<T, V>
		{
			return reinterpret_cast<pointer_like<V, link_type>>(reinterpret_cast<std::byte *>(ptr) + offset);
		}

		template<class V>
		static auto *to_T(V *ptr) noexcept requires t_or_const<link_type, V>
		{
			return reinterpret_cast<pointer_like<V, T>>(reinterpret_cast<std::byte *>(ptr) - offset);
		}

		template<class V>
		static auto &next(V *cur) noexcept requires t_or_const<link_type, V>
		{
			return cur->Flink;
		}

		template<class V>
		static auto &prev(V *cur) noexcept requires t_or_const<link_type, V>
		{
			return cur->Blink;
		}
	};

	template<class T, class LinkEntry>
	class effective_db_list
	{
		using link_type = typename LinkEntry::link_type;

		link_type head;

		template<class V>
		static auto *to_link(V *ptr) noexcept requires t_or_const<T, V>
		{
			return LinkEntry::to_link(ptr);
		}

		template<class V>
		static auto *to_T(V *ptr) noexcept requires t_or_const<link_type, V>
		{
			return LinkEntry::to_T(ptr);
		}

		template<class V>
		static auto &nextl(V *ptr) noexcept requires t_or_const<link_type, V>
		{
			return LinkEntry::next(ptr);
		}

		template<class V>
		static auto &prevl(V *ptr) noexcept requires t_or_const<link_type, V>
		{
			return LinkEntry::prev(ptr);
		}

		constexpr void iRemove(T *pElement) noexcept
		{
			auto *pCur = to_link(pElement);
			nextl(prevl(pCur)) = nextl(pCur);
			prevl(nextl(pCur)) = prevl(pCur);
#ifdef _DEBUG
			nextl(pCur) = prevl(pCur) = (link_type *)(DWORD_PTR)0xbaadf00d;
#endif
		}

		constexpr void iAddHead(T *pElement) noexcept
		{
			auto *pCur = to_link(pElement);
			auto *pFirst = nextl(&head);

			nextl(pCur) = pFirst;
			prevl(pCur) = &head;
			prevl(pFirst) = pCur;
			nextl(&head) = pCur;
		}

	public:
		using value_type = T;

		constexpr void iClear() noexcept
		{
			for (link_type *next, *p = nextl(&head); p != &head; p = next)
			{
				next = nextl(p);
				delete to_T(p);
			}
		}
	public:
		constexpr effective_db_list() noexcept
		{
			nextl(&head) = prevl(&head) = &head;
		}

		effective_db_list(const effective_db_list &o) = delete;
		effective_db_list &operator =(const effective_db_list &o) = delete;

		constexpr effective_db_list(effective_db_list &&o) noexcept
		{
#ifdef _DEBUG
			head.Next = head.Prev = &head;		// in debug mode in order to satisfy assert in copy operator, no-op in release mode
#endif
			operator =(std::move(o));
		}

		constexpr effective_db_list &operator =(effective_db_list &&o) noexcept
		{
			assert(empty());

			if (!o.empty())
			{
				nextl(&head) = nextl(&o.head);
				prevl(&head) = prevl(&o.head);
				prevl(nextl(&head)) = &head;
				nextl(prevl(&head)) = &head;

				nextl(&o.head) = prevl(&o.head) = &o.head;
			}

			return *this;
		}

		constexpr void clear() noexcept
		{
			nextl(&head) = prevl(&head) = &head;
		}

		constexpr void add_tail(T *pElement) noexcept
		{
			auto *pCur = to_link(pElement);
			auto *pLast = prevl(&head);

			nextl(pCur) = &head;
			prevl(pCur) = pLast;
			nextl(pLast) = pCur;
			prevl(&head) = pCur;
		}

		constexpr void add_head(T *pElement) noexcept
		{
			iAddHead(pElement);
		}

		constexpr void insert_before(T *pElement, T *before) noexcept
		{
			if (before == nullptr)
				add_tail(pElement);
			else
			{
				auto *pCur = to_link(pElement);
				auto *pbefore = to_link(before);
				auto *prev = prevl(pbefore);

				prevl(pCur) = prev;
				nextl(pCur) = before;
				prevl(pbefore) = pCur;
				nextl(prev) = pCur;
			}
		}

		constexpr void insert_after(T *pElement, T *after) noexcept
		{
			if (after == nullptr)
				add_head(pElement);
			else
			{
				auto *pCur = to_link(pElement);
				auto *pafter = to_link(after);
				auto *next = nextl(pafter);

				prevl(pCur) = pafter;
				nextl(pCur) = next;
				nextl(pafter) = pCur;
				prevl(next) = pCur;
			}
		}

		constexpr T *get_head() const noexcept
		{
			if (empty())
				return {};
			else
				return to_T(nextl(&head));
		}

		constexpr T *get_head_unsafe() const noexcept
		{
			return to_T(nextl(&head));
		}

		constexpr T *get_tail() const noexcept
		{
			if (empty())
				return nullptr;
			else
				return to_T(prevl(&head));
		}

		constexpr T *get_tail_unsafe() const noexcept
		{
			return to_T(prevl(&head));
		}

		template<bool mark_detached = false>
		constexpr T *remove_head() noexcept
		{
			T *ret;
			{
				if (empty())
					return {};
				ret = to_T(nextl(&head));
				iRemove(ret);
			}
			if constexpr (mark_detached)
				to_link(ret)->mark_detached();
			return ret;
		}

		template<bool mark_detached = false>
		constexpr T *remove_tail() noexcept
		{
			T *ret;
			{
				if (empty())
					return {};
				ret = to_T(prevl(&head));
				iRemove(ret);
			}

			if constexpr (mark_detached)
				to_link(ret)->mark_detached();
			return ret;
		}

		template<bool mark_detached = false>
		constexpr bool remove(T *pElement) noexcept		// returns true if the item was removed
		{
			{
				if constexpr (mark_detached)
				{
					if (to_link(pElement)->is_detached())
						return false;
				}
				else
				{
					assert(in_list(pElement));
				}

				iRemove(pElement);
			}
			if constexpr (mark_detached)
				to_link(pElement)->mark_detached();

			return true;
		}

		constexpr void touch(T *pElement) noexcept
		{
			auto *pCur = to_link(pElement);
			if (nextl(&head) != pCur)
			{
				assert(in_list(pElement));

				iRemove(pElement);
				iAddHead(pElement);
			}
		}

		constexpr void swap(T *p1, T *p2) noexcept
		{
			using func = void (effective_db_list:: *)(T *pElement, T *before) noexcept;
			const func routines[] =
			{
				&effective_db_list::InsertBefore,
				&effective_db_list::InsertAfter
			};

			assert(in_list(p1) && in_list(p2));

			int after[2] = { 0,0 };
			T *p1b = get_next(p1);
			T *p2b = get_next(p2);

			if (p1b == p2)
			{
				after[0] = 1;
				p1b = get_prev(p1);
			}

			if (p2b == p1)
			{
				after[1] = 1;
				p2b = get_prev(p2);
			}

			iRemove(p1);
			iRemove(p2);

			(this->*routines[after[0]])(p2, p1b);
			(this->*routines[after[1]])(p1, p2b);
		}

		constexpr bool in_list(T *pElement) const noexcept
		{
			auto *pCur = to_link(pElement);
			for (auto *it = nextl(&head); it != &head; it = nextl(it))
			{
				if (it == pCur)
					return true;
			}
			return false;
		}

		[[nodiscard]]
		constexpr bool empty() const noexcept
		{
			return nextl(&head) == &head;
		}

		constexpr T *get_prev(T *cur) const noexcept
		{
			auto *prev = prevl(to_link(cur));
			return prev == &head ? nullptr : to_T(prev);
		}

		constexpr T *get_next(T *cur) const noexcept
		{
			auto *next = nextl(to_link(cur));
			return next == &head ? nullptr : to_T(next);
		}

		constexpr const T *get_prev(const T *cur) const noexcept
		{
			const auto *prev = prevl(to_link(cur));
			return prev == &head ? nullptr : to_T(prev);
		}

		constexpr const T *get_next(const T *cur) const noexcept
		{
			const auto *next = nextl(to_link(cur));
			return next == &head ? nullptr : to_T(next);
		}

		constexpr T *safe_get_prev(T *cur) const noexcept
		{
			return get_prev(cur);
		}

		constexpr T *safe_get_next(T *cur) const noexcept
		{
			return get_next(cur);
		}

		constexpr T *get_next_unsafe(T *cur) const noexcept
		{
			auto *next = nextl(to_link(cur));
			return to_T(next);
		}

		constexpr const T *get_next_unsafe(const T *cur) const noexcept
		{
			const auto *next = nextl(to_link(cur));
			return to_T(next);
		}

		constexpr T *get_prev_unsafe(T *cur) const noexcept
		{
			auto *prev = prevl(to_link(cur));
			return to_T(prev);
		}

		constexpr const T *get_prev_unsafe(const T *cur) const noexcept
		{
			const auto *prev = prevl(to_link(cur));
			return to_T(prev);
		}

		constexpr bool eof(const T *ptr) const noexcept
		{
			return to_link(ptr) == &head;
		}

	private:

		// TODO: iterator_facade from belt is required
		// std-compliant iterators
		//template<bool forward, bool is_const>
		//class list_iterator : public belt::iterator_facade<list_iterator<forward, is_const>>
		//{
		//	using value_type = std::conditional_t<is_const, std::add_const_t<link_type>, link_type>;
		//	friend class CEffectiveDBList;
		//	value_type *current;

		//	constexpr list_iterator(value_type *element) noexcept :
		//		current{ element }
		//	{
		//	}

		//public:
		//	list_iterator() = default;

		//	auto operator <=>(const list_iterator &o) const noexcept
		//	{
		//		return current <=> o.current;
		//	}

		//	bool operator ==(const list_iterator &o) const noexcept
		//	{
		//		return current == o.current;
		//	}

		//	constexpr void increment() noexcept
		//	{
		//		if constexpr (forward)
		//			current = nextl(current);
		//		else
		//			current = prevl(current);
		//	}

		//	constexpr void decrement() noexcept
		//	{
		//		if constexpr (forward)
		//			current = prevl(current);
		//		else
		//			current = nextl(current);
		//	}

		//	constexpr T &dereference() const noexcept
		//	{
		//		return *to_T(current);
		//	}
		//};

	public:
		//using iterator = list_iterator<true, false>;
		//using reverse_iterator = list_iterator<false, false>;
		//using const_iterator = list_iterator<true, true>;
		//using const_reverse_iterator = list_iterator<false, true>;

		//constexpr auto begin() noexcept
		//{
		//	return list_iterator<true, false>{nextl(&head)};
		//}

		//constexpr auto cbegin() const noexcept
		//{
		//	return list_iterator<true, true>{nextl(&head)};
		//}

		//constexpr auto begin() const noexcept
		//{
		//	return cbegin();
		//}

		//static constexpr auto at(T *ptr) noexcept
		//{
		//	return list_iterator<true, false>{to_link(ptr)};
		//}

		//static constexpr auto at(const T *ptr) noexcept
		//{
		//	return list_iterator<true, true>{to_link(ptr)};
		//}

		//static constexpr auto rat(T *ptr) noexcept
		//{
		//	return list_iterator<false, false>{to_link(ptr)};
		//}

		//static constexpr auto rat(const T *ptr) noexcept
		//{
		//	return list_iterator<false, true>{to_link(ptr)};
		//}

		//constexpr auto end() noexcept
		//{
		//	return list_iterator<true, false>{&head};
		//}

		//constexpr auto cend() const noexcept
		//{
		//	return list_iterator<true, true>{&head};
		//}

		//constexpr auto end() const noexcept
		//{
		//	return cend();
		//}

		//constexpr auto rbegin() noexcept
		//{
		//	return list_iterator<false, false>{head.Prev};
		//}

		//constexpr auto rcbegin() const noexcept
		//{
		//	return list_iterator<false, true>{head.Prev};
		//}

		//constexpr auto rbegin() const noexcept
		//{
		//	return rcbegin();
		//}

		//constexpr auto rend() noexcept
		//{
		//	return list_iterator<false, false>{&head};
		//}

		//constexpr auto rcend() const noexcept
		//{
		//	return list_iterator<false, true>{&head};
		//}

		//constexpr auto rend() const noexcept
		//{
		//	return rcend();
		//}
	};
}
