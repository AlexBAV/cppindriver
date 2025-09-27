# Writing Windows Drivers using C++ and STL

## Introduction

Windows drivers have been traditionally developed using C programming language. All usage examples, all existing frameworks and APIs only imply C.

But what would you say, if I tell you that drivers not only can be developed using C++ (including latest language standards, like C++23), but can also use large portion of standard library, including STL. My recent driver is safe with using the following STL headers:

* `<memory>`: `std::unique_ptr`, including `std::make_unique_*`.
* `<array>`
* `<atomic>`
* `<algorithm>`
* `<ranges>`
* `<chrono>`
* `<type_traits>`
* `<concepts>`
* `<string_view>`
* `<utility>`: `std::exchange`, `std::move`, `std::swap`, `std::pair` ...
* `<tuple>`
* `<optional>`
* `<variant>`[^variant]
* `<bit>`
* `<span>`
* `<expected>`
* `<mutex>`[^mutex]
* `<coroutine>`

[^variant]: Actually, due to the obligatory support for `valueless_by_exception`, which implies support for exceptions, `std::variant` cannot be used without too much effort. We use `boost::variant2` instead, a version of variant with similar API but without support for "valueless by exception".

[^mutex]: We don't use any synchronization primitive classes from `<mutex>` as they require runtime support and are not directly available in kernel mode. However, the header itself is safe to include and any lock classes, such as `std::unique_lock`, `std::scoped_lock` or `std::shared_lock` can safely be used in kernel code with any compatible manually-written synchronization primitive classes.

Additionally, the following libraries have been successfully used from Boost:

* `variant2`
* `intrusive_ptr`

### Background

I've started my first Windows driver in 90s and was immediately disappointed with requirement to do it in C. However, after some investigations, I've managed to write the driver code in C++. That work was published in my 1998 article "A Serial Port Spy for NT" in Dr. Dobbs journal.

Of course that source code predated not only C++11, but even C++98 and more accurately should be called "C with classes". MSVC's C++ Standard Library was completely unprepared to be used for kernel mode driver development at that time and, as the result, the code avoided including any standard header at all.

Nevertheless, using C++ allowed me to have a C++ object for each device object. C++ device object lifetime was bound to system device object lifetime, with appropriate calls to constructors and destructors. It also simplified I/O dispatching, provided RAII for using locks and allowed to develop generic containers (mostly linked lists, of course) among other things.

That driver, and a number of others that used the same technology have served our company for more than 20 years. Of course they received updates and improvements, but the overall design of "C with classes" has not changed for years. Implementation for some small classes or functions from C++ standard library they began to use (like `std::move`) have been copied from STL headers, because it was still impossible to include any standard header.

### Modern Approach

Recently, we started developing the new product that had to include two kind of device drivers: a WDM driver (Windows Driver Model) and a KMDF driver (Kernel-Mode Driver Framework). I've decided to check what has changed in terms of C++ support in kernel driver development and whether I could extend its C++ support to latest language standards as well as, probably, use some STL?

It turns out, that latest releases of MSVC (now tested with Visual Studio 2022 17.14.16), including STL, have been developed with kernel mode support in mind. I'm not sure if this is now in official requirement list or what (I could not find any publicly available information from Microsoft about using C++ in drivers). 

