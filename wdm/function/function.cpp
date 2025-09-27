//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Sample function driver
// 
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#include "pch.h"
#include <drv/decl.h>
#include <drv/csq.h>

#include "function_ex.h"

constexpr const auto MaxBufferSize = 1 * 1024 * 1024;

/// <summary>
/// A simple queue implementation
/// </summary>
class static_buffer
{
	std::unique_ptr<std::byte[]> storage{ std::make_unique_for_overwrite<std::byte[]>(MaxBufferSize) };
	size_t used{};

public:
	[[nodiscard]]
	auto free_space() const noexcept
	{
		return MaxBufferSize - used;
	}

	[[nodiscard]]
	auto size() const noexcept
	{
		return used;
	}

	[[nodiscard]]
	bool empty() const noexcept
	{
		return used == 0;
	}

	auto *data() noexcept
	{
		return storage.get();
	}

	auto begin() noexcept
	{
		return data();
	}

	auto end() noexcept
	{
		return data() + size();
	}

	void append(std::span<const std::byte> appended_data) noexcept
	{
		assert(used + appended_data.size() <= MaxBufferSize);
		sr::copy(appended_data, end());
		used += appended_data.size();
	}

	void erase(size_t bytes) noexcept
	{
		assert(bytes <= used);
		if (bytes == used) [[likely]]
			used = 0;
		else
		{
			sr::copy(std::span{ *this }.subspan(bytes), begin());
			used -= bytes;
		}
	}
};

/// <summary>
/// Function device object C++ object
/// </summary>
class function_device_t : public drv::device_t<function_device_t>
{
	PDEVICE_OBJECT pdo, nextdo;
	drv::unicode_string_t devinterface;
	drv::cancel_safe_queue_default<> in_queue, out_queue;
	std::atomic<int> opened_count{};
	static_buffer buffer;
	wil::kernel_spin_lock buffer_lock;

	//
	void process_pending_reads() noexcept;
	void process_pending_writes() noexcept;

public:
	function_device_t(PDEVICE_OBJECT pdo, PDEVICE_OBJECT fdo, PDEVICE_OBJECT nextdo) noexcept :
		drv::device_t<function_device_t>{ fdo },
		pdo{ pdo },
		nextdo{ nextdo }
	{
		// Our sample device provides buffered I/O for simplicity
		fdo->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
		fdo->Flags &= ~DO_DEVICE_INITIALIZING;
	}

	NTSTATUS drv_final_construct() noexcept;

