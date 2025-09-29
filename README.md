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
* `<utility>`: `std::exchange`, `std::move`, `std::swap`, `std::pair` …
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

## Samples in this Repository

This repository includes the following samples:

* `wdm/filter`

  A sample filter driver using WDM. It creates a device object and attaches it to the device stack. It then forwards all IRPs to the underlying device object and additionally handles minor PNP requests `IRP_MN_START_DEVICE`, `IRP_MN_STOP_DEVICE` and `IRP_MN_REMOVE_DEVICE` in a completion routine. It enables and disables a device interface in response to the first two and completely destroys the C++ device object as well as its filter device object in response to the last one.
  
  It additionally illustrates how we can define a custom I/O control code and handle it synchronously in a Device I/O control dispatch routine.

* `wdm/function`

  A sample function driver using WDM. It creates a function device object for a loopback device. It registers a device interface and allows itself to be opened by any number of user-mode or kernel-mode callers. It maintains an internal 1MB buffer and stores all data sent to it (using the `WriteFile` function). This data may then be read back with a call to `ReadFile`, either using the same or any other opened handle. A read request is processed synchronously if there are any data in a buffer and asynchronously if the buffer is empty. Correspondingly, a write request is processed synchronously if there is enough room in an internal buffer. Otherwise, the write requests becomes pending until some other caller reads data from the buffer, either using the same handle, or any other handle.

  It illustrates synchronous and asynchronous I/O processing, the use of `cancel_safe_queue` wrapper for kernel Cancel Safe Queues, cancellation of pending I/O requests on handle close among other things.

* `kmdf/function`

  Will be added later

## C++! What About Template Code Bloat?

One of the often heard argument against using C++ in device drivers is that it leads to a code bloat. However, modern compilers are extremely good at optimizing C++ code and we do not use a runtime library, which is usually the one responsible for a the rest of "code bloat".

The sample drivers included in this repository, when compiled for x64 Release target (with optimizations for speed):

* `wdm/filter` - 6.5 KB
* `wdm/function` - 9.5 KB

Of course they are very simple, but nevertheless have almost all required boilerplate and only business logic needs to be added above that. Sample function driver even has some business logic (it implements a simple loopback device with asynchronous I/O processing).

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
#define BOOST_NO_EXCEPTIONS

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

