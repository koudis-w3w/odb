// file      : odb/sqlite/simple-object-statements.hxx
// copyright : Copyright (c) 2005-2013 Code Synthesis Tools CC
// license   : GNU GPL v2; see accompanying LICENSE file

#ifndef ODB_SQLITE_SIMPLE_OBJECT_STATEMENTS_HXX
#define ODB_SQLITE_SIMPLE_OBJECT_STATEMENTS_HXX

#include <odb/pre.hxx>

#include <vector>
#include <cassert>
#include <cstddef> // std::size_t

#include <odb/forward.hxx>
#include <odb/traits.hxx>

#include <odb/details/shared-ptr.hxx>

#include <odb/sqlite/version.hxx>
#include <odb/sqlite/forward.hxx>
#include <odb/sqlite/sqlite-types.hxx>
#include <odb/sqlite/binding.hxx>
#include <odb/sqlite/statement.hxx>
#include <odb/sqlite/statements-base.hxx>

#include <odb/sqlite/details/export.hxx>

namespace odb
{
  namespace sqlite
  {
    // The container_statement_cache class is only defined (and used) in
    // the generated source file. However, object_statements may be
    // referenced from another source file in the case of a polymorphic
    // hierarchy (though in this case the container statement cache is
    // not used). As a result, we cannot have a by-value member and
    // instead will store a pointer and lazily allocate the cache if
    // and when needed. We will also need to store a pointer to the
    // deleter function which will be initialized during allocation
    // (at that point we know that the cache class is defined).
    //
    template <typename T>
    struct container_statement_cache_ptr
    {
      typedef sqlite::connection connection_type;

      container_statement_cache_ptr (): p_ (0) {}
      ~container_statement_cache_ptr ()
      {
        if (p_ != 0)
          (this->*deleter_) (0, 0);
      }

      T&
      get (connection_type& c, binding& id)
      {
        if (p_ == 0)
          allocate (&c, &id);

        return *p_;
      }

    private:
      void
      allocate (connection_type*, binding*);

    private:
      T* p_;
      void (container_statement_cache_ptr::*deleter_) (
        connection_type*, binding*);
    };

    template <typename T>
    void container_statement_cache_ptr<T>::
    allocate (connection_type* c, binding* id)
    {
      // To reduce object code size, this function acts as both allocator
      // and deleter.
      //
      if (p_ == 0)
      {
        p_ = new T (*c, *id);
        deleter_ = &container_statement_cache_ptr<T>::allocate;
      }
      else
        delete p_;
    }

    //
    // Implementation for objects with object id.
    //

    class LIBODB_SQLITE_EXPORT object_statements_base: public statements_base
    {
    public:
      // Locking.
      //
      void
      lock ()
      {
        assert (!locked_);
        locked_ = true;
      }

      void
      unlock ()
      {
        assert (locked_);
        locked_ = false;
      }

      bool
      locked () const
      {
        return locked_;
      }

      struct auto_unlock
      {
        // Unlocks the statement on construction and re-locks it on
        // destruction.
        //
        auto_unlock (object_statements_base&);
        ~auto_unlock ();

      private:
        auto_unlock (const auto_unlock&);
        auto_unlock& operator= (const auto_unlock&);

      private:
        object_statements_base& s_;
      };

    public:
      virtual
      ~object_statements_base ();

    protected:
      object_statements_base (connection_type& conn)
        : statements_base (conn), locked_ (false)
      {
      }

    protected:
      bool locked_;
    };

    template <typename T, bool optimistic>
    struct optimistic_data;

    template <typename T>
    struct optimistic_data<T, true>
    {
      typedef T object_type;
      typedef object_traits_impl<object_type, id_sqlite> object_traits;

      optimistic_data (bind*);

      // The id + optimistic column binding.
      //
      std::size_t id_image_version_;
      binding id_image_binding_;

      details::shared_ptr<delete_statement> erase_;
    };

    template <typename T>
    struct optimistic_data<T, false>
    {
      optimistic_data (bind*) {}
    };

    template <typename T>
    class object_statements: public object_statements_base
    {
    public:
      typedef T object_type;
      typedef object_traits_impl<object_type, id_sqlite> object_traits;
      typedef typename object_traits::id_type id_type;
      typedef typename object_traits::pointer_type pointer_type;
      typedef typename object_traits::image_type image_type;
      typedef typename object_traits::id_image_type id_image_type;

