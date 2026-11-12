#include <compiler.h>
#include <elf.h>
#include <errno.h>
#include <kio.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <string.h>
#include <types.h>
#include <loader.h>
#include <vfs.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

#define USER_MIN_ADDR  0x10000ull
#define USER_SPACE_END 0x0000800000000000ull

#define MAX_ELF_SIZE (256 * 1024)
#define MAX_PHNUM    16

static inline void *phys_to_virt(uint64_t phys)
{
    return (void *)(uintptr_t)(PHYS_MAP_BASE + phys);
}

static int load_segment(const struct elf64_phdr *ph, const uint8_t *file,
                        uint64_t file_size)
{
    uint64_t vstart;
    uint64_t vend;

    if (ph->filesz > ph->memsz || ph->filesz > file_size ||
        ph->offset > file_size - ph->filesz) {
        return -EINVAL;
    }
    if (ph->vaddr + ph->memsz < ph->vaddr) {
        return -EINVAL;
    }

    vstart = ph->vaddr & ~(PAGE_SIZE - 1);
    vend = (ph->vaddr + ph->memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (vstart < USER_MIN_ADDR || vend > USER_SPACE_END) {
        return -EINVAL;
    }

    for (uint64_t page = vstart; page < vend; page += PAGE_SIZE) {
        uint64_t copy_start = page > ph->vaddr ? page : ph->vaddr;
        uint64_t copy_end = page + PAGE_SIZE;
        uint64_t segment_end = ph->vaddr + ph->filesz;
        uint8_t *target;
        uint64_t frame;

        if (copy_end > segment_end) {
            copy_end = segment_end;
        }

        frame = pmm_alloc_frame();
        if (frame == 0) {
            return -ENOMEM;
        }
        target = phys_to_virt(frame);
        memset(target, 0, PAGE_SIZE);

        if (copy_end > copy_start) {
            uint64_t src = ph->offset + (copy_start - ph->vaddr);
            uint64_t dst = copy_start - page;

            memcpy(target + dst, file + src, copy_end - copy_start);
        }

        if (vmm_map(page, frame, VMM_USER_FLAGS) != 0) {
            pmm_free_frame(frame);
            return -ENOMEM;
        }
    }

    return 0;
}

int elf_load(const char *path, uint64_t *entry_out)
{
    struct elf64_ehdr eh;
    struct elf64_phdr phdrs[MAX_PHNUM];
    uint8_t *file;
    int64_t size;
    int fd;
    int rc;

    if (!path || !entry_out) {
        return -EINVAL;
    }

    fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        return fd;
    }
    file = kmalloc(MAX_ELF_SIZE);
    if (!file) {
        fd_close(fd);
        return -ENOMEM;
    }

    size = fd_read(fd, file, MAX_ELF_SIZE);
    fd_close(fd);
    if (size < 0) {
        kfree(file);
        return (int)size;
    }

    if ((uint64_t)size < sizeof(eh)) {
        kfree(file);
        return -ENOEXEC;
    }

    memcpy(&eh, file, sizeof(eh));
    if (memcmp(eh.ident, "\x7f" "ELF", 4) != 0 ||
        eh.ident[4] != ELFCLASS64 || eh.ident[5] != ELFDATA2LSB ||
        eh.type != ET_EXEC || eh.machine != EM_X86_64 ||
        eh.ehsize != sizeof(eh) || eh.phentsize != sizeof(struct elf64_phdr) ||
        eh.phnum == 0 || eh.phnum > MAX_PHNUM ||
        eh.phoff > (uint64_t)size ||
        eh.phnum * sizeof(struct elf64_phdr) > (uint64_t)size - eh.phoff) {
        kfree(file);
        return -ENOEXEC;
    }

    memcpy(phdrs, file + eh.phoff, eh.phnum * sizeof(struct elf64_phdr));

    if (eh.entry < USER_MIN_ADDR || eh.entry >= USER_SPACE_END) {
        kfree(file);
        return -ENOEXEC;
    }

    for (uint16_t i = 0; i < eh.phnum; i++) {
        if (phdrs[i].type != PT_LOAD || phdrs[i].memsz == 0) {
            continue;
        }
        rc = load_segment(&phdrs[i], file, (uint64_t)size);
        if (rc < 0) {
            kfree(file);
            return rc;
        }
    }

    kfree(file);
    *entry_out = eh.entry;
    return 0;
}
