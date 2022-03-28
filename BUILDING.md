## Prerequisites

To build this package, you will need to install [Conan][] and [CMake][] (>=
3.16).

[Conan]: https://conan.io/downloads.html
[CMake]: https://cmake.org/download/

Linux developers will commonly have a default [Conan profile][1] that compiles
with GCC and links with libstdc++.
You can check your default profile with `conan profile show default`.
(If you have not yet generated a default profile, then you can with `conan
profile new default --detect`.)

If you are linking with libstdc++ (see profile setting `compiler.libcxx`),
then you will need to choose a Conan profile with the `libstdc++11` ABI.
The easiest way is to change your default profile to use it like this:

```
conan profile update settings.compiler.libcxx=libstdc++11 default
```

Windows developers will commonly have a default Conan profile that is missing
a selection for the [runtime library][2].
We recommend that you make that selection in your Conan profile to ensure
a consistent choice among all packages.

```
conan profile update settings.compiler.runtime=MT default
```


## How to build

Here is an example building the package in a release configuration.
These commands should work from just about any shell,
including Bash and PowerShell.

```
conan export external/rocksdb
mkdir .build
cd .build
conan install .. --build missing
cmake -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
./rippled --help
```

In addition to choosing a different configuration, there are a few options you
can pass to the CMake configure command:

- `unity:BOOL`: Perform a "unity" build. Source files will be concatenated
    into fewer, but much larger, translation units. This type of build may
    be faster, at the expense of requiring much more memory.


## How to add a dependency

If you want to experiment with a new package, here are the steps to get it
working:

1. Search for the package on [Conan Center](https://conan.io/center/).
1. In [`conanfile.py`](./conanfile.py):
  1. Add a version of the package to the `requires` property.
  1. Change any default options for the package by adding them to the
  `default_options` property (with syntax `'$package:$option': $value`)
1. In [`CMakeLists.txt`](./CMakeLists.txt):
  1. Add a call to `find_package($package REQUIRED)`.
  1. Link a library from the package to the target `ripple_libs` (search for
  the existing call to `target_link_libraries(ripple_libs INTERFACE ...)`).
1. Start coding! Don't forget to include whatever headers you need from the
   package.


[1]: https://docs.conan.io/en/latest/reference/profiles.html
[2]: https://docs.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library
[3]: https://cmake.org/cmake/help/git-stage/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html
