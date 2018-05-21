/*
   Copyright [2018] [IBM Corporation]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef _FABRIC_FID_PTR_H_
#define _FABRIC_FID_PTR_H_

/*
 * Authors:
 */

#include <rdma/fabric.h>

#include <memory> /* shared_ptr */

/**
 * Fabric/RDMA-based network component
 */

template <typename T>
  int fid_close(T *f)
  {
    return fi_close(&f->fid);
  }

/* fid is shared not so much for true sharing as for automatic lifetime control.
 * A unique_ptr might work as well.
 */

template <typename T>
  std::shared_ptr<T> fid_ptr(T *f)
  {
    return std::shared_ptr<T>(f, fid_close<T>);
  }

template <typename T>
  class fid_delete
  {
  public:
    void operator()(T *f) { fid_close(f); }
  };

template <typename T>
  using fid_unique_ptr = std::unique_ptr<T, fid_delete<T>>;

#endif
