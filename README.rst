CMake Python frontend proof of concept
**************************************

CMake with an embedded Python interpreter that allows optionally replacing parts (or the whole) of the configure process with Python. It's working code you can try out right now.

.. code-block:: python

  #!cmakepy
  import cmake
  
  cmake.fn.message("STATUS", "Hello from CMake python")
  
  cmake.cmake_minimum_required("3.25")
  cmake.project("pytest", version="0.1", description="Hello from CMake python", languages=["C","CXX"])
  cmake.add_executable("pytest", ["main.cpp"])

The full CMake command interface is available, so any CMake function can be called from python.

Works on any file normally executed by CMake
============================================

Just put the text ``#!cmakepy`` as the first line in any script and it'll be treated as Python.

This includes any file included by CMake ``include()`` or ``add_subdirectory()``.

This also means that to execute subdir scripts (added by add_subdirectory()) CMakeLists.txt is still used. Use ``#!cmakepy`` and it'll be executed as Python.

So no CMakeLists.py for now. (Why? Because it was easier to do it this way at the moment...)

From CMake to Python and back
=============================

Two-way interop between CMake script code and Python code also possible, meaning CMake code can call Python functions and vice-versa.

``test.cmake``: Run with: ``cmake -P test.cmake``

.. code-block:: cmake

  function(cmfunc)
      message("Back to CMake: ${ARGN}")
  endfunction()
  
  # let's call some python
  include(${CMAKE_CURRENT_LIST_DIR}/pyfunc.cmake.py)
  pyfunc(first "second;2" third and the;rest "of;the" args)

`pyfunc.cmake.py`:

.. code-block:: python

  #!cmakepy
  import cmake
  
  def pyfunc(arg1, arg2, arg3, *args):
      print("We're in python now!")
      print(f"    arg1 : {arg1}")
      print(f"    arg2 : {arg2}")
      print(f"    arg3 : {arg3}")
      print(f"    *args: {args}")
      # let's call back some CMake
      cmake.fn.cmfunc("Hi", "from", "python")
  
  cmake.export_function(pyfunc)

Where's more documentation?
===========================

That's all for now, the above should get anyone started.

The first example above uses hand-written python wrappers (except for message), but those are just a few dummies for now.

The raw CMake API and any user-defined CMake functions are available through cmake.fn.XYZ() (note the ".fn." !).

Other than that, take a look at '`<installdir>/share/cmake-3.27/Modules/CMakePy/cmake.py'` for what's available.

Additional features include:

* CMake variable access (get/set)
* scoping between cmake/python function calls
* exceptions (propagating through cmake/python boundaries)
* include() python scripts from CMake or CMake scripts from python
* ...

Why do all this?
================

To experiment. To be able to better answer "what if?" questions like "What if CMake had another frontend language?". Now it does - sort of.

The implementation takes a minimalist approach: achieve the integration with as little code as possible and as little changes to existing CMake C++ code as possible.

The same goes for how the python script is executed: just take the file and run it, no special entry points or functions to implement, just write code as if you're writing CMake code, except in Python.

Because it is optional and non-intrusive, it's possible to augment any existing CMake project of any size with Python snippets to experiment with all sorts of scenarios. That's the primary intent of this experiment.

Have fun!
