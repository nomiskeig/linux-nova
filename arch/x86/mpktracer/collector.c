#include "config.h"
#include "instruction.h"
#include "register.h"
#include "regs.h"
#include "shared.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Zydis.h>
#include <pthread.h>
#include <string.h>
#include <xmmintrin.h>
#ifdef TRACER_USE_SPINLOCK_FOR_ADDRESS
pthread_spinlock_t address_lock;
#endif
#ifdef TRACER_USE_MUTEX_FOR_ADDRESS
pthread_mutex_t address_lock;
#endif
#else
#include <Zydis.h>
#endif

#ifdef TRACER_USERSPACE
#define printf(...) printf(__VA_ARGS__)
#else
#define printf(...) pr_info(__VA_ARGS__)
#endif

#include "collector.h"
#include "logging.h"
#include "patcher.h"
// if called with operand_size > 64, it assumes that reg is an xmm register
static int _patch_mov_mem_and_reg(MOV_TYPE type, char *address_to_patch,
				  int operand_size, TracerRegister reg,
				  TracerRegister mem_base,
				  ZydisDisassembledInstruction *instruction)
{
	ZydisDecodedOperand *additional_mem = &instruction->operands[0];
	int current_index = 0;
	int prefix_index = 0;
	int dest_index = tracer_reg_to_index(reg);
	int source_index = tracer_reg_to_index(mem_base);
	int base_length = 2;

#ifdef TRACER_LOG_ERROR
	if (operand_size == 128) {
		TRACER_PRINT_ERROR(
			"Tried to patch_mov_mem_and_reg with 128 bytes");
	}
#endif
	switch (type) {
	case MEM_TO_REG: {
		if (additional_mem &&
		    additional_mem->mem.index != ZYDIS_REGISTER_NONE) {
			/*TRACER_PRINT_ERROR(
                "Memory we need to copy over for the colleciton uses sib
               byte");*/
		}
		TRACER_PRINT_DEBUG_COLLECTOR("Patching mov %i, [%i]",
					     dest_index, source_index);

		break;
	}
	case REG_TO_MEM: {
		TRACER_PRINT_DEBUG_COLLECTOR("Patching mov [%i], %i",
					     source_index, dest_index);

		break;
	}
	}
	if (operand_size == 256) {
		base_length = 4;
		address_to_patch[0] = 0xc4;
		address_to_patch[1] = 0xc1 + (source_index < 8 ? 0x20 : 0) +
				      (dest_index >= 8 ? -0x80 : 0);
		address_to_patch[2] = 0x7c;
		address_to_patch[3] = 0x10;
		current_index = 4;

	} else {
		TRACER_PRINT_DEBUG_COLLECTOR("Operand size is %i",
					     operand_size);
		if (operand_size == 64) {
			address_to_patch[current_index] = 0x48;
			current_index += 1;
		}
		switch (type) {
		case REG_TO_MEM:
			address_to_patch[current_index] = 0x89;
			break;
		case MEM_TO_REG:
			address_to_patch[current_index] = 0x8B;
			break;
		}
		current_index += 1;
		switch (type) {
		case REG_TO_MEM:
			if (dest_index >= 8) {
				// destinateion is r8-15
				address_to_patch[prefix_index] |= 0x04;
			}
			if (source_index >= 8) {
				address_to_patch[prefix_index] |= 0x01;
			}
			break;
		case MEM_TO_REG:
			if (dest_index >= 8) {
				// destinateion is r8-15
				address_to_patch[prefix_index] |= 0x04;
			}
			if (source_index >= 8) {
				address_to_patch[prefix_index] |= 0x01;
			}
			break;
		}
	}
	address_to_patch[current_index] = (dest_index % 8) * 8;
	address_to_patch[current_index] += source_index % 8;
	int has_displacement = 0;
	int has_sib_byte = 0;
	int modrm_index = current_index;
	int is_already_modrm_01 = 0;
	if (source_index % 8 == 4 || source_index % 8 == 5) {
		// case that sib byte must be used, in the case of 5 this originally
		// means displacement so we have to subtract one from the modrm byte to
		// make it use the sib byte as well
		if (source_index % 8 == 5) {
			// we use the modrm with 8 byte offset in the case that we are in
			// teh 00/101 row
			address_to_patch[current_index] += 0x40;
			is_already_modrm_01 = 1;
		}
		current_index += 1;
		if (source_index % 8 == 4) {
			address_to_patch[current_index] = 0x24;
			has_sib_byte = 1;
		}
		if (source_index % 8 == 5) {
			// this is the offset in the case we use ebp
			has_displacement = 1;
			address_to_patch[current_index] = 0x00;
		}
	}

	// handle displacement in memory source
	if (type == MEM_TO_REG) {
		if (instruction->info.raw.disp.size > 0) {
			if (instruction->info.raw.disp.size == 8 &&
			    instruction->info.raw.disp.value != 0) {
				if (is_already_modrm_01 != 1) {
					address_to_patch[modrm_index] += 0x40;
				}
				address_to_patch[modrm_index + 1 + has_sib_byte] =
					instruction->info.raw.disp.value;
				has_displacement = 1;

			} else if (instruction->info.raw.disp.size == 32) {
				TRACER_PRINT_ERROR(
					"mem_to_reg, disp size 32 is not handled");
			}
		}
	}
	// TODO: this needs special care if source is RSP or RBP since then SIB
	// displacemenet is used
	return base_length + 1 + has_displacement + has_sib_byte;
}

