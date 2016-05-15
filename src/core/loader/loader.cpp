// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <string>

#include "common/logging/log.h"
#include "common/string_util.h"

#include "core/file_sys/archive_romfs.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/fs/archive.h"
#include "core/loader/3dsx.h"
#include "core/loader/elf.h"
#include "core/loader/ncch.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Loader {

const std::initializer_list<Kernel::AddressMapping> default_address_mappings = {
    { 0x1FF50000,   0x8000, true  }, // part of DSP RAM
    { 0x1FF70000,   0x8000, true  }, // part of DSP RAM
    { 0x1F000000, 0x600000, false }, // entire VRAM
};

FileType IdentifyFile(FileUtil::IOFile& file) {
    FileType type;

#define CHECK_TYPE(loader) \
    type = AppLoader_##loader::IdentifyType(file); \
    if (FileType::Error != type) \
        return type;

    CHECK_TYPE(THREEDSX)
    CHECK_TYPE(ELF)
    CHECK_TYPE(NCCH)

#undef CHECK_TYPE

    return FileType::Unknown;
}

FileType IdentifyFile(const std::string& file_name) {
    FileUtil::IOFile file(file_name, "rb");
    if (!file.IsOpen()) {
        LOG_ERROR(Loader, "Failed to load file {}", file_name);
        return FileType::Unknown;
    }

    return IdentifyFile(file);
}

FileType GuessFromExtension(const std::string& extension_) {
    std::string extension = Common::ToLower(extension_);

    if (extension == ".elf" || extension == ".axf")
        return FileType::ELF;

    if (extension == ".cci" || extension == ".3ds")
        return FileType::CCI;

    if (extension == ".cxi")
        return FileType::CXI;

    if (extension == ".3dsx")
        return FileType::THREEDSX;

    return FileType::Unknown;
}

const char* GetFileTypeString(FileType type) {
    switch (type) {
    case FileType::CCI:
        return "NCSD";
    case FileType::CXI:
        return "NCCH";
    case FileType::CIA:
        return "CIA";
    case FileType::ELF:
        return "ELF";
    case FileType::THREEDSX:
        return "3DSX";
    case FileType::Error:
    case FileType::Unknown:
        break;
    }

    return "unknown";
}

std::unique_ptr<AppLoader> GetLoader(FileUtil::IOFile&& file, FileType type,
    const std::string& filename, const std::string& filepath) {
    switch (type) {

    // 3DSX file format.
    case FileType::THREEDSX:
        return std::make_unique<AppLoader_THREEDSX>(std::move(file), filename, filepath);

    // Standard ELF file format.
    case FileType::ELF:
        return std::make_unique<AppLoader_ELF>(std::move(file), filename);

    // NCCH/NCSD container formats.
    case FileType::CXI:
    case FileType::CCI:
        return std::make_unique<AppLoader_NCCH>(std::move(file), filepath);

    default:
        return std::unique_ptr<AppLoader>();
    }
}

ResultStatus LoadFile(const std::string& filename) {
    FileUtil::IOFile file(filename, "rb");
    if (!file.IsOpen()) {
        LOG_ERROR(Loader, "Failed to load file {}", filename);
        return ResultStatus::Error;
    }

    std::string filename_filename, filename_extension;
    Common::SplitPath(filename, nullptr, &filename_filename, &filename_extension);

    FileType type = IdentifyFile(file);
    FileType filename_type = GuessFromExtension(filename_extension);

    if (type != filename_type) {
        LOG_WARNING(Loader, "File {} has a different type than its extension.", filename);
        if (FileType::Unknown == type)
            type = filename_type;
    }

    LOG_INFO(Loader, "Loading file {} as {}...", filename, GetFileTypeString(type));

    std::unique_ptr<AppLoader> app_loader = GetLoader(std::move(file), type, filename_filename, filename);

    switch (type) {

    // 3DSX file format...
    // or NCCH/NCSD container formats...
    case FileType::THREEDSX:
    case FileType::CXI:
    case FileType::CCI:
    {
        // Load application and RomFS
        ResultStatus result = app_loader->Load();
        if (ResultStatus::Success == result) {
            Service::FS::RegisterArchiveType(std::make_unique<FileSys::ArchiveFactory_RomFS>(*app_loader), Service::FS::ArchiveIdCode::RomFS);
            return ResultStatus::Success;
        }
        return result;
    }

    // Standard ELF file format...
    case FileType::ELF:
        return app_loader->Load();

    // CIA file format...
    case FileType::CIA:
        return ResultStatus::ErrorNotImplemented;

    // Error occurred durring IdentifyFile...
    case FileType::Error:

    // IdentifyFile could know identify file type...
    case FileType::Unknown:
    {
        LOG_CRITICAL(Loader, "File {} is of unknown type.", filename);
        return ResultStatus::ErrorInvalidFormat;
    }
    }
    return ResultStatus::Error;
}

} // namespace Loader
