#include <arch/x86_64/cpu.h>
#include <errno.h>
#include <mm/usercopy.h>
#include <mm/vmm.h>
#include <string.h>
#include <types.h>

#define USER_SPACE_END 0x0000800000000000ull

bool user_range_ok(uint64_t addr, uint64_t len, bool write)
{
    uint64_t end;

    if (len == 0) {
        return true;
    }
    if (addr >= USER_SPACE_END || len > USER_SPACE_END - addr) {
        return false;
    }

    end = addr + len;
    for (uint64_t page = addr & ~(PAGE_SIZE - 1); page < end;
         page += PAGE_SIZE) {
        if (!vmm_user_accessible(page, write)) {
            return false;
        }
    }
    return true;
}

int copy_from_user(void *dst, uint64_t src, size_t len)
{
    if (!user_range_ok(src, len, false)) {
        return -EFAULT;
    }
    usercopy_begin();
    memcpy(dst, (const void *)(uintptr_t)src, len);
    usercopy_end();
    return 0;
}

int copy_to_user(uint64_t dst, const void *src, size_t len)
{
    if (!user_range_ok(dst, len, true)) {
        return -EFAULT;
    }
    usercopy_begin();
    memcpy((void *)(uintptr_t)dst, src, len);
    usercopy_end();
    return 0;
}

int copy_user_string(char *dst, uint64_t src, size_t cap)
{
    if (cap == 0) {
        return -ENAMETOOLONG;
    }

    for (size_t i = 0; i < cap; i++) {
        char c;

        if (copy_from_user(&c, src + i, 1) != 0) {
            return -EFAULT;
        }
        dst[i] = c;
        if (c == '\0') {
            return 0;
        }
    }
    return -ENAMETOOLONG;
}
