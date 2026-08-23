#include <os/kernel/sync/runtime_mutex.hpp>

namespace os::kernel {

static_assert(sizeof(RuntimeMutex) >= sizeof(Mutex));

}
