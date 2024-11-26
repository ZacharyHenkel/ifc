// Copyright Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <windows.h>

#include "gsl/span"
#include "ifc/abstract-sgraph.hxx"
#include "ifc/file.hxx"

void translate_exception()
{
    try
    {
        throw;
    }
    catch (const ifc::IfcArchMismatch&)
    {
        std::cerr << "ifc architecture mismatch\n";
    }
    catch (const char* message)
    {
        std::cerr << "caught: " << message;
    }
    catch (...)
    {
        std::cerr << "unknown exception caught\n";
    }
}

using namespace std::literals;

struct Arguments {
    // Files to process.
    std::vector<std::string> files;
};

void print_help(std::filesystem::path path)
{
    auto name = path.stem().string();
    std::cout << "Usage:\n\n";
    std::cout << name << " ifc-file1 [ifc-file2 ...] [--color/-c]\n";
    std::cout << name << " --help/-h\n";
}

Arguments process_args(int argc, char** argv)
{
    Arguments result;
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == "--help"sv or argv[i] == "-h"sv)
        {
            print_help(argv[0]);
            exit(0);
        }

        else if (argv[i][0] != '-')
        {
            result.files.push_back(argv[i]);
        }
        else
        {
            std::cout << "Unknown command line argument '" << argv[i] << "'\n";
            print_help(argv[0]);
            std::exit(1);
        }
    }

    if (result.files.empty())
    {
        std::cout << "Specify filepath of an ifc file\n";
        print_help(argv[0]);
        std::exit(1);
    }

    return result;
}

std::vector<std::byte> load_file(const std::string& name)
{
    std::filesystem::path path{name};
    auto size = std::filesystem::file_size(path);
    std::vector<std::byte> v;
    v.resize(size);
    std::ifstream file(name, std::ios::binary);
    file.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(v.size()));
    return v;
}

class HandleWrapper {
public:
    explicit HandleWrapper(HANDLE handle_) : handle(handle_) {}
    ~HandleWrapper()
    {
        if (handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
    }
    HANDLE get() const
    {
        return handle;
    }

private:
    HANDLE handle;
};

gsl::span<std::byte> memory_map_file(const std::string& file_path)
{
    HandleWrapper file_handle(CreateFileA(file_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file_handle.get() == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error("Failed to open file");
    }

    DWORD file_size = GetFileSize(file_handle.get(), nullptr);
    if (file_size == INVALID_FILE_SIZE)
    {
        throw std::runtime_error("Failed to determine file size");
    }

    HandleWrapper file_mapping(CreateFileMappingA(file_handle.get(), nullptr, PAGE_READWRITE, 0, 0, nullptr));
    if (file_mapping.get() == nullptr)
    {
        throw std::runtime_error("Failed to create file mapping");
    }

    void* mapped_data = MapViewOfFile(file_mapping.get(), FILE_MAP_WRITE, 0, 0, 0);
    if (mapped_data == nullptr)
    {
        throw std::runtime_error("Failed to map view of file");
    }

    return gsl::span<std::byte>(reinterpret_cast<std::byte*>(mapped_data), file_size);
}

constexpr std::string_view srcrootPrefix = R"(SRC_PARENTsrc\)";
constexpr std::string_view publicPath    = R"(\public\)";
constexpr std::string_view icachePrefix  = R"(ICACHECUR\)";
constexpr std::string_view icacheSuffix  = R"(\src)";
constexpr std::string_view sourceFilePartitionName  = "name.source-file";

void process_ifc(const std::string& name)
{
    auto contents = memory_map_file(name);

    ifc::InputIfc ifcFile{contents};
    ifc::Pathname path{name.c_str()};
    ifcFile.validate<ifc::UnitSort::Primary>(path, ifc::Architecture::Unknown, ifc::Pathname{},
                                          ifc::IfcOptions::IntegrityCheck);

    const auto& partitionTable = ifcFile.partition_table();
    auto sourceFilePartitionData =
        std::find_if(begin(partitionTable), end(partitionTable),
                     [&,ifcFile](const auto& entry) {
                                                    return sourceFilePartitionName.compare(ifcFile.get(entry.name)) == 0;
    });
    if (sourceFilePartitionData == end(partitionTable))
        return;

    ifcFile.position(sourceFilePartitionData->offset);
    auto sourceFiles = ifcFile.read_array<ifc::symbolic::SourceFileName>(sourceFilePartitionData->cardinality);
    for (auto& file : sourceFiles)
    {
        if (std::to_underlying(file.name) != 0)
        {
            std::string sourceFileStr = ifcFile.get(file.name);
            if (sourceFileStr.starts_with(srcrootPrefix))
            {
                auto idx = sourceFileStr.find(publicPath);
                if (idx != std::string::npos)
                {
                    std::string projectName =
                        sourceFileStr.substr(srcrootPrefix.size(), idx - srcrootPrefix.size());
                    auto pathSep = projectName.find(R"(\)");
                    if (pathSep != std::string::npos)
                        projectName[pathSep] = '_';
                    std::string replacement{icachePrefix};
                    replacement += projectName;
                    replacement += icacheSuffix;
                    sourceFileStr.replace(0, replacement.size(), replacement);
                    ifcFile.set(file.name, sourceFileStr);
                }
            }
        }
        ifcFile.reset_content_hash();
    }
}

int main(int argc, char** argv)
{
    Arguments arguments = process_args(argc, argv);

    try
    {
        for (const auto& file : arguments.files)
            process_ifc(file);
    }
    catch (...)
    {
        translate_exception();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
