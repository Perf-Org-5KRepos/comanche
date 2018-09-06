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

/*
 * Authors:
 *
 */

#include "fabric_cq.h"

#include <api/fabric_itf.h> /* Component::IFabric_op_completer::cb_acceptance */
#include "fabric_check.h" /* CHECK_FI_ERR */
#include "fabric_runtime_error.h"
#include <boost/io/ios_state.hpp>
#include <iostream> /* cerr */
#include <tuple> /* get */
#include <utility> /* move, swap */

Fabric_cq::Fabric_cq(fid_unique_ptr<::fid_cq> &&cq_, const char *type_)
  : _cq(std::move(cq_))
  , _type{type_}
  , _inflight{0U}
  , _completions{}
  , _stats{}
{
}

Fabric_cq::stats::~stats()
{
  if ( std::getenv("FABRIC_STATS") )
  {
    std::cerr << "Fabric_cq(" << this << ") ct " << ct_total << " defer " << defer_total << "\n";
  }
}

::fi_cq_err_entry Fabric_cq::get_cq_comp_err()
{
  ::fi_cq_err_entry err{0,0,0,0,0,0,0,0,0,0,0};
  CHECK_FI_ERR(cq_readerr(&err, 0));

  boost::io::ios_base_all_saver sv(std::cerr);
  std::cerr << __func__ << " : "
                  << " op_context " << err.op_context
                  << std::hex
                  << " flags " << err.flags
                  << std::dec
                  << " len " << err.len
                  << " buf " << err.buf
                  << " data " << err.data
                  << " tag " << err.tag
                  << " olen " << err.olen
                  << " err " << err.err
                  << " (text) " << ::fi_strerror(err.err)
                  << " prov_errno " << err.prov_errno
                  << " err_data " << err.err_data
                  << " err_data_size " << err.err_data_size
                  << " (text) " << ::fi_cq_strerror(&*_cq, err.prov_errno, err.err_data, nullptr, 0U)
        << std::endl;
  return err;
}

std::size_t Fabric_cq::process_or_queue_completion(const Fabric_cq::fi_cq_entry_t &cq_entry_, const Component::IFabric_op_completer::complete_tentative &cb_, ::status_t status_)
{
  std::size_t ct_total = 0U;
  if ( cb_(cq_entry_.op_context, status_, cq_entry_.flags, cq_entry_.len, nullptr) == Component::IFabric_op_completer::cb_acceptance::ACCEPT )
  {
    ++ct_total;
  }
  else
  {
    queue_completion(cq_entry_, status_);
    ++_stats.defer_total;
  }

  return ct_total;
}

std::size_t Fabric_cq::process_or_queue_completion(const Fabric_cq::fi_cq_entry_t &cq_entry_, const Component::IFabric_op_completer::complete_param_tentative &cb_, ::status_t status_, void *cb_param_)
{
  std::size_t ct_total = 0U;
  if ( cb_(cq_entry_.op_context, status_, cq_entry_.flags, cq_entry_.len, nullptr, cb_param_) == Component::IFabric_op_completer::cb_acceptance::ACCEPT )
  {
    ++ct_total;
  }
  else
  {
    queue_completion(cq_entry_, status_);
    ++_stats.defer_total;
  }

  return ct_total;
}

std::size_t Fabric_cq::process_cq_comp_err(const Component::IFabric_op_completer::complete_old &cb_)
{
  const auto cq_entry = get_cq_comp_err();
  cb_(cq_entry.op_context, E_FAIL);
  return 1U;
}

std::size_t Fabric_cq::process_cq_comp_err(const Component::IFabric_op_completer::complete_definite &cb_)
{
  const auto cq_entry = get_cq_comp_err();
  cb_(cq_entry.op_context, E_FAIL, cq_entry.flags, cq_entry.len, nullptr);
  return 1U;
}

std::size_t Fabric_cq::process_or_queue_cq_comp_err(const Component::IFabric_op_completer::complete_tentative &cb_)
{
  const auto e = get_cq_comp_err();
  const Fabric_cq::fi_cq_entry_t err_entry{e.op_context, e.flags, e.len, e.buf, e.data};
  return process_or_queue_completion(err_entry, cb_, E_FAIL);
}

std::size_t Fabric_cq::process_cq_comp_err(const Component::IFabric_op_completer::complete_param_definite &cb_, void *cb_param_)
{
  const auto cq_entry = get_cq_comp_err();
  cb_(cq_entry.op_context, E_FAIL, cq_entry.flags, cq_entry.len, nullptr, cb_param_);
  return 1U;
}

std::size_t Fabric_cq::process_or_queue_cq_comp_err(const Component::IFabric_op_completer::complete_param_tentative &cb_, void *cb_param_)
{
  const auto e = get_cq_comp_err();
  const Fabric_cq::fi_cq_entry_t err_entry{e.op_context, e.flags, e.len, e.buf, e.data};
  return process_or_queue_completion(err_entry, cb_, E_FAIL, cb_param_);
}

