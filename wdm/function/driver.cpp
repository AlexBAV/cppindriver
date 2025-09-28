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
#include <drv/decl_impl.h>
#include <drv/allocator_impl.h>

DRIVER_ADD_DEVICE Driver_AddDevice;

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, [[maybe_unused]] PUNICODE_STRING RegistryPath)
{
	// DriverEntry is called at PASSIVE_LEVEL
	PAGED_CODE();

	// Set dispatch routines
	sr::fill(sr::subrange(DriverObject->MajorFunction, DriverObject->MajorFunction + IRP_MJ_MAXIMUM_FUNCTION + 1), [](PDEVICE_OBJECT DeviceObject, PIRP Irp) noexcept
	{
		return static_cast<drv::IDevice *>(DeviceObject->DeviceExtension)->drv_dispatch(Irp);
	});

	// Set AddDevice routine
	DriverObject->DriverExtension->AddDevice = Driver_AddDevice;
	return STATUS_SUCCESS;
}
