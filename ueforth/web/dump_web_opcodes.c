#include <stdio.h>
#include <string.h>

#include "common/opcodes.h"

#define PLATFORM_OPCODE_LIST \
  X("CALL", OP_CALL, sp = Call(sp, tos); DROP) \

enum {
  OP_DOCOLON = 0,
  OP_DOCREATE = 1,
  OP_DODOES = 2,
#define X(name, op, code) op,
  PLATFORM_OPCODE_LIST
  OPCODE_LIST
#undef X
};

int main(int argc, char *argv[]) {
  if (argc == 2 && strcmp(argv[1], "cases") == 0) {
#define X(name, op, code) printf("          case %d: %s; break;\n", op, #code);
    PLATFORM_OPCODE_LIST
    OPCODE_LIST
#undef X
  } else if (argc == 2 && strcmp(argv[1], "dict") == 0) {
#define X(name, op, code) printf("  create(" #name ", %d);\n", op);
    PLATFORM_OPCODE_LIST
    OPCODE_LIST
#undef X
  } else {
    fprintf(stderr, "USAGE: %s cases/dict\n", argv[1]);
    return 1;
  }
}