namespace
{
  std::size_t constexpr ct_max = 16;
}
/**
 * Poll completions (e.g., completions)
 *
 * @param completion_callback (context_t, ::status_t status, void* error_data)
 *
 * @return Number of completions processed
 */

std::size_t Fabric_cq::poll_completions(const Component::IFabric_op_completer::complete_old &cb_)
{
  std::size_t ct_total = 0;
  std::array<Fabric_cq::fi_cq_entry_t, ct_max> cq_entry;
  bool drained = false;
  while ( ! drained )
  {
    const auto ct = cq_read(&cq_entry[0], ct_max);
    if ( ct < 0 )
    {
      switch ( const auto e = unsigned(-ct) )
      {
      case FI_EAVAIL:
        ct_total += process_cq_comp_err(cb_);
        break;
      case FI_EAGAIN:
        drained = true;
        break;
      case FI_EINTR:
        /* seen when profiling with gperftools */
        break;
      default:
        throw fabric_runtime_error(e, __FILE__, __LINE__);
      }
    }
    else
    {
      ct_total += ct;
      for ( unsigned ix = 0U; ix != ct; ++ix )
      {
        cb_(cq_entry[ix].op_context, S_OK);
      }
    }
  }

  ct_total += drain_old_completions(cb_);

  _stats.ct_total += ct_total;

  return ct_total;
}

std::size_t Fabric_cq::poll_completions(const Component::IFabric_op_completer::complete_definite &cb_)
{
  std::size_t ct_total = 0;
  std::array<Fabric_cq::fi_cq_entry_t, ct_max> cq_entry;
  bool drained = false;
  while ( ! drained )
  {
    const auto ct = cq_read(&cq_entry[0], ct_max);
    if ( ct < 0 )
    {
      switch ( const auto e = unsigned(-ct) )
      {
      case FI_EAVAIL:
        ct_total += process_cq_comp_err(cb_);
        break;
      case FI_EAGAIN:
        drained = true;
        break;
      case FI_EINTR:
        /* seen when profiling with gperftools */
        break;
      default:
        throw fabric_runtime_error(e, __FILE__, __LINE__);
      }
    }
    else
    {
      ct_total += ct;
      for ( unsigned ix = 0U; ix != ct; ++ix )
      {
        cb_(cq_entry[ix].op_context, S_OK, cq_entry[ix].flags, cq_entry[ix].len, nullptr);
      }
    }
  }

  ct_total += drain_old_completions(cb_);

  _stats.ct_total += ct_total;
  return ct_total;
}

std::size_t Fabric_cq::poll_completions_tentative(const Component::IFabric_op_completer::complete_tentative &cb_)
{
  std::size_t ct_total = 0;
  std::array<Fabric_cq::fi_cq_entry_t, ct_max> cq_entry;
  bool drained = false;
  while ( ! drained )
  {
    const auto ct = cq_read(&cq_entry[0], ct_max);
    if ( ct < 0 )
    {
      switch ( const auto e = unsigned(-ct) )
      {
      case FI_EAVAIL:
        ct_total += process_or_queue_cq_comp_err(cb_);
        break;
      case FI_EAGAIN:
        drained = true;
        break;
      case FI_EINTR:
        /* seen when profiling with gperftools */
        break;
      default:
        throw fabric_runtime_error(e, __FILE__, __LINE__);
      }
    }
    else
    {
      for ( unsigned ix = 0U; ix != ct; ++ix )
      {
        ct_total += process_or_queue_completion(cq_entry[ix], cb_, S_OK);
      }
    }
  }

  ct_total += drain_old_completions(cb_);

  _stats.ct_total += ct_total;
  return ct_total;
}

std::size_t Fabric_cq::poll_completions(const Component::IFabric_op_completer::complete_param_definite &cb_, void *cb_param_)
{
  std::size_t ct_total = 0;
  std::array<Fabric_cq::fi_cq_entry_t, ct_max> cq_entry;
  bool drained = false;
  while ( ! drained )
  {
    const auto ct = cq_read(&cq_entry[0], ct_max);
    if ( ct < 0 )
    {
      switch ( const auto e = unsigned(-ct) )
      {
      case FI_EAVAIL:
        ct_total += process_cq_comp_err(cb_, cb_param_);
        break;
      case FI_EAGAIN:
        drained = true;
        break;
      case FI_EINTR:
        /* seen when profiling with gperftools */
        break;
      default:
        throw fabric_runtime_error(e, __FILE__, __LINE__);
      }
    }
    else
    {
      ct_total += ct;
      for ( unsigned ix = 0U; ix != ct; ++ix )
      {
        cb_(cq_entry[ix].op_context, S_OK, cq_entry[ix].flags, cq_entry[ix].len, nullptr, cb_param_);
      }
    }
  }

  ct_total += drain_old_completions(cb_, cb_param_);

  _stats.ct_total += ct_total;
  return ct_total;
}

