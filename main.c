#include "return_codes.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ZLIB
#ifdef ZLIB
#include <zlib.h>
#elif defined(LIBDEFLATE)
#include <libdeflate.h>
#elif defined(ISAL)
#error "ISA-L not supported"
#else
#error "No compression library defined"
#endif

typedef unsigned int uint;
typedef unsigned char uchar;

struct dataIHDR
{
	uint width;
	uint height;
	uchar bit_depth;
	uint color_type;
	uchar compression_method;
	uchar filter_method;
	uchar interlace_method;
} dataIHDR;

typedef struct chunk
{
	uint length;
	uchar type[4];
	void *data;
	uint CRC;
} chunk;

void filter(uchar *buffer, uint buffer_size, uint width, uint option);
void bigEnd_fread(void *ptr, uint size, uint count, FILE *file);

void bigEnd_fread(void *ptr, uint size, uint count, FILE *file)
{
	uchar *buf = (uchar *)ptr;
	fread(buf, size, count, file);
	for (int i = 0; i < size / 2; i++)
	{
		uchar tmp = buf[i];
		buf[i] = buf[size - i - 1];
		buf[size - i - 1] = tmp;
	}
}

void filter(uchar *buffer, uint buffer_size, uint width, uint option)
{
	for (uint i = 0; i < buffer_size; i += width)
	{
		if (buffer[i] == 0x00)
		{
			continue;
		}
		else if (buffer[i] == 0x01)
		{
			for (uint j = i + 2; j < i + width; j++)
			{
				if (j % width < option + 1)
				{
					continue;
				}
				buffer[j] += buffer[j - option];
			}
		}
		else if (buffer[i] == 0x02)
		{
			for (uint j = i + 2; j < i + width; j++)
			{
				if (j < width)
				{
					continue;
				}
				buffer[j] += buffer[j - width];
			}
		}
		else if (buffer[i] == 0x03)
		{
			for (uint j = i; j < i + width; j++)
			{
				buffer[j] += ((j % width < option + 1 ? 0 : buffer[j - option]) + (j < width ? 0 : buffer[j - width])) / 2;
			}
		}
		else if (buffer[i] == 0x04)
		{
			for (uint j = i + 1; j < i + width; j++)
			{
				int a = j % width < option + 1 ? 0 : buffer[j - option];
				int b = i == 0 ? 0 : buffer[j - width];
				int c = i == 0 || j % width < option + 1 ? 0 : buffer[j - width - option];
				int p = a + b - c;
				int pa = abs(p - a);
				int pb = abs(p - b);
				int pc = abs(p - c);
				if (pa <= pb && pa <= pc)
				{
					buffer[j] += a;
				}
				else if (pb <= pc)
				{
					buffer[j] += b;
				}
				else
				{
					buffer[j] += c;
				}
			}
		}
		else
		{
			fprintf(stderr, "Unsupported filter type\n");
			exit(ERROR_UNSUPPORTED);
		}
	}
}

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		fprintf(stderr, "Not 3 arguments\n");
		return ERROR_PARAMETER_INVALID;
	}
	FILE *png_file;
	png_file = fopen(argv[1], "rb");
	if (!png_file)
	{
		fprintf(stderr, "Cannot open file: %s\n", argv[1]);
		return ERROR_CANNOT_OPEN_FILE;
	}
	uchar signature[8];
	fread(signature, 1, 8, png_file);
	if (signature[0] != 0x89 || signature[1] != 0x50 || signature[2] != 0x4E || signature[3] != 0x47 ||
		signature[4] != 0x0D || signature[5] != 0x0A || signature[6] != 0x1A || signature[7] != 0x0A)
	{
		fprintf(stderr, "Not a PNG file\n");
		return ERROR_UNSUPPORTED;
	}
	chunk chunkIHDR;
	bigEnd_fread(&chunkIHDR.length, 4, 1, png_file);
	if (chunkIHDR.length != 13)
	{
		fprintf(stderr, "Not a PNG file\n");
		return ERROR_UNSUPPORTED;
	}
	fread(&chunkIHDR.type, 4, 1, png_file);
	if (chunkIHDR.type[0] != 0x49 || chunkIHDR.type[1] != 0x48 || chunkIHDR.type[2] != 0x44 || chunkIHDR.type[3] != 0x52)
	{
		fprintf(stderr, "Not a PNG file\n");
		return ERROR_UNSUPPORTED;
	}
	chunkIHDR.data = &dataIHDR;
	bigEnd_fread(&dataIHDR.width, 4, 1, png_file);
	bigEnd_fread(&dataIHDR.height, 4, 1, png_file);
	fread(&dataIHDR.bit_depth, 1, 1, png_file);
	if (dataIHDR.bit_depth != 0x08)
	{
		fprintf(stderr, "Unsupported bit depth\n");
		return ERROR_UNSUPPORTED;
	}
	fread(&dataIHDR.color_type, 1, 1, png_file);
	if (dataIHDR.color_type != 0x02 && dataIHDR.color_type != 0x00 && dataIHDR.color_type != 0x03)
	{
		fprintf(stderr, "Unsupported color type\n");
		return ERROR_UNSUPPORTED;
	}
	fread(&dataIHDR.compression_method, 1, 1, png_file);
	fread(&dataIHDR.filter_method, 1, 1, png_file);
	fread(&dataIHDR.interlace_method, 1, 1, png_file);
	bigEnd_fread(&chunkIHDR.CRC, 4, 1, png_file);
	uint buffer_size = 0;
	uchar *buffer = malloc(buffer_size);
	if (!buffer)
	{
		fprintf(stderr, "Cannot allocate memory\n");
		return ERROR_OUT_OF_MEMORY;
	}
	bool exit = false;
	bool IDAT = false;
	int PLTE[2] = { 0 };
	uchar *PLTE_data;
	chunk anonymous_chunk;
	while (!feof(png_file))
	{
		exit = false;
		anonymous_chunk.data = NULL;
		bigEnd_fread(&anonymous_chunk.length, 4, 1, png_file);
		fread(&anonymous_chunk.type, 4, 1, png_file);
		if (anonymous_chunk.type[0] == 'P' && anonymous_chunk.type[1] == 'L' && anonymous_chunk.type[2] == 'T' &&
			anonymous_chunk.type[3] == 'E')
		{
			if (PLTE[0] == 1)
			{
				fprintf(stderr, "Extra PLTE chunks\n");
				return ERROR_UNSUPPORTED;
			}
			PLTE[0] = 1;
			PLTE[1] = 0;
			if (IDAT)
			{
				fprintf(stderr, "PLTE chunk after IDAT chunk\n");
				return ERROR_UNSUPPORTED;
			}
			if (dataIHDR.color_type != 0x03)
			{
				fprintf(stderr, "PLTE chunk didn't expect\n");
				return ERROR_UNSUPPORTED;
			}
			if (anonymous_chunk.length % 3 != 0)
			{
				fprintf(stderr, "PLTE chunk length is not divisible by 3\n");
				return ERROR_UNSUPPORTED;
			}
			PLTE_data = malloc(anonymous_chunk.length);
			if (!PLTE_data)
			{
				fprintf(stderr, "Cannot allocate memory\n");
				return ERROR_OUT_OF_MEMORY;
			}
			fread(PLTE_data, 1, anonymous_chunk.length, png_file);
			uchar color[3] = { 0 };
			for (uint i = 0; i < anonymous_chunk.length; i += 3)
			{
				memcpy(color, PLTE_data + i, 3);
				if (color[0] != color[1] || color[0] != color[2])
				{
					PLTE[1] = 1;
				}
			}
			bigEnd_fread(&anonymous_chunk.CRC, 4, 1, png_file);
			continue;
		}
		if (anonymous_chunk.type[0] == 0x49 && anonymous_chunk.type[1] == 0x44 && anonymous_chunk.type[2] == 0x41 &&
			anonymous_chunk.type[3] == 0x54)
		{
			IDAT = true;
			buffer_size += anonymous_chunk.length;
			uchar *newBuffer = realloc(buffer, buffer_size);
			if (!newBuffer)
			{
				free(buffer);
				fprintf(stderr, "Cannot allocate memory\n");
				return ERROR_OUT_OF_MEMORY;
			}
			buffer = newBuffer;
			fread(buffer + buffer_size - anonymous_chunk.length, 1, anonymous_chunk.length, png_file);
			bigEnd_fread(&anonymous_chunk.CRC, 4, 1, png_file);
			continue;
		}
		if (anonymous_chunk.type[0] == 0x49 && anonymous_chunk.type[1] == 0x45 && anonymous_chunk.type[2] == 0x4E &&
			anonymous_chunk.type[3] == 0x44)
		{
			exit = true;
			fseek(png_file, anonymous_chunk.length + 4, SEEK_CUR);
		}
		fseek(png_file, anonymous_chunk.length, SEEK_CUR);
		bigEnd_fread(&anonymous_chunk.CRC, 4, 1, png_file);
	}
	fclose(png_file);
	if (!exit || !IDAT)
	{
		fprintf(stderr, "Invalid file\n");
		return ERROR_DATA_INVALID;
	}
	uint decompressed_size = dataIHDR.width * dataIHDR.height * 3;
	uchar *decompressed_buffer = malloc(decompressed_size);
	if (!decompressed_buffer)
	{
		fprintf(stderr, "Cannot allocate memory\n");
		return ERROR_OUT_OF_MEMORY;
	}
