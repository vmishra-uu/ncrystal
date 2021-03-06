#ifndef NCrystal_Mem_hh
#define NCrystal_Mem_hh

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  This file is part of NCrystal (see https://mctools.github.io/ncrystal/)   //
//                                                                            //
//  Copyright 2015-2020 NCrystal developers                                   //
//                                                                            //
//  Licensed under the Apache License, Version 2.0 (the "License");           //
//  you may not use this file except in compliance with the License.          //
//  You may obtain a copy of the License at                                   //
//                                                                            //
//      http://www.apache.org/licenses/LICENSE-2.0                            //
//                                                                            //
//  Unless required by applicable law or agreed to in writing, software       //
//  distributed under the License is distributed on an "AS IS" BASIS,         //
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  //
//  See the License for the specific language governing permissions and       //
//  limitations under the License.                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "NCrystal/NCException.hh"
#include <memory>
#include <utility>//std::move
#include <cassert>
#include <functional>

namespace NCrystal {

  //Base class for ref-counted objects, destructors of which should be protected
  //rather than public.

  class NCRYSTAL_API RCBase {
  public:
    unsigned refCount() const throw() { return m_refCount; }

    void ref() const throw() { ++m_refCount; }

    void unref() const
    {
      nc_assert(m_refCount>0);
      --m_refCount;
      if (m_refCount==0)
        delete this;
    }

    void unrefNoDelete() const throw()
    {
      assert(m_refCount>0);//not nc_assert, since it can throw.
      --m_refCount;
    }

    //Monitor number of RCBase instances or enable dbg printouts. The lvl
    //parameter should be 0, 1 or 2. Default will be 0, or the value set in the
    //NCRYSTAL_DEBUGMEM env var:
    static long nInstances();
    static void enableMemDbg(int lvl);
  protected:
    RCBase() throw();
    virtual ~RCBase();
  private:
    RCBase( const RCBase & );
    RCBase & operator= ( const RCBase & );
    mutable unsigned m_refCount;
  };

  template< class T >
  struct NCRYSTAL_API RCHolder {
    typedef T element_type;
    RCHolder() = default;
    explicit RCHolder( T* t, bool ref = true ) : m_obj( t ) { if ( m_obj && ref ) m_obj->ref(); }
    ~RCHolder() { if ( m_obj ) m_obj->unref(); }
    RCHolder( const RCHolder & o ) : m_obj(o.m_obj) { if (m_obj) m_obj->ref(); }
    T* obj() { return m_obj; }
    const T* obj() const { return m_obj; }
    void clear() {
      const T* old = m_obj;
      m_obj = 0;//clear first in case unref throws
      if (old)
        old->unref();
    }
    RCHolder & operator= ( const RCHolder & o)
    {
      if (o.m_obj!=m_obj) {
        clear();
        m_obj = o.m_obj;
        if (m_obj)
          m_obj->ref();
      }
      return *this;
    }
    RCHolder & operator= ( T* newobj )
    {
      if (newobj!=m_obj) {
        clear();
        m_obj = newobj;
        if (m_obj)
          m_obj->ref();
      }
      return *this;
    }
    RCHolder( RCHolder&& o ) { std::swap(m_obj,o.m_obj); }
    RCHolder& operator=( RCHolder&& o ) { clear(); std::swap(m_obj,o.m_obj); return *this; }
    bool operator==( decltype(nullptr) ) { return m_obj==nullptr; }
    bool operator!=( decltype(nullptr) ) { return m_obj!=nullptr; }
    bool operator()() const { return m_obj!=0; }
    bool operator!() const { return m_obj==0; }
    //Release obj without triggering deletion:
    T * releaseNoDelete() { T* old = m_obj; if ( m_obj ) m_obj->unrefNoDelete(); m_obj = 0; return old; }
    const T * releaseNoDelete() const { const T* old = m_obj; if ( m_obj ) m_obj->unrefNoDelete(); m_obj = 0; return old; }

    template<class TOther>
    RCHolder<TOther> dyncast() const
    {
      return RCHolder<TOther>(dynamic_cast<TOther*>(m_obj));
    }

    T* operator->() { return m_obj; }
    const T* operator->() const { return m_obj; }
    T& operator*() { nc_assert(m_obj); return *m_obj; }
    const T& operator*() const { nc_assert(m_obj); return *m_obj; }

  private:
    T* m_obj = nullptr;
  };

  typedef RCHolder<const RCBase> RCGuard;

  //get_pointer specialisation for RCHolder allows easy integration with boost:
  template< class T >
  T* get_pointer(const RCHolder<T>& r) { return const_cast<T*>(r.obj()); }

  //Attempt to clear all NCrystal caches (should be safe to call as it will not
  //clear data associated to active object for which client code has ownership):
  NCRYSTAL_API void clearCaches();

  //For internal NCrystal usage, registered functions will be invoked whenever
  //clearCaches() is called:
  NCRYSTAL_API void registerCacheCleanupFunction(std::function<void()>);

}


#if __cplusplus < 201402L
//Make sure we can use std::make_unique from C++14 even in C++11 code
namespace std {
  template<typename T, typename ...Args>
  inline std::unique_ptr<T> make_unique( Args&& ...args )
  {
    return std::unique_ptr<T>( new T( std::forward<Args>(args)... ) );
  }
}
#endif


#endif
