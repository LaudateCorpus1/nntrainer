// SPDX-License-Identifier: Apache-2.0
/**
 * Copyright (C) 2021 Parichay Kapoor <pk.kapoor@samsung.com>
 *
 * @file   tensor_pool.cpp
 * @date   19 Aug 2021
 * @brief  This is TensorPool for all requested tensors
 * @see    https://github.com/nnstreamer/nntrainer
 * @author Parichay Kapoor <pk.kapoor@samsung.com>
 * @author Jihoon Lee <jhoon.it.lee@samsung.com>
 * @bug	   No known bugs except for NYI items
 *
 * @todo   add checks for request/updates that finalize is not done
 * @todo   check before allocate that finalize is done
 */

#include <memory_pool.h>
#include <nntrainer_log.h>
#include <tensor.h>
#include <tensor_pool.h>
#include <tensor_wrap_specs.h>
#include <util_func.h>

namespace nntrainer {

/// lambda overload helper
template <class... Ts> struct overloaded_ : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded_(Ts...)->overloaded_<Ts...>;

/**
 * @brief     Request tensor with the given spec
 *
 * @note returns empty tensor which will be filled when allocate is called.
 * @note we assume that the caller checks if the exec_order and lifespan are
 * compatible.
 */
Tensor *TensorPool::requestTensor(const TensorDim &dim,
                                  const std::vector<unsigned int> &exec_order,
                                  TensorLifespan lifespan,
                                  const std::string &name,
                                  const Tensor::Initializer &init) {
  return registerRequestSpec(
    {std::make_unique<Tensor>(dim, false, init, name),
     TensorPool::SourceDetails{0, lifespan, exec_order, {}}});
}

/**
 * @brief     Request tensor with the given spec
 *
 * @note returns empty tensor which will be filled when allocate is called.
 * @note we assume that the caller checks if the exec_order and lifespan are
 * compatible.
 */
Tensor *
TensorPool::requestExternallyAllocateTensor(const TensorDim &dim,
                                            const std::string &name,
                                            const Tensor::Initializer &init) {
  return requestTensor(dim, {}, TensorLifespan::UNMANAGED, name, init);
}

/**
 * @brief     Request tensor which has been already requested with the given
 * spec
 *
 * @note returns empty tensor which will be filled when allocate is called.
 * @note we assume that the caller checks if the exec_order and lifespan are
 * compatible.
 */
Tensor *TensorPool::requestPrerequestedTensor(
  const TensorDim &dim, const std::vector<unsigned int> &exec_order,
  TensorLifespan lifespan, const std::string &name,
  const std::string &shared_name, const Tensor::Initializer &init,
  const unsigned int offset) {
  auto &spec = getSourceSpec(shared_name);
  unsigned adjusted_offset =
    std::visit(overloaded_{[](const SourceDetails &s) { return 0u; },
                           [](const DependentDetails &s) { return s.offset; }},
               pool[name_map.at(shared_name)].details);
  adjusted_offset += offset;

  NNTR_THROW_IF(spec.tensor->getDim().getDataLen() <
                  adjusted_offset + dim.getDataLen(),
                std::invalid_argument)
    << "view tensor size + offset > source tensor size, view tensor size: "
    << dim.getDataLen() << " offset: " << adjusted_offset
    << " source tensor: " << spec.tensor->getDim().getDataLen()
    << " name: " << spec.tensor->getName();

  if (init != Tensor::Initializer::NONE &&
      spec.tensor->getInitializer() != init)
    throw std::invalid_argument("Request tensor initialization mismatch");

  expandLifespan(spec, exec_order, lifespan);
  std::get<SourceDetails>(spec.details).dependents.push_back(pool.size());

  /** @note below invalidates spec reference */
  /** @note in case of view of view, internal datastructure saves the src to
   * view index, not view to view reference in order to flatten depth */
  auto parent_name = name_map.at(spec.tensor->getName());
  return registerRequestSpec(
    {std::make_unique<Tensor>(dim, false, init, name),
     TensorPool::DependentDetails{parent_name, adjusted_offset}});
}

/**
 * @brief finalize the requested tensors
 *
 * @details finalize the requested tensors, request memory for them and plan
 * layout for their allocations.
 */
void TensorPool::finalize(const MemoryPlanner &planner,
                          unsigned int start_order, unsigned int end_order) {
  mem_pool.clear();
  unsigned int bytes_requested = 0;
  for (auto &spec : pool) {
    auto details = std::get_if<SourceDetails>(&spec.details);
    if (!details || details->lifespan == TensorLifespan::UNMANAGED ||
        details->exec_order.empty()) {
      continue;
    }
    details->token = 0;

    /** 1. create the validity ranges for the all the requested tensors */
    unsigned int validity_start =
      *std::min_element(details->exec_order.begin(), details->exec_order.end());
    unsigned int validity_end =
      *std::max_element(details->exec_order.begin(), details->exec_order.end());

    /**
     * use lifespan to update the validity.
     * if the validity is long term, the tensor must stay valid for the
     * complete duration.
     */
    if (isTensorLongTerm(details->lifespan)) {
      validity_start = start_order;
      validity_end = end_order;
    }

    /** 2. for each tensor request if it is in the provided range */
    if (validity_end < start_order || validity_start > end_order)
      continue;
    validity_start = std::max(validity_start, start_order);
    validity_end = std::min(validity_end, end_order);

    /**
     * 3. requestMemory for all the tensors and set their tokens
     * @note +1 is to make the validity_end exlusive in the interval range
     */
    details->token = mem_pool.requestMemory(spec.tensor->bytes(),
                                            validity_start, validity_end + 1);
#ifdef DEBUG
    if (details->token == 0)
      throw std::runtime_error("Received invalid token from memory pool");
#endif

    bytes_requested += spec.tensor->bytes();
  }

  /** 4. finalizeLayout for the memory pool. */
  if (bytes_requested > 0) {
    double efficiency = mem_pool.planLayout(planner);
    ml_logd("Memory layout efficiency = %lf", efficiency);
  }
}

/**
 * @brief Set the batch size for the inputs/outputs of the layers
 */
void TensorPool::setBatchSize(const std::string &name, unsigned int batch) {
  if (name_map.find(name) == name_map.end())
    throw std::invalid_argument("Requested tensor not found");

  pool[name_map[name]].tensor->updateBatch(batch);
}

/**
 * @brief Allocate memory for all the managed tensors
 */
void TensorPool::allocate() {
  mem_pool.allocate();

  /** set the pointers using the token for all the tensors */
  for (auto &spec : pool) {
    auto details = std::get_if<SourceDetails>(&spec.details);
    if (!details || details->token == 0) {
      continue;
    }
    spec.tensor->setData(mem_pool.getMemory(details->token), true);
    syncDependents(spec);
  }
}

/**
 * @brief Deallocate memory for all the managed tensors
 */
void TensorPool::deallocate() {
  mem_pool.deallocate();

  /** nullify the data pointers for the tensors */
  for (auto &spec : pool) {
    spec.tensor->setData(nullptr);
  }
}

const std::vector<unsigned int> &
TensorPool::getExecutionOrder(const std::string &name) {
  return std::get<SourceDetails>(getSourceSpec(name).details).exec_order;
}

/**
 * @brief     Expand the lifespan of the tensor with the given name
 *
 */
TensorPool::RequestSpec &
TensorPool::expandLifespan(const std::string &name,
                           const std::vector<unsigned> &exec_order,
                           TensorLifespan lifespan) {
  auto &spec = getSourceSpec(name);
  expandLifespan(spec, exec_order, lifespan);
  return spec;
}

void TensorPool::expandLifespan(RequestSpec &spec,
                                const std::vector<unsigned int> &exec_order,
                                TensorLifespan lifespan) {
  auto &details = std::get<SourceDetails>(spec.details);
  NNTR_THROW_IF((details.lifespan != TensorLifespan::UNMANAGED &&
                 lifespan == TensorLifespan::UNMANAGED),
                std::invalid_argument)
    << "Extending to lifespan to unmanaged is not possible for name: "
    << spec.tensor->getName();

  if (details.lifespan != TensorLifespan::UNMANAGED) {
    /// update only if lifespan is unmanaged
    details.lifespan =
      enum_class_or<TensorLifespan>(details.lifespan, lifespan);
  }
  details.exec_order.insert(details.exec_order.end(), exec_order.begin(),
                            exec_order.end());
}

void TensorPool::syncDependents(const RequestSpec &spec) {
  /// @note syncing dependents of dependents is invalid and will throw.
  auto &dependents = std::get<SourceDetails>(spec.details).dependents;
  for (auto &dep : dependents) {
    auto &dep_spec = pool.at(dep);
    auto offset = std::get<DependentDetails>(dep_spec.details).offset;
    dep_spec.tensor->setData(spec.tensor->getData() + offset);
  }
}

Tensor *TensorPool::registerRequestSpec(RequestSpec &&spec) {
  auto &name = spec.tensor->getName();
  if (name_map.find(name) != name_map.end())
    throw std::invalid_argument("Cannot request tensor with same name");

  if (spec.tensor->empty())
    throw std::invalid_argument("Cannot request tensor with size 0");

  if (name.empty())
    throw std::invalid_argument("Cannot request tensor with empty name");

  pool.push_back(std::move(spec));
  name_map[name] = pool.size() - 1;

  return pool.back().tensor.get();
}

TensorPool::RequestSpec &TensorPool::getSourceSpec(const std::string &name) {
  RequestSpec *rs = &pool.at(name_map.at(name));
  while (auto dep_details = std::get_if<DependentDetails>(&rs->details)) {
    rs = &pool.at(dep_details->parent_idx);
  }

  return *rs;
}

void TensorPool::fillPlaceholder(const std::string &name, const Tensor &t) {
  auto &spec = getSourceSpec(name);
  auto &details = std::get<SourceDetails>(spec.details);
  NNTR_THROW_IF(details.lifespan != TensorLifespan::UNMANAGED,
                std::invalid_argument)
    << "Cannot set external tensor for non-zero lifespan for " << name;

  NNTR_THROW_IF(t.size() == 0 && t.getData(), std::invalid_argument)
    << "Error: setting invalid external tensor size 0 for " << name;

  NNTR_THROW_IF(t.size() != 0 && t.size() < spec.tensor->size(),
                std::invalid_argument)
    << "Error: setting external tensor of smaller size for "
    << spec.tensor->getName() << "(maybe view of " << name << ")";

  spec.tensor->setData(t.getData());
  syncDependents(spec);
}

Tensor *TensorPool::placeholder(const std::string &name, const TensorDim &dim) {
  /// @todo rename requestExternallyAllocateTensor -> placeholder
  return requestExternallyAllocateTensor(dim, name);
}

Tensor *TensorPool::create(const std::string &name, const TensorDim &dim,
                           const std::vector<unsigned int> &exec_order,
                           TensorLifespan lifespan,
                           const Tensor::Initializer &init) {
  /// @todo rename requestTensor -> create
  return requestTensor(dim, exec_order, lifespan, name, init);
}

Tensor *TensorPool::view(const std::string &name, const std::string &reference,
                         const TensorDim &dim,
                         const std::vector<unsigned int> &exec_order,
                         TensorLifespan lifespan, const unsigned int offset) {
  /// @todo rename requestPrerequestedTensor -> view
  return requestPrerequestedTensor(dim, exec_order, lifespan, name, reference,
                                   Tensor::Initializer::NONE, offset);
}

Tensor *TensorPool::extend(const std::string &name,
                           const std::vector<unsigned int> &exec_order,
                           TensorLifespan lifespan) {
  NNTR_THROW_IF(!tensorExist(name), std::invalid_argument)
    << " cannot extend tensor which does not exist, name: " << name;
  auto &spec = getSourceSpec(name);
  expandLifespan(spec, exec_order, lifespan);
  return getTensor(name);
}

bool TensorPool::tensorExist(const std::string &name) {
  return name_map.count(name);
}

/**
 * @brief     Check if the lifespan leads to long term valitidy
 *
 */
bool TensorPool::isTensorLongTerm(const TensorLifespan &lifespan) {
  switch (lifespan) {
  case TensorLifespan::EPOCH_LIFESPAN:
    [[fallthrough]];
  case TensorLifespan::MAX_LIFESPAN:
    return true;
  case TensorLifespan::FORWARD_FUNC_LIFESPAN:
    [[fallthrough]];
  case TensorLifespan::BACKWARD_FUNC_LIFESPAN:
    [[fallthrough]];
  case TensorLifespan::ITERATION_LIFESPAN:
    [[fallthrough]];
  case TensorLifespan::UNMANAGED:
    [[fallthrough]];
  default:
    return false;
  }
}

} // namespace nntrainer
