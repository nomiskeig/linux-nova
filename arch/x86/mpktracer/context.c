

#include "context.h"
#include "logging.h"
#include "patcher.h"
#include "register.h"
#include "regs.h"
#include "trampoline.h"
#ifdef TRACER_USERSPACE
#include <string.h>
#endif

static TracerRegister get_free_reg(used_regs used, used_regs modified) {
    TracerRegister reg = R15;
    while (used & reg || modified & reg || reg == RSP) {
        reg = reg >> 1;
        // TODO: this could theorically not find a register
    }
    return reg;
}
int patch_create_context(long address, AddressReg valueReg, used_regs used,
                         int include_load, int for_trampoline,
                         used_regs modified, long *store_at_end,
                         ContextInfo *info, used_regs used_registers) {
    // TODO: this should probably not happen from the trampoline

    int current_offset = 0;
    used |= used_registers;

    if (include_load) {
        // load the mmx registers

        // patch the value address, load it from rsi
        ((char *)(address + current_offset))[0] = 0x49;
        ((char *)(address + current_offset))[1] = 0x89;

        switch (valueReg) {
        case ADDRESS_R15:
            ((char *)(address + current_offset))[2] = 0xF7;
            break;
        case ADDRESS_R14:
            ((char *)(address + current_offset))[2] = 0xF6;
            break;
        case ADDRESS_R13:
            ((char *)(address + current_offset))[2] = 0xF5;
            break;
        case ADDRESS_R12:
            ((char *)(address + current_offset))[2] = 0xF4;
            break;
        case ADDRESS_R11:
            ((char *)(address + current_offset))[2] = 0xF3;
            break;
        }
        current_offset += 3;
        // mov address of regs into rax
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x89;
        ((char *)(address + current_offset))[2] = 0xF8;
        current_offset += 3;
    }
    if (used & FLAGS) {
        int index = TRACER_REG_FLAGS;
        // decrement the stack pointer by 8, mov the value to temp r12 ( it gets
        // restored anyways), then from r12 to [rsp], then popfq

        // sub rsp, 0x8
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x83;
        ((char *)(address + current_offset))[2] = 0xec;
        ((char *)(address + current_offset))[3] = 0x08;

        // mov r12, rax + index * 8
        ((char *)(address + current_offset))[4] = 0x4c;
        ((char *)(address + current_offset))[5] = 0x8b;
        ((char *)(address + current_offset))[6] = 0xa0;
        // the following line represents the state of my brain
        *((int *)&((char *)(address + current_offset))[7]) = 8 * index;
        // mov [rsp], r12

        ((char *)(address + current_offset))[11] = 0x4c;
        ((char *)(address + current_offset))[12] = 0x89;
        ((char *)(address + current_offset))[13] = 0x24;
        ((char *)(address + current_offset))[14] = 0x24;
        // popfq

        ((char *)(address + current_offset))[15] = 0x9d;
#ifdef TRACER_LOG_ERROR
        used -= FLAGS;
#endif
        current_offset += 16;
    }
    // restore the used registers
    if (used & RSP && for_trampoline) {
        // we mov the old value to the location of rsp, which is located at the
        // start of the trampoline, there are 8 bytes
        // we do it before all other registes so that we can a register as temp,
        // if the register is relevant, it will be restored afterwards anyways
        int index = TRACER_REG_RSP;
        // load the address of locatoin of rsp into r15
        // mov r15, imm
        //
        /*((char *)(address + current_offset))[0] = 0x49;
        ((char *)(address + current_offset))[1] = 0xbf;
        *((long *)&(((char *)(address + current_offset))[2])) =
            (long)&store_at_end[OLD_RSP_INDEX];
*/
        /*((char *)(address + current_offset))[0] = 0x90;
        ((char *)(address + current_offset))[1] = 0x90;
        ((char *)(address + current_offset))[2] = 0x90;
        ((char *)(address + current_offset))[3] = 0x90;
        ((char *)(address + current_offset))[4] = 0x90;
        ((char *)(address + current_offset))[5] = 0x90;
        ((char *)(address + current_offset))[6] = 0x90;
        ((char *)(address + current_offset))[7] = 0x90;
        ((char *)(address + current_offset))[8] = 0x90;
        ((char *)(address + current_offset))[9] = 0x90;

        */
        // mov [r15], rsp
        //((char *)(address + current_offset))[10] = 0x90;
        //((char *)(address + current_offset))[11] = 0x90;
        //((char *)(address + current_offset))[12] = 0x90;
        /*((char *)(address + current_offset))[10] = 0x49;
        ((char *)(address + current_offset))[11] = 0x89;
        ((char *)(address + current_offset))[12] = 0x27;
                */
        //((char *)(address + current_offset))[12] = 0x3f;

        // we search an unused and unmodified register (and we just assume that
        // we can always find one)
        TracerRegister rsp_temp = get_free_reg(used, modified);
        info->reg_for_rsp = rsp_temp;
        current_offset +=
            patch_reg_mov((char *)(address + current_offset), RSP, rsp_temp);

        // mov rsp, [rax + index]
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x60;
        ((char *)(address + current_offset))[3] = (index) * 8;

#ifdef TRACER_LOG_ERROR
        used -= RSP;
#endif
        current_offset += 4;
    }
    if (used & RSP && !for_trampoline) {
        // we store rsp at the beginning of the buffer (this is fine since there
        // is one buffer for each thread) mov [rip + offset], rsp

        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x89;
        ((char *)(address + current_offset))[2] = 0x25;
        // add 7 because we need to calculate the end of the instruction
        int offset = (long)store_at_end + 8 * OLD_RSP_INDEX -
                     (address + current_offset + 7);
        *((int *)(address + current_offset + 3)) = offset;
        // load rsp
        // mov rsp, [rax + offset]
        int index = TRACER_REG_RSP;
        ((char *)(address + current_offset))[7] = 0x48;
        ((char *)(address + current_offset))[8] = 0x8b;
        ((char *)(address + current_offset))[9] = 0xa0;
        *((int *)(address + current_offset + 10)) = index * 8;

#ifdef TRACER_LOG_ERROR
        used -= RSP;
#endif
        current_offset += 14;
    }
    if (used & R12) {
        int index = TRACER_REG_R12;
        ((char *)(address + current_offset))[0] = 0x4C;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x60;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= R12;
#endif
        current_offset += 4;
    }
    if (used & RDX) {
        int index = TRACER_REG_RDX;
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x50;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= RDX;
#endif
        current_offset += 4;
    }
    if (used & RSI) {
        int index = TRACER_REG_RSI;
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x70;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= RSI;
#endif
        current_offset += 4;
    }
    if (used & R13) {
        int index = TRACER_REG_R13;
        ((char *)(address + current_offset))[0] = 0x4C;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x68;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= R13;
#endif
        current_offset += 4;
    }
    if (used & R14) {
        int index = TRACER_REG_R14;
        ((char *)(address + current_offset))[0] = 0x4C;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x70;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= R14;
#endif
        current_offset += 4;
    }
    if (used & R8) {
        int index = TRACER_REG_R8;
        ((char *)(address + current_offset))[0] = 0x4C;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x40;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= R8;
#endif
        current_offset += 4;
    }
    if (used & RBX) {
        int index = TRACER_REG_RBX;
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x58;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= RBX;
#endif
        current_offset += 4;
    }
    if (used & R15) {
        int index = TRACER_REG_R15;
        ((char *)(address + current_offset))[0] = 0x4C;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x78;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= R15;
#endif
        current_offset += 4;
    }
    if (used & RCX) {
        int index = TRACER_REG_RCX;
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x48;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= RCX;
#endif
        current_offset += 4;
    }
    if (used & R9) {
        int index = TRACER_REG_R9;
        ((char *)(address + current_offset))[0] = 0x4C;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x48;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= R9;
#endif
        current_offset += 4;
    }
    if (used & R10) {
        int index = TRACER_REG_R10;
        ((char *)(address + current_offset))[0] = 0x4C;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x50;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= R10;
#endif
        current_offset += 4;
    }
    if (used & R11) {
        int index = TRACER_REG_R11;
        ((char *)(address + current_offset))[0] = 0x4C;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x58;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= R11;
#endif
        current_offset += 4;
    }
    if (used & RDI) {
        int index = TRACER_REG_RDI;
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x78;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= RDI;
#endif
        current_offset += 4;
    }

    if (for_trampoline == 0 && used & RBP) {
        // we need to preserve rbp in the signal handler, since it is used to
        // load the address of the buffer to xsave to at least, we can use the
        // storage in the beginning, store it at  an offset of 24 mov rbp, [rip+
        // offset]
        int offset =
            (long)store_at_end + 24 - ((long)address + current_offset + 7);
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x89;
        ((char *)(address + current_offset))[2] = 0x2d;
        *((int *)(address + current_offset + 3)) = offset;
        current_offset += 7;
    }
    if (used & RBP) {
        TRACER_PRINT_DEBUG("patching to have rbp in context");
        // push the old rbp value onto the stack so we can restore it after the
        // executions
        //((char*)(address+ current_offset))[0] = 0x48;
        //((char*)(address+ current_offset))[1] = 0x89;
        //((char*)(address+ current_offset))[2] = 0x6c;
        //((char*)(address+ current_offset))[3] = 0x24;
        //((char*)(address+ current_offset))[4] = 0xf8;
        //((char *)(address + current_offset))[0] = 0x55;
        int index = TRACER_REG_RBP;
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x68;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= RBP;
#endif
        current_offset += 4;
    }
    if (used & RAX) {
        int index = TRACER_REG_RAX;
        ((char *)(address + current_offset))[0] = 0x48;
        ((char *)(address + current_offset))[1] = 0x8B;
        ((char *)(address + current_offset))[2] = 0x40;
        ((char *)(address + current_offset))[3] = index * 8;
#ifdef TRACER_LOG_ERROR
        used -= RAX;
#endif
        current_offset += 4;
    }

#ifdef TRACER_LOG_ERROR
    if (used != 0 && used < RIP) {
        print_registers(used);
        TRACER_PRINT_ERROR("There are registers used but not restored: 0x%x",
                           used);
    }
#endif
    return current_offset;
}

