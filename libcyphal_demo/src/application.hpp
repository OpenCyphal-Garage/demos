// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "platform/linux/epoll_single_threaded_executor.hpp"
#include "platform/o1_heap_memory_resource.hpp"
#include "platform/string.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/application/registry/register.hpp>
#include <libcyphal/application/registry/registry_impl.hpp>
#include <libcyphal/types.hpp>

/// The main application class.
///
/// Expected to be a singleton.
///
class Application final
{
public:
    struct Regs
    {
        /// Defines general purpose string parameter exposed as mutable register.
        ///
        template <std::size_t N>
        struct StringParam
        {
            StringParam(const libcyphal::application::registry::IRegister::Name     name,
                        libcyphal::application::registry::Registry&                 registry,
                        const cetl::string_view                                     initial_value,
                        const libcyphal::application::registry::IRegister::Options& options = {})
                : value_{initial_value}
                , memory_{registry.memory()}
                , register_{registry.route(
                      name,
                      [this] {
                          //
                          return makeStringValue(value_);
                      },
                      [this](const auto& value) -> cetl::optional<libcyphal::application::registry::SetError> {
                          //
                          if (value.is_string())
                          {
                              this->value_ = libcyphal::application::registry::makeStringView(value.get_string().value);
                              return cetl::nullopt;
                          }
                          return libcyphal::application::registry::SetError::Semantics;
                      },
                      options)}
            {
            }

            CETL_NODISCARD platform::String<N>& value()
            {
                return value_;
            }

        private:
            CETL_NODISCARD libcyphal::application::registry::IRegister::Value makeStringValue(
                const cetl::string_view sv) const
            {
                using Value = libcyphal::application::registry::IRegister::Value;

                const Value::allocator_type allocator{&memory_};
                Value                       value{allocator};
                auto&                       str = value.set_string();
                std::copy(sv.begin(), sv.end(), std::back_inserter(str.value));
                return value;
            }

            platform::String<N>                                            value_;
            cetl::pmr::memory_resource&                                    memory_;
            libcyphal::application::registry::Register<sizeof(void*) * 12> register_;

        };  // StringParam

        explicit Regs(libcyphal::application::registry::Registry& registry)
            : registry_{registry}
        {
        }

    private:
        friend class Application;

        libcyphal::application::registry::Registry& registry_;

        // clang-format off
        StringParam<64>     can_iface_{ "uavcan.can.iface",         registry_,  {"vcan0"},      {true}};
        StringParam<50>     node_desc_{ "uavcan.node.description",  registry_,  {NODE_NAME},    {true}};
        StringParam<64>     udp_iface_{ "uavcan.udp.iface",         registry_,  {"127.0.0.1"},  {true}};
        // clang-format on

    };  // Regs

    struct IfaceParams
    {
        Regs::StringParam<64>& udp_iface;
        Regs::StringParam<64>& can_iface;
    };

    struct NodeParams
    {
        Regs::StringParam<50>& description;
    };

    Application();
    ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&)                 = delete;
    Application& operator=(Application&&)      = delete;

    CETL_NODISCARD platform::Linux::EpollSingleThreadedExecutor& executor() noexcept
    {
        return executor_;
    }

    CETL_NODISCARD cetl::pmr::memory_resource& memory() noexcept
    {
        return o1_heap_mr_;
    }

    CETL_NODISCARD libcyphal::application::registry::Registry& registry() noexcept
    {
        return registry_;
    }

    CETL_NODISCARD IfaceParams getIfaceParams() noexcept
    {
        return {regs_.udp_iface_, regs_.can_iface_};
    }

    CETL_NODISCARD NodeParams getNodeParams() noexcept
    {
        return {regs_.node_desc_};
    }

private:
    // MARK: Data members:

    platform::Linux::EpollSingleThreadedExecutor executor_;
    platform::O1HeapMemoryResource               o1_heap_mr_;
    libcyphal::application::registry::Registry   registry_;
    Regs                                         regs_;

};  // Application

#endif  // APPLICATION_HPP
