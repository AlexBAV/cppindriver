//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once
#include "ntstatus.h"

namespace drv
{
	namespace details
	{
		struct IDevice
		{
			virtual ~IDevice() = default;
			virtual NTSTATUS drv_dispatch(PIRP irp) noexcept = 0;
		};

#define LIST_OF_REQUESTS \
	X(create) \
	X(close) \
	X(cleanup) \
	X(read) \
	X(write) \
	X(device_control) \
	X(internal_device_control) \
	X(pnp) \
	X(power) \
// end of macro

#define X(v) \
		template<class T> \
		concept has_dispatch_##v = requires(T &derived, PIRP irp) \
		{ \
			{ derived.drv_dispatch_##v(irp) }; \
		}; \
// end of macro
		LIST_OF_REQUESTS
#undef X

		template<class Derived>
		class device_t : public IDevice
		{
		protected:
			using device_base = device_t;

			IO_REMOVE_LOCK RemoveLock;
			PDEVICE_OBJECT ThisDO{};
			std::atomic<bool> delete_pending{};

			device_t(PDEVICE_OBJECT thisdo) noexcept :
				ThisDO{ thisdo }
			{
				IoInitializeRemoveLock(&RemoveLock, 0, 0, 0);
			}

			virtual NTSTATUS drv_dispatch_default(PIRP Irp) noexcept
			{
				if (auto status = this->acquire_remove_lock(Irp); !nt_success(status))
					return complete_request(Irp, status);
				Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
				Irp->IoStatus.Information = 0;
				::IoCompleteRequest(Irp, IO_NO_INCREMENT);
				this->release_remove_lock(Irp);
				return STATUS_NOT_SUPPORTED;
			}

			NTSTATUS complete_request(PIRP Irp, NTSTATUS status, ULONG_PTR Information = 0) noexcept
			{
				Irp->IoStatus.Status = status;
				Irp->IoStatus.Information = Information;
				IoCompleteRequest(Irp, IO_NO_INCREMENT);
				return status;
			}

			NTSTATUS complete_request_and_release_remove_lock(PIRP Irp, NTSTATUS status, ULONG_PTR Information = 0) noexcept
			{
				Irp->IoStatus.Status = status;
				Irp->IoStatus.Information = Information;
				IoCompleteRequest(Irp, IO_NO_INCREMENT);
				this->release_remove_lock(Irp);
				return status;
			}

			NTSTATUS drv_dispatch_power(PIRP Irp) noexcept
			{
				switch (IoGetCurrentIrpStackLocation(Irp)->MinorFunction)
				{
				case IRP_MN_QUERY_POWER:
				case IRP_MN_SET_POWER:
					PoStartNextPowerIrp(Irp);
					break;
				}

				return complete_request(Irp, STATUS_SUCCESS);
			}

			NTSTATUS drv_dispatch_pnp(PIRP Irp) noexcept
			{
				NTSTATUS status = this->acquire_remove_lock(Irp);
				if (STATUS_SUCCESS != status)
					return this->complete_request(Irp, status);

				auto stack = IoGetCurrentIrpStackLocation(Irp);
				if (stack->MinorFunction == IRP_MN_REMOVE_DEVICE)
					return delete_device(Irp);
				else
					this->release_remove_lock(Irp);
				return status;
			}

			NTSTATUS delete_device(PIRP irp) noexcept
			{
				IoReleaseRemoveLockAndWait(&this->RemoveLock, irp);
				auto obj = this->ThisDO;
				std::destroy_at(static_cast<Derived *>(this));
				IoDeleteDevice(obj);
			}

		private:
			auto &derived()
			{
				return *static_cast<Derived *>(this);
			}

			virtual NTSTATUS drv_dispatch(PIRP Irp) noexcept override
			{
				switch (IoGetCurrentIrpStackLocation(Irp)->MajorFunction)
				{
				case IRP_MJ_READ:
					if constexpr (has_dispatch_read<Derived>)
						return derived().drv_dispatch_read(Irp);
					else
						break;
				case IRP_MJ_WRITE:
					if constexpr (has_dispatch_write<Derived>)
						return derived().drv_dispatch_write(Irp);
					else
						break;
				case IRP_MJ_DEVICE_CONTROL:
					if constexpr (has_dispatch_device_control<Derived>)
						return derived().drv_dispatch_device_control(Irp);
					else
						break;
				case IRP_MJ_INTERNAL_DEVICE_CONTROL:
					if constexpr (has_dispatch_internal_device_control<Derived>)
						return derived().drv_dispatch_internal_device_control(Irp);
					else
						break;
				case IRP_MJ_PNP:
					if constexpr (has_dispatch_pnp<Derived>)
						return derived().drv_dispatch_pnp(Irp);
					else
						break;
				case IRP_MJ_CREATE:
					if constexpr (has_dispatch_create<Derived>)
						return derived().drv_dispatch_create(Irp);
					else
						break;
				case IRP_MJ_CLOSE:
					if constexpr (has_dispatch_close<Derived>)
						return derived().drv_dispatch_close(Irp);
					else
						break;
				case IRP_MJ_CLEANUP:
					if constexpr (has_dispatch_cleanup<Derived>)
						return derived().drv_dispatch_cleanup(Irp);
					else
						break;
				case IRP_MJ_POWER:
					if constexpr (has_dispatch_power<Derived>)
						return derived().drv_dispatch_power(Irp);
					else
						break;
				}
				return derived().drv_dispatch_default(Irp);
			}

		public:
			[[nodiscard]]
			bool is_deleted() const noexcept
			{
				return delete_pending.load(std::memory_order_relaxed);
			}

			void set_deleted() noexcept
			{
				delete_pending.store(true, std::memory_order_relaxed);
			}

			[[nodiscard]]
			NTSTATUS acquire_remove_lock(void *tag) noexcept
			{
				return IoAcquireRemoveLock(&RemoveLock, tag);
			}

			void release_remove_lock(void *tag) noexcept
			{
				IoReleaseRemoveLock(&RemoveLock, tag);
			}

			static Derived *from_device_object(PDEVICE_OBJECT obj) noexcept
			{
				return static_cast<Derived *>(static_cast<IDevice *>(obj->DeviceExtension));
			}
			////////////////////////
		};

		template<class Derived>
		inline void intrusive_ptr_add_ref(device_t<Derived> *p) noexcept
		{
			if (!nt_success(p->acquire_remove_lock(p)))
				p->set_deleted();
		}

		template<class Derived>
		inline void intrusive_ptr_release(device_t<Derived> *p) noexcept
		{
			p->release_remove_lock(p);
		}

		template<class Derived>
		class basic_filter_device_t : public device_t<Derived>
		{
		protected:
			using filter_base = basic_filter_device_t;

			PDEVICE_OBJECT PDO{}, NextDO{};

			basic_filter_device_t(PDEVICE_OBJECT pdo, PDEVICE_OBJECT fido, PDEVICE_OBJECT nextdo) noexcept :
				device_t<Derived>{ fido },
				PDO{ pdo },
				NextDO{ nextdo }
			{
			}

			NTSTATUS delete_device(PIRP irp) noexcept
			{
				IoReleaseRemoveLockAndWait(&this->RemoveLock, irp);
				IoDetachDevice(NextDO);
				auto obj = this->ThisDO;
				std::destroy_at(static_cast<Derived *>(this));
				IoDeleteDevice(obj);
				return STATUS_SUCCESS;
			}

		public:
			NTSTATUS drv_dispatch_power(PIRP Irp) noexcept
			{
				auto status = this->acquire_remove_lock(Irp);
				if (STATUS_SUCCESS != status)
				{
					Irp->IoStatus.Status = status;
					PoStartNextPowerIrp(Irp);
					return this->complete_request(Irp, status);
				}

				PoStartNextPowerIrp(Irp);
				IoSkipCurrentIrpStackLocation(Irp);
				status = PoCallDriver(NextDO, Irp);
				this->release_remove_lock(Irp);
				return status;
			}

			NTSTATUS drv_dispatch_pnp(PIRP Irp) noexcept
			{
				auto status = this->acquire_remove_lock(Irp);
				if (STATUS_SUCCESS != status)
					return this->complete_request(Irp, status);

				auto stack = IoGetCurrentIrpStackLocation(Irp);
				auto fcn = stack->MinorFunction;
				IoSkipCurrentIrpStackLocation(Irp);
				status = IoCallDriver(NextDO, Irp);
				if (fcn == IRP_MN_REMOVE_DEVICE)
					return delete_device(Irp);
				else
					this->release_remove_lock(Irp);
				return status;
			}

			NTSTATUS drv_dispatch_default(PIRP Irp) noexcept
			{
				NTSTATUS status = this->acquire_remove_lock(Irp);
				if (STATUS_SUCCESS != status)
					return this->complete_request(Irp, status);
				IoSkipCurrentIrpStackLocation(Irp);
				status = IoCallDriver(NextDO, Irp);
				this->release_remove_lock(Irp);
				return status;
			}
		};
	}

	using details::IDevice;
	using details::device_t;
	using details::basic_filter_device_t;
}

#define DISPATCH_PROLOG(Irp) \
{ \
	if (auto status = this->acquire_remove_lock(Irp); !nt_success(status)) \
		return complete_request((Irp), status); \
} \
// end of macro