int patch_store_updated_regs(char *current_address, used_regs used_registers,
                             used_regs modified_registers, int for_trampoline,
                             long *end_of_tramp_storage, ContextInfo *info) {
    char *address = current_address;

    TRACER_PRINT_DEBUG_PATCHER(
        "Patching saving of the following modified registers: %x",
        modified_registers);
#ifdef TRACER_LOG_DEBUG_PATCHER
    print_registers(modified_registers);
#endif
    used_regs leftover_modified_registers = modified_registers;
    int already_restored_rsp = 0;
    if (used_registers & RSP && for_trampoline != 0 &&
        !(modified_registers & RSP)) {
        // NOTE: i am not sure about the third condition
        // we restored the old rsp value, but now we have to restore it from the
        // end of the trampoline mov rsp, [rip + offset]
        // we stored put the rsp value into a register that is not modified and
        // not used, now we load it from there
        /*address[0] = 0x48;
        address[1] = 0x8b;
        address[2] = 0x25;
        int offset = (int)((long)&end_of_tramp_storage[OLD_RSP_INDEX] -
                           (long)address - 7);
        *((int *)&address[3]) = offset;
                */
        TracerRegister rsp_reg = info->reg_for_rsp;
        address += patch_reg_mov(address, rsp_reg, RSP);
    }
    if (modified_registers & RSP && for_trampoline == 0 &&
        !(modified_registers & RAX)) {
        // mov [rax + index * 8], rdx
        address[0] = 0x48;
        address[1] = 0x89;
        address[2] = 0xA0;
        int index = TRACER_REG_RSP;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= RSP;

#endif
    }
    if (used_registers & RSP && for_trampoline == 0) {
        // we just reload the the rsp value from the beginning of the buffer
        // (note that we (ab)use the end_of_tramp_storage variable)
        address[0] = 0x48;
        address[1] = 0x8b;
        address[2] = 0x25;
        int offset = (long)end_of_tramp_storage + 8 * OLD_RSP_INDEX -
                     ((long)address + 7);
        *((int *)&address[3]) = offset;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= RSP;

#endif
    }
    if (modified_registers &&
        ((used_registers & RAX) || (modified_registers & RAX))) {
        TRACER_PRINT_DEBUG_PATCHER("Saving modfied rax");
        // we have to restore rax since it contains the base address for the
        // regs struct in either case and was overwritte
        if (for_trampoline) {
            if (modified_registers & RSP) {
                already_restored_rsp = 1;
                // we need to handle this as a special case, since we have a
                // chicken and egg problem: In order to save rax, we rely on rsp
                // to store the modified value of rax In order to save rsp, we
                // rely on rax not being modified So, if both are modified, we
                // have to handle in with a special case:  We have to old value
                // of rsp stored at the end of the trampoline. We store the new
                // version as well, then we restore the old version. Now, we can
                // store rax, then reload the old version of rax and then store
                // the new value of rsp

                // Store the new version of rsp with a relative mov
                // Mov [rip + displacement],rsp
                //
                /*int offset = (long)&end_of_tramp_storage[NEW_RSP_INDEX] -
                              (long)address - 7;
                 address[0] = 0x48;
                 address[1] = 0x89;
                 address[2] = 0x25;
                 *((int *)&address[3]) = offset;
                 address += 7; */
                // store the new rsp into a register that is not used in order
                // to prevent race conditions
                TracerRegister reg_for_new_rsp = get_free_reg(
                    used_registers, modified_registers | info->reg_for_rsp);
                info->temp_rsp = reg_for_new_rsp;
                TRACER_PRINT_DEBUG_CONTEXT("got %lx as temp reg for rsp",
                                           info->temp_rsp);
                print_registers(info->temp_rsp);
                address += patch_reg_mov(address, RSP, info->temp_rsp);

                // restore the old value
                // we put rsp into an unsused register, lets restore it from
                // there
                address += patch_reg_mov(address, info->reg_for_rsp, RSP);
            }

            // rax was modified, so we have to restore rax before we load the
            // address of the regs struct
            // TODO: test this
            // mov [rsp+ index* 8], rax
            int index = TRACER_REG_RAX;
            address[0] = 0x48;
            address[1] = 0x89;
            address[2] = 0x44;
            address[3] = 0x24;
            address[4] = 8 * index;

            // the base of the struct is rsp, so mov rax, rsp
            address[5] = 0x48;
            address[6] = 0x89;
            address[7] = 0xe0;
            address = address + 8;
            // rax was modified, so we have to restore rax before we load the
            // address of the regs struct
            if (modified_registers & RSP) {
                // save the new value of rsp (this is only run in the case that
                // rsp and rax are both modified) now that rax is restored, we
                // reload the new value of rsp into rsp, then store rsp at the
                // offset of rax, afterwards, we restore the old version of rax
                // again mov rsp, [rip + offset]
                // we cannot use NEW_RSP_INDEX, since that is racy
                /*int offset = (long)&end_of_tramp_storage[NEW_RSP_INDEX] -
                             (long)address - 7;
                address[0] = 0x48;
                address[1] = 0x8b;
                address[2] = 0x25;
                *((int *)&address[3]) = offset;
                address += 7;*/
                int index = TRACER_REG_RSP;
                //
                address += patch_reg_mov(address, info->temp_rsp, RSP);
                // mov rsp, [rax + index * 8]
                address[0] = 0x48;
                address[1] = 0x89;
                address[2] = 0x60;
                address[3] = index * 8;
                address += 4;
                // we put rsp into an unsused register, lets restore it from
                // there
                address += patch_reg_mov(address, info->reg_for_rsp, RSP);
            }

        } else {
            // rax was modified, so we have to restore rax before we load the
            // address of the regs struct
            // we need an unused register
            // lets try to use r15
#ifdef TRACER_LOG_ERROR
            int use_r14 = 0;
            if (modified_registers & R15) {
                use_r14 = 1;

                if (modified_registers & R14) {
                    TRACER_PRINT_ERROR("uses r14 as well");
                }
            }
#endif

            // mov the base of regs into r15
            // mov r15, -8[rbp]
            if (modified_registers & RBP) {
                // if rbp was modifed, we cannot use it to reload the base, so
                // just use the rbp value stored in front
                int offset =
                    (long)end_of_tramp_storage + 8 - ((long)address + 7);
                address[0] = 0x4c;
                address[1] = 0x8b;
                address[2] = use_r14 ? 0x35: 0x3d;
                *((int *)&address[3]) = offset;
                address += 7;

            } else {
                address[0] = 0x4c;

                address[1] = 0x8b;
                address[2] = use_r14 ? 0x75 : 0x7d;
                address[3] = 0xf8;
				address += 4;
            }
            int index = TRACER_REG_RAX;
            // TODO/PERF: this can be optimized out if rax is not modified
            // mov rax, [r15 + index * 8];
            address[0] = 0x49;
            address[1] = 0x89;
            address[2] = use_r14 ? 0x46 : 0x47;
            address[3] = index * 8;

            // we can jstu mov rax, r15
            address[4] = 0x4c;
            address[5] = 0x89;
            address[6] = use_r14 ? 0xf0 : 0xf8;
            address = address + 7;
        }
#ifdef TRACER_LOG_ERROR
        if (modified_registers & RAX) {
            leftover_modified_registers -= RAX;
        }
        if (already_restored_rsp) {
            leftover_modified_registers -= RSP;
        }

#endif
    }
    // this has to go before the restoreing of the flags because the flags
    // thingy destroys r15 but if the rsp is modified, we most likey stored the
    // original rsp in r15, but then we loose it b/c of the flag restoration
    // if rsp is modified, the restauration above is not correct
    if (modified_registers & RSP && for_trampoline && !already_restored_rsp) {
        int index = TRACER_REG_RSP;

        // in order to restore rsp, we cannot just push the rsp onto the
        // stack since then the instruction changes the rsp, so for storing
        // the old context, we
        //
        // mov rsp, [rax + index * 8]
        // TODO: maybe we can fix this by storing the new value as well
        address[0] = 0x48;
        address[1] = 0x89;
        address[2] = 0x60;
        address[3] = index * 8;
        address += 4;
        // mov location of rsp to to rsp.. first, load the address into rsp
        address += patch_reg_mov(address, info->reg_for_rsp, RSP);
        // address[4] = 0x48;
        // address[5] = 0xbc;
        //*((long *)&address[6]) = (long)end_of_tramp_storage + OLD_RSP_INDEX *
        // 8 ;
        //  mov rsp, [rsp]
        /*address[14] = 0x48;
        address[15] = 0x8b;
        address[16] = 0x24;
        address[17] = 0x24;
        address += 18;*/

#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= RSP;
#endif
    }
    if (modified_registers & RSI) {
        // mov [rax + index * 8], rsi
        address[0] = 0x48;
        address[1] = 0x89;
        address[2] = 0xb0;
        int index = TRACER_REG_RSI;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= RSI;

#endif
    }
    if (modified_registers & RBX) {
        // mov [rax + index * 8], rbx
        address[0] = 0x48;
        address[1] = 0x89;
        address[2] = 0x98;
        int index = TRACER_REG_RBX;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= RBX;

#endif
    }
    if (modified_registers & R8) {
        // mov [rax + index * 8], r8
        address[0] = 0x4c;
        address[1] = 0x89;
        address[2] = 0x80;
        int index = TRACER_REG_R8;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= R8;

#endif
    }
    if (modified_registers & R9) {
        // mov [rax + index * 8], r9
        address[0] = 0x4c;
        address[1] = 0x89;
        address[2] = 0x88;
        int index = TRACER_REG_R9;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= R9;

#endif
    }
    if (modified_registers & R10) {
        // mov [rax + index * 8], r10
        address[0] = 0x4c;
        address[1] = 0x89;
        address[2] = 0x90;
        int index = TRACER_REG_R10;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= R10;

#endif
    }
    if (modified_registers & R11) {
        // mov [rax + index * 8], r11
        address[0] = 0x4c;
        address[1] = 0x89;
        address[2] = 0x98;
        int index = TRACER_REG_R11;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= R11;

#endif
    }
    if (modified_registers & R12) {
        // mov [rax + index * 8], r11
        address[0] = 0x4c;
        address[1] = 0x89;
        address[2] = 0xA0;
        int index = TRACER_REG_R12;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= R12;

#endif
    }
    if (modified_registers & R13) {
        // mov [rax + index * 8], r15
        address[0] = 0x4c;
        address[1] = 0x89;
        address[2] = 0xa8;
        int index = TRACER_REG_R13;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= R13;

#endif
    }
    if (modified_registers & R14) {
        // mov [rax + index * 8], r15
        address[0] = 0x4c;
        address[1] = 0x89;
        address[2] = 0xb0;
        int index = TRACER_REG_R14;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= R14;

#endif
    }
    if (modified_registers & R15) {
        // mov [rax + index * 8], r15
        address[0] = 0x4c;
        address[1] = 0x89;
        address[2] = 0xb8;
        int index = TRACER_REG_R15;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= R15;

#endif
    }
    if (modified_registers & RDX) {
        // mov [rax + index * 8], rdx
        address[0] = 0x48;
        address[1] = 0x89;
        address[2] = 0x90;
        int index = TRACER_REG_RDX;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= RDX;

#endif
    }
    if (modified_registers & RCX) {
        // mov [rax + index * 8], rdx
        address[0] = 0x48;
        address[1] = 0x89;
        address[2] = 0x88;
        int index = TRACER_REG_RCX;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= RCX;
#endif
    }
    if (modified_registers & RDI) {
        // mov [rax + index * 8], rdi
        address[0] = 0x48;
        address[1] = 0x89;
        address[2] = 0xb8;
        int index = TRACER_REG_RDI;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= RDI;
#endif
    }
    if (modified_registers & RBP) {
        // mov [rax + index * 8], rbp
        address[0] = 0x48;
        address[1] = 0x89;
        address[2] = 0xa8;
        int index = TRACER_REG_RBP;
        *((int *)&address[3]) = index * 8;
        address += 7;
#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= RBP;
#endif
    }

    if (modified_registers & FLAGS) {

        // TODO: i think this push onto the stack overwrites the exesting value
        // since we do not update the sp, but it should be findesince we dont
        // need it afterwards anyways
        // we push the flags register onto the stack and copy it to the correct
        // location
        // pushfq
        address[0] = 0x9C;
        // here, all the modified registers should already be restored, so we
        // can just use the r15 register for temp mov r15, (rsp)
        address[1] = 0x4c;
        address[2] = 0x8b;
        address[3] = 0x3c;
        address[4] = 0x24;

        // mov (rax + index * 8), r15
        int index = TRACER_REG_FLAGS;
        address[5] = 0x4c;
        address[6] = 0x89;
        address[7] = 0xb8;
        *((int *)&address[8]) = index * 8;
        // popfq
        address[12] = 0x9d;
        address += 13;

#ifdef TRACER_LOG_ERROR
        leftover_modified_registers -= FLAGS;
#endif
    }
    if (for_trampoline == 0 && (used_registers | modified_registers) & RBP) {
        // we need to preserve rbp in the signal handler, since it is used to
        // load the address of the buffer to xsave to at least, we can use the
        // storage in the beginning, store it at  an offset of 24 mov rbp, [rip+
        // offset]
        int offset = (long)end_of_tramp_storage + 24 - ((long)address + 7);
        address[0] = 0x48;
        address[1] = 0x8b;
        address[2] = 0x2d;
        *((int *)&address[3]) = offset;
        address += 7;
    }

#ifdef TRACER_LOG_ERROR
    // we ignore the rip register, as it is technically modified by things like
    // jumps, but thats
    if (leftover_modified_registers & RIP) {
        leftover_modified_registers -= RIP;
    }
    if (leftover_modified_registers > 0 &&
        leftover_modified_registers < IGNORE) {
        print_registers(leftover_modified_registers);
        TRACER_PRINT_ERROR("There are modified registers that are not "
                           "restored, rest are %x",
                           leftover_modified_registers);
    }
#endif
    return address - current_address;
}
