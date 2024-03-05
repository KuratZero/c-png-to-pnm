//
// Created by Artemii Kazakov, ITMO.
//
#pragma once

#ifndef ERROR_MESSAGE_CANNOT_OPEN_FILE
#define ERROR_MESSAGE_CANNOT_OPEN_FILE(FILE_NAME, FILE_MODE, NUMBER_ERROR)                                             \
	fprintf(stderr, "Error while open file \"%s\" with file_mode \"%s\".\n", FILE_NAME, FILE_MODE);                    \
	return NUMBER_ERROR;
#endif

#ifndef ERROR_GOTO
#define ERROR_GOTO(NAME, NUMBER_ERROR, GOTO_LABEL)                                                                     \
	NAME = NUMBER_ERROR;                                                                                               \
	goto GOTO_LABEL;
#endif

#ifndef CHECK_ERROR_WITH_FREE
#define CHECK_ERROR_WITH_FREE(GOOD, FUNCTION, NAME, FREE)                                                              \
	int check_error_##NAME = FUNCTION;                                                                                 \
	if (check_error_##NAME != GOOD)                                                                                    \
	{                                                                                                                  \
		FREE;                                                                                                          \
		return check_error_##NAME;                                                                                     \
	}
#endif

#ifndef ERROR_MESSAGE_OUT_OF_MEMORY
#define ERROR_MESSAGE_OUT_OF_MEMORY(NAME, NUMBER_ERROR)                                                                \
	fprintf(stderr, "Error memory allocation failed for: \"%s.\"\n", NAME);                                            \
	return NUMBER_ERROR;
#endif

#ifndef CHECK_ERROR
#define CHECK_ERROR(GOOD, FUNCTION, NAME)                                                                              \
	int check_error_##NAME = FUNCTION;                                                                                 \
	if (check_error_##NAME != GOOD)                                                                                    \
	{                                                                                                                  \
		return check_error_##NAME;                                                                                     \
	}
#endif