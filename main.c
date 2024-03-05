//
// Created by Artemii Kazakov, ITMO.
//

#include "errors.h"
#include "return_codes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ZLIB)

#include <zlib.h>

#elif defined(LIBDEFLATE)

#include <libdeflate.h>

#elif defined(ISAL)

#include <include/igzip_lib.h>

#endif

// ---- MACROS ----
#define ARR(NAME, X, Y, N) NAME[(((X) * (N)) + (Y))]

// ---- CONSTS ----
const int COUNT_CHUNK_TYPES = 7;
const char PNG_SIGNATURE[8] = { (char)0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
const char PNG_CHUNKS_SIGNATURE[6][4] = {
	{ 0x49, 0x48, 0x44, 0x52 }, { 0x49, 0x44, 0x41, 0x54 }, { 0x49, 0x45, 0x4e, 0x44 },
	{ 0x50, 0x4c, 0x54, 0x45 }, { 0x74, 0x52, 0x4e, 0x53 }, { 0x62, 0x4b, 0x47, 0x44 }
};

// ---- STRUCTURES ----
enum filter_type
{
	NONE,
	SUB,
	UP,
	AVERAGE,
	PAETH
};

enum chunk_type
{
	IHDR,
	IDAT,
	IEND,
	PLTE,
	tRNS,
	bKGD,
	ANOTHER
};

struct chunk
{
	unsigned int length;
	enum chunk_type type;
	char *data;
};

// ---- PROTOTYPES ----

// - MAJOR -
void write_to_lines(unsigned int, unsigned int, int, int, const char *, char, const unsigned char[3], int *, char **, struct chunk, int, struct chunk);

void change_background(int, char **, char, struct chunk, unsigned char[], int *, int, struct chunk);

int read_all_chunks(FILE *, unsigned long[256], unsigned long *, char **, struct chunk *, int *, int, struct chunk **, const enum chunk_type *, int *);

int read_chunk(FILE *, struct chunk *, const unsigned long *);

enum chunk_type change_type_chunk(char[4]);

void unfilter_png(unsigned int, unsigned int, int, char *);

// - UTILS -
int check_equal_array(int, const char[], const char[]);

int read_nbytes_file(char[8], int, FILE *);

int allocate_vector(unsigned int, char **);

void free_chunk(struct chunk);

int make_int_chars4(const char[4]);

int make_int_char4(char, char, char, char);

int union_two(unsigned long, const char *, unsigned long, const char *, char **);

// - CRC -
void make_crc_table(unsigned long *);

unsigned long update_crc(const unsigned long *, unsigned long, const unsigned char *, int);

int crc(unsigned int *, const unsigned long *, const unsigned char *, const unsigned char *, int);

int main(int argc, char *argv[])
{
	if (argc != 3 && argc != 4 && argc != 6)
	{
		fprintf(stderr,
				"Number of arguments is %d, but must be not less than 2 "
				"(... input_file output_file R/X G B), where R/X G B is optional.\n",
				argc - 1);
		return ERROR_PARAMETER_INVALID;
	}

	unsigned long crc_table[256];
	make_crc_table(crc_table);

	FILE *input;
	if ((input = fopen(argv[1], "rb")) == NULL)
	{
		ERROR_MESSAGE_CANNOT_OPEN_FILE(argv[1], "rb", ERROR_CANNOT_OPEN_FILE)
	}

	char signature[8];
	if (read_nbytes_file(signature, 8, input) != SUCCESS)
	{
		fprintf(stderr, "Error while read file's signature from input file.\n");
		fclose(input);
		return ERROR_DATA_INVALID;
	}
	if (!check_equal_array(8, signature, PNG_SIGNATURE))
	{
		fprintf(stderr, "Input file is not png\n");
		fclose(input);
		return ERROR_DATA_INVALID;
	}

	struct chunk ihdr;
	CHECK_ERROR_WITH_FREE(SUCCESS, read_chunk(input, &ihdr, crc_table), read_ihdr_chunk, fclose(input))
	int error_block_1 = SUCCESS;

	if (ihdr.type != IHDR)
	{
		fprintf(stderr, "First chunk is not IHDR.\n");
		ERROR_GOTO(error_block_1, ERROR_DATA_INVALID, block_1)
	}
	unsigned int width = make_int_char4(ihdr.data[0], ihdr.data[1], ihdr.data[2], ihdr.data[3]);
	unsigned int height = make_int_char4(ihdr.data[4], ihdr.data[5], ihdr.data[6], ihdr.data[7]);
	char color_type = ihdr.data[9];
	int bytes_pixel = 1;
	int bytes_pixel_out = 1;
	if (color_type == 2)
	{
		bytes_pixel_out = bytes_pixel = 3;
	}
	else if (color_type == 3)
	{
		bytes_pixel_out = 3;
	}
	else if (color_type == 4)
	{
		bytes_pixel_out = 1;
		bytes_pixel = 2;
	}
	else if (color_type == 6)
	{
		bytes_pixel_out = 3;
		bytes_pixel = 4;
	}

	if (ihdr.data[8] != 8 || ihdr.data[10] != 0 || ihdr.data[11] != 0 || ihdr.data[12] != 0)
	{
		fprintf(stderr, "Unsupported png image.\n");
		ERROR_GOTO(error_block_1, ERROR_UNSUPPORTED, block_1)
	}

block_1:;
	free_chunk(ihdr);
	if (error_block_1 != SUCCESS)
	{
		fclose(input);
		return error_block_1;
	}

	int error_block_2 = SUCCESS;

	char *i_data = NULL;
	unsigned long i_data_len = 0;

	struct chunk plte, bkgd, trns;
	int go_plte = 0;
	int num_in_blocks = 2;
	struct chunk *in_blocks[] = { &bkgd, &trns };
	enum chunk_type name_in_blocks[] = { bKGD, tRNS };
	int go_in_blocks[] = { 0, 0 };

	int error_read_all_chunks =
		read_all_chunks(input, crc_table, &i_data_len, &i_data, &plte, &go_plte, num_in_blocks, in_blocks, name_in_blocks, go_in_blocks);
	if (error_read_all_chunks != SUCCESS)
	{
		ERROR_GOTO(error_block_2, error_read_all_chunks, block_2)
	}

	int check_end_input = fgetc(input);
	if (check_end_input != EOF)
	{
		fprintf(stderr, "Expected end of input file.\n");
		ERROR_GOTO(error_block_2, ERROR_DATA_INVALID, block_2)
	}

	if (go_in_blocks[1] && (color_type == 4 || color_type == 6))
	{
		fprintf(stderr, "Found tRNS chunk - shall not appear with color_type 4 or 6.\n");
		ERROR_GOTO(error_block_2, ERROR_DATA_INVALID, block_2)
	}

	if ((go_plte && (color_type == 0 || color_type == 4)) || (!go_plte && color_type == 3))
	{
		if (color_type == 3)
		{
			fprintf(stderr, "Not found PLTE chunk for color_type 3.\n");
		}
		else
		{
			fprintf(stderr, "Found PLTE chunk - shall not appear with color_type 0 or 4.\n");
		}
		ERROR_GOTO(error_block_2, ERROR_DATA_INVALID, block_2)
	}
	if (go_plte)
	{
		if (plte.length % 3 != 0)
		{
			fprintf(stderr, "PLTE chunk length must be divisible by 3.\n");
			ERROR_GOTO(error_block_2, ERROR_DATA_INVALID, block_2)
		}
	}

	char *png_data;
	int alloc_vec_png_data = allocate_vector(bytes_pixel * (width * height + height), &png_data);
	if (alloc_vec_png_data != SUCCESS)
	{
		fprintf(stderr, "Error allocate for png_data vector.\n");
		ERROR_GOTO(error_block_2, alloc_vec_png_data, block_2)
	}

block_2:
	fclose(input);
	if (error_block_2 != SUCCESS)
	{
		free(i_data);
		if (go_plte)
		{
			free_chunk(plte);
		}
		for (int i = 0; i < num_in_blocks; i++)
		{
			if (go_in_blocks[i])
			{
				free_chunk(*in_blocks[i]);
			}
		}
		return error_block_2;
	}

	int error_block_3 = SUCCESS;

#if defined(ZLIB)
	z_stream infl;
	infl.zalloc = Z_NULL;
	infl.zfree = Z_NULL;
	infl.avail_in = i_data_len;
	infl.next_in = (Bytef *)i_data;
	infl.avail_out = bytes_pixel * (width * height + height);
	infl.next_out = (Bytef *)png_data;
	if (inflateInit(&infl) != Z_OK)
	{
		fprintf(stderr, "Error init inflate stream.\n");
		ERROR_GOTO(error_block_3, ERROR_OUT_OF_MEMORY, block_3)
	}

	if (inflate(&infl, Z_NO_FLUSH) != Z_STREAM_END)
	{
		fprintf(stderr, "Chunks IDAT is broken with ZLIB.\n");
		ERROR_GOTO(error_block_3, ERROR_DATA_INVALID, block_3)
	}
#elif defined(LIBDEFLATE)
	struct libdeflate_decompressor *infl;
	infl = libdeflate_alloc_decompressor();
	unsigned long end;
	enum libdeflate_result error_inflate =
		libdeflate_zlib_decompress(infl, i_data, i_data_len, png_data, bytes_pixel * (width * height + height), &end);
	if (error_inflate != LIBDEFLATE_SUCCESS)
	{
		fprintf(stderr, "Error while decompress IDAT chunks with LIBDEFLATE.\n");
		ERROR_GOTO(error_block_3, ERROR_DATA_INVALID, block_3)
	}
#elif defined(ISAL)
	struct inflate_state *istate;
	struct isal_zlib_header *iheader;

	istate = malloc(sizeof(struct inflate_state));
	iheader = malloc(sizeof(struct isal_zlib_header));
	if (istate == NULL || iheader == NULL)
	{
		fprintf(stderr, "Error allocate memory for ISAL inflate.\n");
		ERROR_GOTO(error_block_3, ERROR_OUT_OF_MEMORY, block_3)
	}

	isal_inflate_init(istate);

	istate->next_in = (uint8_t *)i_data;
	istate->avail_in = i_data_len;
	istate->avail_out = bytes_pixel * (width * height + height);
	istate->next_out = (uint8_t *)png_data;

	int error_read_header = isal_read_zlib_header(istate, iheader);
	if (error_read_header != ISAL_DECOMP_OK)
	{
		fprintf(stderr, "Error while read header with ISAL.\n");
		ERROR_GOTO(error_block_3, ERROR_DATA_INVALID, block_3)
	}

	int error_inflate = isal_inflate(istate);

	if (error_inflate != ISAL_DECOMP_OK)
	{
		fprintf(stderr, "Error while decompress IDAT chunks with ISAL.\n");
		ERROR_GOTO(error_block_3, ERROR_DATA_INVALID, block_3)
	}
#endif

block_3:;
#if defined(ZLIB)
	int error_infl_end = inflateEnd(&infl);
	if (error_infl_end)
	{
		fprintf(stderr, "Unknown error_inflate while inflate End with ZLIB.\n");
		error_block_3 = ERROR_UNKNOWN;
	}
#elif defined(LIBDEFLATE)
	libdeflate_free_decompressor(infl);
#elif defined(ISAL)
	if (istate != NULL)
	{
		free(istate);
	}
	if (iheader != NULL)
	{
		free(iheader);
	}
#endif
	free(i_data);

	char *lines;
	int allocate_result_vector = allocate_vector(width * height * bytes_pixel_out * sizeof(char), &lines);
	if (allocate_result_vector != SUCCESS)
	{
		error_block_3 = allocate_result_vector;
	}

	if (error_block_3 != SUCCESS)
	{
		free(png_data);
		if (go_plte)
		{
			free_chunk(plte);
		}
		for (int i = 0; i < num_in_blocks; i++)
		{
			if (go_in_blocks[i])
			{
				free_chunk(*in_blocks[i]);
			}
		}
		return error_block_3;
	}

	unfilter_png(width, height, bytes_pixel, png_data);

	unsigned char background[3] = { 0, 0, 0 };
	int go_background = 0;
	change_background(argc, argv, color_type, plte, background, &go_background, go_in_blocks[0], (*in_blocks[0]));

	int pos = 0;
	write_to_lines(width, height, bytes_pixel, bytes_pixel_out, png_data, color_type, background, &pos, &lines, plte, go_in_blocks[1], (*in_blocks[1]));

	free(png_data);
	if (go_plte)
	{
		free_chunk(plte);
	}
	for (int i = 0; i < num_in_blocks; i++)
	{
		if (go_in_blocks[i])
		{
			free_chunk(*in_blocks[i]);
		}
	}

	FILE *output;
	if ((output = fopen(argv[2], "wb")) == NULL)
	{
		free(lines);
		ERROR_MESSAGE_CANNOT_OPEN_FILE(argv[2], "wb", ERROR_CANNOT_OPEN_FILE)
	}

	fprintf(output, "P%c\n", ((color_type == 0 || color_type == 4) ? '5' : '6'));
	fprintf(output, "%u %u\n", width, height);
	fprintf(output, "255\n");

	unsigned long error_puts = fwrite(lines, sizeof(char), width * height * bytes_pixel_out, output);
	int main_return_code = SUCCESS;
	if (error_puts != pos)
	{
		fprintf(stderr, "Error write in output file TOTAL OUT = %lu\\%d\n", error_puts, pos);
		main_return_code = ERROR_UNKNOWN;
	}

	fclose(output);
	free(lines);
	return main_return_code;
}

// - MAJOR -

void write_to_lines(
	unsigned int width,
	unsigned int height,
	int bytes_pixel,
	int bytes_pixel_out,
	const char *png_data,
	char color_type,
	const unsigned char background[3],
	int *pos,
	char **lines,
	struct chunk plte,
	int go_trns,
	struct chunk trns)
{
	for (int i = 0; i < height; i++)
	{
		for (int j = 1; j < width * bytes_pixel + 1; j++)
		{
			if (color_type == 6 || color_type == 4)
			{
				int ialp = (unsigned char)ARR(png_data, i, j + bytes_pixel_out, width * bytes_pixel + 1);
				float alpha = (float)ialp / 255;
				for (int it = 0; it < bytes_pixel_out; it++)
				{
					unsigned char pix = ARR(png_data, i, j + it, width * bytes_pixel + 1);
					(*lines)[(*pos)++] = (char)(alpha * (float)pix + (1 - alpha) * (float)background[it]);
				}
				j += bytes_pixel_out;
			}
			if (color_type == 3)
			{
				unsigned char number_plte_block = ARR(png_data, i, j, width * bytes_pixel + 1);
				char r = ARR(plte.data, number_plte_block, 0, 3);
				char g = ARR(plte.data, number_plte_block, 1, 3);
				char b = ARR(plte.data, number_plte_block, 2, 3);
				if (go_trns)
				{
					if (number_plte_block < trns.length)
					{
						int ialp = (unsigned char)trns.data[number_plte_block];
						float alpha = (float)ialp / 255;
						r = (char)(alpha * (float)r + (1 - alpha) * (float)background[0]);
						g = (char)(alpha * (float)g + (1 - alpha) * (float)background[1]);
						b = (char)(alpha * (float)b + (1 - alpha) * (float)background[2]);
					}
				}
				(*lines)[(*pos)++] = r;
				(*lines)[(*pos)++] = g;
				(*lines)[(*pos)++] = b;
			}
			if (color_type == 2)
			{
				char r = ARR(png_data, i, j++, width * bytes_pixel + 1);
				char g = ARR(png_data, i, j++, width * bytes_pixel + 1);
				char b = ARR(png_data, i, j, width * bytes_pixel + 1);
				if (go_trns)
				{
					for (int ti = 0; ti < trns.length / 6; ti++)
					{
						if (r == ARR(trns.data, ti, 1, 6) && g == ARR(trns.data, ti, 3, 6) && b == ARR(trns.data, ti, 5, 6))
						{
							r = (char)background[0];
							g = (char)background[1];
							b = (char)background[2];
							break;
						}
					}
				}
				(*lines)[(*pos)++] = r;
				(*lines)[(*pos)++] = g;
				(*lines)[(*pos)++] = b;
			}
			if (color_type == 0)
			{
				char x = ARR(png_data, i, j, width * bytes_pixel + 1);
				if (go_trns)
				{
					for (int ti = 0; ti < trns.length / 2; ti++)
					{
						if (x == ARR(trns.data, ti, 1, 2))
						{
							x = (char)background[0];
						}
					}
				}
				(*lines)[(*pos)++] = x;
			}
		}
	}
}

void change_background(int argc, char *argv[], char color_type, struct chunk plte, unsigned char background[], int *go_background, int go_bkgd, struct chunk bkgd)
{
	if (argc == 4)
	{
		unsigned char x = strtol(argv[3], NULL, 10);
		if (color_type == 3)
		{
			if (x < plte.length / 3)
			{
				background[0] = ARR(plte.data, x, 0, 3);
				background[1] = ARR(plte.data, x, 1, 3);
				background[2] = ARR(plte.data, x, 2, 3);
			}
		}
		else
		{
			background[0] = background[1] = background[2] = (char)x;
		}
		*go_background = 1;
	}
	else if (argc == 6)
	{
		for (int i = 0; i < 3; i++)
		{
			background[i] = (char)strtol(argv[3 + i], NULL, 10);
		}
		*go_background = 1;
	}

	if (!(*go_background) && go_bkgd)
	{
		if (color_type == 6 || color_type == 2)
		{
			background[0] = bkgd.data[1];
			background[1] = bkgd.data[3];
			background[2] = bkgd.data[5];
		}
		if (color_type == 0 || color_type == 4)
		{
			background[0] = background[1] = background[2] = bkgd.data[1];
		}
		if (color_type == 3)
		{
			unsigned char index_plte = bkgd.data[0];
			background[0] = ARR(plte.data, index_plte, 0, 3);
			background[1] = ARR(plte.data, index_plte, 1, 3);
			background[2] = ARR(plte.data, index_plte, 2, 3);
		}
	}
}

int read_all_chunks(
	FILE *input,
	unsigned long crc_table[256],
	unsigned long *i_data_len,
	char **i_data,
	struct chunk *plte,
	int *plte_go,
	int num_in_blocks,
	struct chunk **in_blocks,
	const enum chunk_type *name_in_blocks,
	int *go_in_blocks)
{
	int return_code = SUCCESS;
	struct chunk chunk;
	do
	{
		CHECK_ERROR(SUCCESS, read_chunk(input, &chunk, crc_table), read_chunk_error_check)
		if (chunk.type == IHDR)
		{
			fprintf(stderr, "More than one IHDR chunk found - error.\n");
			return_code = ERROR_DATA_INVALID;
			goto end_read;
		}
		else if (chunk.type == PLTE)
		{
			if (!(*plte_go))
			{
				(*plte) = chunk;
				*plte_go = 1;
			}
			else
			{
				fprintf(stderr, "More than one PLTE chunk found - error.\n");
				return_code = ERROR_DATA_INVALID;
				goto end_read;
			}
			continue;
		}
		else if (chunk.type == IDAT)
		{
			char *t;
			unsigned long new_i_data_len = (*i_data_len) + chunk.length;
			union_two((*i_data_len), (*i_data), chunk.length, chunk.data, &t);
			free((*i_data));
			(*i_data_len) = new_i_data_len;
			(*i_data) = t;
		}
		else
		{
			int check_inp = 0;
			for (int i = 0; i < num_in_blocks; i++)
			{
				if (name_in_blocks[i] == chunk.type)
				{
					check_inp = 1;
					if (!go_in_blocks[i])
					{
						(*in_blocks)[i] = chunk;
						go_in_blocks[i] = 1;
					}
					else
					{
						if (name_in_blocks[i] == tRNS)
						{
							char *tmp_in_block;
							union_two((*in_blocks)[i].length, (*in_blocks)[i].data, chunk.length, chunk.data, &tmp_in_block);
							free_chunk(chunk);
							chunk.length = (*in_blocks)[i].length + chunk.length;
							chunk.data = tmp_in_block;
						}
						free_chunk((*in_blocks)[i]);
						(*in_blocks)[i] = chunk;
					}
				}
			}
			if (check_inp)
			{
				continue;
			}
		}
		free_chunk(chunk);
	} while (chunk.type != IEND);

end_read:
	if (return_code != SUCCESS)
	{
		if (*plte_go)
		{
			free_chunk(*plte);
		}
		for (int i = 0; i < num_in_blocks; i++)
		{
			if (go_in_blocks[i])
			{
				free_chunk(*(in_blocks[i]));
			}
		}
	}
	return return_code;
}

int read_chunk(FILE *input, struct chunk *chunk, const unsigned long *crc_table)
{
	unsigned int len;
	char len_inp[4];
	char type_inp[4];
	char *data;
	unsigned int crc1;
	char crc_inp[4];
	enum chunk_type type;

	CHECK_ERROR(SUCCESS, read_nbytes_file(len_inp, 4, input), read_nbytes_file_error_length)

	len = make_int_chars4(len_inp);

	CHECK_ERROR(SUCCESS, read_nbytes_file(type_inp, 4, input), read_nbytes_file_error_type)
	type = change_type_chunk(type_inp);

	CHECK_ERROR(SUCCESS, allocate_vector(len, &data), alloc_vec)
	CHECK_ERROR_WITH_FREE(SUCCESS, read_nbytes_file(data, len, input), read_nbytes_file_error_data, free(data))

	CHECK_ERROR_WITH_FREE(SUCCESS, read_nbytes_file(crc_inp, 4, input), read_nbytes_file_error_crc, free(data))

	crc1 = make_int_chars4(crc_inp);
	unsigned int crc2;
	CHECK_ERROR_WITH_FREE(SUCCESS, crc(&crc2, crc_table, (unsigned char *)type_inp, (unsigned char *)data, len), crc_compute, free(data))
	if (crc1 != crc2 && type < 3)
	{
		fprintf(stderr, "No good crc for main chunk.\n");
		free(data);
		return ERROR_DATA_INVALID;
	}

	chunk->length = len;
	chunk->type = type;
	chunk->data = data;

	return SUCCESS;
}

enum chunk_type change_type_chunk(char inp[4])
{
	for (int i = 0; i < COUNT_CHUNK_TYPES - 1; i++)
	{
		if (check_equal_array(4, inp, PNG_CHUNKS_SIGNATURE[i]))
		{
			return (enum chunk_type)i;
		}
	}
	return ANOTHER;
}

void unfilter_png(unsigned int width, unsigned int height, int bytes_pixel, char *png_data)
{
	unsigned long delm = width * bytes_pixel + 1;

	for (int i = 0; i < height; i++)
	{
		enum filter_type filter = (enum filter_type)ARR(png_data, i, 0, delm);

		if (filter == NONE)
			continue;
		for (int j = 1; j < width * bytes_pixel + 1; j++)
		{
			unsigned char a_byte = (j - bytes_pixel >= 1 ? ARR(png_data, i, j - bytes_pixel, delm) : 0);
			unsigned char b_byte = (i - 1 >= 0 ? ARR(png_data, i - 1, j, delm) : 0);
			unsigned char c_byte = (i - 1 >= 0 && j - bytes_pixel >= 1 ? ARR(png_data, i - 1, j - bytes_pixel, delm) : 0);
			if (filter == SUB)
			{
				ARR(png_data, i, j, delm) += a_byte;
			}
			else if (filter == UP)
			{
				ARR(png_data, i, j, delm) += b_byte;
			}
			else if (filter == AVERAGE)
			{
				ARR(png_data, i, j, delm) += floor(((double)a_byte + (double)b_byte) / 2.0);
			}
			else if (filter == PAETH)
			{
				int p_byte = (int)a_byte + (int)b_byte - (int)c_byte;
				int pa = abs(p_byte - (int)a_byte);
				int pb = abs(p_byte - (int)b_byte);
				int pc = abs(p_byte - (int)c_byte);
				unsigned char rec;
				if (pa <= pb && pa <= pc)
				{
					rec = a_byte;
				}
				else if (pb <= pc)
				{
					rec = b_byte;
				}
				else
				{
					rec = c_byte;
				}
				ARR(png_data, i, j, delm) += rec;
			}
		}
	}
}

// ---- UTILS -----

int check_equal_array(int n, const char a[], const char b[])
{
	for (int i = 0; i < n; i++)
	{
		if (a[i] != b[i])
			return 0;
	}
	return 1;
}

int read_nbytes_file(char *arr, int n, FILE *input)
{
	for (int i = 0; i < n; i++)
	{
		int t = fgetc(input);
		if (t == EOF)
		{
			fprintf(stderr, "Unexpected end of file or error while read from file.\n");
			return ERROR_DATA_INVALID;
		}
		arr[i] = (char)t;
	}
	return SUCCESS;
}

int allocate_vector(unsigned int n, char **vector)
{
	*vector = malloc(sizeof(char) * n);
	if (*vector == NULL)
	{
		ERROR_MESSAGE_OUT_OF_MEMORY("vector", ERROR_OUT_OF_MEMORY)
	}
	return SUCCESS;
}

void free_chunk(struct chunk chunk)
{
	if (chunk.data != NULL)
	{
		free(chunk.data);
	}
}

int make_int_chars4(const char arr[4])
{
	int result = 0;
	for (int it = 24, i = 0; it >= 0; it -= 8, i++)
	{
		result |= ((unsigned char)arr[i]) << it;
	}
	return result;
}

int make_int_char4(char c1, char c2, char c3, char c4)
{
	char arr[4] = { c1, c2, c3, c4 };
	return make_int_chars4(arr);
}

int union_two(unsigned long a_len, const char *a, unsigned long b_len, const char *b, char **res)
{
	CHECK_ERROR(SUCCESS, allocate_vector(a_len + b_len, res), allocate_res)
	for (unsigned long i = 0; i < a_len + b_len; i++)
	{
		if (i < a_len)
		{
			(*res)[i] = a[i];
		}
		else
		{
			(*res)[i] = b[i - a_len];
		}
	}
	return SUCCESS;
}

// ---- CRC ----
void make_crc_table(unsigned long *crc_table)
{
	unsigned long c;
	int n, k;

	for (n = 0; n < 256; n++)
	{
		c = (unsigned long)n;
		for (k = 0; k < 8; k++)
		{
			if (c & 1)
				c = 0xedb88320L ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc_table[n] = c;
	}
}

unsigned long update_crc(const unsigned long *crc_table, unsigned long crc, const unsigned char *buf, int len)
{
	unsigned long c = crc;
	int n;

	for (n = 0; n < len; n++)
	{
		c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

int crc(unsigned int *result, const unsigned long *crc_table, const unsigned char *type, const unsigned char *data, int len)
{
	unsigned char *tmp;
	CHECK_ERROR(SUCCESS, allocate_vector(4 + len, (char **)&tmp), allocate_tmp_vec)
	for (int i = 0; i < 4 + len; i++)
	{
		tmp[i] = i < 4 ? type[i] : data[i - 4];
	}
	*result = update_crc(crc_table, 0xffffffffL, tmp, 4 + len) ^ 0xffffffffL;
	free(tmp);
	return SUCCESS;
}