static int
patch_pre_not_written_into_reg(char *address_to_patch,
			       ZydisDisassembledInstruction *instruction,
			       AddressReg address_r)
{
	// this basically patches a mov from the memory to the tmp register
	TracerRegister temp_reg;
	if (get_value_size(instruction) <= 8) {
		used_regs used = get_used_registers(instruction);
		used_regs modified = get_modified_registers(instruction);
		TracerRegister address_reg =
			address_reg_to_tracer_reg(address_r);
		used_regs combined = used | modified | address_reg;
		temp_reg = get_and_set_unused_register(&combined);

	} else {
		used_regs used = get_used_xmm_registers(instruction);
		used_regs modified = get_modified_xmm_registers(instruction);
		used_regs combined = used | modified;

		temp_reg = get_and_set_unused_register(&combined);
	};
	ZydisDecodedOperand *source = get_memory_opearand(instruction);
	int size = _patch_mov_mem_and_reg(
		MEM_TO_REG, address_to_patch,
		get_value_size(instruction) <= 8 ?
			64 :
			get_value_size(instruction) * 8,
		temp_reg, get_offset_of_reg(source->mem.base), instruction);
	return size;
}
static int
patch_default_collect_read_value(char *address_to_patch,
				 ZydisDisassembledInstruction *instruction,
				 AddressReg address_reg)
{
	// get the register that the instruction reads into;
	TracerRegister reg = 0;
	int address_reg_index =
		tracer_reg_to_index(address_reg_to_tracer_reg(address_reg));
	TRACER_PRINT_DEBUG("got traer_reg_to_index\n");
#ifdef TRACER_LOG_ERROR
	int found_reg = 0;
#endif
	for (int i = 0; i < instruction->info.operand_count; i++) {
		if (instruction->operands[i].type ==
		    ZYDIS_OPERAND_TYPE_REGISTER) {
#ifdef TRACER_LOG_ERROR
			found_reg = 1;
#endif
			reg = get_offset_of_reg(
				instruction->operands[i].reg.value);
			TRACER_PRINT_DEBUG("got reg index first, reg is %x\n",
					   reg);
			if (reg >= IGNORE) {
				reg = get_offset_of_xmm_reg(
					instruction->operands[i].reg.value);
				TRACER_PRINT_DEBUG(
					"got reg index second, reg is %x\n",
					reg);
			}
		}
	}
	TRACER_PRINT_DEBUG("getting reg index of register %x", reg);
	int reg_index = tracer_reg_to_index(reg);
#ifdef TRACER_LOG_ERROR
	if (found_reg == 0) {
		TRACER_PRINT_ERROR(
			"Did not find a register that the instruction read into");
	}
#endif
// adopted from
// https://yhbt.net/lore/all/20200203161904.846921260@linuxfoundation.org/
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
	// now, we patch a mov isntruction, but we have to respect the operand sizej
	// for the instruction handler, the address to store the reg to is rbp-0x10
	// we laod the the value from rsp + offset into rax,
	// then load the value address into rbx
	//
	// we know that the address is in adddress reg, os we adopt the instruciton
	// below
	int prefix_mod = 0x0;
	int offset = 0;
	int length_additional = 0;
	int size_modifier = 0;
	switch (get_value_size(instruction)) {
	case 1:
		address_to_patch[0] = 0x41 + (reg_index >= 8 ? 0x4 : 0);
		address_to_patch[1] = 0x88;
		address_to_patch[2] =
			0x00 + address_reg_index % 8 + (reg_index % 8) * 8;
		return 3;

	case 2:
		address_to_patch[0] = 0x66;
		length_additional += 1;
		offset += 1;
#ifndef TRACER_USERSPACE
		// fall through
#endif

	case 4:
		prefix_mod = -0x08;
#ifndef TRACER_USERSPACE
		// fall through
#endif
	case 8:
		address_to_patch[0 + offset] =
			0x49 + (reg_index >= 8 ? 0x4 : 0) + prefix_mod;
		address_to_patch[1 + offset] = 0x89;
		address_to_patch[2 + offset] =
			0x00 + (reg_index % 8) * 8 + address_reg_index % 8;
		return 3 + length_additional;
	case 16:
		size_modifier = 0x04;
#ifndef TRACER_USERSPACE
		// fall through
#endif

	case 32: {
		// use a vmovups,
		address_to_patch[0] = 0xc4;
		address_to_patch[1] = 0x41 + (reg_index < 8 ? 0x80 : 0);
		address_to_patch[2] = 0x7c - size_modifier;
		address_to_patch[3] = 0x11;
		address_to_patch[4] =
			0x00 + address_reg_index % 8 + 8 * (reg_index % 8);
		return 5;
		break;
	}

	default:
		TRACER_PRINT_ERROR(
			"found an unsupported operand size for a read, the size is %li",
			get_value_size(instruction));
		break;
	}
	return 0;
}

