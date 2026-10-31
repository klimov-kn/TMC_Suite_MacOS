/// @file sysmemory.cpp
/// @brief Запрос объёма памяти у системы (`sysmem::query`).
///
/// Часть модуля «Редактор заданий TPL Designer», слой интерфейса. Единственный файл
/// `src/gui` с ветками по операционным системам.
/// @see https://www.tamic.ru/docs/code/tpl-designer.html#gui-memory
#include "sysmemory.h"

#  include <mach/mach.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>

namespace sysmem {

Info query()
{
    Info r;
    uint64_t total = 0;
    size_t len = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &len, nullptr, 0) == 0 && total > 0) {
        vm_statistics64_data_t vm;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        vm_size_t page = 0;
        if (host_page_size(mach_host_self(), &page) == KERN_SUCCESS
            && host_statistics64(mach_host_self(), HOST_VM_INFO64,
                                 reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS) {
            r.ok = true;
            r.total = double(total);
            // Как «доступно» у Монитора системы: свободные, неактивные и
            // сбрасываемые страницы система отдаст расчёту без подкачки.
            r.available = double(vm.free_count + vm.inactive_count + vm.purgeable_count
                                 + vm.speculative_count) * double(page);
        }
    }
    if (r.available > r.total)
        r.available = r.total;
    return r;
}

} // namespace sysmem