A WDM driver entry point is called `DriverEntry`. It is very simple for a PNP driver.

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
	drv::init_dispatch_routines(DriverObject);

	// Set AddDevice routine
	DriverObject->DriverExtension->AddDevice = Driver_AddDevice;
	return STATUS_SUCCESS;
}
```

`init_dispatch_routines` implementation illustrates that you can freely use standard algorithms, ranges and lambdas in a kernel driver:

```cpp
inline void init_dispatch_routines(PDRIVER_OBJECT DriverObject) noexcept
{
	// Set dispatch routines
	sr::fill(sr::subrange(DriverObject->MajorFunction, DriverObject->MajorFunction + IRP_MJ_MAXIMUM_FUNCTION + 1), [](PDEVICE_OBJECT DeviceObject, PIRP Irp) noexcept
	{
		return static_cast<IDevice *>(DeviceObject->DeviceExtension)->drv_dispatch(Irp);
	});
}
```

#### Function Device Objects

A function device object class must derive from `device_t<Derived>` template class.

```cpp
class function_device_t : public drv::device_t<function_device_t>
{
	...
	function_device_t(PDEVICE_OBJECT pdo, PDEVICE_OBJECT fdo, PDEVICE_OBJECT nextdo, std::wstring_view devinterface) noexcept :
		drv::device_t<function_device_t>{ fdo },
		pdo{ pdo },
		nextdo{ nextdo },
		devinterface{ devinterface }
	{
		...
	}
	...
	// Dispatch routines for major I/O codes we handle
	NTSTATUS drv_dispatch_pnp(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_create(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_cleanup(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_close(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_read(drv::irp_t &&irp) noexcept;
	NTSTATUS drv_dispatch_write(drv::irp_t &&irp) noexcept;
};
```

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
	...
};
```

[^dispatch]: The library currently handles only subset of all supported major functions, covering the most common ones. However, if required, support can be extended to less common ones.

Device object class can override any dispatch routine[^dispatch] by declaring a public function with the name that follows this template: `drv_dispatch_XXX`, where `XXX` is a major function code, like `create`, `close`, `read`, `write` and so on. Overridden dispatch routine must either synchronously complete the passed IRP, or do it asynchronously, as any other WDM driver.

### Creating Device Objects

Here's the implementation of `Driver_AddDevice` routine for a sample function driver:

```cpp
NTSTATUS Driver_AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT pdo)
{
	// AddDevice is called at PASSIVE_LEVEL
	PAGED_CODE();

	return function_device_t::create_and_attach_device_object(DriverObject, pdo);
}
```

The library provides a static function `device_t<Derived>::create_and_attach_device_object` which does the following:

1. Creates a kernel device object, reserving the storage for a `Derived` class in the device object extension.
2. Attaches it to the device stack.
3. Constructs C++ object `Derived` in the device extension.
4. Calls an optional `Derived::drv_final_construct` method on the created object. If it returns an error value, all steps are reverted and error code is returned to the caller.

Parts of device initialization code that require the C++ object to exist but which can still fail can be placed into the optional `drv_final_construct` class member function:

```cpp
NTSTATUS function_device_t::drv_final_construct() noexcept
{
	PAGED_CODE();

	drv::sys_unicode_string_t link;
	auto status = IoRegisterDeviceInterface(pdo, &function::GUID_DEVINTERFACE_MY_FUNCTION, 
		nullptr, &link); 
	if (nt_success(status))
		devinterface = link;
		
	return status;
}
```

If `drv_final_construct` returns error, `create_and_attach_device_object` reverts all the steps (destroys C++ object, detaches device from device stack and deletes kernel device object) and returns the same error code.

### Destroying Device Objects

The lifetime of a C++ device class is bound to the lifetime of the kernel device object. The actual physical storage of a C++ object is within the device extension of the device object, therefore, we must be very careful when accessing it at the time device object is deleted.

The library provides support for destroying C++ device object with a call to `device_t::delete_device(void *tag)` function. When this function returns, C++ device object cannot be used anymore.

The time this function is called depends on the device object type. The sample filter driver illustrates how you can do it for a filter device object in a completion routine for a PNP request `IRP_MN_REMOVE_DEVICE`:

```cpp
NTSTATUS filter_device_t::on_pnp_completion(PIRP irp) noexcept
{
	if (irp->PendingReturned)
		IoMarkIrpPending(irp);

	switch (IoGetCurrentIrpStackLocation(irp)->MinorFunction)
	{
	case IRP_MN_START_DEVICE:
		on_device_started();
		break;
	case IRP_MN_STOP_DEVICE:
		on_device_stopped();
		break;
	case IRP_MN_REMOVE_DEVICE:
		on_device_stopped();
		delete_device(irp);
		return STATUS_SUCCESS;
	}

	release_remove_lock(irp);
	return STATUS_SUCCESS;
}

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
```

### The `irp_t` Wrapper Class

WDM driver must be very careful with managing the lifetime of IRP requests objects. Correctly handling the lifetime of IRP is hard and error-prone.

The library has a wrapper class `irp_t` (defined in `irp.h` header) that puts a large part of IRP lifetime management to the compiler. When the device object's dispatch routine is invoked, an r-value reference to the `irp_t` object is passed to it. The object must be empty at the time it is destroyed, otherwise, an assertion failure is triggered in a debug build.

Methods that complete the request or pass it down to another driver in a chain require you to pass an r-value reference, explicitly signaling the end of the object visibility and accessibility in the current scope.

```cpp
NTSTATUS filter_device_t::drv_dispatch_device_control(drv::irp_t &&irp) noexcept
{
	...
	// The following will not compile:
	return irp.complete(STATUS_SUCCESS); // irp is not an r-value reference
	// or
	return irp.call_driver(NextDO);	// irp is not an r-value reference

	// The following will compile:
	return std::move(irp).complete(STATUS_SUCCESS);
	// or
	return std::move(irp).call_driver(NextDO);

	// The following will assert in debug build and 
	// trigger an error in any static analysis tool:
	std::move(irp).complete(STATUS_SUCCESS);
	if (nt_success(irp->IoStatus.Status)) { ... } // use of moved-from object
	...
}
```

Note that the wrapper class is not used in a completion routine. IRP is already completed at the time a completion routine is invoked, but is guaranteed to be accessible until it returns. There is no need to manage lifetime of the object during its execution and a completion routine should never attempt to complete it again or pass it to any other drivers.

If the driver wants to postpone the completion of an IRP it received, but cannot call any other driver to do it, it must store it for later processing. Correctly storing IRPs is hard, because they can be cancelled at any time and the driver must be ready to handle those cancellation requests.

Traditionally, the safest way to store an IRP is to use the Cancel-Safe Queue. The library provides a wrapper class `cancel_safe_queue`, defined in `csq.h` header, that simplifies the usage of Cancel-Safe queues. The sample `function` driver illustrates how this class can be used to safely store IRPs.

```cpp
NTSTATUS function_device_t::drv_dispatch_read(drv::irp_t &&irp) noexcept
{
	DISPATCH_PROLOG(irp);
	const auto tag = irp.tag();

	const auto read_data = std::span{ static_cast<std::byte *>(irp->AssociatedIrp.SystemBuffer), 
		irp.current_stack_location()->Parameters.Read.Length };

	NTSTATUS result;
	if (auto l = buffer_lock.acquire(); !buffer.empty())
	{
		// Buffer is not empty, we can complete read request synchronously
		const auto bytes_to_copy = std::min(read_data.size(), buffer.size());
		sr::copy(std::span{ buffer }.subspan(0, bytes_to_copy), read_data.begin());
		buffer.erase(bytes_to_copy);
		// Release spin lock before completing IRP
		l.reset();
		result = std::move(irp).complete(STATUS_SUCCESS, bytes_to_copy);
	}
	else
	{
		// Release spinlock before inserting IRP into CSQ
		l.reset();
		// Mark IRP pending and insert it into CSQ
		irp.mark_pending();
		in_queue.insert(std::move(irp));
		result = STATUS_PENDING;
	}

	process_pending_writes();
	release_remove_lock(tag);
	return result;
}
```

## Coroutines? In a Driver?

After successfully using C++ and a large portion of STL in kernel-mode driver, I was wondering, if it was possible to use coroutines as well?

Personally, I'm a big fan of C++ coroutines and use them extensively in desktop software development. From years of usage, some patterns have became customary and I really missed them in kernel-mode development. 

I was very skeptical when I started experimenting with coroutines in kernel mode, but, to my surprise, their adoption turned to be extremely simple! The only thing required for coroutines is… an allocator, and we already have one!

That's it! You can immediately start creating and returning awaitable objects:

A simple example is a helper function `resume_background` that can be used to resume the execution of a currently running coroutine in the context of a system worker thread at `PASSIVE_LEVEL` IRQL:

```cpp
#include <coroutine>

inline auto resume_background(PDEVICE_OBJECT pdo) noexcept
{
	struct awaitable
	{
		PDEVICE_OBJECT pdo{};

		constexpr bool await_ready() const noexcept
		{
			return false;
		}

		constexpr void await_resume() const noexcept
		{
		}

		void await_suspend(std::coroutine_handle<> handle) const noexcept
		{
			IoQueueWorkItemEx(IoAllocateWorkItem(pdo), []([[maybe_unused]] PVOID IoObject, PVOID Context, PIO_WORKITEM IoWorkItem) noexcept
			{
				std::coroutine_handle<>::from_address(Context)();
				IoFreeWorkItem(IoWorkItem);
			}, DelayedWorkQueue, handle.address());
		}
	};

	return awaitable{ pdo };
}
...
drv::coro::fire_and_forget my_device::foo()
{
	// imaging this coroutine is called at DISPATCH_LEVEL and must complete
	// at PASSIVE_LEVEL

	co_await resume_background(pdo());
	// continue at PASSIVE_LEVEL
	...
}
```

A slightly more complex example: a coroutine that constructs an URB and asynchronously sends it to a target device object:

```cpp
auto send_urb_async(PDEVICE_OBJECT pdo, USBD_HANDLE usbd, PURB urb, kcoro::cancellation_token &token) noexcept
{
	// Define the awaitable type
    struct awaitable
    {
        PDEVICE_OBJECT pdo;
        USBD_HANDLE usbd;
        PURB urb;
        kcoro::cancellation_token &token;

        std::coroutine_handle<> resume;

        IO_STATUS_BLOCK iostatus{};
        kcoro::cancellation_subscription_token subscription_token;
        std::atomic<PIRP> irp{};

        static constexpr bool await_ready() noexcept
        {
            return false;
        }

        bool await_suspend(std::coroutine_handle<> resume_) noexcept
        {
            resume = resume_;

            irp = IoAllocateIrp(pdo->StackSize, false);
            if (!irp)
            {
                iostatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                return false;
            }

            subscription_token = token.subscribe([this](auto unlock) noexcept
            {
                auto i = irp.exchange({}, std::memory_order_relaxed);
                unlock();
                if (i)
                    IoCancelIrp(i);
            });

            auto stack = IoGetNextIrpStackLocation(irp);

            stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
            stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
            USBD_AssignUrbToIoStackLocation(usbd, stack, urb);

            IoSetCompletionRoutine(irp, []([[maybe_unused]] PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) -> NTSTATUS
            {
                auto pc = static_cast<awaitable *>(Context);
                pc->irp.store({}, std::memory_order_relaxed);
                pc->subscription_token.destroy();

                pc->iostatus = Irp->IoStatus;
                IoFreeIrp(Irp);
                pc->resume();
                return STATUS_MORE_PROCESSING_REQUIRED;
            }, this, true, true, true);

            IoCallDriver(pdo, irp);
            return true;
        }

        NTSTATUS await_resume() const noexcept
        {
            return iostatus.Status;
        }
    };

    return awaitable{ pdo, usbd, urb, token };
}
```

What about promise objects? The most simple one is `fire_and_forget`, which can be used whenever the caller is not interested in a result of coroutine operation:

```cpp
struct fire_and_forget
{
	struct promise_type
	{
		constexpr std::suspend_never initial_suspend() const noexcept
		{
			return{};
		}

		constexpr std::suspend_never final_suspend() const noexcept
		{
			return{};
		}

		auto get_return_object() noexcept
		{
			return fire_and_forget{};
		}

		void return_void() noexcept
		{
		}
	};
};
```