#pragma GCC diagnostic pop
// Normallay, we put the address for the value in r15. If the instruction uses
// r15 (e.g. mov [rax], r15), then this breaks and we have to use another
// register that is not used by the instruction. We try the registers downwards,
// starting with r15.
//
//
//
AddressReg
get_register_for_trace_value_address(ZydisDisassembledInstruction *instruction,
				     used_regs used)
{
	if (!(used & R15)) {
		TRACER_PRINT_DEBUG_COLLECTOR("Using R15 for the value address");
		return ADDRESS_R15;
	}
	if (used & R15) {
		if (!(used & R14)) {
			TRACER_PRINT_DEBUG_COLLECTOR(
				"Using R14 for the value address");
			return ADDRESS_R14;
		}
	}
	if (used & R14) {
		if (!(used & R13)) {
			TRACER_PRINT_DEBUG_COLLECTOR(
				"Using R13 for the value address");
			return ADDRESS_R13;
		}
	}
	if (used & R13) {
		if (!(used & R12)) {
			TRACER_PRINT_DEBUG_COLLECTOR(
				"Using R12 for the value address");
			return ADDRESS_R12;
		}
	}

	return ADDRESS_R11;
}

int patch_rep_prefix(char *address_to_patch,
		     ZydisDisassembledInstruction *instruction)
{
	return 0;
}
// this method is responsible for creating a instruction that is written
// into the stub function. It is basically the same instruction as the one
// we want to trace, but the target memory needs to be changed to the
// correct address. The instruction will be executed in the context of the
// original instruction. The value we want to trace for the instruction must
// be written into the memory pointed to by r15 Arguments:
// - address_to_patch: The address the instruction is written to. It is then
// executed by the stub function.
// - instruction: the disassembled instruction
// - info: additional information about the collection, for debugging
// purposes
int patch_collect_instructions(unsigned long address_to_patch,
			       char *address_of_instruction,
			       ZydisDisassembledInstruction *instruction,
			       AddressReg reg, used_regs used,
			       int for_trampoline
#ifdef TRACER_LOG_ACCESSES
			       ,
			       CollectionInfo *info
#endif
)
{
#ifdef TRACER_LOG_ACCESSES
	if (instruction->operands[0].type != ZYDIS_OPERAND_TYPE_MEMORY) {
		// this is a load, we do not handle it yet

		info->type = TRACER_TYPE_LOAD;
	} else {
		info->type = TRACER_TYPE_WRITE;
	}
	info->implemented = TRACER_STATE_IMPLEMENTED;

#endif
	TracerRegister value_reg = address_reg_to_tracer_reg(reg);
	TRACER_PRINT_DEBUG_COLLECTOR("Collecting %s", instruction->text);
#ifdef TRACER_LOG_DEBUG_COLLECTOR
	int length;
	length = instruction->info.length;
	TRACER_PRINT_DEBUG("[Collector] raw instruction: ");
	for (int i = 0; i < length; i++) {
		TRACER_PRINT_DEBUG("%02hhx ",
				   ((char *)address_of_instruction)[i]);
	}
	TRACER_PRINT_DEBUG("\n");
#endif

	if (instruction->info.mnemonic == ZYDIS_MNEMONIC_STOSQ ||
	    //instruction->info.mnemonic == ZYDIS_MNEMONIC_STOSB ||
	    instruction->info.mnemonic == ZYDIS_MNEMONIC_MOVSB ||
	    instruction->info.mnemonic == ZYDIS_MNEMONIC_MOVSQ ||
	    instruction->info.mnemonic == ZYDIS_MNEMONIC_XCHG) {
		// instruction->info.mnemonic == ZYDIS_MNEMONIC_VMOVD ||
		// instruction->info.mnemonic == ZYDIS_MNEMONIC_VMOVQ) {
#ifndef TRACER_USERSPACE
		pr_info("found unsupported instruction: %i, %s",
			instruction->info.mnemonic, instruction->text);
#endif
		return 0;
	}
	int offset = 0;
	if (!is_write(instruction)) {
		switch (instruction->info.mnemonic) {
		case ZYDIS_MNEMONIC_CMP:
		case ZYDIS_MNEMONIC_VPCMPEQB: {
			TRACER_PRINT_DEBUG("is patching with cmp\n");
			int size = patch_pre_not_written_into_reg(
				(char *)address_to_patch + offset, instruction,
				reg);
			ZydisDisassembledInstruction instr;
			// we copy the memory into a free register and then pass that
			// instruction as the one we try to trace, and thus we patch the
			// collectoin of the register into the correct address;
			TRACER_PRINT_DEBUG_COLLECTOR("before disassemble");
			// we could just disassemle the sintrution but it seems that i have
			// found a bug in zydis
			ZyanStatus status = ZydisDisassembleIntel(
				ZYDIS_MACHINE_MODE_LONG_64,
				address_to_patch + offset,
				(void *)(address_to_patch + offset), size,
				&instr);

			if (!ZYAN_SUCCESS(status)) {
				TRACER_PRINT_DEBUG_COLLECTOR("dead");
			}
			TRACER_PRINT_DEBUG("Adter disassemble");

			/*ZydisDisassembledInstruction copy_instr;
for (int i = 0; i < sizeof(ZydisDisassembledInstruction); i++) {
    *(long *)((long)(&copy_instr) + i) =
        *(long *)((long)(&instr) + i);
}
                    */
			offset += size;
			TRACER_PRINT_DEBUG(
				"before patch collect read value, instr is %s\n",
				instr.text);
			offset += patch_default_collect_read_value(
				(char *)address_to_patch + offset, &instr, reg);
			return offset;
		}

		default: {
			offset += patch_default_collect_read_value(
				(char *)address_to_patch + offset, instruction,
				reg);
			return offset;
		}
		}
	}

	if (needs_value_copy(instruction)) {
		// TODO: test this, basically an add with an rip relative offset, i
		// found this b/c of a
		if (instruction->info.attributes & ZYDIS_ATTRIB_IS_RELATIVE) {
			offset += patch_mov_relative_to_addr_in_reg(
				(char *)address_to_patch, 64, instruction, reg,
				&used);

		} else {
			offset += patch_mov_dest_addr_value_to_addr_in_reg(
				(char *)address_to_patch, 64, instruction, reg,
				&used);
		}
	}
	if (instruction->info.attributes & ZYDIS_ATTRIB_HAS_REP) {
		if (instruction->info.mnemonic == ZYDIS_MNEMONIC_STOSB) {
			// for this, we basically have to change rdi to the value address
			// and then store it back afterwads, i guess we can just use like
			// r14 for temp storage
			offset += patch_rep_prefix((char *)address_to_patch,
						   instruction);
		}
	}

	if (instruction->info.opcode_map == ZYDIS_OPCODE_MAP_0F) {
		TRACER_PRINT_DEBUG("is in 2 byte opcode switch\n");
		// prefix for 2 byte opcodes
		switch (instruction->info.opcode) {
		case 0x29: // movaps
		case 0x11: // movups
		case 0x94: // setz, TODO: test this
		case 0x13: // movlpd
		case 0x17: // movhpd
		case 0xB1: // cmpxchg
		case 0xC3: // movnti
			offset += set_mem_address_to_reg_address(
				instruction, address_of_instruction,
				(char *)address_to_patch + offset, value_reg);
			if (instruction->info.opcode == 0xB1) {
				// we need to reload rax since the cmpxchg instruction may
				// overwrite it, which changes the behaviour of the
				// displaced original instruction
				if (!for_trampoline) {
					offset += load_rax(
						(char *)address_to_patch +
						offset);
				}
			}
			break;
		case 0x7f: // vmovdqu and vmovdqa
		case 0x7e: // mmovd
		case 0xd6: // vmovq
		{
			int res = set_mem_address_to_reg_address_vex(
				instruction, address_of_instruction,
				(char *)address_to_patch + offset, value_reg,
				NULL_F);
#ifdef TRACER_LOG_DEBUG_COLLECTOR
			length = res;
#endif
			offset += res;
			break;
		}

		default: {
#ifdef TRACER_LOG_ACCESSES
			info->implemented = TRACER_STATE_NOT_IMPLEMENTED;
#endif
			TRACER_PRINT_ERROR(
				"Collection of two byte opcode %hhx not "
				"implemented, instruction is: %s",
				instruction->info.opcode, instruction->text);
			return 0;
		}
		}

	} else {
		switch (instruction->info.opcode) {
		case 0xaa:
			break; // stosb, is handled below or above somewhere
		case 0x83: {
			switch (instruction->info.raw.modrm.reg) {
			case 0: // add, TODO: test this
			case 1: // or, TODO: test this
			case 5: // subtract

				offset += set_mem_address_to_reg_address(
					instruction, address_of_instruction,
					(char *)address_to_patch + offset,
					value_reg);
				break;
			default: {
#ifdef TRACER_LOG_ACCESSES
				info->implemented =
					TRACER_STATE_NOT_IMPLEMENTED;
#endif
				TRACER_PRINT_ERROR(
					"Collection of opcode %hhx with extension "
					"%i not implemented",
					instruction->info.opcode,
					instruction->info.raw.modrm.reg);
				return 0;
			}
			}
			break;
		}
		case 0x01: // add
		case 0xC7: // mov imm 16/32 to r/m
		case 0x81: // or imm
		case 0x89: // mov
		case 0x88: // mov byte
		case 0xC6: // mov byte pointer
		{
			offset += set_mem_address_to_reg_address(
				instruction, address_of_instruction,
				(char *)address_to_patch + offset, value_reg);
			break;
		}
		default: {
#ifdef TRACER_LOG_ACCESSES
			info->implemented = TRACER_STATE_NOT_IMPLEMENTED;
#endif

			//*(volatile int*)0x0 = 0x0;
			TRACER_PRINT_ERROR(
				"Collection of opcode %hhx not implemented, instruction is %s",
				instruction->info.opcode, instruction->text);
			return 0;
		}
		}
	}

	// patch the new instruction
#ifdef TRACER_LOG_DEBUG_COLLECTOR
	TRACER_PRINT_DEBUG("[Collector] Patched raw instruction: ");
	for (int i = 0; i < length; i++) {
		TRACER_PRINT_DEBUG("%02hhx ", ((char *)address_to_patch)[i]);
	}
	TRACER_PRINT_DEBUG("\n");
#endif
	return offset;
}

