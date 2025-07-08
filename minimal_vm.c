// minimal-kvm.c
// 編譯: gcc minimal-kvm.c -o minimal-kvm
// 執行: sudo ./minimal-kvm

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define uint64_t unsigned long long

#define KVM_DEV "/dev/kvm"
#define GUEST_MEM_SIZE 0x1000

// 簡單的 guest 代碼: hlt 指令(0xf4) + 無限迴圈
static unsigned char guest_code[] = {
    0xb8, 0x2a, 0x00,       // mov ax, 0x2a
    0xbb, 0x01, 0x00,       // mov bx, 0x1
    0x01, 0xd8,             // add ax, bx
    0xf4,       // hlt
    0xeb, 0xfe  // jmp $
};

int main() {
    int kvm, vm, vcpu;
    int ret;
    struct kvm_run *run;
    void *mem;

    // 1. 開啟 /dev/kvm
    kvm = open(KVM_DEV, O_RDWR);
    if (kvm < 0) {
        perror("open /dev/kvm");
        exit(1);
    }

    // 2. 建立虛擬機
    vm = ioctl(kvm, KVM_CREATE_VM, 0);
    if (vm < 0) {
        perror("KVM_CREATE_VM");
        exit(1);
    }

    // 3. 建立客體記憶體
    mem = mmap(NULL, GUEST_MEM_SIZE, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap guest mem");
        exit(1);
    }

    // 4. 把 guest code 複製進去 guest memory
    memcpy(mem, guest_code, sizeof(guest_code));

    // 5. 設定 guest memory 到 vm
    struct kvm_userspace_memory_region mem_region = {
        .slot = 0,
        .guest_phys_addr = 0x0,
        .memory_size = GUEST_MEM_SIZE,
        .userspace_addr = (uint64_t)mem,
    };

    ret = ioctl(vm, KVM_SET_USER_MEMORY_REGION, &mem_region);
    if (ret < 0) {
        perror("KVM_SET_USER_MEMORY_REGION");
        exit(1);
    }

    // 6. 建立 vCPU
    vcpu = ioctl(vm, KVM_CREATE_VCPU, 0);
    if (vcpu < 0) {
        perror("KVM_CREATE_VCPU");
        exit(1);
    }

    // 7. 映射 vCPU 的共享記憶體區（kvm_run 結構）
    int vcpu_mmap_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (vcpu_mmap_size < 0) {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        exit(1);
    }
    run = mmap(NULL, vcpu_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu, 0);
    if (run == MAP_FAILED) {
        perror("mmap vcpu_run");
        exit(1);
    }

    // 8. 設定 vCPU 寄存器 (x86_64 範例)
    struct kvm_regs regs;
    ret = ioctl(vcpu, KVM_GET_REGS, &regs);
    if (ret < 0) {
        perror("KVM_GET_REGS");
        exit(1);
    }
    regs.rip = 0x0;  // 指令從 guest memory 0x0 開始執行
    regs.rflags = 0x2;
    regs.rax = 0x0;
    regs.rbx = 0x0;
    ret = ioctl(vcpu, KVM_SET_REGS, &regs);
    if (ret < 0) {
        perror("KVM_SET_REGS");
        exit(1);
    }

    /* FIXME: HINT: cs.base to set code section base address (?) */
    struct kvm_sregs sregs;
    ioctl(vcpu, KVM_GET_SREGS, &sregs);
    sregs.cs.base = 0;
    sregs.cs.selector = 0;
    ioctl(vcpu, KVM_SET_SREGS, &sregs);


    // 9. 啟動 vCPU 執行 guest
    while (1) {
        ret = ioctl(vcpu, KVM_RUN, 0);
        if (ret < 0) {
            perror("KVM_RUN");
            exit(1);
        }

        switch (run->exit_reason) {
            case KVM_EXIT_HLT:
                printf("KVM_EXIT_HLT\n");
                ret = ioctl(vcpu, KVM_GET_REGS, &regs);
                if (ret < 0) {
                    perror("KVM_GET_REGS");
                    exit(1);
                }
                printf("rax=0x%llx rbx=0x%llx\n", regs.rax, regs.rbx);
                goto cleanup;
            case KVM_EXIT_IO:
                printf("KVM_EXIT_IO\n");
                break;
            case KVM_EXIT_FAIL_ENTRY:
                printf("KVM_EXIT_FAIL_ENTRY\n");
                goto cleanup;
            case KVM_EXIT_INTERNAL_ERROR:
                printf("KVM_EXIT_INTERNAL_ERROR\n");
                goto cleanup;
            default:
                printf("Unhandled exit reason: %d\n", run->exit_reason);
                goto cleanup;
        }
    }

cleanup:
    munmap(run, vcpu_mmap_size);
    munmap(mem, GUEST_MEM_SIZE);
    close(vcpu);
    close(vm);
    close(kvm);
    return 0;
}