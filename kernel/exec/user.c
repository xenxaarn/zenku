#include <arch/x86_64/gdt.h>
#include <compiler.h>
#include <elf.h>
#include <errno.h>
#include <kio.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <string.h>
#include <types.h>
#include <user.h>
#include <vfs.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ull

/* User programs are linked at 4 MiB; the stack sits near the top of the
 * lower canonical half. */
#define USER_STACK_TOP   0x00007FFFFFF00000ull
#define USER_STACK_PAGES 4
#define USER_MIN_ADDR    0x10000ull
#define USER_SPACE_END   0x0000800000000000ull

#define MAX_ELF_SIZE (256 * 1024)
#define MAX_IMAGE_PAGES 128
#define USER_PHNUM_MAX 16

#define SYSCALL_STACK_SIZE 16384

static uint8_t g_syscall_stack[SYSCALL_STACK_SIZE] ALIGNED(16);

static uint64_t g_pages[MAX_IMAGE_PAGES];
static uint64_t g_page_frames[MAX_IMAGE_PAGES];
static unsigned g_page_count;

extern void user_enter(uint64_t entry, uint64_t stack);
extern void user_exit_to_kernel(void) NORETURN;

static inline void *phys_to_virt(uint64_t phys)
{
    return (void *)(uintptr_t)(PHYS_MAP_BASE + phys);
}

static void image_reset(void)
{
    for (unsigned i = 0; i < g_page_count; i++) {
        vmm_unmap(g_pages[i]);
        pmm_free_frame(g_page_frames[i]);
    }
    g_page_count = 0;
}

static int image_map(uint64_t virt, uint64_t frame)
{
    if (g_page_count >= MAX_IMAGE_PAGES) {
        return -ENOMEM;
    }
    if (vmm_map(virt, frame, VMM_USER_FLAGS) != 0) {
        return -ENOMEM;
    }

    g_pages[g_page_count] = virt;
    g_page_frames[g_page_count] = frame;
    g_page_count++;
    return 0;
}

static int map_page(uint64_t virt)
{
    uint64_t frame = pmm_alloc_frame();
    int rc;

    if (frame == 0) {
        return -ENOMEM;
    }
    memset(phys_to_virt(frame), 0, PAGE_SIZE);

    rc = image_map(virt, frame);
    if (rc < 0) {
        pmm_free_frame(frame);
    }
    return rc;
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
        int rc;

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

        rc = image_map(page, frame);
        if (rc < 0) {
            pmm_free_frame(frame);
            return rc;
        }
    }

    return 0;
}

static int load_elf(const char *path, uint64_t *entry_out)
{
    struct elf64_ehdr eh;
    struct elf64_phdr phdrs[USER_PHNUM_MAX];
    uint8_t *file;
    int64_t size;
    int fd;
    int rc = 0;

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
        eh.phnum == 0 || eh.phnum > USER_PHNUM_MAX ||
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
            image_reset();
            return rc;
        }
    }

    kfree(file);

    for (unsigned i = 0; i < USER_STACK_PAGES; i++) {
        uint64_t virt = USER_STACK_TOP - (uint64_t)(i + 1) * PAGE_SIZE;

        rc = map_page(virt);
        if (rc < 0) {
            image_reset();
            return rc;
        }
    }

    *entry_out = eh.entry;
    return 0;
}

int user_exec(const char *path)
{
    uint64_t entry = 0;
    int console_in;
    int console_out;
    int console_err;
    int rc;

    image_reset();
    rc = load_elf(path, &entry);
    if (rc < 0) {
        kprintf("user: cannot load %s (%d)\n", path, rc);
        return rc;
    }

    tss_set_rsp0((uint64_t)(uintptr_t)(g_syscall_stack +
                                       sizeof(g_syscall_stack)));

    console_in = vfs_open("/dev/console", O_RDONLY);
    console_out = vfs_open("/dev/console", O_WRONLY);
    console_err = vfs_open("/dev/console", O_WRONLY);
    if (console_in < 0 || console_out < 0 || console_err < 0) {
        kprintf("user: cannot set up stdio\n");
        image_reset();
        return -ENODEV;
    }

    kprintf("user: entering ring 3 at 0x%llx (stack 0x%llx, %u pages)\n",
            entry, USER_STACK_TOP, g_page_count);

    user_enter(entry, USER_STACK_TOP);

    kprintf("user: %s returned to the kernel\n", path);
    for (int fd = 0; fd < VFS_MAX_FDS; fd++) {
        fd_close(fd);
    }
    image_reset();
    return 0;
}