      typedef
      typename object_traits::pointer_cache_traits
      pointer_cache_traits;

      typedef
      typename object_traits::container_statement_cache_type
      container_statement_cache_type;

      typedef sqlite::insert_statement insert_statement_type;
      typedef sqlite::select_statement select_statement_type;
      typedef sqlite::update_statement update_statement_type;
      typedef sqlite::delete_statement delete_statement_type;

      // Automatic lock.
      //
      struct auto_lock
      {
        // Lock the statements unless they are already locked in which
        // case subsequent calls to locked() will return false.
        //
        auto_lock (object_statements&);

        // Unlock the statemens if we are holding the lock and clear
        // the delayed loads. This should only happen in case an
        // exception is thrown. In normal circumstances, the user
        // should call unlock() explicitly.
        //
        ~auto_lock ();

        // Return true if this auto_lock instance holds the lock.
        //
        bool
        locked () const;

        // Unlock the statemens.
        //
        void
        unlock ();

      private:
        auto_lock (const auto_lock&);
        auto_lock& operator= (const auto_lock&);

      private:
        object_statements& s_;
        bool locked_;
      };


    public:
      object_statements (connection_type&);

      virtual
      ~object_statements ();

      // Delayed loading.
      //
      typedef void (*loader_function) (
        odb::database&, const id_type&, object_type&);

      void
      delay_load (const id_type& id,
                  object_type& obj,
                  const typename pointer_cache_traits::position_type& p,
                  loader_function l = 0)
      {
        delayed_.push_back (delayed_load (id, obj, p, l));
      }

      void
      load_delayed ()
      {
        assert (locked ());

        if (!delayed_.empty ())
          load_delayed_ ();
      }

      void
      clear_delayed ()
      {
        if (!delayed_.empty ())
          clear_delayed_ ();
      }

      // Object image.
      //
      image_type&
      image () {return image_;}

      // Insert binding.
      //
      std::size_t
      insert_image_version () const { return insert_image_version_;}

      void
      insert_image_version (std::size_t v) {insert_image_version_ = v;}

      binding&
      insert_image_binding () {return insert_image_binding_;}

      // Update binding.
      //
      std::size_t
      update_image_version () const { return update_image_version_;}

      void
      update_image_version (std::size_t v) {update_image_version_ = v;}

      std::size_t
      update_id_image_version () const { return update_id_image_version_;}

      void
      update_id_image_version (std::size_t v) {update_id_image_version_ = v;}

      binding&
      update_image_binding () {return update_image_binding_;}

      // Select binding.
      //
      std::size_t
      select_image_version () const { return select_image_version_;}

      void
      select_image_version (std::size_t v) {select_image_version_ = v;}

      binding&
      select_image_binding () {return select_image_binding_;}

      bool*
      select_image_truncated () {return select_image_truncated_;}

      // Object id image and binding.
      //
      id_image_type&
      id_image () {return id_image_;}

      std::size_t
      id_image_version () const {return id_image_version_;}

      void
      id_image_version (std::size_t v) {id_image_version_ = v;}

      binding&
      id_image_binding () {return id_image_binding_;}

      // Optimistic id + managed column image binding.
      //
      std::size_t
      optimistic_id_image_version () const {return od_.id_image_version_;}

      void
      optimistic_id_image_version (std::size_t v) {od_.id_image_version_ = v;}

      binding&
      optimistic_id_image_binding () {return od_.id_image_binding_;}

      // Statements.
      //
      insert_statement_type&
      persist_statement ()
      {
        if (persist_ == 0)
        {
          persist_.reset (
            new (details::shared) insert_statement_type (
              conn_,
              object_traits::persist_statement,
              insert_image_binding_));
        }

        return *persist_;
      }

      select_statement_type&
      find_statement ()
      {
        if (find_ == 0)
        {
          find_.reset (
            new (details::shared) select_statement_type (
              conn_,
              object_traits::find_statement,
              id_image_binding_,
              select_image_binding_));
        }

        return *find_;
      }

      update_statement_type&
      update_statement ()
      {
        if (update_ == 0)
        {
          update_.reset (
            new (details::shared) update_statement_type (
              conn_,
              object_traits::update_statement,
              update_image_binding_));
        }

        return *update_;
      }

