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
		/// <summary>
		/// Device distpatch interface
		/// </summary>
		struct IDevice
		{
			virtual ~IDevice() = default;
			virtual NTSTATUS drv_dispatch(PIRP irp) noexcept = 0;
		};

		template<class T>
		class device_t;

		/// <summary>
		/// IRP wrapper
		/// </summary>
		class irp_t
		{
			PIRP irp{};

			template<class T>
			friend class device_t;

			void assert_non_empty() const noexcept
			{
				assert(irp && "irp_t must be non-empty");
			}

			void assert_empty() const noexcept
			{
				assert(!irp && "irp_t must be empty");
			}

		public:
			explicit irp_t(PIRP irp) noexcept :
				irp{ irp }
			{
			}

			irp_t(const irp_t &) = delete;
			irp_t &operator =(const irp_t &) = delete;

			irp_t(irp_t &&o) noexcept :
				irp{ std::exchange(o.irp, {}) }
			{
			}

			irp_t &operator =(irp_t &&o) noexcept
			{
				assert_empty();
				irp = std::exchange(o.irp, {});
				return *this;
			}

#if defined(_DEBUG)
			~irp_t()
			{
				assert_empty();
			}
#endif

			/// <summary>
			/// Obtain a tag from the IRP (to be used with remove locks)
			/// </summary>
			/// <returns></returns>
			[[nodiscard]]
			void *tag() const noexcept
			{
				assert_non_empty();
				return irp;
			}

			[[nodiscard]]
			PIRP operator ->() const noexcept
			{
				assert_non_empty();
				return irp;
			}

			/// <summary>
			/// Test if the object is not empty
			/// </summary>
			[[nodiscard]]
			explicit operator bool() const noexcept
			{
				return !empty();
			}

			/// <summary>
			/// Test if the object is empty
			/// </summary>
			[[nodiscard]]
			bool empty() const noexcept
			{
				return !irp;
			}

			/// <summary>
			/// Detach the currently stored raw IRP pointer
			/// </summary>
			[[nodiscard]]
			PIRP detach() && noexcept
			{
				return std::exchange(irp, {});
			}

			/// <summary>
			/// Attach a raw pointer to the object
			/// </summary>
			/// <param name="irp_"></param>
			void attach(PIRP irp_) noexcept
			{
				assert_empty();
				irp = irp_;
			}

			/// <summary>
			/// Complete the request
			/// Must be called on an r-value reference
			/// </summary>
			/// <param name="status">Status to set for IRP</param>
			/// <param name="information">Additional information to set for IRP</param>
			/// <returns>Status passed in `status` parameter</returns>
			[[nodiscard]]
			NTSTATUS complete(NTSTATUS status, ULONG_PTR information = 0) && noexcept
			{
				assert_non_empty();
				irp->IoStatus.Status = status;
				irp->IoStatus.Information = information;
				::IoCompleteRequest(std::move(*this).detach(), IO_NO_INCREMENT);
				return status;
			}

			/// <summary>
			/// Pass the IRP to another driver
			/// Must be called on an r-value reference
			/// </summary>
			/// <param name="obj">Device object</param>
			/// <returns>The result of the call</returns>
			[[nodiscard]]
			NTSTATUS call_driver(PDEVICE_OBJECT obj) && noexcept
			{
				assert_non_empty();
				return IoCallDriver(obj, std::exchange(irp, {}));
			}

			/// <summary>
			/// Pass the IRP to another driver (must be used for IRP_MJ_POWER requests)
			/// Must be called on an r-value reference
			/// </summary>
			/// <param name="obj">Device object</param>
			/// <returns>The result of the call</returns>
			[[nodiscard]]
			NTSTATUS power_call_driver(PDEVICE_OBJECT obj) && noexcept
			{
				return PoCallDriver(obj, std::exchange(irp, {}));
			}

			/// <summary>
			/// Get pointer to the current stack location
			/// </summary>
			/// <returns>IO_STACK_LOCATION *</returns>
			auto current_stack_location() const noexcept
			{
				assert_non_empty();
				return IoGetCurrentIrpStackLocation(irp);
			}

			/// <summary>
			/// Skip current stack location
			/// </summary>
			void skip_stack_location() noexcept
			{
				assert_non_empty();
				IoSkipCurrentIrpStackLocation(irp);
			}

			/// <summary>
			/// Copy current stack location
			/// </summary>
			void copy_stack_location() noexcept
			{
				assert_non_empty();
				IoCopyCurrentIrpStackLocationToNext(irp);
			}

			/// <summary>
			/// Set completion routine
			/// </summary>
			/// <param name="routine">Completion routine</param>
			/// <param name="context">User-defined parameter</param>
			/// <param name="invoke_on_success">Invoke routine on success</param>
			/// <param name="invoke_on_error">Invoke routine on error</param>
			/// <param name="invoke_on_cancel">Invoke routine on cancellation</param>
			void set_completion_routine(PIO_COMPLETION_ROUTINE routine, void *context = nullptr, bool invoke_on_success = true, bool invoke_on_error = true, bool invoke_on_cancel = true) noexcept
			{
				assert_non_empty();
				IoSetCompletionRoutine(irp, routine, context, invoke_on_success, invoke_on_error, invoke_on_cancel);
			}

			/// <summary>
			/// Call PoStartNextPowerIrp for the IRP
			/// </summary>
			void start_next_power_irp() noexcept
			{
				assert_non_empty();
				PoStartNextPowerIrp(irp);
			}
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
		concept has_dispatch_##v = requires(T &derived, irp_t &&irp) \
		{ \
			{ derived.drv_dispatch_##v(std::move(irp)) } -> std::same_as<NTSTATUS>; \
		}; \
// end of macro
		LIST_OF_REQUESTS
#undef X

		/// <summary>
		/// Base class for a function device object
		/// </summary>
		/// <typeparam name="Derived">Name of the derived class</typeparam>
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

			/// <summary>
			/// Complete IRP and release device object's remove lock
			/// </summary>
			/// <param name="irp">An r-value reference to the IRP</param>
			/// <param name="status">Completion status</param>
			/// <param name="information">Completion information</param>
			/// <returns>The same as passed status</returns>
			[[nodiscard]]
			NTSTATUS complete_irp_and_release_remove_lock(irp_t &&irp, NTSTATUS status, ULONG_PTR information = 0) noexcept
			{
				const auto i = irp.tag();
				auto result = std::move(irp).complete(status, information);
				this->release_remove_lock(i);
				return result;
			}

			/// <summary>
			/// Default dispatch routine
			/// Immediately completes passed IRP with STATUS_NOT_SUPPORTED
			/// </summary>
			NTSTATUS drv_dispatch_default(irp_t &&irp) noexcept
			{
				return std::move(irp).complete(STATUS_NOT_SUPPORTED);
			}

			/// <summary>
			/// Default Power dispatch routine
			/// </summary>
			[[nodiscard]]
			NTSTATUS drv_dispatch_power(irp_t &&irp) noexcept
			{
				switch (irp.current_stack_location()->MinorFunction)
				{
				case IRP_MN_QUERY_POWER:
				case IRP_MN_SET_POWER:
					irp.start_next_power_irp();
					break;
				}

				return std::move(irp).complete(STATUS_SUCCESS);
			}

			/// <summary>
			/// Default PNP dispatch routine
			/// Deletes the device and destroys `this` when IRP_MN_REMOVE_DEVICE request is received
			/// </summary>
			[[nodiscard]]
			NTSTATUS drv_dispatch_pnp(irp_t &&irp) noexcept
			{
				const auto tag = irp.tag();

				NTSTATUS status = this->acquire_remove_lock(tag);
				if (STATUS_SUCCESS != status)
					return std::move(irp).complete(status);

				if (auto stack = irp.current_stack_location(); stack->MinorFunction == IRP_MN_REMOVE_DEVICE)
				{
					delete_device(tag);
					return std::move(irp).complete(STATUS_SUCCESS);
				}
				else
					return complete_irp_and_release_remove_lock(std::move(irp), STATUS_SUCCESS);
			}

			/// <summary>
			/// Delete the kernel device object and this
			/// </summary>
			/// <param name="tag">Tag used to acquire remove lock</param>
			[[nodiscard]]
			void delete_device(void *tag) noexcept
			{
				IoReleaseRemoveLockAndWait(&this->RemoveLock, tag);
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
				irp_t irp{ Irp };
				switch (irp.current_stack_location()->MajorFunction)
				{
				case IRP_MJ_READ:
					if constexpr (has_dispatch_read<Derived>)
						return derived().drv_dispatch_read(std::move(irp));
					else
						break;
				case IRP_MJ_WRITE:
					if constexpr (has_dispatch_write<Derived>)
						return derived().drv_dispatch_write(std::move(irp));
					else
						break;
				case IRP_MJ_DEVICE_CONTROL:
					if constexpr (has_dispatch_device_control<Derived>)
						return derived().drv_dispatch_device_control(std::move(irp));
					else
						break;
				case IRP_MJ_INTERNAL_DEVICE_CONTROL:
					if constexpr (has_dispatch_internal_device_control<Derived>)
						return derived().drv_dispatch_internal_device_control(std::move(irp));
					else
						break;
				case IRP_MJ_PNP:
					if constexpr (has_dispatch_pnp<Derived>)
						return derived().drv_dispatch_pnp(std::move(irp));
					else
						break;
				case IRP_MJ_CREATE:
					if constexpr (has_dispatch_create<Derived>)
						return derived().drv_dispatch_create(std::move(irp));
					else
						break;
				case IRP_MJ_CLOSE:
					if constexpr (has_dispatch_close<Derived>)
						return derived().drv_dispatch_close(std::move(irp));
					else
						break;
				case IRP_MJ_CLEANUP:
					if constexpr (has_dispatch_cleanup<Derived>)
						return derived().drv_dispatch_cleanup(std::move(irp));
					else
						break;
				case IRP_MJ_POWER:
					if constexpr (has_dispatch_power<Derived>)
						return derived().drv_dispatch_power(std::move(irp));
					else
						break;
				}
				return derived().drv_dispatch_default(std::move(irp));
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

			/// <summary>
			/// Acquire the remove lock
			/// </summary>
			/// <param name="tag">A custom tag (usually PIRP)</param>
			/// <returns>STATUS_SUCCESS or an error value if device object has been marked for deletion</returns>
			[[nodiscard]]
			NTSTATUS acquire_remove_lock(void *tag) noexcept
			{
				return IoAcquireRemoveLock(&RemoveLock, tag);
			}

			/// <summary>
			/// Release the remove lock
			/// </summary>
			/// <param name="tag">A custom tag that should match the one passed in acquire_remove_lock</param>
			void release_remove_lock(void *tag) noexcept
			{
				IoReleaseRemoveLock(&RemoveLock, tag);
			}

			/// <summary>
			/// Convert the pointer to a kernel device object to the pointer to the C++ device object
			/// </summary>
			/// <param name="obj"></param>
			/// <returns></returns>
			static Derived *from_device_object(PDEVICE_OBJECT obj) noexcept
			{
				return static_cast<Derived *>(static_cast<IDevice *>(obj->DeviceExtension));
			}

			/// <summary>
			/// Initialize a C++ device object in the kernel device object's extension
			/// </summary>
			/// <param name="pdo">Kernel device object</param>
			/// <param name="...args">Any values to be passed to the constructor</param>
			/// <returns>Pointer to a created object</returns>
			template<class...Args>
			static Derived *create_device_object(PDEVICE_OBJECT pdo, Args &&...args) noexcept
			{
				return std::construct_at(from_device_object(pdo), std::forward<Args>(args)...);
			}
		};

		/// <summary>
		/// Integration with boost::intrusive_ptr
		/// Allows usage of boost::intrusive_ptr<DeviceObject> for managing reference-counting links to C++ device objects
		/// </summary>
		template<class Derived>
		inline void intrusive_ptr_add_ref(device_t<Derived> *p) noexcept
		{
			if (!nt_success(p->acquire_remove_lock(p)))
				p->set_deleted();
		}

		/// <summary>
		/// Integration with boost::intrusive_ptr
		/// Allows usage of boost::intrusive_ptr<DeviceObject> for managing reference-counting links to C++ device objects
		/// </summary>
		template<class Derived>
		inline void intrusive_ptr_release(device_t<Derived> *p) noexcept
		{
			p->release_remove_lock(p);
		}

		/// <summary>
		/// Base class for a filter device object
		/// </summary>
		/// <typeparam name="Derived">A name of the derived class</typeparam>
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

			/// <summary>
			/// Delete the kernel device object and this
			/// </summary>
			/// <param name="tag">Tag used to acquire remove lock</param>
			[[nodiscard]]
			void delete_device(void *tag) noexcept
			{
				IoReleaseRemoveLockAndWait(&this->RemoveLock, tag);
				IoDetachDevice(NextDO);
				auto obj = this->ThisDO;
				std::destroy_at(static_cast<Derived *>(this));
				IoDeleteDevice(obj);
			}

		public:
			/// <summary>
			/// Default Power dispatch routine for filter drivers
			/// </summary>
			[[nodiscard]]
			NTSTATUS drv_dispatch_power(irp_t &&irp) noexcept
			{
				const auto tag = irp.tag();
				auto status = this->acquire_remove_lock(tag);
				if (STATUS_SUCCESS != status)
				{
					irp.start_next_power_irp();
					return std::move(irp).complete(status);
				}

				irp.start_next_power_irp();
				status = std::move(irp).power_call_driver(NextDO);
				this->release_remove_lock(tag);
				return status;
			}

			/// <summary>
			/// Default dispatch routine for a filter driver
			/// </summary>
			[[nodiscard]]
			NTSTATUS drv_dispatch_default(irp_t &&irp) noexcept
			{
				const auto tag = irp.tag();

				NTSTATUS status = this->acquire_remove_lock(tag);
				if (STATUS_SUCCESS != status)
					return std::move(irp).complete(status);

				irp.skip_stack_location();
				status = std::move(irp).call_driver(NextDO);
				this->release_remove_lock(tag);
				return status;
			}
		};
	}

	using details::IDevice;
	using details::device_t;
	using details::basic_filter_device_t;
	using details::irp_t;
}

/// <summary>
/// Use this macro in the beginning of a dispatch routine
/// It tries to acquire a remove lock and if it fails, 
/// Completes the IRP and returns a status code
/// </summary>
#define DISPATCH_PROLOG(Irp) \
{ \
	if (auto status = this->acquire_remove_lock((Irp).tag()); !nt_success(status)) \
		return std::move(Irp).complete(status); \
} \
// end of macro
