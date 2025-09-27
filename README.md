# Writing Windows Drivers using C++ and STL

## Introduction

Windows drivers have been traditionally developed using C programming language. All usage examples, all existing frameworks and APIs only imply C.

But what would you say, if I tell you that drivers not only can be developed using C++ (including latest language standards, like C++23), but also can use large portion of STL. My recent driver is safe with using the following STL headers:

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

And even `<coroutine>`!

[^variant]: Actually, due to the obligatory support for `valueless_by_exception`, and, therefore requiring exception support, `std::variant` cannot be used without too much effort. We use `boost::variant2` instead, a version of variant with similar API but without support for "valueless by exception".

[^mutex]: We don't use any synchronization primitive classes from `<mutex>` as they require runtime support. However, the header itself is safe to include and any lock classes, such as `std::unique_lock`, `std::scoped_lock` or `std::shared_lock` can safely be used in kernel code with any compatible manually-written synchronization primitive classes.

Additionally, the following libraries have been successfully used from Boost:

* `variant2`
* `intrusive_ptr`

### Background

I've started writing my first Windows driver in 90s and was immediately disappointed with requirement to do it in C. I've started investigating and the result of that work was that my first driver was written in C++. I've even published this driver in 1998 article "A Serial Port Spy for NT" in Dr. Dobbs journal.

Of course that source code predated not only C++11, but even C++98 and more accurately should be called "C with classes". MSVC's C++ Standard Library was completely unprepared to be used from kernel mode at that time and, as the result, the code avoided including any standard header at all.

Nevertheless, using C++ allowed me to define a C++ object for each device object with careful object lifetime management, bound to kernel device object lifetime, implement simple I/O processing dispatching, RAII for using locks and so on.

That driver, and a number of others that followed have served our company for more than 20 years. Of course they received updates and improvements, but the overall design of "C with classes" has not changed for years. Some small classes or functions from C++ standard library it began to use (like `std::move`) have been copied from STL headers, as I still couldn't include any standard header.

### Modern Approach

Recently, we started developing the new product that included two kind of device drivers: one had to be a WDM driver (Windows Driver Model) and another had to be a KMDF driver (Kernel-Mode Driver Framework). I've decided to check what has changed in terms of C++ support in kernel driver development and whether I could extend C++ support to latest language standards as well as, probably, use some STL?

It turns out, that latest releases of MSVC, including STL have been developed with kernel mode support in mind. I'm not sure if this is now in their requirement list or what (I could not find any publicly available information from Microsoft about using C++ in drivers). Another popular Microsoft library for native Windows development, [Windows Implementation Library (wil)](https://github.com/microsoft/wil) now also has special support for kernel-mode driver development, including wrapper classes for a number of kernel-mode synchronization primitives and their corresponding RAII-friendly locks.

With minimal effort, I've been able to introduce full support for C++23 and STL (subset mentioned above) to both WDM and KMDF drivers for the new product. Not only that, my new drivers now use coroutines that greatly simplifies asynchronous code!

### Limitations

The first and most important limitation is the lack of support for exceptions. You cannot (natively) use exceptions in kernel mode and, therefore, driver code is compiled with exceptions disabled. This blocks a large portion of standard library for us, notably, prohibits the use of `std::vector`. You can still write your own version of vector that uses preallocated memory or implements its own grow strategy or use any existing version (for example, from Boost.Containers).

I've seen attempts to manually implement the required exception machinery in kernel mode, but have not experimented with this myself. It looks a bit "hacky", while I strived to keep the implementation as robust as I possible.

Next limitation is again caused by the lack of Runtime library: you cannot have global objects with constructors and destructors. For the same reason, `thread_local` and static objects with constructors are also not supported.

Apart from that, we can use any Standard Library class or function that could be decoupled from runtime support (that is, have header-only implementation, or their runtime implementation may be substituted).

## Implementation

### It all Starts with an Allocator

If we talk about C++, the first thing we need is an allocator. Runtime library provides the default allocator for all C++ programs, however, we cannot use Runtime library at all. We need to declare the following global functions:

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

Unfortunately, I was not successful in using any predefined project templates from Windows DDK integration with Visual Studio. Using them produced a lot of conflicts when I tried to include standard library headers. 

As a result, both WDM and KMDF drivers do not use standard templates. Usually, that is not a big problem. Personally, I find them too obtrusive: they not only configure your compilation environment, but also try to deal with INF file preparation, checking, signing and so on, tasks that I prefer to automate with the help of other tools.

In "manual" mode, prepare to handler header file dependencies, library file dependencies as well as manually providing a list of import libraries for linking, as we would have to turn the "Ignore Default Libraries" linker option on.

### Preprocessor Defines

While a standard library is now more "friendly" to kernel-mode development, we still need to fine-tune it a bit. Unfortunately, this is something that might need to be revisited each time you upgrade to the new compiler version or SDK version, so be prepared. Add the following defines to your central header:

```cpp
// Set the minimum supported OS version
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
```

Add the following overrides to one of the source files. This will allow successful compilation in DEBUG mode:

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
