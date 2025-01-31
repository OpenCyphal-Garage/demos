// This software is distributed under the terms of the MIT License.
// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
// Copyright Amazon.com Inc. or its affiliates.
// SPDX-License-Identifier: MIT
// Author: Sergei Shirokov <sergei.shirokov@zubax.com>

#ifndef FILE_DOWNLOADER_HPP_INCLUDED
#define FILE_DOWNLOADER_HPP_INCLUDED

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/presentation/client.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/presentation/response_promise.hpp>
#include <libcyphal/time_provider.hpp>
#include <libcyphal/transport/types.hpp>
#include <libcyphal/types.hpp>

#include <uavcan/file/Error_1_0.hpp>
#include <uavcan/file/GetInfo_0_2.hpp>
#include <uavcan/file/Read_1_1.hpp>
#include <uavcan/primitive/Unstructured_1_0.hpp>

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <utility>

class FileDownloader final
{
public:
    /// Factory method to create a FileDownloader instance.
    ///
    static FileDownloader make(libcyphal::presentation::Presentation& presentation,
                               libcyphal::ITimeProvider&              time_provider)
    {
        return FileDownloader{presentation, time_provider};
    }

    FileDownloader(FileDownloader&& other) noexcept
        : presentation_{other.presentation_}
        , time_provider_{other.time_provider_}
        , get_info_client_{std::move(other.get_info_client_)}
        , get_info_promise_{std::move(other.get_info_promise_)}
        , read_client_{std::move(other.read_client_)}
        , read_promise_{std::move(other.read_promise_)}
        , read_request_{std::move(other.read_request_)}
        , file_stats_{other.file_stats_}
    {
    }

    ~FileDownloader() = default;

    FileDownloader(const FileDownloader&)                = delete;
    FileDownloader& operator=(const FileDownloader&)     = delete;
    FileDownloader& operator=(FileDownloader&&) noexcept = delete;

    bool start(const libcyphal::transport::NodeId remote_node_id, const cetl::string_view file_path)
    {
        get_info_promise_.reset();
        get_info_client_.reset();
        read_promise_.reset();
        read_client_.reset();

        file_stats_.file_size        = 0;
        file_stats_.file_progress    = 0;
        file_stats_.file_error.value = uavcan::file::Error_1_0::OK;

        get_info_client_ = makeClient<Svc::GetInfo>("GetInfo", remote_node_id);
        read_client_     = makeClient<Svc::Read>("Read", remote_node_id);
        if (!get_info_client_ || !read_client_)
        {
            file_stats_.file_error.value = uavcan::file::Error_1_0::UNKNOWN_ERROR;
            return false;
        }

        read_request_.offset    = 0;
        file_stats_.start_time  = time_provider_.now();
        read_request_.path.path = {file_path.begin(), file_path.end(), &presentation_.memory()};

        std::cout << "Getting file info (path='" << file_path << "')...\n";
        Svc::GetInfo::Request gi_request{&presentation_.memory()};
        gi_request.path.path = {file_path.begin(), file_path.end(), &presentation_.memory()};
        return makeRequest<Svc::GetInfo>("GetInfo",
                                         get_info_client_,
                                         get_info_promise_,
                                         gi_request,
                                         [this](const auto& arg) {
                                             //
                                             handleGetInfoPromiseResult(arg.result);
                                         });
    }

private:
    using NodeId                 = libcyphal::transport::NodeId;
    using Presentation           = libcyphal::presentation::Presentation;
    using UnstructuredData       = uavcan::primitive::Unstructured_1_0;
    using ResponsePromiseFailure = libcyphal::presentation::ResponsePromiseFailure;

    template <typename T>
    struct SvcSpec : T
    {
        using Client  = libcyphal::presentation::ServiceClient<T>;
        using Promise = libcyphal::presentation::ResponsePromise<typename T::Response>;
        using Failure = typename Client::Failure;
    };
    struct Svc
    {
        using Read    = SvcSpec<uavcan::file::Read_1_1>;
        using GetInfo = SvcSpec<uavcan::file::GetInfo_0_2>;

    };  // Svc

    struct FileStats
    {
        uavcan::file::Error_1_0 file_error;
        libcyphal::TimePoint    start_time;
        std::size_t             file_size{0};
        std::size_t             file_progress{0};

    };  // FileStats

    FileDownloader(Presentation& presentation, libcyphal::ITimeProvider& time_provider)
        : presentation_{presentation}
        , time_provider_{time_provider}
    {
    }

    template <typename Service>
    auto makeClient(const cetl::string_view role, const NodeId server_node_id)
        -> cetl::optional<typename Service::Client>
    {
        auto maybe_client = presentation_.makeClient<Service>(server_node_id);
        if (const auto* const failure = cetl::get_if<Presentation::MakeFailure>(&maybe_client))
        {
            (void) failure;
            std::cerr << "Can't make '" << role << "' client.\n";
            return cetl::nullopt;
        }
        return cetl::get<typename Service::Client>(std::move(maybe_client));
    }