	NTSTATUS drv_dispatch_pnp(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_create(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_cleanup(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_close(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_read(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_write(drv::irp_t &&irp) noexcept;
};

/// <summary>
/// PNP Driver AddDevice
/// </summary>
NTSTATUS Driver_AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT pdo)
{
	// AddDevice is called at PASSIVE_LEVEL
	PAGED_CODE();

	// Create device object, attach it to device stack and create C++ device object
	return function_device_t::create_and_attach_device_object(DriverObject, pdo);
}

NTSTATUS function_device_t::drv_final_construct() noexcept
{
	drv::sys_unicode_string_t link;
	auto status = IoRegisterDeviceInterface(pdo, &function::GUID_DEVINTERFACE_MY_FUNCTION, nullptr, &link); 
	if (nt_success(status))
		devinterface = link;
		
	return status;
}

/// <summary>
/// PNP dispatch routine. Enables and disables device interface and destroys C++ object, detaches and deletes device object
/// </summary>
/// <param name="irp"></param>
/// <returns></returns>
NTSTATUS function_device_t::drv_dispatch_pnp(drv::irp_t &&irp) noexcept
{
	DISPATCH_PROLOG(irp);

	switch (irp.current_stack_location()->MinorFunction)
	{
	case IRP_MN_START_DEVICE:
		std::ignore = IoSetDeviceInterfaceState(&devinterface, true);
		break;
	case IRP_MN_STOP_DEVICE:
		std::ignore = IoSetDeviceInterfaceState(&devinterface, false);
		break;
	case IRP_MN_REMOVE_DEVICE:
		std::ignore = IoSetDeviceInterfaceState(&devinterface, false);
		IoDetachDevice(nextdo);
		delete_device(irp.tag());
		return std::move(irp).complete(STATUS_SUCCESS);
	}

	return complete_irp_and_release_remove_lock(std::move(irp), STATUS_SUCCESS);
}

/// <summary>
/// CREATE dispatch routine
/// </summary>
/// <param name="irp"></param>
/// <returns></returns>
NTSTATUS function_device_t::drv_dispatch_create(drv::irp_t &&irp) noexcept
{
	DISPATCH_PROLOG(irp);
	opened_count.fetch_add(1, std::memory_order_relaxed);
	return complete_irp_and_release_remove_lock(std::move(irp), STATUS_ACCESS_DENIED);
}

/// <summary>
/// CLEANUP dispatch routine.
/// Its main job is to cancel all pending requests from a given file object
/// </summary>
NTSTATUS function_device_t::drv_dispatch_cleanup(drv::irp_t &&irp) noexcept
{
	DISPATCH_PROLOG(irp);

	auto file_object = irp.current_stack_location()->FileObject;

	// we must cancell all pending requests
	while (auto pending_irp = in_queue.remove_next(file_object))
		std::ignore = std::move(pending_irp).complete(STATUS_CANCELLED);

	while (auto pending_irp = out_queue.remove_next(file_object))
		std::ignore = std::move(pending_irp).complete(STATUS_CANCELLED);

	return complete_irp_and_release_remove_lock(std::move(irp), STATUS_SUCCESS);
}

/// <summary>
/// CLOSE dispatch routine
/// </summary>
NTSTATUS function_device_t::drv_dispatch_close(drv::irp_t &&irp) noexcept
{
	DISPATCH_PROLOG(irp);
	opened_count.fetch_sub(1, std::memory_order_relaxed);
	return complete_irp_and_release_remove_lock(std::move(irp), STATUS_SUCCESS);
}

/// <summary>
/// READ dispatch routine
/// </summary>
NTSTATUS function_device_t::drv_dispatch_read(drv::irp_t &&irp) noexcept
{
	DISPATCH_PROLOG(irp);
	const auto tag = irp.tag();

	const auto read_data = std::span{ static_cast<std::byte *>(irp->AssociatedIrp.SystemBuffer), irp.current_stack_location()->Parameters.Read.Length };

	NTSTATUS result;

	if (auto l = buffer_lock.acquire(); !buffer.empty())
	{
		// Buffer is not empty, we can complete read request synchronously
		const auto bytes_to_copy = std::min(read_data.size(), buffer.size());
		sr::copy(std::span{ buffer }.subspan(0, bytes_to_copy), read_data.begin());
		buffer.erase(bytes_to_copy);
		// Release spin lock as we are about to complete IRP
		l.reset();
		result = std::move(irp).complete(STATUS_SUCCESS, bytes_to_copy);		
	}
	else
	{
		// Buffer is empty, mark this IRP as pending and put it into the CSQ
		l.reset();
		irp.mark_pending();
		in_queue.insert(std::move(irp));
		result = STATUS_PENDING;
	}

	// Check if any pending writes can be processed
	process_pending_writes();
	release_remove_lock(tag);
	return result;
}

/// <summary>
/// WRITE dispatch routine
/// </summary>
NTSTATUS function_device_t::drv_dispatch_write(drv::irp_t &&irp) noexcept
{
	DISPATCH_PROLOG(irp);
	const auto tag = irp.tag();

	const auto input_data = std::span{ static_cast<std::byte *>(irp->AssociatedIrp.SystemBuffer), irp.current_stack_location()->Parameters.Write.Length };

	NTSTATUS result;
	
	if (auto l = buffer_lock.acquire(); buffer.free_space() >= input_data.size())
	{
		// There is enough free space in a buffer, copy and complete IRP synchronously
		buffer.append(input_data);
		result = std::move(irp).complete(STATUS_SUCCESS, input_data.size());
	}
	else
	{
		const auto free_space = buffer.free_space();
		// There is not enough room in the buffer, we will copy a portion of it and will queue IRP
		buffer.append(input_data.subspan(0, free_space));
		// store the number of bytes we already copied
		irp->Tail.Overlay.DriverContext[0] = reinterpret_cast<PVOID>(free_space);
		irp.mark_pending();
		out_queue.insert(std::move(irp));
		result = STATUS_PENDING;
	}

	// Check if any pending reads can be processed
	process_pending_reads();
	release_remove_lock(tag);
	return result;
}

/// <summary>
/// Process any pending reads
/// </summary>
void function_device_t::process_pending_reads() noexcept
{
	bool requests_processed{};
	while (auto irp = in_queue.remove_next())
	{
		auto destination = std::span{ static_cast<std::byte *>(irp->AssociatedIrp.SystemBuffer), irp.current_stack_location()->Parameters.Read.Length };
		auto l = buffer_lock.acquire();
		const auto bytes_to_copy = std::min(destination.size(), buffer.size());
		if (bytes_to_copy)
		{
			sr::copy(std::span{ buffer }.subspan(0, bytes_to_copy), destination.begin());
			buffer.erase(bytes_to_copy);
			requests_processed = true;
			// do not hold spin lock when we complete IRP
			l.reset();
			std::ignore = std::move(irp).complete(STATUS_SUCCESS, bytes_to_copy);
		}
		else
		{
			// do not hold spin lock as we are going to acquire another one
			l.reset();
			// the buffer is empty, requeue irp
			in_queue.insert(std::move(irp));
			break;
		}
	}

	if (requests_processed)
		process_pending_writes();
}

/// <summary>
/// Process any pending writes
/// </summary>
void function_device_t::process_pending_writes() noexcept
{
	bool buffer_grown{};
	while (auto irp = out_queue.remove_next())
	{
		const auto bytes_taken_so_far = reinterpret_cast<ULONG_PTR>(irp->Tail.Overlay.DriverContext[0]);
		const auto irp_buffer = std::span{ static_cast<std::byte *>(irp->AssociatedIrp.SystemBuffer), irp.current_stack_location()->Parameters.Write.Length }
			.subspan(bytes_taken_so_far);

		auto l = buffer_lock.acquire();
		const auto free_space = buffer.free_space();
		if (const auto bytes_to_copy = std::min(free_space, irp_buffer.size()))
		{
			buffer.append(irp_buffer.subspan(bytes_to_copy));
			buffer_grown = true;
			l.reset();
			if (irp_buffer.size() == bytes_to_copy)
			{
				// all data from the write request has been consumed, we can complete it
				std::ignore = std::move(irp).complete(STATUS_SUCCESS, bytes_taken_so_far + bytes_to_copy);
			}
			else
			{
				// update the number of copied bytes and requeue this IRP
				irp->Tail.Overlay.DriverContext[0] = reinterpret_cast<PVOID>(bytes_taken_so_far + bytes_to_copy);
				out_queue.insert(std::move(irp));
				break;
			}
		}
		else
		{
			out_queue.insert(std::move(irp));
			break;
		}
	}

	if (buffer_grown)
		process_pending_reads();
}
