// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef PLATFORM_BSD_KQUEUE_SINGLE_THREADED_EXECUTOR_HPP_INCLUDED
#define PLATFORM_BSD_KQUEUE_SINGLE_THREADED_EXECUTOR_HPP_INCLUDED

#include "platform/posix/posix_executor_extension.hpp"
#include "platform/posix/posix_platform_error.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/rtti.hpp>
#include <cetl/visit_helpers.hpp>
#include <libcyphal/errors.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/platform/single_threaded_executor.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/types.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fcntl.h>
#include <limits>
#include <sys/event.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace platform
{
namespace bsd
{

/// @brief Defines BSD Linux platform-specific single-threaded executor based on `kqueue` mechanism.
///
class KqueueSingleThreadedExecutor final : public libcyphal::platform::SingleThreadedExecutor,
                                           public posix::IPosixExecutorExtension
{
public:
    KqueueSingleThreadedExecutor()
        : kqueuefd_{::kqueue()}
        , total_awaitables_{0}
    {
    }

    KqueueSingleThreadedExecutor(const KqueueSingleThreadedExecutor&)                = delete;
    KqueueSingleThreadedExecutor(KqueueSingleThreadedExecutor&&) noexcept            = delete;
    KqueueSingleThreadedExecutor& operator=(const KqueueSingleThreadedExecutor&)     = delete;
    KqueueSingleThreadedExecutor& operator=(KqueueSingleThreadedExecutor&&) noexcept = delete;

    ~KqueueSingleThreadedExecutor() override
    {
        if (kqueuefd_ >= 0)
        {
            ::close(kqueuefd_);
        }
    }

    CETL_NODISCARD cetl::optional<PollFailure> pollAwaitableResourcesFor(
        const cetl::optional<libcyphal::Duration> timeout) const override
    {
        CETL_DEBUG_ASSERT((total_awaitables_ > 0) || timeout,
                          "Infinite timeout without awaitables means that we will sleep forever.");

        if (total_awaitables_ == 0)
        {
            if (!timeout)
            {
                return libcyphal::ArgumentError{};
            }

            std::this_thread::sleep_for(*timeout);
            return cetl::nullopt;
        }

        // Convert libcyphal timeout (if any) to the `struct timespec` timeout in ns.
        // Any possible negative timeout will be treated as zero (return immediately from the `::kevent`).
        //
        struct timespec timeout_spec
        {};
        const struct timespec* timeout_spec_ptr = nullptr;
        if (timeout)
        {
            using PollDuration  = std::chrono::nanoseconds;
            using TimeoutNsType = decltype(timespec::tv_nsec);

            // Fill nanoseconds part of the timeout spec taking into account the maximum possible value.
            //
            timeout_spec.tv_nsec = static_cast<TimeoutNsType>(  //
                std::max(static_cast<PollDuration::rep>(0),
                         std::min(std::chrono::duration_cast<PollDuration>(*timeout).count(),
                                  static_cast<PollDuration::rep>(std::numeric_limits<TimeoutNsType>::max()))));

            timeout_spec_ptr = &timeout_spec;
        }

        std::array<KEvent, MaxEvents> evs{};
        const int kqueue_result = ::kevent(kqueuefd_, nullptr, 0, evs.data(), evs.size(), timeout_spec_ptr);
        if (kqueue_result < 0)
        {
            const auto err = errno;
            if (err == EINTR)
            {
                // Normally, we would just retry a system call (`::kevent`),
                // but we need updated timeout (from the main loop).
                return cetl::nullopt;
            }
            return libcyphal::transport::PlatformError{posix::PosixPlatformError{err}};
        }
        if (kqueue_result == 0)
        {
            return cetl::nullopt;
        }
        const auto kqueue_nfds = static_cast<std::size_t>(kqueue_result);

        const auto now_time = now();
        for (std::size_t index = 0; index < kqueue_nfds; ++index)
        {
            const KEvent& ev = evs[index];
            if (auto* const cb_interface = static_cast<AwaitableNode*>(ev.udata))
            {
                cb_interface->schedule(Callback::Schedule::Once{now_time});
            }
        }

        return cetl::nullopt;
    }

protected:
    // MARK: - IPosixExecutorExtension

    CETL_NODISCARD Callback::Any registerAwaitableCallback(Callback::Function&&    function,
                                                           const Trigger::Variant& trigger) override
    {
        AwaitableNode new_cb_node{*this, std::move(function)};

        cetl::visit(  //
            cetl::make_overloaded(
                [&new_cb_node](const Trigger::Readable& readable) {
                    //
                    new_cb_node.setup(readable.fd, EVFILT_READ);
                },
                [&new_cb_node](const Trigger::Writable& writable) {
                    //
                    new_cb_node.setup(writable.fd, EVFILT_WRITE);
                }),
            trigger);

        insertCallbackNode(new_cb_node);
        return {std::move(new_cb_node)};
    }

    // MARK: - RTTI

    CETL_NODISCARD void* _cast_(const cetl::type_id& id) & noexcept override
    {
        if (id == IPosixExecutorExtension::_get_type_id_())
        {
            return static_cast<IPosixExecutorExtension*>(this);
        }
        return Base::_cast_(id);
    }
    CETL_NODISCARD const void* _cast_(const cetl::type_id& id) const& noexcept override
    {
        if (id == IPosixExecutorExtension::_get_type_id_())
        {
            return static_cast<const IPosixExecutorExtension*>(this);
        }
        return Base::_cast_(id);
    }

private:
    using KEvent = struct kevent;
    using Base   = SingleThreadedExecutor;
    using Self   = KqueueSingleThreadedExecutor;

    /// No Sonar cpp:S4963 b/c `AwaitableNode` supports move operation.
    ///
    class AwaitableNode final : public CallbackNode  // NOSONAR cpp:S4963
    {
    public:
        AwaitableNode(Self& executor, Callback::Function&& function)
            : CallbackNode{executor, std::move(function)}
            , fd_{-1}
            , filter_{0}
        {
        }

        ~AwaitableNode() override
        {
            if (fd_ >= 0)
            {
                KEvent ev{};
                EV_SET(&ev, fd_, filter_, EV_DELETE, 0, 0, 0);
                ::kevent(getExecutor().kqueuefd_, &ev, 1, nullptr, 0, nullptr);
                getExecutor().total_awaitables_--;
            }
        }

        AwaitableNode(AwaitableNode&& other) noexcept
            : CallbackNode(std::move(static_cast<CallbackNode&&>(other)))
            , fd_{std::exchange(other.fd_, -1)}
            , filter_{std::exchange(other.filter_, 0)}
        {
            if (fd_ >= 0)
            {
                KEvent ev{};
                EV_SET(&ev, fd_, filter_, EV_DELETE, 0, 0, 0);
                ::kevent(getExecutor().kqueuefd_, &ev, 1, nullptr, 0, nullptr);
                EV_SET(&ev, fd_, filter_, EV_ADD, 0, 0, this);
                ::kevent(getExecutor().kqueuefd_, &ev, 1, nullptr, 0, nullptr);
            }
        }

        AwaitableNode(const AwaitableNode&)                      = delete;
        AwaitableNode& operator=(const AwaitableNode&)           = delete;
        AwaitableNode& operator=(AwaitableNode&& other) noexcept = delete;

        int fd() const noexcept
        {
            return fd_;
        }

        std::int16_t filter() const noexcept
        {
            return filter_;
        }

        void setup(const int fd, const std::int16_t filter) noexcept
        {
            CETL_DEBUG_ASSERT(fd >= 0, "");
            CETL_DEBUG_ASSERT(filter != 0, "");

            fd_     = fd;
            filter_ = filter;

            getExecutor().total_awaitables_++;
            KEvent ev{};
            EV_SET(&ev, fd, filter_, EV_ADD, 0, 0, this);
            ::kevent(getExecutor().kqueuefd_, &ev, 1, nullptr, 0, nullptr);
        }

    private:
        Self& getExecutor() noexcept
        {
            // No lint b/c we know for sure that the executor is of type `Self`.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            return static_cast<Self&>(executor());
        }

        // MARK: Data members:

        int           fd_;
        std::int16_t  filter_;

    };  // AwaitableNode

    // MARK: - Data members:

    static constexpr int MaxEvents = 16;

    int         kqueuefd_;
    std::size_t total_awaitables_;

};  // KqueueSingleThreadedExecutor

}  // namespace bsd
}  // namespace platform

#endif  // PLATFORM_BSD_KQUEUE_SINGLE_THREADED_EXECUTOR_HPP_INCLUDED
