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
#include <uavcan/file/Read_1_1.hpp>

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
        , read_client_{std::move(other.read_client_)}
        , response_promise_{std::move(other.response_promise_)}
        , current_request_{std::move(other.current_request_)}
    {
    }

    ~FileDownloader() = default;

    FileDownloader(const FileDownloader&)                = delete;
    FileDownloader& operator=(const FileDownloader&)     = delete;
    FileDownloader& operator=(FileDownloader&&) noexcept = delete;

    bool start(const libcyphal::transport::NodeId remote_node_id, const cetl::string_view file_path)
    {
        response_promise_.reset();
        read_client_.reset();

        auto maybe_client = presentation_.makeClient<ReadService>(remote_node_id);
        if (const auto* const failure = cetl::get_if<Presentation::MakeFailure>(&maybe_client))
        {
            (void) failure;
            return false;
        }
        read_client_.emplace(cetl::get<ReadClient>(std::move(maybe_client)));

        current_request_.offset    = 0;
        current_request_.path.path = {file_path.begin(), file_path.end(), &presentation_.memory()};

        return initiateNextRequest();
    }

private:
    using Presentation = libcyphal::presentation::Presentation;

    using ReadService     = uavcan::file::Read_1_1;
    using ReadClient      = libcyphal::presentation::ServiceClient<ReadService>;
    using ResponsePromise = libcyphal::presentation::ResponsePromise<ReadService::Response>;
    using ResponseData    = ReadService::Response::_traits_::TypeOf::data;

    FileDownloader(Presentation& presentation, libcyphal::ITimeProvider& time_provider)
        : presentation_{presentation}
        , time_provider_{time_provider}
    {
    }

    bool initiateNextRequest()
    {
        response_promise_.reset();

        constexpr auto timeout = std::chrono::seconds{1};
        auto maybe_promise = read_client_->request(time_provider_.now() + timeout, current_request_);
        if (const auto* const failure = cetl::get_if<ReadClient::Failure>(&maybe_promise))
        {
            (void) failure;
            read_client_.reset();
            return false;
        }
        response_promise_.emplace(cetl::get<ResponsePromise>(std::move(maybe_promise)));
        response_promise_->setCallback([this](const auto& args) {
            //
            handlePromiseResult(std::move(args.result));
        });

        return true;
    }

    void handlePromiseResult(ResponsePromise::Result result)
    {
        if (const auto* const success = cetl::get_if<ResponsePromise::Success>(&result))
        {
            const auto& response = success->response;
            if (response._error.value == uavcan::file::Error_1_0::OK)
            {
                const auto data_size = response.data.value.size();
                current_request_.offset += response.data.value.size();

                // Are we done?
                if (data_size == ResponseData::_traits_::ArrayCapacity::value)
                {
                    initiateNextRequest();
                    return;
                }
            }
        }
        std::cout << "Download complete\n";
        response_promise_.reset();
        read_client_.reset();
    }

    // MARK: Data members:

    Presentation&                   presentation_;
    libcyphal::ITimeProvider&       time_provider_;
    cetl::optional<ReadClient>      read_client_;
    cetl::optional<ResponsePromise> response_promise_;
    ReadService::Request            current_request_{&presentation_.memory()};

};  // FileDownloader

#endif  // FILE_DOWNLOADER_HPP_INCLUDED