#ifdef ZLIB
	int result = uncompress(decompressed_buffer, (uLongf *)&decompressed_size, buffer, buffer_size);
	while (result == Z_BUF_ERROR)
	{
		decompressed_size *= 2;
		uchar *newDB = realloc(decompressed_buffer, decompressed_size);
		if (!newDB)
		{
			free(decompressed_buffer);
			fprintf(stderr, "Cannot allocate memory\n");
			return ERROR_OUT_OF_MEMORY;
		}
		decompressed_buffer = newDB;
		result = uncompress(decompressed_buffer, (uLongf *)&decompressed_size, buffer, buffer_size);
	}
#elif defined(LIBDEFLATE)
	struct libdeflate_decompressor *decompress = libdeflate_alloc_decompressor();
	uint abacaba;
	enum libdeflate_result result =
		libdeflate_deflate_decompress(decompress, buffer, buffer_size, decompressed_buffer, decompressed_size, &abacaba);
	if (reult != LIBDEFLATE_SUCCESS)
	{
		fprintf(stderr, "Uncompress error\n");
		return ERROR_DATA_INVALID;
	}
#elif defined(ISAL)
	free(decompressed_buffer);
	return ERROR_UNSUPPORTED;
#else
	free(decompressed_buffer);
	return ERROR_UNKNOWN;
