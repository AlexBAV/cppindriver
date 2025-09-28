//-------------------------------------------------------------------------------------------------------
// drv - Windows Driver C++ Support Library
// Copyright (C) 2025 HHD Software Ltd.
// Written by Alex Bessonov
//
// Sample filter driver
// 
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#include "pch.h"
#include "filter_ex.h"

class filter_device_t : public drv::basic_filter_device_t<filter_device_t>
{
	// Illustrate the usage of convenient UNICODE_STRING wrapper
	drv::unicode_string_t devinterface;
	// Illustrate the usage of std::atomic
	std::atomic<int> counter{};

	//
	NTSTATUS on_pnp_completion(PIRP irp) noexcept;

public:
	filter_device_t(PDEVICE_OBJECT pdo, PDEVICE_OBJECT fido, PDEVICE_OBJECT nextdo, std::wstring_view devinterface) noexcept :
		drv::basic_filter_device_t<filter_device_t>{ pdo, fido, nextdo },
		devinterface{ devinterface }
	{
		auto copiedflags = NextDO->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO);
		if (!copiedflags)
			copiedflags = DO_DIRECT_IO;
		ThisDO->Flags |= copiedflags | DO_POWER_PAGABLE;
		ThisDO->Flags &= ~DO_DEVICE_INITIALIZING;
	}

	[[nodiscard]]
	NTSTATUS drv_dispatch_device_control(drv::irp_t &&irp) noexcept;

	[[nodiscard]]
	NTSTATUS drv_dispatch_pnp(drv::irp_t &&irp) noexcept;
};

/// <summary>
/// Implementation of drivers' AddDevice routine
/// It creates a filter device object (FiDO), attaches it to device stack and creates an instance of filter_device_t class
/// </summary>
NTSTATUS Driver_AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT pdo)
{
	// AddDevice is called at PASSIVE_LEVEL
	PAGED_CODE();

	PDEVICE_OBJECT fido;
	if (auto status = IoCreateDevice(DriverObject, sizeof(filter_device_t), nullptr, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, false, &fido); nt_error(status))
		return status;

	SCOPE_EXIT_CANCELLABLE(c1)
	{
		IoDeleteDevice(fido);
	};

	auto nextdo = IoAttachDeviceToDeviceStack(fido, pdo);
	if (!nextdo)
		return STATUS_DELETE_PENDING;

	SCOPE_EXIT_CANCELLABLE(c2)
	{
		IoDetachDevice(nextdo);
	};

	drv::sys_unicode_string_t link;
	if (auto status = IoRegisterDeviceInterface(pdo, &filter::GUID_DEVINTERFACE_MY_FILTER, nullptr, &link); nt_error(status))
		return status;

	c2.cancel();
	c1.cancel();

	filter_device_t::create_device_object(fido, pdo, fido, nextdo, link);
	return STATUS_SUCCESS;
}

/// <summary>
/// PNP I/O completion routine
/// </summary>
NTSTATUS filter_device_t::on_pnp_completion(PIRP irp) noexcept
{
	if (irp->PendingReturned)
		IoMarkIrpPending(irp);

	switch (IoGetCurrentIrpStackLocation(irp)->MinorFunction)
	{
	case IRP_MN_START_DEVICE:
	{
		std::ignore = IoSetDeviceInterfaceState(&devinterface, true);
		break;
	}
	case IRP_MN_STOP_DEVICE:
		std::ignore = IoSetDeviceInterfaceState(&devinterface, false);
		break;
	case IRP_MN_REMOVE_DEVICE:
		std::ignore = IoSetDeviceInterfaceState(&devinterface, false);
		delete_device(irp);
		return STATUS_SUCCESS;
	}

	release_remove_lock(irp);
	return STATUS_SUCCESS;
}

/// <summary>
/// Device Control I/O dispatch routine
/// It handles a custom IOCTL_GET_VERSION I/O control request and returns a structure
/// It also illustrates the usage of device remove lock
/// 
/// Other device I/O controls are forwarded down the device stack
/// </summary>
NTSTATUS filter_device_t::drv_dispatch_device_control(drv::irp_t &&irp) noexcept
{
	switch (auto &dic = irp.current_stack_location()->Parameters.DeviceIoControl; dic.IoControlCode)
	{
	case filter::IOCTL_GET_VERSION:
		DISPATCH_PROLOG(irp);
		if (dic.OutputBufferLength >= sizeof(filter::version_info))
		{
			auto *pv = static_cast<filter::version_info *>(irp->AssociatedIrp.SystemBuffer);
			pv->current_version = filter::CurrentVersion;
			pv->requested_count = counter.fetch_add(1, std::memory_order_relaxed);

			return complete_irp_and_release_remove_lock(std::move(irp), STATUS_SUCCESS, sizeof(*pv));
		}
		else
			return complete_irp_and_release_remove_lock(std::move(irp), STATUS_INSUFFICIENT_RESOURCES);
	}

	return filter_base::drv_dispatch_default(std::move(irp));
}

/// <summary>
/// PNP dispatch routine
/// It installs a completion routine and forwards the request down the device stack
/// </summary>
NTSTATUS filter_device_t::drv_dispatch_pnp(drv::irp_t &&irp) noexcept
{
	DISPATCH_PROLOG(irp);

	irp.copy_stack_location();
	irp.set_completion_routine([](PDEVICE_OBJECT DeviceObject, PIRP Irp, [[maybe_unused]] PVOID Context) noexcept
	{
		return from_device_object(DeviceObject)->on_pnp_completion(Irp);
	});

	return std::move(irp).call_driver(NextDO);
}