int patch_mov_dest_addr_value_to_addr_in_reg(
	char *address_to_patch, int op_size,
	ZydisDisassembledInstruction *instruction, AddressReg dest,
	used_regs *used)
{
	TracerRegister temp_reg = get_and_set_unused_register(used);
	ZydisDecodedOperand *source = &instruction->operands[0];
	int size = _patch_mov_mem_and_reg(MEM_TO_REG, address_to_patch, op_size,
					  temp_reg,
					  get_offset_of_reg(source->mem.base),
					  instruction);
	size += _patch_mov_mem_and_reg(REG_TO_MEM, address_to_patch + size,
				       op_size, temp_reg,
				       address_reg_to_tracer_reg(dest), NULL);
	return size;
}
TracerRegister address_reg_to_tracer_reg(AddressReg reg)
{
	switch (reg) {
	case ADDRESS_R15:
		return R15;
		break;
	case ADDRESS_R14:
		return R14;
		break;
	case ADDRESS_R13:
		return R13;
		break;
	case ADDRESS_R12:
		return R12;
		break;
	case ADDRESS_R11:
		return R12;
		break;
	}

	TRACER_PRINT_ERROR("Reached the end of address_reg_to_tracer_reg, that "
			   "should not happen");
	return R12;
}

int set_mem_address_to_reg_address(ZydisDisassembledInstruction *instruction,
				   char *instruction_address,
				   char *address_to_patch, TracerRegister reg)
{
	char *raw_instruction = instruction_address;
	char new_instruction[15];
	int amount_prefixes = instruction->info.raw.prefix_count;
	int total_length = amount_prefixes;
	// copy over the prefixes
	for (int i = 0; i < amount_prefixes; i++) {
		new_instruction[i] = raw_instruction[i];
	}
	int rex_index = amount_prefixes - 1;
	if (!(ZYDIS_ATTRIB_HAS_REX & instruction->info.attributes)) {
		// add a rex prefix
		total_length += 1;
		rex_index = amount_prefixes;
		new_instruction[rex_index] = 0x40;
	}
	if (reg >= R8) {
		// make new registers addressable
		new_instruction[rex_index] |= 0x1;
	}
	if (new_instruction[rex_index] & 0x2) {
		new_instruction[rex_index] &= ~0x2;
	}
	// copy over the opcode
	int opcode_index = rex_index + 1;
	int opcode_length = instruction->info.raw.modrm.offset -
			    instruction->info.raw.prefix_count;
	for (int i = 0; i < opcode_length; i++) {
		new_instruction[opcode_index + i] =
			raw_instruction[amount_prefixes + i];
		total_length += 1;
	}

	// we need to take care of the case where the sib byte uses relative
	// offsets
	if (tracer_reg_to_index(reg) % 8 != 5) {
		// set modrm/byte to always use the sib byte but keep the register
		new_instruction[total_length] =
			0x04 + instruction->info.raw.modrm.reg * 8;

		total_length += 1;
		// the case without offset
		if (instruction->info.raw.modrm.mod == 0) {
		}
		// add the sib byte to point to the correct register
		new_instruction[total_length] =
			0x20 + (tracer_reg_to_index(reg) % 8);
		total_length += 1;
	} else {
		// we use the modrm_byte with iwth mod 01
		new_instruction[total_length] =
			0x40 + (tracer_reg_to_index(reg) % 8) +
			instruction->info.raw.modrm.reg * 8;
		// set displacement to 0;
		new_instruction[total_length + 1] = 0x0;
		total_length += 2;
	}

	// copy over immidate

	for (int i = 0; i < instruction->info.raw.imm[0].size / 8; i++) {
		new_instruction[total_length] =
			raw_instruction[instruction->info.raw.imm[0].offset + i];
		total_length += 1;
	}

	for (int i = 0; i < total_length; i++) {
		address_to_patch[i] = new_instruction[i];
	}
	return total_length;
}
int set_mem_address_to_reg_address_vex(
	ZydisDisassembledInstruction *instruction, char *instruction_address,
	char *address_to_patch, TracerRegister reg, VexOpcodeMap map)
{
	// assume that we can use 3 byte form with c4 prefix
	// we also assume that we have one register source and one memory
	// destination c4 prefix
	// copy over the prefixes
	for (int i = 0; i < instruction->info.raw.prefix_count; i++) {
		address_to_patch[i] = instruction_address[i];
	}
	int offset = instruction->info.raw.prefix_count;
	address_to_patch[offset] = 0xc4;
	int dest_index = tracer_reg_to_index(reg);
	char second_byte = 0x0;
	second_byte |= 0x40;
	if (instruction->info.raw.vex.R) {
		second_byte |= 0x80;
	}
	if (!(dest_index >= 8)) {
		second_byte |= 0x20;
	}
	second_byte |= map;
	address_to_patch[offset + 1] = second_byte;
	address_to_patch[offset + 2] = 0x7a;
	if (get_value_size(instruction) == 32) {
		address_to_patch[offset + 2] |= 0x04;
	}
	if (get_value_size(instruction) == 4 ||
	    get_value_size(instruction) == 8) {
		address_to_patch[offset + 2] |= 1;
		address_to_patch[offset + 2] &= ~(1 << 0x1);
	}
	// copy over the opcode
	address_to_patch[offset + 3] =
		instruction_address[instruction->info.raw.prefix_count +
				    instruction->info.raw.vex.size];
	offset += 4;
	// make modrm
	// we need to take care of the case where the sib byte uses relative
	// offsets
	if (tracer_reg_to_index(reg) % 8 != 5) {
		// set modrm/byte to always use the sib byte but keep the register
		address_to_patch[offset] =
			0x04 + instruction->info.raw.modrm.reg * 8;

		offset += 1;
		// add the sib byte to point to the correct register
		address_to_patch[offset] =
			0x20 + (tracer_reg_to_index(reg) % 8);
		offset += 1;
	} else {
		// we use the modrm_byte with iwth mod 01
		address_to_patch[offset] = 0x40 +
					   (tracer_reg_to_index(reg) % 8) +
					   instruction->info.raw.modrm.reg * 8;
		// set displacement to 0;
		address_to_patch[offset + 1] = 0x0;
		offset += 2;
	}

	// copy over immidate
	for (int i = 0; i < instruction->info.raw.imm[0].size / 8; i++) {
		address_to_patch[offset] =
			instruction_address[instruction->info.raw.imm[0].offset +
					    i];
		offset += 1;
	}
	TRACER_PRINT_DEBUG_COLLECTOR("New instruction looks like this: ");
	for (int i = 0; i < offset; i++) {
		TRACER_PRINT_DEBUG("%02hhx ", address_to_patch[i]);
	}
	TRACER_PRINT_DEBUG("\n");
	return offset;
}
long get_and_set_value_address(ZydisDisassembledInstruction *instruction,
			       Trace *trace, Valuebuffer *buffer)
{
	long value_size = get_value_size(instruction);
	TRACER_PRINT_DEBUG_COLLECTOR("operand width in bytes: %li", value_size);
	if (!(instruction->info.attributes & ZYDIS_ATTRIB_HAS_REP)) {
		set_length(trace, value_size);
	}
	if (value_size <= 8) {
		set_intern(trace);
		TRACER_PRINT_DEBUG_COLLECTOR("Address of next value: 0x%px",
					     (void *)(&(trace->value)));
		return (long)&trace->value;
	} else {
#ifdef TRACER_USE_SPINLOCK_FOR_ADDRESS
		pthread_spin_lock(&address_lock);
#endif
#ifdef TRACER_USE_MUTEX_FOR_ADDRESS
		pthread_mutex_lock(&address_lock);
#endif

		// in order to synchronize this, we have to use cmpxchng since we
		// cannot just update the next_offset once. We have to read it once
		// to get the address in order to calculate the offset, and then we
		// have to write it. With cmpxchng, we can
		// 1. Read the value
		// 2. Calculate the new next_offset
		// 3. Use cmpxchg in order to make sure that we only update the
		// next_value pointer if we still read the old value first
		set_extern(trace);
#ifndef TRACER_ALLIGN_ALL
		long old_offset = buffer->next_offset;
	retry: {
		long address = (long)&buffer->data[old_offset];
		int offset = 0;
		if (needs_allignment(instruction)) {
			offset = value_size - (address % value_size);
			TRACER_PRINT_DEBUG_COLLECTOR("Allining with offset %i",
						     offset);
			TRACER_PRINT_DEBUG_COLLECTOR(
				"External address of next value: 0x%px",

				(void *)address + offset);
		}
		// this can be updated like this, since this is our trace, theres no
		// contention, and we only continue if buffer->next_offset was the
		// same the entire
		trace->value = old_offset + offset;
#ifdef TRACER_OVERWRITE_TRACES
		long next_offset = 0 + value_size + offset;
#else
		long next_offset = old_offset + value_size + offset;
#endif

		// see
		// https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
		// To be honest, i have no idea what arguments for the memory
		// ordering are correct, but those below seem to work. I also tried
		// __ATOMIC_ACQ_REL, but then it did not work sometimes this
		// compiles, but clangd shows it as an error (we basically dont care
		// :))
#ifndef TRACER_USE_SPINLOCK_FOR_ADDRESS
#ifndef TRACER_USE_MUTEX_FOR_ADDRESS
		if (!__atomic_compare_exchange_8(
			    &buffer->next_offset, &old_offset,

			    next_offset, 0, __ATOMIC_SEQ_CST,
			    __ATOMIC_SEQ_CST)) {
			goto retry;
		}
#endif
#endif
#ifdef TRACER_LOG_ERROR
		if ((long)buffer->next_offset >
		    ((long)&buffer->data[0] + sizeof(buffer->data))) {
			TRACER_PRINT_ERROR("Value buffer is full");
		}
#endif

#ifdef TRACER_USE_SPINLOCK_FOR_ADDRESS
		long res = address + offset;
		pthread_spin_unlock(&address_lock);
		return res;
#endif
#ifdef TRACER_USE_MUTEX_FOR_ADDRESS
		long res = address + offset;
		pthread_mutex_unlock(&address_lock);
		return res;
#endif
		// buffer->next_offset += value_size + offset;
		return address + offset;
	}
#else
		// we just assume that that the buffer->next_offset is aligned to 256
		// bit and then incremement by that, this way the address is both
		// aligned and we have enough space, altough we loose space. We cannot
		// just add the value that we need, because then we loose the alignment
		/*long old_offset =
            __atomic_fetch_add(&buffer->next_offset, 256, __ATOMIC_SEQ_CST);
                */

		long old_offset;
		asm("mov $256, %%rax\n\t"
		    "lock xadd %%rax, %1\n\t" // add to next_trace_address, rax has to
		    // be
		    // the register containing the size;
		    //"lock xadd %%rbx, %3\n\t" // add one to the amount, i think it is
		    // fine
		    // to do it like this, we dont loose the
		    // update to the amount, it may only be
		    // scheduled after another addition to amount,
		    // but in the end, it is fine (unless you want
		    // to access a trace at runtime, but then
		    // thats not my problem anyways )
		    // rax now contains the value which we use for this trace
		    "mov %%rax, %0\n\t"
		    : "=m"(old_offset)
		    : "m"(buffer->next_offset)

		    : "rax", "rbx");

		trace->value = old_offset;
		return (long)(&buffer->data[old_offset]);

#endif
	}
}