#endif
	free(buffer);
	FILE *output_file;
	output_file = fopen(argv[2], "wb");
	if (!output_file)
	{
		fprintf(stderr, "Cannot open file: %s\n", argv[2]);
		return ERROR_CANNOT_OPEN_FILE;
	}
	uchar color_type[2];
	uint option = 1;
	color_type[0] = 'P';
	if (dataIHDR.color_type == 0x00)
	{
		color_type[1] = '5';
	}
	else if (dataIHDR.color_type == 0x02)
	{
		option = 3;
		color_type[1] = '6';
	}
	else if (dataIHDR.color_type == 0x03)
	{
		if (PLTE[1])
		{
			option = 3;
			color_type[1] = '6';
		}
		else
			color_type[1] = '5';
		fprintf(output_file, "%c%c\n%u %u\n255\n", color_type[0], color_type[1], dataIHDR.width, dataIHDR.height);
		for (int i = 1; i < decompressed_size; i++)
		{
			if (i % dataIHDR.width == 0)
			{
				continue;
			}
			uint shift = decompressed_buffer[i];
			fwrite(PLTE_data + shift * option, 1, option, output_file);
		}
		free(PLTE_data);
	}
	else
	{
		fprintf(stderr, "Unsupported color type\n");
		return ERROR_UNSUPPORTED;
	}
	if (dataIHDR.color_type != 3)
		filter(decompressed_buffer, decompressed_size, dataIHDR.width * option + 1, option);
	fprintf(output_file, "%c%c\n%u %u\n255\n", color_type[0], color_type[1], dataIHDR.width, dataIHDR.height);
	for (int i = 0; i < dataIHDR.height; ++i)
	{
		fwrite(decompressed_buffer + i * (dataIHDR.width * option + 1) + 1, 1, dataIHDR.width * option, output_file);
	}
	free(decompressed_buffer);
	fclose(output_file);
	return SUCCESS;
}
