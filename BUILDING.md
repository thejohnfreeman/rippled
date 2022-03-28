# How to add a dependency

If you want to experiment with a new package, here are the steps to get it
working:

1. Search for the package on [Conan Center](https://conan.io/center/).
2. In [`conanfile.py`](./conanfile.py):
  2.a. Add a version of the package to the `requires` property.
  2.b. Change any default options for the package by adding them to the
  `default_options` property (with syntax `'$package:$option': $value`)
3. In [`CMakeLists.txt`](./CMakeLists.txt):
  3.a. Add a call to `find_package($package REQUIRED)`.
  3.b. Link a library from the package to the target `ripple_libs` (search for
  the existing call to `target_link_libraries(ripple_libs INTERFACE ...)`).
4. Start coding! Don't forget to include whatever headers you need from the
   package.
