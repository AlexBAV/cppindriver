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
	drv::unicode_string_t devinterface;
	std::atomic<int> counter{};

	//
	void on_device_started() noexcept
	{
		drv::sys_unicode_string_t link;
		if (auto status = IoRegisterDeviceInterface(PDO, &filter::GUID_DEVINTERFACE_MY_FILTER, nullptr, &link); nt_success(status))
		{
			devinterface = link;
			std::ignore = IoSetDeviceInterfaceState(&link, true);
		}
	}

	void on_device_stopped() noexcept
	{
		std::ignore = IoSetDeviceInterfaceState(&devinterface, false);
	}

	NTSTATUS on_pnp_completion(PIRP irp) noexcept;

public:
	filter_device_t(PDEVICE_OBJECT pdo, PDEVICE_OBJECT fido, PDEVICE_OBJECT nextdo) noexcept :
		drv::basic_filter_device_t<filter_device_t>{ pdo, fido, nextdo }
	{
		auto copiedflags = NextDO->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO);
		if (!copiedflags)
			copiedflags = DO_DIRECT_IO;
		ThisDO->Flags |= copiedflags | DO_POWER_PAGABLE;
		ThisDO->Flags &= ~DO_DEVICE_INITIALIZING;
	}

	NTSTATUS drv_dispatch_device_control(PIRP irp) noexcept;
	NTSTATUS drv_dispatch_pnp(PIRP irp) noexcept;
};

NTSTATUS Driver_AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT pdo)
{
	PDEVICE_OBJECT fido;
	if (auto status = IoCreateDevice(DriverObject, sizeof(filter_device_t), nullptr, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, false, &fido); nt_error(status))
		return status;

	auto nextdo = IoAttachDeviceToDeviceStack(fido, pdo);
	if (!nextdo)
	{
		IoDeleteDevice(fido);
		return STATUS_DELETE_PENDING;
	}

	filter_device_t::create_device_object(fido, pdo, fido, nextdo);
	return STATUS_SUCCESS;
}

NTSTATUS filter_device_t::on_pnp_completion(PIRP irp) noexcept
{
	if (irp->PendingReturned)
		IoMarkIrpPending(irp);

	switch (IoGetCurrentIrpStackLocation(irp)->MinorFunction)
	{
	case IRP_MN_START_DEVICE:
	{
		on_device_started();
		break;
	}
	case IRP_MN_STOP_DEVICE:
		on_device_stopped();
		break;
	case IRP_MN_REMOVE_DEVICE:
		on_device_stopped();
		return delete_device(irp);
	}

	release_remove_lock(irp);
	return STATUS_SUCCESS;
}

NTSTATUS filter_device_t::drv_dispatch_device_control(PIRP irp) noexcept
{
	switch (auto &dic = IoGetCurrentIrpStackLocation(irp)->Parameters.DeviceIoControl; dic.IoControlCode)
	{
	case filter::IOCTL_GET_VERSION:
		DISPATCH_PROLOG(irp);
		if (dic.OutputBufferLength >= sizeof(filter::version_info))
		{
			auto *pv = static_cast<filter::version_info *>(irp->AssociatedIrp.SystemBuffer);
			pv->current_version = filter::CurrentVersion;
			pv->requested_count = counter.fetch_add(1, std::memory_order_relaxed);

			return complete_request_and_release_remove_lock(irp, STATUS_SUCCESS, sizeof(*pv));
		}
		else
			return complete_request_and_release_remove_lock(irp, STATUS_INSUFFICIENT_RESOURCES);
	}

	return filter_base::drv_dispatch_default(irp);
}

NTSTATUS filter_device_t::drv_dispatch_pnp(PIRP irp) noexcept
{
	DISPATCH_PROLOG(irp);

	IoCopyCurrentIrpStackLocationToNext(irp);
	IoSetCompletionRoutine(irp, [](PDEVICE_OBJECT DeviceObject, PIRP Irp, [[maybe_unused]] PVOID Context) noexcept
	{
		return from_device_object(DeviceObject)->on_pnp_completion(Irp);
	}, nullptr, true, true, true);

	return IoCallDriver(NextDO, irp);
}
