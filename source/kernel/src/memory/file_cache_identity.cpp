#include <os/kernel/memory/file_cache_identity.hpp>

namespace os::kernel {

bool FileCacheIdentitiesEqual(const FileCacheIdentity &left,
                              const FileCacheIdentity &right) noexcept {
    return left.superblock_identifier == right.superblock_identifier &&
           left.superblock_generation == right.superblock_generation &&
           left.node_identifier == right.node_identifier &&
           left.node_generation == right.node_generation;
}

bool FileCacheIdentityIsValid(const FileCacheIdentity &identity) noexcept {
    return identity.superblock_identifier != 0ULL && identity.superblock_generation != 0ULL &&
           identity.node_identifier != UINT64_MAX && identity.node_generation != 0ULL;
}

}