int load_rax(char *address)
{
	// PERF: we do not have to restore rax in every case, only when it is
	// used or modified by the instruction in the case of cmpxchg it is
	// modified, so it must be reloaded

	// mov rax, -8[rbp]
	address[0] = 0x48;
	address[1] = 0x8b;
	address[2] = 0x45;
	address[3] = 0xf8;

	// mov rax, [rax + index of rax * 8] = mov rax, [rax + 13 * 8];
	address[4] = 0x48;
	address[5] = 0x8b;
	address[6] = 0x40;
	address[7] = 0x68;
	return 8;
}
// moves the value of the location the register points to to the register
// itself
static int _patch_mov_value_at_address_to_same_reg(TracerRegister reg,
						   char *address)
{
	int index = tracer_reg_to_index(reg);
	int size = 0;
	// TODO: this is not tested
	address[0] = 0x48;
	if (index >= 8) {
		address[0] = 0x4d;
	}
	size += 1;
	if (index % 8 == 4) {
		address[1] = 0x8b;
		address[2] = 0x24;
		address[3] = 0x24;
		size += 3;

	} else if (index % 8 == 5) {
		address[1] = 0x8b;
		address[2] = 0x6d;
		address[3] = 0x00;
		size += 3;

	} else {
		address[1] = 0x8b;
		address[2] = (index % 8) + (index % 8) * 8;
		size += 2;
	}

	return size;
}

static int _patch_mov_value_at_rel_address_to_reg(
	ZydisDisassembledInstruction *instruction, char *address,
	TracerRegister temp_reg)
{
	int size = 0;
	size += patch_mov_absolute_offset_to_reg(instruction, temp_reg,
						 address);
	// now, the address is in temp register, now we do mov temp_reg,
	// [temp_reg]

	size += _patch_mov_value_at_address_to_same_reg(temp_reg,
							address + size);
	return size;
};
int patch_mov_relative_to_addr_in_reg(char *address_to_patch, int op_size,
				      ZydisDisassembledInstruction *instruction,
				      AddressReg dest, used_regs *used)
{
	TracerRegister temp_reg = get_and_set_unused_register(used);
	int size = 0;

	size += _patch_mov_value_at_rel_address_to_reg(
		instruction, address_to_patch, temp_reg);
	size += _patch_mov_mem_and_reg(REG_TO_MEM, address_to_patch + size,
				       op_size, temp_reg,
				       address_reg_to_tracer_reg(dest), NULL);
	return size;
}
