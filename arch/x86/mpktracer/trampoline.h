
#pragma once

#include "allocator.h"
#include "context.h"
#include "register.h"
#ifdef TRACER_USERSPACE
#include <Zydis/Disassembler.h>
#include <stdlib.h>
#endif
typedef struct {
	char *address_of_instruction;
	char *rip_of_instruction;
	int num_instructions;
	long original_address;
	int index;
	ZydisDisassembledInstruction *instructions[5];

} ProbeSite;

#define STORE_SIZE_END 48
enum {
	NEW_RSP_INDEX,
	OLD_RSP_INDEX,
	RETURN_ADDRESS_INDEX,
	JUMP_TAKEN_RETURN_ADDRESS_INDEX,
	TAKEN_PROBE_ADDRESS,
	TEMP_RSP
};
typedef enum {
	TRACER_DO_TRACE,
	TRACER_DO_NOT_TRACE,
	TRACER_UNDECIDED
} tracer_follow_type;

#define NUM_INVALID_OPCODES 23
extern unsigned char invalid_opcodes[23];
void install_trampoline(long address, int pkey, char first_byte,
			int is_tracing_following, long original_address);
int write_trampoline(ProbeSite *probe_site, long address_of_trampoline,

		     int pkey, int is_tracing_following);
#define MAX_DISPLACED_INSTRUCTIONS 20000
// PERF: using this struct has 2 disadvantages: 1) it has a capped limit of
// possible displacements 2) search time increases linearly, it should probably
// be replaced by a hashtable
#define MAX_CHARS_DISPLACED_LOCATION \
	20 // thats 5 + the 15 of the last instruction
typedef struct {
	char instruction[20];
	long orig_address;
	tracer_follow_type type;
#if defined(TRACER_CACHE_INSTRUCTIONS) || defined(TRACER_ENCODE_INDEX_ON_TRAMPOLINE)
	ZydisDisassembledInstruction disassembled_instructions[5];
	long orig_addresses[5];
#endif
} DisplacedInstructionLocation;

typedef struct {
	DisplacedInstructionLocation instructions[MAX_DISPLACED_INSTRUCTIONS];
	int next_index;
} DisplacedInstructions;
long find_displaced_location(long address_of_instruciton,
			     DisplacedInstructions *displaced_instructions,
			     int *is_following,
			     tracer_follow_type *following_must_be_traced);
DisplacedInstructionLocation *
get_displaced_location_info(long address_of_instruciton,
			    DisplacedInstructions *displaced_instructions);

void set_following_must_be_traced(long original_address,
				  tracer_follow_type type);
int patch_execute(ProbeSite *probe_site, char *address, long *store_at_end,
		  int is_single_instruction, long address_of_single_instruction,
		  ContextInfo *context_info, int pkey,
		  int is_tracing_following, AddressReg value_reg);

int patch_reg_mov(char *address_to_patch, TracerRegister from,
		  TracerRegister to);