    template <typename Service, typename Handler>
    bool makeRequest(  //
        const cetl::string_view                    role,
        cetl::optional<typename Service::Client>&  client,
        cetl::optional<typename Service::Promise>& promise,
        const typename Service::Request&           request,
        Handler&&                                  handler,
        const libcyphal::Duration                  timeout = std::chrono::seconds{1})
    {
        promise.reset();

        auto maybe_promise = client->request(time_provider_.now() + timeout, request);
        if (const auto* const failure = cetl::get_if<typename Service::Failure>(&maybe_promise))
        {
            (void) failure;
            file_stats_.file_error.value = uavcan::file::Error_1_0::UNKNOWN_ERROR;
            std::cerr << "Can't make '" << role << "' request.\n";
            complete();
            return false;
        }

        promise.emplace(cetl::get<typename Service::Promise>(std::move(maybe_promise)));
        promise->setCallback(std::forward<Handler>(handler));
        return true;
    }

    void handleGetInfoPromiseResult(const Svc::GetInfo::Promise::Result& result)
    {
        if (const auto* const failure = cetl::get_if<ResponsePromiseFailure>(&result))
        {
            (void) failure;
            file_stats_.file_error.value = uavcan::file::Error_1_0::UNKNOWN_ERROR;
            std::cerr << "GetInfo request failed.\n";
            complete();
            return;
        }
        const auto success = cetl::get<Svc::GetInfo::Promise::Success>(result);

        get_info_promise_.reset();
        get_info_client_.reset();

        const auto& response = success.response;
        if (response._error.value == uavcan::file::Error_1_0::OK)
        {
            file_stats_.file_size = response.size;
            std::cout << "Downloading (size=" << file_stats_.file_size << ") ...\n";
            if (file_stats_.file_size > 0)
            {
                file_stats_.start_time = time_provider_.now();

                printProgress();
                initiateNextReadRequest();
                return;
            }

            file_stats_.file_error.value = uavcan::file::Error_1_0::OK;
        }
        else
        {
            file_stats_.file_error = response._error;
            std::cerr << "Can't get file info (err=" << response._error.value << ").\n";
        }

        complete();
    }

    bool initiateNextReadRequest()
    {
        return makeRequest<Svc::Read>("Read", read_client_, read_promise_, read_request_, [this](const auto& arg) {
            //
            handleReadPromiseResult(arg.result);
        });
    }

    void handleReadPromiseResult(Svc::Read::Promise::Result result)
    {
        if (const auto* const failure = cetl::get_if<ResponsePromiseFailure>(&result))
        {
            (void) failure;
            file_stats_.file_error.value = uavcan::file::Error_1_0::UNKNOWN_ERROR;
            std::cerr << "Read request failed.\n";
            complete();
            return;
        }
        const auto success = cetl::get<Svc::Read::Promise::Success>(std::move(result));

        const auto& response = success.response;
        if (response._error.value == uavcan::file::Error_1_0::OK)
        {
            const auto data_size = response.data.value.size();
            read_request_.offset += response.data.value.size();

            printProgress();

            // Are we done?
            if (data_size == UnstructuredData::_traits_::ArrayCapacity::value)
            {
                initiateNextReadRequest();
                return;
            }
        }
        else
        {
            file_stats_.file_error = response._error;
            std::cerr << "Can't read file (err=" << response._error.value << ").\n";
        }
        complete();
    }

    void printProgress()
    {
        CETL_DEBUG_ASSERT(file_stats_.file_size > 0, "");
        CETL_DEBUG_ASSERT(read_request_.offset <= file_stats_.file_size, "");

        const auto progress = (read_request_.offset * 100U) / file_stats_.file_size;
        CETL_DEBUG_ASSERT(progress <= 100U, "");

        // Print progress only if its integer % has changed (or in the beginning).
        if ((progress != file_stats_.file_progress) || (read_request_.offset == 0))
        {
            file_stats_.file_progress = progress;
            const auto duration       = time_provider_.now() - file_stats_.start_time;
            if (const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count())
            {
                const auto speed_kb_per_sec = (read_request_.offset * 1000000U) / (duration_us * 1024U);
                std::cout << "\r  progress " << progress << "% (speed=" << speed_kb_per_sec << "KB/s)            "
                          << std::flush;
            }
            else
            {
                std::cout << "\r  progress " << progress << "%" << std::flush;
            }
        }
    }

    void complete()
    {
        const auto duration = time_provider_.now() - file_stats_.start_time;
        std::cout << "\nDownload completed (err=" << file_stats_.file_error.value  //
                  << ", time=" << std::fixed << std::setprecision(6)               // NOLINT
                  << std::chrono::duration_cast<std::chrono::duration<double>>(duration).count() << "s).\n"
                  << std::flush;

        get_info_promise_.reset();
        get_info_client_.reset();
        read_promise_.reset();
        read_client_.reset();
    }

    // MARK: Data members:

    Presentation&                         presentation_;
    libcyphal::ITimeProvider&             time_provider_;
    cetl::optional<Svc::GetInfo::Client>  get_info_client_;
    cetl::optional<Svc::GetInfo::Promise> get_info_promise_;
    cetl::optional<Svc::Read::Client>     read_client_;
    cetl::optional<Svc::Read::Promise>    read_promise_;
    Svc::Read::Request                    read_request_{&presentation_.memory()};
    FileStats                             file_stats_;

};  // FileDownloader

#endif  // FILE_DOWNLOADER_HPP_INCLUDED
