#pragma once

#include <string>
#include "../Interface/Types.h"
#include "../Interface/File.h"
#include "../stringtools.h"

class INotEnoughSpaceCallback;
class CWData;
class CRData;

class FileMetadata
{
public:
	FileMetadata()
		: last_modified(0), created(0), exist(false), has_orig_path(false)
	{

	}

	FileMetadata(std::string file_permissions, int64 last_modified,
		int64 created, std::string orig_path)
		: file_permissions(file_permissions),
		last_modified(last_modified), created(created),
		orig_path(orig_path)
	{

	}

	std::string file_permissions;
	int64 last_modified;
	int64 created;
	std::string shahash;
	bool has_orig_path;
	std::string orig_path;
	bool exist;

	bool operator==(const FileMetadata& other)
	{
		return file_permissions==other.file_permissions
			&& last_modified== other.last_modified
			&& created == other.created;
	}

	static bool hasPermission(const std::string& permissions, int id, bool& denied);

	bool hasPermission(int id, bool& denied) const;

	void serialize(CWData& data) const;

	bool read(CRData& data);

	bool read(str_map& extra_params);

	void set_shahash(const std::string& the_shahash);

	void set_orig_path(const std::string& the_orig_path);
};

bool write_file_metadata(const std::wstring& out_fn, INotEnoughSpaceCallback *cb, const FileMetadata& metadata, bool overwrite_existing);

bool write_file_metadata(IFile* out, INotEnoughSpaceCallback *cb, const FileMetadata& metadata, bool overwrite_existing, int64& truncate_to_bytes);

bool is_metadata_only(IFile* hash_file);

bool read_metadata(const std::wstring& in_fn, FileMetadata& metadata);

bool read_metadata(IFile* in, FileMetadata& metadata);

bool has_metadata(const std::wstring& in_fn, const FileMetadata& metadata);

int64 os_metadata_offset(IFile* meta_file);

int64 read_hashdata_size(IFile* meta_file);

bool copy_os_metadata(const std::wstring& in_fn, const std::wstring& out_fn, INotEnoughSpaceCallback *cb);

namespace
{
	const wchar_t* metadata_dir_fn=L".dir_metadata";
}

std::wstring escape_metadata_fn(const std::wstring& fn);

std::wstring unescape_metadata_fn(const std::wstring& fn);
