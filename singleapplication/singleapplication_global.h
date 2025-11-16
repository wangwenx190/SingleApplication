#pragma once

#ifndef SINGLEAPPLICATION_API
#  ifdef SINGLEAPPLICATION_BUILD_STATIC
#    define SINGLEAPPLICATION_API
#  else // !SINGLEAPPLICATION_BUILD_STATIC
#    ifdef _WIN32
#      ifdef SINGLEAPPLICATION_BUILD_LIBRARY
#        define SINGLEAPPLICATION_API __declspec(dllexport)
#      else // !SINGLEAPPLICATION_BUILD_LIBRARY
#        define SINGLEAPPLICATION_API __declspec(dllimport)
#      endif // SINGLEAPPLICATION_BUILD_LIBRARY
#    else // !_WIN32
#      define SINGLEAPPLICATION_API __attribute__((visibility("default")))
#    endif // _WIN32
#  endif // SINGLEAPPLICATION_BUILD_STATIC
#endif // SINGLEAPPLICATION_API
