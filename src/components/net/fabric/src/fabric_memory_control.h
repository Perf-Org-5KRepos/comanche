/*
   Copyright [2017-2019] [IBM Corporation]
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


#ifndef _FABRIC_MEMORY_CONTROL_H_
#define _FABRIC_MEMORY_CONTROL_H_

#include <api/fabric_itf.h> /* Component::IFabric_connection */

#include <cstdint> /* uint64_t */
#include <map>
#include <memory> /* shared_ptr */
#include <mutex>
#include <vector>

struct fi_info;
struct fid_domain;
struct fid_mr;
class Fabric;

class Fabric_memory_control
  : public Component::IFabric_connection
{
  Fabric &_fabric;
  std::shared_ptr<::fi_info> _domain_info;
  std::shared_ptr<::fid_domain> _domain;
  std::mutex _m; /* protects _mr_addr_to_desc, _mr_desc_to_addr */
  /*
   * Map of [starts of] registered memory regions to fi_mr objects.
   * The map is maintained because no other layer provides fi_mr values for
   * the addresses in an iovec.
   * descriptors.
   */
  std::map<const void *, ::fid_mr * const> _mr_addr_to_fimr;
  /* since fi_mr_attr_raw may not be implemented, add map from fi_mr * to address.
   *
   * Note: "man fi_mr" says "it is safe to use this call with any memory region",
   * so no longer sure from whence the "may not be implemented" concern came.
   */
  std::map<const ::fid_mr *, const void * const> _mr_fimr_to_addr;

  /*
   * @throw fabric_runtime_error : std::runtime_error : ::fi_mr_reg fail
   */
  ::fid_mr *make_fid_mr_reg_ptr(
    const void *buf
    , std::size_t len
    , std::uint64_t access
    , std::uint64_t key
    , std::uint64_t flags
  ) const;

public:
  /*
   * @throw fabric_bad_alloc : std::bad_alloc - out of memory
   * @throw fabric_runtime_error : std::runtime_error : ::fi_domain fail
   */
  explicit Fabric_memory_control(
    Fabric &fabric
    , ::fi_info &info
  );

  ~Fabric_memory_control();

  Fabric &fabric() const { return _fabric; }
  ::fi_info &domain_info() const { return *_domain_info; }
  ::fid_domain &domain() const { return *_domain; }

  /* BEGIN Component::IFabric_connection */
  /**
   * @contig_addr - the address of memory to be registered for RDMA. Restrictions
   * in "man fi_verbs" apply: the memory must be page aligned. The ibverbs layer
   * will execute an madvise(MADV_DONTFORK) syscall against the region. Any error
   * returned from that syscal will cause the register_memory function to fail.
   *
   * @throw std::range_error - address already registered
   * @throw std::logic_error - inconsistent memory address tables
   * @throw fabric_runtime_error : std::runtime_error : ::fi_mr_reg fail
   */
  memory_region_t register_memory(const void * contig_addr, std::size_t size, std::uint64_t key, std::uint64_t flags) override;
  /**
   * @throw std::range_error - address not registered
   * @throw std::logic_error - inconsistent memory address tables
   */
  void deregister_memory(const memory_region_t memory_region) override;
  std::uint64_t get_memory_remote_key(const memory_region_t memory_region) const noexcept override;
  void *get_memory_descriptor(const memory_region_t memory_region) const noexcept override;
  /* END Component::IFabric_connection */

  std::vector<void *> populated_desc(const std::vector<::iovec> & buffers);
  std::vector<void *> populated_desc(const ::iovec *first, const ::iovec *last);
};

#endif
