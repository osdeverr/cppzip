#pragma once
#include <zip.h>

#include <filesystem>
#include <stdexcept>

#include <futile/futile.h>
#include <ulib/containers/span.h>

#define CPPZIP_THROW_ERROR(err, message)                                                                               \
    throw ::cppzip::zip_error                                                                                          \
    {                                                                                                                  \
        message ": error " + std::to_string(err)                                                                       \
    }

namespace cppzip
{
    class zip_error : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    enum class open_mode
    {
        read,
        create,
        truncate
    };

    inline struct in_memory_t
    {
    } in_memory;

    enum class zip_entry_type
    {
        directory,
        file
    };

    struct zip_entry
    {
        zip_entry_type type;
        std::string path;

        std::int64_t index;
    };

    class zip_file
    {
    public:
        zip_file(in_memory_t)
        {
            zip_error_t src_err;

            _source = zip_source_buffer_create(0, 0, 0, &src_err);
            if (!_source)
                CPPZIP_THROW_ERROR(src_err.zip_err, "Failed to create in-memory ZIP file source");

            zip_source_keep(_source);

            zip_error_t open_err;

            _file = zip_open_from_source(_source, ZIP_TRUNCATE, &open_err);
            if (!_file)
                CPPZIP_THROW_ERROR(open_err.zip_err, "Failed to create in-memory ZIP file");
        }

        zip_file(const void *buffer, std::size_t size)
        {
            zip_error_t src_err;

            _source = zip_source_buffer_create(buffer, size, 0, &src_err);
            if (!_source)
                CPPZIP_THROW_ERROR(src_err.zip_err, "Failed to create in-memory ZIP file source");

            zip_source_keep(_source);

            zip_error_t open_err;

            _file = zip_open_from_source(_source, 0, &open_err);
            if (!_file)
                CPPZIP_THROW_ERROR(open_err.zip_err, "Failed to load in-memory ZIP file");
        }

        zip_file(ulib::span<unsigned char> from) : zip_file{from.data(), from.size_in_bytes()}
        {
        }

        zip_file(const std::filesystem::path &path, open_mode mode = open_mode::read)
        {
            zip_error_t src_err;

            _source = zip_source_file_create((const char *)path.generic_u8string().c_str(), 0, 0, &src_err);
            if (!_source)
                CPPZIP_THROW_ERROR(src_err.zip_err, "Failed to create ZIP file source");

            zip_source_keep(_source);

            zip_error_t open_err;

            int flags = 0;

            if (mode == open_mode::create)
                flags = ZIP_CREATE;
            if (mode == open_mode::truncate)
                flags = ZIP_CREATE | ZIP_TRUNCATE;

            _file = zip_open_from_source(_source, flags, &open_err);
            if (!_file)
                CPPZIP_THROW_ERROR(open_err.zip_err, "Failed to open ZIP file");
        }

        void create_directory(const std::filesystem::path &path)
        {
            zip_dir_add(_file, (const char *)path.generic_u8string().c_str(), ZIP_FL_ENC_UTF_8);
        }

        void add_file(const std::filesystem::path &path, const std::filesystem::path &to)
        {
            zip_error_t src_err;

            auto source = zip_source_file_create((const char *)path.generic_u8string().c_str(), 0, 0, &src_err);
            if (!source)
                CPPZIP_THROW_ERROR(src_err.zip_err, "Failed to create ZIP file source while adding file");

            zip_file_add(_file, (const char *)to.generic_u8string().c_str(), source, ZIP_FL_ENC_UTF_8);
        }

        void add_directory(const std::filesystem::path &path, const std::filesystem::path &to)
        {
            if (!to.empty())
                create_directory(to);

            for (auto &entry : std::filesystem::directory_iterator{path})
            {
                if (entry.is_directory())
                    add_directory(entry.path(), to / entry.path().filename());
                else if (entry.is_regular_file())
                    add_file(entry.path(), to / entry.path().filename());
            }
        }

        void finalize()
        {
            if (_file)
            {
                zip_close(_file);
                _file = nullptr;
            }
        }

        void discard()
        {
            if (_file)
            {
                zip_discard(_file);
                _file = nullptr;
            }
        }

        std::vector<unsigned char> finalize_to_buffer()
        {
            std::vector<unsigned char> result;

            finalize();

            zip_source_open(_source);
            zip_source_seek(_source, 0, SEEK_END);

            zip_int64_t sz = zip_source_tell(_source);
            result.resize((std::size_t)sz);

            zip_source_seek(_source, 0, SEEK_SET);
            zip_source_read(_source, result.data(), sz);
            zip_source_close(_source);

            return result;
        }

        std::vector<zip_entry> get_entries()
        {
            std::vector<zip_entry> result;

            for (zip_int64_t i = 0; i < zip_get_num_entries(_file, 0); i++)
            {
                std::string path = zip_get_name(_file, i, 0);
                result.emplace_back(
                    zip_entry{path.back() == '/' ? zip_entry_type::directory : zip_entry_type::file, path, i});
            }

            return result;
        }

        std::vector<unsigned char> get_file_contents(const zip_entry &entry)
        {
            auto fp = zip_fopen_index(_file, entry.index, 0);

            std::vector<unsigned char> contents;
            zip_fseek(fp, 0, SEEK_END);

            zip_int64_t sz = zip_source_tell(_source);
            contents.resize((std::size_t)sz);

            zip_fseek(fp, 0, SEEK_SET);
            zip_fread(fp, contents.data(), sz);

            zip_fclose(fp);

            return contents;
        }

        void unpack_to(const std::filesystem::path &to)
        {
            std::filesystem::create_directories(to);

            for (auto &entry : get_entries())
            {
                if (entry.type == zip_entry_type::directory) // Directory entry
                {
                    std::filesystem::create_directories(to / entry.path);
                }
                else
                {
                    auto contents = get_file_contents(entry);
                    futile::open(to / entry.path, "wb").write(contents);
                }
            }
        }

        zip_file(const zip_file &) = delete;

        zip_file(zip_file &&other)
        {
            _file = other._file;
            other._file = nullptr;
        }

        ~zip_file()
        {
            discard();

            if (_source)
                zip_source_free(_source);
        }

    private:
        zip_t *_file = nullptr;
        zip_source_t *_source = nullptr;
    };

    inline zip_file create_archive()
    {
        return zip_file{in_memory};
    }

    inline zip_file open_archive(const std::filesystem::path &from, open_mode mode = open_mode::read)
    {
        return zip_file{from, mode};
    }

    inline zip_file open_archive(ulib::span<unsigned char> from)
    {
        return zip_file{from};
    }

    inline void create_archive(const std::filesystem::path &from, const std::filesystem::path &out_zip)
    {
        zip_file zip{out_zip, open_mode::truncate};
        zip.add_directory(from, "");
        zip.finalize();
    }

    inline std::vector<unsigned char> create_archive(const std::filesystem::path &from)
    {
        zip_file zip{in_memory};
        zip.add_directory(from, "");
        return zip.finalize_to_buffer();
    }

    inline void unpack_archive(const std::filesystem::path &zip_path, const std::filesystem::path &out_path)
    {
        zip_file zip{zip_path, open_mode::read};
        zip.unpack_to(out_path);
    }
} // namespace cppzip
