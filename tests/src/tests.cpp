#include <cppzip/cppzip.hpp>
#include <gtest/gtest.h>

#include <futile/futile.h>

TEST(cppzip, CanOpenZipFiles)
{
    std::filesystem::path kDataFolder = "../../../../tests/data";

    const auto test1_zip = kDataFolder / "test1.zip";
    const auto nonexistent_zip = kDataFolder / "nonexistent.zip";

    EXPECT_NO_THROW(cppzip::zip_file{test1_zip});
    EXPECT_THROW(cppzip::zip_file{nonexistent_zip}, cppzip::zip_error);
}

TEST(cppzip, CanCreateInMemoryZipFiles)
{
    cppzip::zip_file zip{cppzip::in_memory};

    std::filesystem::path kDataFolder = "../../../../tests/data";

    EXPECT_NO_THROW(zip.create_directory("stuff"));
    EXPECT_NO_THROW(zip.create_directory("test"));

    EXPECT_NO_THROW(zip.add_directory(kDataFolder / "contents", ""));

    auto saved = zip.finalize_to_buffer();

    EXPECT_NO_THROW(futile::open("./created.zip", "wb").write(saved));

    auto reopened_archive = cppzip::open_archive(saved);
    EXPECT_NO_THROW(reopened_archive.discard());
}

TEST(cppzip, CanCreateFsZipFiles)
{
    cppzip::zip_file zip{"./created_as_file.zip", cppzip::open_mode::truncate};

    std::filesystem::path kDataFolder = "../../../../tests/data";

    EXPECT_NO_THROW(zip.create_directory("stuff"));
    EXPECT_NO_THROW(zip.create_directory("test"));

    EXPECT_NO_THROW(zip.add_directory(kDataFolder / "contents", ""));

    EXPECT_NO_THROW(zip.finalize());
}

TEST(cppzip, CanUnpackZipFiles)
{
    cppzip::zip_file zip{"./created_as_file.zip", cppzip::open_mode::read};

    EXPECT_NO_THROW(zip.unpack_to("./unpacked"));
}

TEST(cppzip, CanPackZipFilesShortForm)
{
    EXPECT_NO_THROW(cppzip::create_archive("../../../../tests/data/contents", "./created_as_file_2.zip"));
}

TEST(cppzip, CanUnpackZipFilesShortForm)
{
    EXPECT_NO_THROW(cppzip::unpack_archive("./created_as_file_2.zip", "./unpacked-v2"));
}
