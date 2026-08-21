#pragma once

#include <stdint.h>

namespace os::kernel {

struct FileCacheIdentity final {
    uint64_t superblock_identifier;
    uint64_t superblock_generation;
    uint64_t node_identifier;
    uint64_t node_generation;
};

[[nodiscard]] bool FileCacheIdentitiesEqual(const FileCacheIdentity &left,
                                            const FileCacheIdentity &right) noexcept;
[[nodiscard]] bool FileCacheIdentityIsValid(const FileCacheIdentity &identity) noexcept;

}
