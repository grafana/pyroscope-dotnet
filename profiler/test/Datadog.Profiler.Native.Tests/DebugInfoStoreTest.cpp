// Unless explicitly stated otherwise all files in this repository are licensed under the Apache 2 License.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2022 Datadog, Inc.

#include "gtest/gtest.h"

#include "DebugInfoStore.h"
#include "ProfilerMockedInterface.h"

#include "shared/src/native-src/dd_filesystem.hpp"
#include "shared/src/native-src/string.h"

#ifdef _WINDOWS
#include "..\Datadog.Profiler.Native.Windows\SymPdbParser.h"
#include "..\Datadog.Profiler.Native.Windows\DbgHelpParser.h"
#include <metahost.h>
#include <atlbase.h>
#endif

using ::testing::Return;
using ::testing::ReturnRef;

namespace
{
    // Helper function to get the current process executable path (cross-platform)
    bool GetCurrentProcessPath(std::string& outPath)
    {
#ifdef _WINDOWS
        char buffer[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        if (len == 0 || len == MAX_PATH)
        {
            return false;
        }
        outPath = std::string(buffer);
#else
        char buffer[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len == -1)
        {
            return false;
        }
        buffer[len] = '\0';
        outPath = std::string(buffer);
#endif
        return true;
    }

    // Helper function to build path to a sample PDB based on process location
    // Returns a pair of (pdbPath, modulePath)
    std::pair<fs::path, fs::path> GetSamplePdbPath(const std::string& sampleName, const std::string& targetFramework)
    {
        std::string processPath;
        if (!GetCurrentProcessPath(processPath))
        {
            return {};
        }

        // Build path relative to the process location: ../../src/Demos/{sampleName}/{targetFramework}/{sampleName}.pdb
        fs::path pdbPath = fs::path(processPath).parent_path() / ".." / ".." / "src" / "Demos" / sampleName / targetFramework / (sampleName + ".pdb");

        // Module is in the same directory with .exe extension
        fs::path modulePath = pdbPath.parent_path() / (sampleName + ".exe");

        return {pdbPath, modulePath};
    }
} // anonymous namespace

#ifdef _WINDOWS

// This test validates that SymPdbParser can parse symbols from a .NET Framework PDB (Windows PDB format).
// .NET Framework (net48) produces Windows PDB files that cannot be parsed as Portable PDBs.
TEST(DebugInfoStoreTest, ParseModuleDebugInfo_SymPdbParser)
{
    // Get paths to the sample PDB and module
    auto [pdbPath, modulePath] = GetSamplePdbPath("Samples.Computer01", "net48");

    if (pdbPath.empty())
    {
        GTEST_SKIP() << "Failed to get current process path";
        return;
    }

    std::error_code ec;
    if (!fs::exists(pdbPath, ec) || !fs::exists(modulePath, ec))
    {
        GTEST_SKIP() << "Samples.Computer01.pdb (net48) not found. Build the Samples.Computer01 project for net48 first.";
        return;
    }

    // Create a minimal mock configuration
    auto [configuration, mockConfiguration] = CreateConfiguration();
    EXPECT_CALL(mockConfiguration, IsDebugInfoEnabled()).WillRepeatedly(Return(true));

    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comInitialized = SUCCEEDED(hr);

    // Get IMetaDataImport from the module file for Windows PDB parsing
    CComPtr<ICLRMetaHost> pMetaHost;
    hr = CLRCreateInstance(CLSID_CLRMetaHost, IID_ICLRMetaHost, (void**)&pMetaHost);
    ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to create CLRMetaHost";

    // Get .NET Framework 4.0 runtime for metadata access
    CComPtr<ICLRRuntimeInfo> pRuntimeInfo;
    hr = pMetaHost->GetRuntime(L"v4.0.30319", IID_ICLRRuntimeInfo, (void**)&pRuntimeInfo);
    ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to get .NET Framework 4.0 runtime";

    CComPtr<IMetaDataDispenser> pMetaDataDispenser;
    hr = pRuntimeInfo->GetInterface(CLSID_CorMetaDataDispenser, IID_IMetaDataDispenser, (void**)&pMetaDataDispenser);
    ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to get IMetaDataDispenser";

    // Convert module path to wide string
    int len = MultiByteToWideChar(CP_UTF8, 0, modulePath.string().c_str(), -1, nullptr, 0);
    std::wstring wModulePath(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, modulePath.string().c_str(), -1, &wModulePath[0], len);

    // Open the module to get metadata
    CComPtr<IMetaDataImport> pMetaDataImport;
    hr = pMetaDataDispenser->OpenScope(wModulePath.c_str(), ofRead, IID_IMetaDataImport, (IUnknown**)&pMetaDataImport);
    ASSERT_TRUE(SUCCEEDED(hr)) << "Failed to open module metadata";

    // Parse the PDB
    ModuleDebugInfo moduleInfo;
    SymParser parser;
    bool success = parser.LoadPdbFile(pMetaDataImport, &moduleInfo, pdbPath.string(), modulePath.string());

    // COM cleanup is automatic via CComPtr
    if (comInitialized)
    {
        CoUninitialize();
    }

    // Validate that symbols were loaded
    // For .NET Framework PDB (Windows PDB format), the LoadingState should be Windows
    ASSERT_EQ(moduleInfo.LoadingState, SymbolLoadingState::Windows)
        << "Expected Windows PDB format for .NET Framework compilation (net48)";

    // Validate that debug info was populated
    ASSERT_FALSE(moduleInfo.Files.empty()) << "Expected at least one source file in debug info";
    ASSERT_FALSE(moduleInfo.RidToDebugInfo.empty()) << "Expected RID to debug info mapping for Windows PDB";

    // The first entry should be the empty string placeholder
    ASSERT_EQ(moduleInfo.Files[0], DebugInfoStore::NoFileFound);

    // Validate that we have actual source files loaded
    ASSERT_GT(moduleInfo.Files.size(), 1) << "Expected more than just the placeholder entry";

    // At least one file should reference a .cs source file
    bool foundCsFile = false;
    for (const auto& file : moduleInfo.Files)
    {
        if (file.find(".cs") != std::string::npos)
        {
            foundCsFile = true;
            break;
        }
    }
    ASSERT_TRUE(foundCsFile) << "Expected at least one .cs source file in debug info";

    // Validate that we have RID mappings
    ASSERT_GT(moduleInfo.RidToDebugInfo.size(), 1) << "Expected RID to debug info mappings";

    // Validate that at least some entries have valid line numbers
    bool foundValidLineNumber = false;
    for (const auto& debugInfo : moduleInfo.RidToDebugInfo)
    {
        if (debugInfo.StartLine > 0 && debugInfo.File != DebugInfoStore::NoFileFound)
        {
            foundValidLineNumber = true;
            break;
        }
    }
    ASSERT_TRUE(foundValidLineNumber) << "Expected at least one RVA with valid line number and source file";
}

// This test validates that DbgHelpParser can parse symbols from a .NET Framework PDB (Windows PDB format).
// DbgHelpParser uses the Windows DbgHelp API and doesn't require IMetaDataImport.
TEST(DebugInfoStoreTest, ParseModuleDebugInfo_DbgHelp)
{
    // Get paths to the sample PDB and module
    auto [pdbPath, modulePath] = GetSamplePdbPath("Samples.Computer01", "net48");

    if (pdbPath.empty())
    {
        GTEST_SKIP() << "Failed to get current process path";
        return;
    }

    std::error_code ec;
    if (!fs::exists(pdbPath, ec) || !fs::exists(modulePath, ec))
    {
        GTEST_SKIP() << "Samples.Computer01.pdb (net48) not found. Build the Samples.Computer01 project for net48 first.";
        return;
    }

    // Parse the PDB using DbgHelpParser
    ModuleDebugInfo moduleInfo;
    DbgHelpParser parser;
    bool success = parser.LoadPdbFile(&moduleInfo, pdbPath.string());

    ASSERT_TRUE(success) << "Failed to load PDB file with DbgHelpParser";

    // Validate that symbols were loaded
    // For .NET Framework PDB (Windows PDB format), the LoadingState should be Windows
    ASSERT_EQ(moduleInfo.LoadingState, SymbolLoadingState::Windows)
        << "Expected Windows PDB format for .NET Framework compilation (net48)";

    // Validate that debug info was populated
    ASSERT_FALSE(moduleInfo.Files.empty()) << "Expected at least one source file in debug info";
    ASSERT_FALSE(moduleInfo.RidToDebugInfo.empty()) << "Expected RID to debug info mapping for Windows PDB";

    // The first entry should be the empty string placeholder
    ASSERT_EQ(moduleInfo.Files[0], DebugInfoStore::NoFileFound);

    // Validate that we have actual source files loaded
    ASSERT_GT(moduleInfo.Files.size(), 1) << "Expected more than just the placeholder entry";

    // At least one file should reference a .cs source file
    bool foundCsFile = false;
    for (const auto& file : moduleInfo.Files)
    {
        if (file.find(".cs") != std::string::npos)
        {
            foundCsFile = true;
            break;
        }
    }
    ASSERT_TRUE(foundCsFile) << "Expected at least one .cs source file in debug info";

    // Validate that we have RID mappings
    ASSERT_GT(moduleInfo.RidToDebugInfo.size(), 1) << "Expected RID to debug info mappings";

    // Validate that at least some entries have valid line numbers
    bool foundValidLineNumber = false;
    for (const auto& debugInfo : moduleInfo.RidToDebugInfo)
    {
        if (debugInfo.StartLine > 0 && debugInfo.File != DebugInfoStore::NoFileFound)
        {
            foundValidLineNumber = true;
            break;
        }
    }
    ASSERT_TRUE(foundValidLineNumber) << "Expected at least one RID with valid line number and source file";
}

#endif // _WINDOWS


// Additional test to verify that .NET Core/5+ PDBs are Portable format
TEST(DebugInfoStoreTest, ParseModuleDebugInfo_NetCorePortable)
{
    // Get paths to the sample PDB and module
    auto [pdbPath, modulePath] = GetSamplePdbPath("Samples.BuggyBits", "net10.0");
    if (pdbPath.empty())
    {
        GTEST_SKIP() << "Failed to get current process path";
        return;
    }

    std::error_code ec;
    if (!fs::exists(pdbPath, ec) || !fs::exists(modulePath, ec))
    {
        GTEST_SKIP() << "Samples.BuggyBits.pdb (net10.0) not found. This is expected if net10.0 is not compiled.";
        return;
    }

    ModuleDebugInfo moduleInfo;

    auto [configuration, mockConfiguration] = CreateConfiguration();
    EXPECT_CALL(mockConfiguration, IsDebugInfoEnabled()).WillRepeatedly(Return(true));

    DebugInfoStore debugInfoStore(nullptr, configuration.get());

    // For Portable PDB, we don't need IMetaDataImport, so pass 0 as moduleId
    debugInfoStore.ParseModuleDebugInfo(0, pdbPath.string(), modulePath.string(), moduleInfo);

    // For .NET Core/5+ PDB (Portable PDB format), the LoadingState should be Portable
    ASSERT_EQ(moduleInfo.LoadingState, SymbolLoadingState::Portable)
        << "Expected Portable PDB format for .NET Core/5+ compilation (net10.0)";

    // Portable PDBs use RID-based lookup
    ASSERT_FALSE(moduleInfo.RidToDebugInfo.empty()) << "Expected RID to debug info mapping for Portable PDB";
    ASSERT_FALSE(moduleInfo.Files.empty()) << "Expected source files in debug info";
}

// Reproduces the stale-iterator-after-insertion pattern from DebugInfoStore::Get().
// In the original code, find() returns an iterator, then ParseModuleDebugInfo() inserts
// into the same unordered_map, potentially causing a rehash that invalidates all iterators.
// The old iterator was then reused — undefined behavior per the C++ standard.
//
// This test uses a standalone unordered_map to demonstrate that rehash does occur and
// that re-acquiring the iterator after insertion is the correct fix.
// Under ASAN, if the rehash frees the old bucket array, using the stale iterator
// would access freed memory and be caught.
TEST(DebugInfoStoreTest, StaleIteratorAfterInsert_RehashInvalidatesIterators)
{
    // Use the same types as DebugInfoStore::_modulesInfo
    std::unordered_map<ModuleID, ModuleDebugInfo> map;

    // Fill the map to just below the rehash threshold so one more insert triggers rehash.
    // max_load_factor() defaults to 1.0, and initial bucket_count() is implementation-defined.
    // We force a known state by reserving exactly 1 bucket, then filling to capacity.
    map.rehash(1);
    auto bucketsBefore = map.bucket_count();
    auto threshold = static_cast<size_t>(bucketsBefore * map.max_load_factor());

    // Fill up to exactly the threshold (the point where one more insert triggers rehash)
    for (size_t i = 0; i < threshold; ++i)
    {
        map[static_cast<ModuleID>(i)] = ModuleDebugInfo{};
    }

    // Use a key that's not in the map yet
    ModuleID missingKey = static_cast<ModuleID>(threshold + 1000);
    auto it = map.find(missingKey);
    ASSERT_EQ(it, map.cend()) << "Key should not exist yet";

    auto bucketsBeforeInsert = map.bucket_count();

    // This insertion mirrors what ParseModuleDebugInfo does: map[missingKey] = ...
    map[missingKey] = ModuleDebugInfo{};

    auto bucketsAfterInsert = map.bucket_count();

    // Verify that rehash actually occurred — this is the precondition for the UB
    // If bucket_count changed, all prior iterators (including `it`) are invalidated.
    // NOTE: If this assertion fails, increase the fill count or adjust rehash(1) above.
    ASSERT_NE(bucketsBeforeInsert, bucketsAfterInsert)
        << "Expected rehash to occur after insertion (bucket count should change)";

    // The FIX: after insertion, re-acquire the iterator. This must find the element.
    auto it2 = map.find(missingKey);
    ASSERT_NE(it2, map.cend()) << "Re-acquired iterator must find the just-inserted element";
}
