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
#include <cetl/unbounded_variant.hpp>
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
    /// Defines type-erased register.
    ///
    /// The Size of the unbounded variant is arbitrary (16 pointers),
    /// but should be enough for any register implementation.
    /// The implementation should not be copyable but moveable.
    ///
    template <std::size_t Footprint = sizeof(void*) * 16>
    using Register = libcyphal::ImplementationCell<libcyphal::application::registry::IRegister,
                                                   cetl::unbounded_variant<Footprint, false, true>>;

    struct Regs
    {
        Regs(libcyphal::application::registry::Registry& registry)
            : udp_iface_{registry.parameterize<String<64>>("uavcan.udp.iface", {"127.0.0.1"})}
            , can_iface_{registry.expose("uavcan.can.iface", can_iface_str_)}
        //, rgb_{registry.expose("rgb", rgb_arr_)}
        {
            registry.route(
                "uavcan.can.iface.alt",
                [this] {
                    //
                    return static_cast<cetl::string_view>(can_iface_str_);
                },
                [this](const auto& value) {
                    //
                    if (auto str = libcyphal::application::registry::get<cetl::string_view>(value))
                    {
                        can_iface_str_ = str.value();
                        return true;
                    }
                    return false;
                });
        }

        template <std::size_t N>
        using String = platform::String<N>;

        template <typename T>
        using Param = libcyphal::application::registry::ParamRegister<T>;

        Param<String<64>> udp_iface_;

        std::array<float, 3> rgb_arr_{};
        String<64>           can_iface_str_;

    private:
        Register<> rgb_;
        Register<> can_iface_;
    };

     Application();
    ~Application();

                 Application(const Application&) = delete;
    Application& operator=(const Application&)   = delete;
                 Application(Application&&)      = delete;
    Application& operator=(Application&&)        = delete;

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

private:
    // MARK: Data members:

    platform::Linux::EpollSingleThreadedExecutor executor_;
    platform::O1HeapMemoryResource               o1_heap_mr_;

    libcyphal::application::registry::Registry registry_{o1_heap_mr_};
    Regs                                       regs_{registry_};

};  // Application

#endif  // APPLICATION_HPP