std::size_t Fabric_cq::poll_completions_tentative(const Component::IFabric_op_completer::complete_param_tentative &cb_, void *cb_param_)
{
  std::size_t ct_total = 0;
  std::array<Fabric_cq::fi_cq_entry_t, ct_max> cq_entry;
  bool drained = false;
  while ( ! drained )
  {
    const auto ct = cq_read(&cq_entry[0], ct_max);
    if ( ct < 0 )
    {
      switch ( const auto e = unsigned(-ct) )
      {
      case FI_EAVAIL:
        ct_total += process_or_queue_cq_comp_err(cb_, cb_param_);
        break;
      case FI_EAGAIN:
        drained = true;
        break;
      case FI_EINTR:
        /* seen when profiling with gperftools */
        break;
      default:
        throw fabric_runtime_error(e, __FILE__, __LINE__);
      }
    }
    else
    {
      for ( unsigned ix = 0U; ix != ct; ++ix )
      {
        ct_total += process_or_queue_completion(cq_entry[ix], cb_, S_OK, cb_param_);
      }
    }
  }

  ct_total += drain_old_completions(cb_, cb_param_);

  _stats.ct_total += ct_total;
  return ct_total;
}

ssize_t Fabric_cq::cq_read(void *buf, size_t count) noexcept
{
  auto r =
    0U == _inflight
    ? -FI_EAGAIN
    : ::fi_cq_read(&*_cq, buf, count)
    ;

  if ( 0 < r )
  {
    _inflight -= r;
  }

  return r;
}

ssize_t Fabric_cq::cq_readerr(::fi_cq_err_entry *buf, uint64_t flags) noexcept
{
  auto r = ::fi_cq_readerr(&*_cq, buf, flags);

  if ( 0 < r )
  {
    _inflight -= r;
  }

  return r;
}

void Fabric_cq::queue_completion(const Fabric_cq::fi_cq_entry_t &cq_entry_, ::status_t status_)
{
  _completions.push(completion_t(status_, cq_entry_));
}

std::size_t Fabric_cq::drain_old_completions(const Component::IFabric_op_completer::complete_old &cb_)
{
  std::size_t ct_total = 0U;
  while ( ! _completions.empty() )
  {
    auto c = _completions.front();
    _completions.pop();
    const auto &cq_entry = std::get<1>(c);
    cb_(cq_entry.op_context, std::get<0>(c));
    ++ct_total;
  }
  return ct_total;
}

std::size_t Fabric_cq::drain_old_completions(const Component::IFabric_op_completer::complete_definite &cb_)
{
  std::size_t ct_total = 0U;
  while ( ! _completions.empty() )
  {
    auto c = _completions.front();
    _completions.pop();
    const auto &cq_entry = std::get<1>(c);
    cb_(cq_entry.op_context, std::get<0>(c), cq_entry.flags, cq_entry.len, nullptr);
    ++ct_total;
  }
  return ct_total;
}

std::size_t Fabric_cq::drain_old_completions(const Component::IFabric_op_completer::complete_tentative &cb_)
{
  std::size_t ct_total = 0U;
  if ( ! _completions.empty() )
  {
    std::size_t defer_total = 0U;
    std::queue<completion_t> deferred_completions;
    while ( ! _completions.empty() )
    {
      auto c = _completions.front();
      _completions.pop();
      const auto &cq_entry = std::get<1>(c);
      if ( cb_(cq_entry.op_context, std::get<0>(c), cq_entry.flags, cq_entry.len, nullptr) == Component::IFabric_op_completer::cb_acceptance::ACCEPT )
      {
        ++ct_total;
      }
      else
      {
        deferred_completions.push(c);
        ++defer_total;
      }
    }
    std::swap(deferred_completions, _completions);
    _stats.defer_total += defer_total;
  }
  return ct_total;
}

std::size_t Fabric_cq::drain_old_completions(const Component::IFabric_op_completer::complete_param_definite &cb_, void *cb_param_)
{
  std::size_t ct_total = 0U;
  while ( ! _completions.empty() )
  {
    auto c = _completions.front();
    _completions.pop();
    const auto &cq_entry = std::get<1>(c);
    cb_(cq_entry.op_context, std::get<0>(c), cq_entry.flags, cq_entry.len, nullptr, cb_param_);
    ++ct_total;
  }
  return ct_total;
}

std::size_t Fabric_cq::drain_old_completions(const Component::IFabric_op_completer::complete_param_tentative &cb_, void *cb_param_)
{
  std::size_t ct_total = 0U;
  if ( ! _completions.empty() )
  {
    std::size_t defer_total = 0U;
    std::queue<completion_t> deferred_completions;
    while ( ! _completions.empty() )
    {
      auto c = _completions.front();
      _completions.pop();
      const auto &cq_entry = std::get<1>(c);
      if ( cb_(cq_entry.op_context, std::get<0>(c), cq_entry.flags, cq_entry.len, nullptr, cb_param_) == Component::IFabric_op_completer::cb_acceptance::ACCEPT )
      {
        ++ct_total;
      }
      else
      {
        deferred_completions.push(c);
        ++defer_total;
      }
    }
    std::swap(deferred_completions, _completions);
    _stats.defer_total += defer_total;
  }
  return ct_total;
}