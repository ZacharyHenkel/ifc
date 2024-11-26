// Copyright Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <ifc/file.hxx>

namespace ifc {
    // Verify integrity of ifc.  To do this we know that the header content after the hash
    // starts after the interface signature and the first 256 bits.
    constexpr size_t hash_start     = sizeof(InterfaceSignature);
    constexpr size_t contents_start = hash_start + sizeof(SHA256Hash);
    static_assert(contents_start == 36); // 4 bytes for Signature + 8*4 bytes for SHA2

    void InputIfc::validate_content_integrity(const InputIfc& file)
    {
        auto result          = generate_content_hash(file);
        auto actual_first    = reinterpret_cast<const uint8_t*>(result.value.data());
        auto actual_last     = actual_first + std::size(result.value) * 4;
        auto expected_first  = reinterpret_cast<const uint8_t*>(&file.contents()[hash_start]);
        auto expected_last   = expected_first + sizeof(SHA256Hash);
        if (not std::equal(actual_first, actual_last, expected_first, expected_last))
        {
            throw IntegrityCheckFailed{bytes_to_hash(expected_first, expected_last), result};
        }
    }

    SHA256Hash InputIfc::generate_content_hash(const InputIfc& file)
    {
        const auto& contents = file.contents();
        return hash_bytes(contents.data() + contents_start, contents.data() + contents.size());
    }

    void InputIfc::reset_content_hash()
    {
        auto result = generate_content_hash(*this);
        auto hash_location = &contents()[hash_start];
        memcpy(hash_location, result.value.data(), sizeof(SHA256Hash));
    }

} // namespace ifc