      delete_statement_type&
      erase_statement ()
      {
        if (erase_ == 0)
        {
          erase_.reset (
            new (details::shared) delete_statement_type (
              conn_,
              object_traits::erase_statement,
              id_image_binding_));
        }

        return *erase_;
      }

      delete_statement_type&
      optimistic_erase_statement ()
      {
        if (od_.erase_ == 0)
        {
          od_.erase_.reset (
            new (details::shared) delete_statement_type (
              conn_,
              object_traits::optimistic_erase_statement,
              od_.id_image_binding_));
        }

        return *od_.erase_;
      }

      // Container statement cache.
      //
      container_statement_cache_type&
      container_statment_cache ()
      {
        return container_statement_cache_.get (conn_, id_image_binding_);
      }

    public:
      // select = total
      // insert = total - inverse - managed_optimistic
      // update = total - inverse - managed_optimistic - id - readonly
      //
      static const std::size_t select_column_count =
        object_traits::column_count;

      static const std::size_t insert_column_count =
        object_traits::column_count - object_traits::inverse_column_count -
        object_traits::managed_optimistic_column_count;

      static const std::size_t update_column_count = insert_column_count -
        object_traits::id_column_count - object_traits::readonly_column_count;

      static const std::size_t id_column_count =
        object_traits::id_column_count;

      static const std::size_t managed_optimistic_column_count =
        object_traits::managed_optimistic_column_count;

    private:
      object_statements (const object_statements&);
      object_statements& operator= (const object_statements&);

    private:
      void
      load_delayed_ ();

      void
      clear_delayed_ ();

    private:
      container_statement_cache_ptr<container_statement_cache_type>
      container_statement_cache_;

      image_type image_;

      // Select binding.
      //
      std::size_t select_image_version_;
      binding select_image_binding_;
      bind select_image_bind_[select_column_count];
      bool select_image_truncated_[select_column_count];

      // Insert binding.
      //
      std::size_t insert_image_version_;
      binding insert_image_binding_;
      bind insert_image_bind_[insert_column_count];

      // Update binding. Note that the id suffix is bound to id_image_
      // below instead of image_ which makes this binding effectively
      // bound to two images. As a result, we have to track versions
      // for both of them. If this object uses optimistic concurrency,
      // then the binding for the managed column (version, timestamp,
      // etc) comes after the id and the image for such a column is
      // stored as part of the id image.
      //
      std::size_t update_image_version_;
      std::size_t update_id_image_version_;
      binding update_image_binding_;
      bind update_image_bind_[update_column_count + id_column_count +
                              managed_optimistic_column_count];

      // Id image binding (only used as a parameter). Uses the suffix in
      // the update bind.
      //
      id_image_type id_image_;
      std::size_t id_image_version_;
      binding id_image_binding_;

      // Extra data for objects with optimistic concurrency support.
      //
      optimistic_data<T, managed_optimistic_column_count != 0> od_;

      details::shared_ptr<insert_statement_type> persist_;
      details::shared_ptr<select_statement_type> find_;
      details::shared_ptr<update_statement_type> update_;
      details::shared_ptr<delete_statement_type> erase_;

      // Delayed loading.
      //
      struct delayed_load
      {
        typedef typename pointer_cache_traits::position_type position_type;

        delayed_load () {}
        delayed_load (const id_type& i,
                      object_type& o,
                      const position_type& p,
                      loader_function l)
            : id (i), obj (&o), pos (p), loader (l)
        {
        }

        id_type id;
        object_type* obj;
        position_type pos;
        loader_function loader;
      };

      typedef std::vector<delayed_load> delayed_loads;
      delayed_loads delayed_;

      // Delayed vectors swap guard. See the load_delayed_() function for
      // details.
      //
      struct swap_guard
      {
        swap_guard (object_statements& os, delayed_loads& dl)
            : os_ (os), dl_ (dl)
        {
          dl_.swap (os_.delayed_);
        }

        ~swap_guard ()
        {
          os_.clear_delayed ();
          dl_.swap (os_.delayed_);
        }

      private:
        object_statements& os_;
        delayed_loads& dl_;
      };
    };
  }
}

#include <odb/sqlite/simple-object-statements.ixx>
#include <odb/sqlite/simple-object-statements.txx>

#include <odb/post.hxx>

#endif // ODB_SQLITE_SIMPLE_OBJECT_STATEMENTS_HXX
