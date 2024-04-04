#ifndef TTREK_BASE64_H
#define TTREK_BASE64_H

#include <tcl.h>
#include <stddef.h>
#include <stdint.h>
#include "common.h"

int base64_encode(const char *input, Tcl_Size input_length, char *output, Tcl_Size *output_length);
int base64_decode(const char* input, Tcl_Size input_length, char *output, Tcl_Size *output_length);

#endif //TTREK_BASE64_H
