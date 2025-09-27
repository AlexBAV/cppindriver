//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include <cstddef>

namespace bkern
{
	template<class Derived>
	class cancel_safe_queue
	{
		IO_CSQ queue;
		KSPIN_LOCK lock;
		belt::CEffectiveDBList<IRP, belt::list_entry<IRP, offsetof(IRP, Tail.Overlay.ListEntry)>> list;

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

		NTSTATUS on_insert_impl(PIRP irp, [[maybe_unused]] void *context) noexcept
		{
			list.AddTail(irp);

			return STATUS_SUCCESS;
		}

		void on_remove_impl(PIRP irp) noexcept
		{
			list.Remove(irp);
		}

		PIRP on_peek_impl(PIRP irp, [[maybe_unused]] void *context) noexcept
		{
			if (irp == nullptr)
				return list.GetHead();
			else
				return list.GetNext(irp);
		}

	public:
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
				derived(Csq).csq_on_cancel(Irp);
			}
			);
		}

		cancel_safe_queue(const cancel_safe_queue &) = delete;
		cancel_safe_queue &operator =(const cancel_safe_queue &) = delete;

		// overrides
		void csq_on_cancel(PIRP irp) noexcept
		{
			irp->IoStatus.Status = STATUS_CANCELLED;
			irp->IoStatus.Information = 0;
			IoCompleteRequest(irp, IO_NO_INCREMENT);
		}

		// public API
		NTSTATUS insert(PIRP Irp, PIO_CSQ_IRP_CONTEXT Context = nullptr, PVOID InsertContext = nullptr) noexcept
		{
			return IoCsqInsertIrpEx(&queue, Irp, Context, InsertContext);
		}

		PIRP remove_next(PVOID PeekContext = nullptr) noexcept
		{
			return IoCsqRemoveNextIrp(&queue, PeekContext);
		}
	};
}