Another strong point in favour of this conclusion is that another popular Microsoft library for native Windows development, [Windows Implementation Library (wil)](https://github.com/microsoft/wil) now also provides special support for kernel-mode driver development, including wrapper classes for a number of kernel-mode synchronization primitives and their corresponding RAII-friendly locks. Looks like at least some teams at Microsoft use C++ for driver development.

With minimal effort, I've been able to introduce full support for C++23 and STL (subset mentioned above) to both WDM and KMDF drivers for the new product. Not only that, my new drivers can use C++ coroutines, which greatly simplifies asynchronous code!

### Limitations

The first and most important limitation is the lack of support for exceptions. You cannot (natively) use exceptions in kernel mode and, therefore, driver code is compiled with exceptions disabled. This blocks a large portion of standard library for us, notably, prohibits the use of `std::vector`. You can still write your own version of vector that uses preallocated memory or implements its own grow strategy or use any existing version (for example, from Boost.Containers).

I've seen attempts to manually implement the required exception machinery in kernel mode, but have not experimented with it myself. It looks very "hacky" to me, while I strived to keep the implementation as robust as possible.

Next limitation is again caused by the lack of Runtime library: you cannot have global objects with constructors and destructors. For the same reason, `thread_local` and static objects with constructors may also not be used.

Apart from that, we can use any Standard Library class or function that could be decoupled from runtime support (that is, either have header-only implementation, or their runtime implementation may be substituted).

## Implementation

### It all Starts with an Allocator

If we talk about C++, the first thing we need is an allocator. Runtime library provides the default allocator for all C++ programs, however, we cannot use Runtime library at all. The following global functions are declared in `allocator.h` file:

```cpp
enum class pool_type
{
	NonPaged,
	Paged,
};

extern void *operator new(size_t size);
extern void *operator new[](size_t size);
extern void *operator new(size_t size, pool_type pool);
extern void *operator new[](size_t size, pool_type pool);
extern void *operator new(size_t size, std::align_val_t);

extern void operator delete(void *ptr) noexcept;
extern void operator delete[](void *ptr) noexcept;
extern void operator delete(void *ptr, size_t) noexcept;
extern void operator delete(void *ptr, size_t, std::align_val_t) noexcept;
extern void operator delete[](void *ptr, size_t) noexcept;
```

Their implementations can be found in `allocator_impl.h` header, which is supposed to be included in one of the driver's source files. The default allocator uses non-paged pool, but there are overloads that accept the pool type, allowing you to construct objects on the paged pool, if required.

### Standard Windows DDK Project Templates

Unfortunately, I was not successful in using predefined project templates from Windows DDK integration with Visual Studio. Using them produced a lot of conflicts when I tried to include standard library headers. As a result, both WDM and KMDF drivers do not use standard templates and that is not a big problem.

Personally, I find them too obtrusive: they not only configure your compilation environment, but also try to deal with INF file preparation, checking, signing and so on, tasks that I prefer to automate with the help of other tools.

In "manual" mode, prepare to handler header file dependencies, library file dependencies as well as manually providing a list of import libraries for linking, as we would have to turn the "Ignore Default Libraries" linker option on. Sample projects illustrate how that can be done.

### Preprocessor Defines

While a standard library is now more "friendly" to kernel-mode development, we still need to fine-tune it a bit. Unfortunately, this is something that might need to be revisited each time you upgrade to the new compiler version or SDK version, so be prepared. Include the `decl.h` in your central header (fine-tuning is valid for Visual Studio 2022 17.14.16 and Platform SDK 26100):

```cpp
// Define supported OS version
#define DECLSPEC_DEPRECATED_DDK
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#define NTDDI_VERSION NTDDI_WIN10_RS3
#include <sdkddkver.h>

// Disable all debug STL machinery
// This will allow us to safely compile debug versions of our driver
#if _MSC_VER >= 1944
#define _MSVC_STL_HARDENING 0
#define _MSVC_STL_DOOM_FUNCTION(expr)
#else
#define _CONTAINER_DEBUG_LEVEL 0
#define _ITERATOR_DEBUG_LEVEL 0
#define _STL_CRT_SECURE_INVALID_PARAMETER(expr) 
#endif

// Disable call to invalid_parameter runtime function, used in Debug
#define _CRT_SECURE_INVALID_PARAMETER(expr)

// Prevent atomic from referencing CRT's invalid_parameter function
#define _INVALID_MEMORY_ORDER

// Use assert from wdm.h header
#define assert NT_ASSERT

// If you are using boost, the following may also be required
#define BOOST_DISABLE_ASSERTS

// Define architecture type as required by WDM
#if defined(_M_AMD64)
#define _AMD64_
#elif defined(_M_ARM64)
#define _ARM64_
#endif
```

Include the `decl_impl.h` in one of the source files. It will add the following overrides, allowing the driver to be successfully compiled in DEBUG mode:

```cpp
#if defined(_DEBUG)
int __cdecl _CrtDbgReport(
	[[maybe_unused]] int         _ReportType,
	[[maybe_unused]] char const *_FileName,
	[[maybe_unused]] int         _Linenumber,
	[[maybe_unused]] char const *_ModuleName,
	[[maybe_unused]] char const *_Format,
	...)
{
	return 0;
}

int __cdecl _CrtDbgReportW(
	[[maybe_unused]] int            _ReportType,
	[[maybe_unused]] wchar_t const *_FileName,
	[[maybe_unused]] int            _LineNumber,
	[[maybe_unused]] wchar_t const *_ModuleName,
	[[maybe_unused]] wchar_t const *_Format,
	...)
{
	return 0;
}

extern "C" void __cdecl __security_init_cookie(void)
{
}

void __cdecl _wassert(
	[[maybe_unused]] wchar_t const *_Message,
	[[maybe_unused]] wchar_t const *_File,
	[[maybe_unused]] unsigned       _Line
)
{
}

#endif
```

### DriverEntry

A WDM driver entry point is called `DriverEntry`. It is very simple for a PNP driver (note the use of lambdas):

```cpp
#include <drv/decl_impl.h>
#include <drv/allocator_impl.h>

// Forward-declare AddDevice routine
DRIVER_ADD_DEVICE Driver_AddDevice;

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, [[maybe_unused]] PUNICODE_STRING RegistryPath)
{
	// DriverEntry is called at PASSIVE_LEVEL
	PAGED_CODE();

	// Set dispatch routines
	sr::fill(sr::subrange(DriverObject->MajorFunction, DriverObject->MajorFunction + IRP_MJ_MAXIMUM_FUNCTION + 1), 
		[](PDEVICE_OBJECT DeviceObject, PIRP Irp) noexcept
		{
			return static_cast<drv::IDevice *>(DeviceObject->DeviceExtension)->drv_dispatch(Irp);
		});

	// Set AddDevice routine
	DriverObject->DriverExtension->AddDevice = Driver_AddDevice;
	return STATUS_SUCCESS;
}
```

Each device object class the driver defines must derive from the `IDevice` interface (aka abstract base class). 

#### Function Device Objects

A function device object class must derive from `device_t<Derived>` template class.

#### Filter Device Objects

A filter device object class must derive from `basic_filter_device_t<Derived>` template class. Example from the filter sample driver:

```cpp
class filter_device_t : public drv::basic_filter_device_t<filter_device_t>
{ 
	... 
public:
	filter_device_t(PDEVICE_OBJECT pdo, PDEVICE_OBJECT fido, PDEVICE_OBJECT nextdo) noexcept :
		drv::basic_filter_device_t<filter_device_t>{ pdo, fido, nextdo }
	{
		...
	}
};
```

[^dispatch]: The library currently handles only subset of all supported major functions, covering the most common ones. However, if required, support can be extended to less common ones.

Device object class can override any dispatch routine[^dispatch] by declaring a public function with the name that follows this template: `drv_dispatch_XXX`, where `XXX` is a major function code, like `create`, `close`, `read`, `write` and so on. Overridden dispatch routine must either synchronously complete the passed IRP, or do it asynchronously, as any other WDM driver.

Library has a wrapper for cancel-safe queue in `csq.h` header, which may be used if the driver needs to safely store passed IRP for later processing.

### Creating Device Objects

Here's the implementation of `Driver_AddDevice` routine for a sample filter driver:

```cpp
NTSTATUS Driver_AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT pdo)
{
	// Create kernel device object, passing the size of C++ class
	PDEVICE_OBJECT fido;
	if (auto status = IoCreateDevice(DriverObject, sizeof(filter_device_t), nullptr, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, false, &fido); nt_error(status))
		return status;

	// Attach device to device stack
	auto nextdo = IoAttachDeviceToDeviceStack(fido, pdo);
	if (!nextdo)
	{
		IoDeleteDevice(fido);
		return STATUS_DELETE_PENDING;
	}

	// Create C++ device object for kernel device object fido, passing the rest values as constructor parameters
	filter_device_t::create_device_object(fido, pdo, fido, nextdo);
	return STATUS_SUCCESS;
}
```

In order to create a C++ device object and associate it with a kernel device object, you need to pass a size of the device oject class in a call to `IoCreateDevice`:

```cpp
PDEVICE_OBJECT fido;
if (auto status = IoCreateDevice(DriverObject, sizeof(filter_device_t), nullptr, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, false, &fido); nt_error(status))
	return status;
```

And create a C++ device object, associated with created kernel device object (the following will call a constructor, passing it any values you specify):

```cpp
filter_device_t::create_device_object(fido, pdo, fido, nextdo);
```

### Destroying Device Objects

The lifetime of a C++ device class is bound to the lifetime of the kernel device object. The actual physical storage of a C++ object is within the device extension of the device object, therefore, we must be very careful when accessing it at the time device object is deleted.

The library provides support for destroying C++ device object with a call to `device_t::delete_device(PIRP)` function. When this function returns, C++ device object cannot be used anymore.

The time this function is called depends on the device object type. The sample filter driver illustrates how you can do it for a filter device object in a completion routine for a PNP request `IRP_MN_REMOVE_DEVICE`:

```cpp
NTSTATUS filter_device_t::on_pnp_completion(PIRP irp) noexcept
{
	if (irp->PendingReturned)
		IoMarkIrpPending(irp);

	switch (IoGetCurrentIrpStackLocation(irp)->MinorFunction)
	{
		...
	case IRP_MN_REMOVE_DEVICE:
		// Disable device interface
		on_device_stopped();
		// Destroy C++ device object and delete kernel device object
		// This will also stop and wait on device remove lock
		return delete_device(irp);
	}

	// release remove lock and complete
	release_remove_lock(irp);
	return STATUS_SUCCESS;
}

NTSTATUS filter_device_t::drv_dispatch_pnp(PIRP irp) noexcept
{
	// acquire device remove lock
	DISPATCH_PROLOG(irp);

	IoCopyCurrentIrpStackLocationToNext(irp);
	IoSetCompletionRoutine(irp, [](PDEVICE_OBJECT DeviceObject, PIRP Irp, [[maybe_unused]] PVOID Context) noexcept
	{
		return from_device_object(DeviceObject)->on_pnp_completion(Irp);
	}, nullptr, true, true, true);

	return IoCallDriver(NextDO, irp);
}
```
