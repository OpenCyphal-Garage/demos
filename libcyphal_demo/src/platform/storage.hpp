/// @copyright
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT

#ifndef PLATFORM_STORAGE_HPP_INCLUDED
#define PLATFORM_STORAGE_HPP_INCLUDED

#include "string.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>
#include <libcyphal/platform/storage.hpp>
#include <libcyphal/types.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/stat.h>

namespace platform
{
namespace storage
{

/// Defines an example key-value storage implementation.
///
/// This implementation uses the file system to store key-value pairs.
/// C API (instead of C++ file streams) is used to ensure that there is no c++ heap involved.
/// IO error handling is missing for brevity - the goal is to have primitive platform storage.
///
class KeyValue final : public libcyphal::platform::storage::IKeyValue
{
    using Error = libcyphal::platform::storage::Error;

public:
    explicit KeyValue(const char* const root_path)
        : root_path_{root_path}
    {
        if ((mkdir(root_path, 0755) != 0) && (errno != EEXIST))  // NOLINT
        {
            std::cerr << "Error making folder: '" << root_path_ << "'.\n";
            std::cerr << "Error: " << std::strerror(errno) << "\n";
        }
    }

    // MARK: - IKeyValue

    auto get(const cetl::string_view        key,
             const cetl::span<std::uint8_t> data) const -> libcyphal::Expected<std::size_t, Error> override
    {
        const auto  file_path = makeFilePath(key);
        FILE* const file      = std::fopen(file_path.c_str(), "rb");  // NOLINT
        if (file == nullptr)
        {
            return Error::Existence;
        }

        const auto data_size = std::fread(data.data(), 1, data.size(), file);
        (void) std::fclose(file);  // NOLINT

        return data_size;
    }

    auto put(const cetl::string_view key, const cetl::span<const std::uint8_t> data)  //
        -> cetl::optional<Error> override
    {
        const auto  file_path = makeFilePath(key);
        FILE* const file      = std::fopen(file_path.c_str(), "wb");  // NOLINT
        if (file == nullptr)
        {
            return Error::Existence;
        }

        (void) std::fwrite(data.data(), 1, data.size(), file);  // NOLINT
        (void) std::fclose(file);                               // NOLINT

        return cetl::nullopt;
    }

    auto drop(const cetl::string_view key) -> cetl::optional<Error> override
    {
        const auto file_path = makeFilePath(key);
        if ((std::remove(file_path.c_str()) != 0) && (errno != ENOENT))
        {
            std::cerr << "Error removing file: '" << file_path.c_str() << "'.\n";
            std::cerr << "Error: " << std::strerror(errno) << "\n";
            return Error::IO;
        }
        return cetl::nullopt;
    }

private:
    static constexpr std::size_t MaxPathLen = 64;
    using StringPath                        = String<MaxPathLen>;

    /// In practice, the keys could be hashed, so it won't be necessary to deal with directory nesting.
    /// This is fine b/c we don't need key listing, and so we don't have to retain the key names.
    ///
    /// But with the below implementation, users can easily remove a single value by deleting
    /// the corresponding file on their system (under `/tmp/org.opencyphal.demos.libcyphal/` folder) -
    /// with hashes it would be harder to figure out which file to delete.
    ///
    StringPath makeFilePath(const cetl::string_view key) const
    {
        StringPath file_path{root_path_};
        file_path << "/" << key;
        return file_path;
    }

    const cetl::string_view root_path_;

};  // KeyValue

}  // namespace storage
}  // namespace platform

#endif  // PLATFORM_STORAGE_HPP_INCLUDED
