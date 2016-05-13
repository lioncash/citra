// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstdio>
#include <memory>

#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"

#include "core/file_sys/disk_archive.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FileSys namespace

namespace FileSys {

ResultVal<std::unique_ptr<FileBackend>> DiskArchive::OpenFile(const Path& path, const Mode mode) const {
    LOG_DEBUG(Service_FS, "called path={} mode={:01X}", path.DebugStr(), mode.hex);
    auto file = std::make_unique<DiskFile>(*this, path, mode);
    ResultCode result = file->Open();
    if (result.IsError())
        return result;
    return MakeResult<std::unique_ptr<FileBackend>>(std::move(file));
}

ResultCode DiskArchive::DeleteFile(const Path& path) const {
    std::string file_path = mount_point + path.AsString();

    if (FileUtil::IsDirectory(file_path))
        return ResultCode(ErrorDescription::FS_NotAFile, ErrorModule::FS, ErrorSummary::Canceled, ErrorLevel::Status);

    if (!FileUtil::Exists(file_path))
        return ResultCode(ErrorDescription::FS_NotFound, ErrorModule::FS, ErrorSummary::NotFound, ErrorLevel::Status);

    if (FileUtil::Delete(file_path))
        return RESULT_SUCCESS;

    return ResultCode(ErrorDescription::FS_NotAFile, ErrorModule::FS, ErrorSummary::Canceled, ErrorLevel::Status);
}

bool DiskArchive::RenameFile(const Path& src_path, const Path& dest_path) const {
    return FileUtil::Rename(mount_point + src_path.AsString(), mount_point + dest_path.AsString());
}

bool DiskArchive::DeleteDirectory(const Path& path) const {
    return FileUtil::DeleteDir(mount_point + path.AsString());
}

ResultCode DiskArchive::CreateFile(const FileSys::Path& path, u64 size) const {
    std::string full_path = mount_point + path.AsString();

    if (FileUtil::IsDirectory(full_path))
        return ResultCode(ErrorDescription::FS_NotAFile, ErrorModule::FS, ErrorSummary::Canceled, ErrorLevel::Status);

    if (FileUtil::Exists(full_path))
        return ResultCode(ErrorDescription::FS_AlreadyExists, ErrorModule::FS, ErrorSummary::NothingHappened, ErrorLevel::Status);

    if (size == 0) {
        FileUtil::CreateEmptyFile(full_path);
        return RESULT_SUCCESS;
    }

    FileUtil::IOFile file(full_path, "wb");
    // Creates a sparse file (or a normal file on filesystems without the concept of sparse files)
    // We do this by seeking to the right size, then writing a single null byte.
    if (file.Seek(size - 1, SEEK_SET) && file.WriteBytes("", 1) == 1)
        return RESULT_SUCCESS;

    return ResultCode(ErrorDescription::TooLarge, ErrorModule::FS, ErrorSummary::OutOfResource, ErrorLevel::Info);
}


bool DiskArchive::CreateDirectory(const Path& path) const {
    return FileUtil::CreateDir(mount_point + path.AsString());
}

bool DiskArchive::RenameDirectory(const Path& src_path, const Path& dest_path) const {
    return FileUtil::Rename(mount_point + src_path.AsString(), mount_point + dest_path.AsString());
}

std::unique_ptr<DirectoryBackend> DiskArchive::OpenDirectory(const Path& path) const {
    LOG_DEBUG(Service_FS, "called path={}", path.DebugStr());
    auto directory = std::make_unique<DiskDirectory>(*this, path);
    if (!directory->Open())
        return nullptr;
    return std::move(directory);
}

u64 DiskArchive::GetFreeBytes() const {
    // TODO: Stubbed to return 1GiB
    return 1024 * 1024 * 1024;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

DiskFile::DiskFile(const DiskArchive& archive, const Path& path, const Mode mode) {
    // TODO(Link Mauve): normalize path into an absolute path without "..", it can currently bypass
    // the root directory we set while opening the archive.
    // For example, opening /../../etc/passwd can give the emulated program your users list.
    this->path = archive.mount_point + path.AsString();
    this->mode.hex = mode.hex;
}

ResultCode DiskFile::Open() {
    if (FileUtil::IsDirectory(path))
        return ResultCode(ErrorDescription::FS_NotAFile, ErrorModule::FS, ErrorSummary::Canceled, ErrorLevel::Status);

    // Specifying only the Create flag is invalid
    if (mode.create_flag && !mode.read_flag && !mode.write_flag) {
        return ResultCode(ErrorDescription::FS_InvalidOpenFlags, ErrorModule::FS, ErrorSummary::Canceled, ErrorLevel::Status);
    }

    if (!FileUtil::Exists(path)) {
        if (!mode.create_flag) {
            LOG_ERROR(Service_FS, "Non-existing file {} can't be open without mode create.", path);
            return ResultCode(ErrorDescription::FS_NotFound, ErrorModule::FS, ErrorSummary::NotFound, ErrorLevel::Status);
        } else {
            // Create the file
            FileUtil::CreateEmptyFile(path);
        }
    }

    std::string mode_string = "";
    if (mode.write_flag)
        mode_string += "r+"; // Files opened with Write access can be read from
    else if (mode.read_flag)
        mode_string += "r";

    // Open the file in binary mode, to avoid problems with CR/LF on Windows systems
    mode_string += "b";

    file = std::make_unique<FileUtil::IOFile>(path, mode_string.c_str());
    if (file->IsOpen())
        return RESULT_SUCCESS;
    return ResultCode(ErrorDescription::FS_NotFound, ErrorModule::FS, ErrorSummary::NotFound, ErrorLevel::Status);
}

ResultVal<size_t> DiskFile::Read(const u64 offset, const size_t length, u8* buffer) const {
    if (!mode.read_flag && !mode.write_flag)
        return ResultCode(ErrorDescription::FS_InvalidOpenFlags, ErrorModule::FS, ErrorSummary::Canceled, ErrorLevel::Status);

    file->Seek(offset, SEEK_SET);
    return MakeResult<size_t>(file->ReadBytes(buffer, length));
}

ResultVal<size_t> DiskFile::Write(const u64 offset, const size_t length, const bool flush, const u8* buffer) const {
    if (!mode.write_flag)
        return ResultCode(ErrorDescription::FS_InvalidOpenFlags, ErrorModule::FS, ErrorSummary::Canceled, ErrorLevel::Status);

    file->Seek(offset, SEEK_SET);
    size_t written = file->WriteBytes(buffer, length);
    if (flush)
        file->Flush();
    return MakeResult<size_t>(written);
}

u64 DiskFile::GetSize() const {
    return file->GetSize();
}

bool DiskFile::SetSize(const u64 size) const {
    file->Resize(size);
    file->Flush();
    return true;
}

bool DiskFile::Close() const {
    return file->Close();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

DiskDirectory::DiskDirectory(const DiskArchive& archive, const Path& path) : directory() {
    // TODO(Link Mauve): normalize path into an absolute path without "..", it can currently bypass
    // the root directory we set while opening the archive.
    // For example, opening /../../usr/bin can give the emulated program your installed programs.
    this->path = archive.mount_point + path.AsString();
}

bool DiskDirectory::Open() {
    if (!FileUtil::IsDirectory(path))
        return false;
    unsigned size = FileUtil::ScanDirectoryTree(path, directory);
    directory.size = size;
    directory.isDirectory = true;
    children_iterator = directory.children.begin();
    return true;
}

u32 DiskDirectory::Read(const u32 count, Entry* entries) {
    u32 entries_read = 0;

    while (entries_read < count && children_iterator != directory.children.cend()) {
        const FileUtil::FSTEntry& file = *children_iterator;
        const std::string& filename = file.virtualName;
        Entry& entry = entries[entries_read];

        LOG_TRACE(Service_FS, "File {}: size={} dir={}", filename, file.size, file.isDirectory);

        // TODO(Link Mauve): use a proper conversion to UTF-16.
        for (size_t j = 0; j < FILENAME_LENGTH; ++j) {
            entry.filename[j] = filename[j];
            if (!filename[j])
                break;
        }

        FileUtil::SplitFilename83(filename, entry.short_name, entry.extension);

        entry.is_directory = file.isDirectory;
        entry.is_hidden = (filename[0] == '.');
        entry.is_read_only = 0;
        entry.file_size = file.size;

        // We emulate a SD card where the archive bit has never been cleared, as it would be on
        // most user SD cards.
        // Some homebrews (blargSNES for instance) are known to mistakenly use the archive bit as a
        // file bit.
        entry.is_archive = !file.isDirectory;

        ++entries_read;
        ++children_iterator;
    }
    return entries_read;
}

} // namespace FileSys
