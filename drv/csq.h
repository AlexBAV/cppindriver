//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include <cstddef>
#include "irp.h"
#include "list.h"

namespace drv
{
	namespace details
	{
		/// <summary>
		/// Holds a list of IRPs
		/// </summary>
		class irp_list
		{
			effective_db_list<IRP, list_entry<IRP, offsetof(IRP, Tail.Overlay.ListEntry)>> list;

		public:
#if defined(_DEBUG)
			~irp_list()
			{
				assert(list.empty());
			}
#endif
			NTSTATUS on_insert_impl(PIRP irp, [[maybe_unused]] void *context) noexcept
			{
				list.add_tail(irp);
				return STATUS_SUCCESS;
			}

			void on_remove_impl(PIRP irp) noexcept
			{
				list.remove(irp);
			}

			PIRP on_peek_impl(PIRP irp, void *context) noexcept
			{
				auto next = (irp == nullptr) ? list.get_head() : list.get_next(irp);

				while (next && context && IoGetCurrentIrpStackLocation(next)->FileObject != context)
					next = list.get_next(next);

				return next;
			}
		};

		/// <summary>
		/// Holds a single IRP
		/// </summary>
		class single_irp
		{
			PIRP stored{};
		public:
#if defined(_DEBUG)
			~single_irp()
			{
				assert(!stored);
			}
#endif

			NTSTATUS on_insert_impl(PIRP irp, [[maybe_unused]] void *context) noexcept
			{
				assert(!stored);
				stored = irp;
				return STATUS_SUCCESS;
			}

			void on_remove_impl([[maybe_unused]] PIRP irp) noexcept
			{
				assert(stored == irp);
				stored = {};
			}

			PIRP on_peek_impl(PIRP irp, void *context) noexcept
			{
				if (!stored || irp)
					return {};

				if (context && IoGetCurrentIrpStackLocation(stored)->FileObject != context) [[unlikely]]
					return {};

				return std::exchange(stored, {});
			}
		};

		template<class Derived, class IrpStorage = irp_list>
		class cancel_safe_queue : protected IrpStorage
		{
			IO_CSQ queue;
			KSPIN_LOCK lock;
			
			//
			static cancel_safe_queue &get(IO_CSQ *ptr) noexcept
			{
				return *reinterpret_cast<cancel_safe_queue *>(reinterpret_cast<std::byte *>(ptr) - offsetof(cancel_safe_queue, queue));
			}

			decltype(auto) derived(this auto &self) noexcept
			{
				return static_cast<Derived &>(self);
			}

			static decltype(auto) derived(IO_CSQ *ptr) noexcept
			{
				return get(ptr).derived();
			}

		public:
			using base_queue = cancel_safe_queue;

			cancel_safe_queue() noexcept
			{
				KeInitializeSpinLock(&lock);
				IoCsqInitializeEx(&queue,
					[](_IO_CSQ *Csq, PIRP Irp, PVOID InsertContext) noexcept -> NTSTATUS	// InsertIrp
				{
					return get(Csq).on_insert_impl(Irp, InsertContext);
				},
					[](PIO_CSQ Csq, PIRP Irp) noexcept
				{
					return get(Csq).on_remove_impl(Irp);
				},
				[](PIO_CSQ Csq, PIRP Irp, PVOID PeekContext) noexcept -> PIRP
				{
					return get(Csq).on_peek_impl(Irp, PeekContext);
				},
				[](PIO_CSQ Csq, PKIRQL Irql) noexcept
				{
					KeAcquireSpinLock(&get(Csq).lock, Irql);
				},
				[](PIO_CSQ Csq, KIRQL Irql) noexcept
				{
					KeReleaseSpinLock(&get(Csq).lock, Irql);
				},
				[](PIO_CSQ Csq, PIRP Irp) noexcept
				{
					derived(Csq).csq_on_cancel(irp_t{ Irp });
				}
				);
			}

			cancel_safe_queue(const cancel_safe_queue &) = delete;
			cancel_safe_queue &operator =(const cancel_safe_queue &) = delete;

			// overrides
			void csq_on_cancel(irp_t &&irp) noexcept
			{
				std::ignore = std::move(irp).complete(STATUS_CANCELLED);
			}

			// public API
			void insert(irp_t &&irp, PIO_CSQ_IRP_CONTEXT Context = nullptr, PVOID InsertContext = nullptr) noexcept
			{
				IoCsqInsertIrpEx(&queue, std::move(irp).detach(), Context, InsertContext);
			}

			irp_t remove_next(PVOID PeekContext = nullptr) noexcept
			{
				return irp_t{ IoCsqRemoveNextIrp(&queue, PeekContext) };
			}
		};

		/// <summary>
		/// Default queue implementation (completes IRP with STATUS_CANCELLED on cancel)
		/// </summary>
		/// <typeparam name="IrpStorage"></typeparam>
		template<class IrpStorage = irp_list>
		class cancel_safe_queue_default : public cancel_safe_queue<cancel_safe_queue_default<IrpStorage>, IrpStorage>
		{
		};
	}

	namespace storage_policy
	{
		using details::irp_list;
		using details::single_irp;
	}

	using details::cancel_safe_queue;
	using details::cancel_safe_queue_default;
}
