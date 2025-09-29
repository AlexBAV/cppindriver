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
			[[nodiscard]]
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
			/// Mark IRP pending
			/// </summary>
			void mark_pending() noexcept
			{
				assert_non_empty();
				IoMarkIrpPending(irp);
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
	}
	using details::irp_t;
}
