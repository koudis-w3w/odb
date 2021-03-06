// file      : odb/exception.hxx
// copyright : Copyright (c) 2009-2013 Code Synthesis Tools CC
// license   : GNU GPL v2; see accompanying LICENSE file

#ifndef ODB_EXCEPTION_HXX
#define ODB_EXCEPTION_HXX

#include <odb/pre.hxx>

#include <exception>

#include <odb/forward.hxx>        // odb::core
#include <odb/details/export.hxx>

namespace odb
{
  struct LIBODB_EXPORT exception: std::exception
  {
    virtual const char*
    what () const throw () = 0;
  };

  namespace common
  {
    using odb::exception;
  }
}

#include <odb/post.hxx>

#endif // ODB_EXCEPTION_HXX
