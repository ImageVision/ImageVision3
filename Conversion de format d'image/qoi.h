#ifndef QOI_H
#define QOI_H

#ifdef __cplusplus
extern "C" {
#endif

#define QOI_SRGB 0x00
#define QOI_SRGB_LINEAR_ALPHA 0x01
#define QOI_LINEAR 0x0f

typedef struct {
	unsigned int width;
	unsigned int height;
	unsigned char channels;
	unsigned char colorspace;
} qoi_desc;

#ifndef QOI_NO_STDIO

int qoi_write(const char *filename, const void *data, const qoi_desc *desc);

void *qoi_read(const char *filename, qoi_desc *desc, int channels);

#endif // QOI_NO_STDIO

void *qoi_encode(const void *data, const qoi_desc *desc, int *out_len);

void *qoi_decode(const void *data, int size, qoi_desc *desc, int channels);


#ifdef __cplusplus
}
#endif
#endif // QOI_H


// -----------------------------------------------------------------------------
// Implementation

#ifdef QOI_IMPLEMENTATION
#include <stdlib.h>

#ifndef QOI_MALLOC
	#define QOI_MALLOC(sz) (unsigned char *)malloc(sz)
	#define QOI_FREE(p)    free(p)
#endif

#define QOI_INDEX   0x00 // 00xxxxxx
#define QOI_DIFF_8  0x40 // 01xxxxxx
#define QOI_DIFF_16 0x80 // 10xxxxxx
#define QOI_RUN_8   0xc0 // 110xxxxx
#define QOI_DIFF_24 0xe0 // 1110xxxx
#define QOI_COLOR   0xf0 // 1111xxxx

#define QOI_MASK_2  0xc0 // 11000000
#define QOI_MASK_3  0xe0 // 11100000
#define QOI_MASK_4  0xf0 // 11110000

#define QOI_COLOR_HASH(C) qoi_color_hash(C)
#define QOI_MAGIC \
	(((unsigned int)'q') << 24 | ((unsigned int)'o') << 16 | \
	 ((unsigned int)'i') <<  8 | ((unsigned int)'f'))
#define QOI_HEADER_SIZE 14
#define QOI_PADDING 4

#define QOI_RANGE(value, limit) ((value) >= -(limit) && (value) < (limit))

#define QOI_CHUNK_W 16
#define QOI_CHUNK_H 16
//#define QOI_CHUNKS_SEPARATE
#define QOI_REORDER_PIXELS

typedef union {
	struct { unsigned char r, g, b, a; } rgba;
	unsigned int v;
} qoi_rgba_t;

unsigned int qoi_color_hash(qoi_rgba_t px)
{
	unsigned int h = px.rgba.b | (px.rgba.g << 8) | (px.rgba.r << 16);
	h ^= h >> 15;
	h *= 0xdb91908du;
	h ^= h >> 16;
	h *= 0x6be5be6fu;
	h ^= h >> 17;
	return h ^ px.rgba.a;
}

void qoi_write_32(unsigned char *bytes, int *p, unsigned int v) {
	bytes[(*p)++] = (0xff000000 & v) >> 24;
	bytes[(*p)++] = (0x00ff0000 & v) >> 16;
	bytes[(*p)++] = (0x0000ff00 & v) >> 8;
	bytes[(*p)++] = (0x000000ff & v);
}

unsigned int qoi_read_32(const unsigned char *bytes, int *p) {
	unsigned int a = bytes[(*p)++];
	unsigned int b = bytes[(*p)++];
	unsigned int c = bytes[(*p)++];
	unsigned int d = bytes[(*p)++];
	return (a << 24) | (b << 16) | (c << 8) | d;
}

void *qoi_encode(const void *data, const qoi_desc *desc, int *out_len) {
	if (
		data == NULL || out_len == NULL || desc == NULL ||
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		(desc->colorspace & 0xf0) != 0
	) {
		return NULL;
	}

	int max_size = 
		desc->width * desc->height * (desc->channels + 1) + 
		QOI_HEADER_SIZE + QOI_PADDING;

	int p = 0;
	unsigned char *bytes = QOI_MALLOC(max_size);
	if (!bytes) {
		return NULL;
	}

	qoi_write_32(bytes, &p, QOI_MAGIC);
	qoi_write_32(bytes, &p, desc->width);
	qoi_write_32(bytes, &p, desc->height);
	bytes[p++] = desc->channels;
	bytes[p++] = desc->colorspace;


	const unsigned char *pixels = (const unsigned char *)data;

	qoi_rgba_t index[64] = {0};

	int run = 0;
	int mode = 0;
	qoi_rgba_t px_prev = {.rgba = {.r = 0, .g = 0, .b = 0, .a = 255}};
	qoi_rgba_t px = px_prev;
	
	int px_len = desc->width * desc->height * desc->channels;
	int px_end = px_len - desc->channels;
	int chunks_x_count = desc->width / QOI_CHUNK_W;
	int chunks_y_count = desc->height / QOI_CHUNK_H;
	
	for (int chunk_y = 0; chunk_y < chunks_y_count; chunk_y++) {
		for (int chunk_x = 0; chunk_x < chunks_x_count; chunk_x++) {
			
			int x_pixels = QOI_CHUNK_W;
			int y_pixels = QOI_CHUNK_H;
			
			if(chunk_x == chunks_x_count - 1) {
				x_pixels = desc->width - (chunks_x_count - 1) * QOI_CHUNK_W;
			}
			
			if(chunk_y == chunks_y_count - 1) {
				y_pixels = desc->height - (chunks_y_count - 1) * QOI_CHUNK_H;
			}
			
			#ifdef QOI_CHUNKS_SEPARATE
			memset(index, 0, sizeof(qoi_rgba_t) * 64);
			run = 0;
			px_prev = (qoi_rgba_t) {.rgba = {.r = 0, .g = 0, .b = 0, .a = 255}};
			px = px_prev;
			mode = 0;
			#endif
			
			for(int y = 0; y < y_pixels; y++) {
				for(int x = 0; x < x_pixels; x++) {
					
					int px_pos = chunk_x * QOI_CHUNK_W;
					#ifdef QOI_REORDER_PIXELS
					px_pos += (y&1) ? (x_pixels - x - 1) : x;
					#else
					px_pos += x;
					#endif
					
					px_pos += (chunk_y * QOI_CHUNK_H + y) * desc->width;
					px_pos *= desc->channels;
					
					if (desc->channels == 4) {
						px = *(qoi_rgba_t *)(pixels + px_pos);
					}
					else {
						px.rgba.r = pixels[px_pos];
						px.rgba.g = pixels[px_pos+1];
						px.rgba.b = pixels[px_pos+2];
					}

					if (px.v == px_prev.v) {
						run++;
					}
					else {
						if (mode == 0 && px.rgba.a > 0 && px.rgba.a < 255)
						{
							// switch to alpha mode and stay that way
							bytes[p++] = QOI_COLOR;
							mode = 1;
						}
					}
					
					#ifdef QOI_CHUNKS_SEPARATE
					int chunk_end = (x + y * x_pixels) == (x_pixels * y_pixels - 1);
					#else
					int chunk_end = 0;
					#endif

					if (run > 0 && (px.v != px_prev.v || px_pos == px_end || chunk_end)) {
						int len;
						int start = p;
						--run;
						do
						{
							bytes[p++] = QOI_RUN_8 | (run & 0x1f);
							run >>= 5;
						} while (run > 0);

						// swap to make big endian
						len = (p - start) >> 1;
						for (int i=0; i<len; i++)
						{
							unsigned char tmp = bytes[start + i];
							bytes[start + i] = bytes[p - 1 - i];
							bytes[p - 1 - i] = tmp;
						}

						run = 0;
					}

					if (px.v != px_prev.v) {
						int index_pos = QOI_COLOR_HASH(px) % 64;

						if (index[index_pos].v == px.v) {
							bytes[p++] = QOI_INDEX | index_pos;
						}
						else {
							index[index_pos] = px;

							int vr = px.rgba.r - px_prev.rgba.r;
							int vg = px.rgba.g - px_prev.rgba.g;
							int vb = px.rgba.b - px_prev.rgba.b;
							int va = px.rgba.a - px_prev.rgba.a;

							if (mode == 0)
							{
								// color mode
								if (
									va == 0 && QOI_RANGE(vr, 64) &&
									 QOI_RANGE(vg, 64) && QOI_RANGE(vb, 32)
								) {
									if (
										QOI_RANGE(vr, 2) &&
										QOI_RANGE(vg, 2) && QOI_RANGE(vb, 2)
									) {
										bytes[p++] = QOI_DIFF_8 | ((vr + 2) << 4) | (vg + 2) << 2 | (vb + 2);
									}
									else if (
										QOI_RANGE(vr, 16) &&
										QOI_RANGE(vg, 16) && QOI_RANGE(vb, 8)
									) {
										unsigned int value =
											(QOI_DIFF_16 << 8) | ((vr + 16) << 9) |
											((vg + 16) << 4) | (vb + 8);
										bytes[p++] = (unsigned char)(value >> 8);
										bytes[p++] = (unsigned char)(value);
									}
									else {
										// better to encode color?
										if (1 + (vr != 0) + (vg != 0) + (vb != 0) + (va != 0) < 3)
											goto encodecolor;

										unsigned int value =
											(QOI_DIFF_24 << 16) | ((vr + 64) << 13) |
											((vg + 64) << 6) | (vb + 32);

										bytes[p++] = (unsigned char)(value >> 16);
										bytes[p++] = (unsigned char)(value >> 8);
										bytes[p++] = (unsigned char)(value);
									}
								}
								else {
									goto encodecolor;
								}
							}
							else
							{
								// alpha mode
								if (
									QOI_RANGE(vr, 16) && QOI_RANGE(vg, 16) &&
									QOI_RANGE(vb, 16) && QOI_RANGE(va, 16)
								) {
									if (
										va == 0 && QOI_RANGE(vr, 2) &&
										QOI_RANGE(vg, 2) && QOI_RANGE(vb, 2)
									) {
										bytes[p++] = QOI_DIFF_8 | ((vr + 2) << 4) | (vg + 2) << 2 | (vb + 2);
									}
									else if (
										QOI_RANGE(va, 2) && QOI_RANGE(vr, 8) &&
										QOI_RANGE(vg, 8) && QOI_RANGE(vb, 8)
									) {
										unsigned int value =
											(QOI_DIFF_16 << 8) | ((va + 2) << 12) | ((vr + 8) << 8) |
											((vg + 8) << 4) | (vb + 8);

										bytes[p++] = (unsigned char)(value >> 8);
										bytes[p++] = (unsigned char)(value);
									}
									else {
										// better to encode color?
										if (1 + (vr != 0) + (vg != 0) + (vb != 0) + (va != 0) < 3)
											goto encodecolor;

										unsigned int value =
											(QOI_DIFF_24 << 16) | ((va + 16) << 15) | ((vr + 16) << 10) |
											((vg + 16) << 5) | (vb + 16);

										bytes[p++] = (unsigned char)(value >> 16);
										bytes[p++] = (unsigned char)(value >> 8);
										bytes[p++] = (unsigned char)(value);
									}
								}
								else {
								encodecolor:
									bytes[p++] = QOI_COLOR | (vr?8:0)|(vg?4:0)|(vb?2:0)|(va?1:0);
									if (vr) { bytes[p++] = px.rgba.r; }
									if (vg) { bytes[p++] = px.rgba.g; }
									if (vb) { bytes[p++] = px.rgba.b; }
									if (va) { bytes[p++] = px.rgba.a; }
								}
							}
						}
					}
					px_prev = px;
				}
			}
		}
	}

	for (int i = 0; i < QOI_PADDING; i++) {
		bytes[p++] = 0;
	}

	*out_len = p;
	return bytes;
}

void *qoi_decode(const void *data, int size, qoi_desc *desc, int channels) {
	if (
		data == NULL || desc == NULL ||
		(channels != 0 && channels != 3 && channels != 4) ||
		size < QOI_HEADER_SIZE + QOI_PADDING
	) {
		return NULL;
	}

	const unsigned char *bytes = (const unsigned char *)data;
	int p = 0;

	unsigned int header_magic = qoi_read_32(bytes, &p);
	desc->width = qoi_read_32(bytes, &p);
	desc->height = qoi_read_32(bytes, &p);
	desc->channels = bytes[p++];
	desc->colorspace = bytes[p++];

	if (
		desc->width == 0 || desc->height == 0 || 
		desc->channels < 3 || desc->channels > 4 ||
		header_magic != QOI_MAGIC
	) {
		return NULL;
	}

	if (channels == 0) {
		channels = desc->channels;
	}

	int px_len = desc->width * desc->height * channels;
	unsigned char *pixels = QOI_MALLOC(px_len);
	if (!pixels) {
		return NULL;
	}

	qoi_rgba_t px = {.rgba = {.r = 0, .g = 0, .b = 0, .a = 255}};
	qoi_rgba_t index[64] = {0};

	int run = 0;
	int mode = 0;
	int chunks_len = size - QOI_PADDING;
	
	int chunks_x_count = desc->width / QOI_CHUNK_W;
	int chunks_y_count = desc->height / QOI_CHUNK_H;
	
	for (int chunk_y = 0; chunk_y < chunks_y_count; chunk_y++) {
		for (int chunk_x = 0; chunk_x < chunks_x_count; chunk_x++) {
			
			int x_pixels = QOI_CHUNK_W;
			int y_pixels = QOI_CHUNK_H;
			
			if(chunk_x == chunks_x_count - 1) {
				x_pixels = desc->width - (chunks_x_count - 1) * QOI_CHUNK_W;
			}
			
			if(chunk_y == chunks_y_count - 1) {
				y_pixels = desc->height - (chunks_y_count - 1) * QOI_CHUNK_H;
			}
			
			#ifdef QOI_CHUNKS_SEPARATE
			memset(index, 0, sizeof(qoi_rgba_t) * 64);
			px = (qoi_rgba_t) {.rgba = {.r = 0, .g = 0, .b = 0, .a = 255}};
			run = 0;
			mode = 0;
			#endif
			
			for(int y = 0; y < y_pixels; y++) {
				for(int x = 0; x < x_pixels; x++) {
					
					int px_pos = chunk_x * QOI_CHUNK_W;
					#ifdef QOI_REORDER_PIXELS
					px_pos += (y&1) ? (x_pixels - x - 1) : x;
					#else
					px_pos += x;
					#endif
					
					px_pos += (chunk_y * QOI_CHUNK_H + y) * desc->width;
					px_pos *= channels;
					
					if (run > 0) {
						run--;
					}
					else if (p < chunks_len) {
						int b1 = bytes[p++];

						if ((b1 & QOI_MASK_2) == QOI_INDEX) {
							px = index[b1 ^ QOI_INDEX];
						}
						else if ((b1 & QOI_MASK_3) == QOI_RUN_8) {
							run = b1 & 0x1f;
							while (p < chunks_len && ((b1 = bytes[p]) & QOI_MASK_3) == QOI_RUN_8)
							{
								p++;
								run <<= 5;
								run += b1 & 0x1f;
							}
							// no need to increment here, one implied copy
						}
						else if ((b1 & QOI_MASK_2) == QOI_DIFF_8) {
							px.rgba.r += ((b1 >> 4) & 0x03) - 2;
							px.rgba.g += ((b1 >> 2) & 0x03) - 2;
							px.rgba.b += ( b1       & 0x03) - 2;
						}
						else if ((b1 & QOI_MASK_2) == QOI_DIFF_16) {
							b1 = (b1 << 8) + bytes[p++];

							if (mode == 0) {
								px.rgba.r += ((b1 >> 9) & 0x1f) - 16;
								px.rgba.g += ((b1 >> 4) & 0x1f) - 16;
								px.rgba.b += (b1 & 0x0f) - 8;
							}
							else {
								px.rgba.r += ((b1 >> 8) & 0x0f) - 8;
								px.rgba.g += ((b1 >> 4) & 0x0f) - 8;
								px.rgba.b += (b1 & 0x0f) - 8;
								px.rgba.a += ((b1 >> 12) & 0x03) - 2;
							}
						}
						else if ((b1 & QOI_MASK_4) == QOI_DIFF_24) {
							b1 <<= 16;
							b1 |= bytes[p++] << 8;
							b1 |= bytes[p++];

							if (mode == 0) {
								px.rgba.r += ((b1 >> 13) & 0x7f) - 64;
								px.rgba.g += ((b1 >> 6) & 0x7f) - 64;
								px.rgba.b += (b1 & 0x3f) - 32;
							}
							else {
								px.rgba.r += ((b1 >> 10) & 0x1f) - 16;
								px.rgba.g += ((b1 >> 5) & 0x1f) - 16;
								px.rgba.b += (b1 & 0x1f) - 16;
								px.rgba.a += ((b1 >> 15) & 0x1f) - 16;
							}
						}
						else if ((b1 & QOI_MASK_4) == QOI_COLOR) {
							if ((b1 & 15) == 0)
							{
								// mode switch
								mode ^= 1;
								px_pos -= channels;
								continue;
							}
							if (b1 & 8) { px.rgba.r = bytes[p++]; }
							if (b1 & 4) { px.rgba.g = bytes[p++]; }
							if (b1 & 2) { px.rgba.b = bytes[p++]; }
							if (b1 & 1) { px.rgba.a = bytes[p++]; }
						}

						index[QOI_COLOR_HASH(px) % 64] = px;
					}

					if (channels == 4) { 
						*(qoi_rgba_t*)(pixels + px_pos) = px;
					}
					else {
						pixels[px_pos] = px.rgba.r;
						pixels[px_pos+1] = px.rgba.g;
						pixels[px_pos+2] = px.rgba.b;
					}
				}
			}
		}
	}

	return pixels;
}

#ifndef QOI_NO_STDIO
#include <stdio.h>

int qoi_write(const char *filename, const void *data, const qoi_desc *desc) {
	int size;
	void *encoded = qoi_encode(data, desc, &size);
	if (!encoded) {
		return 0;
	}

	FILE *f = fopen(filename, "wb");
	if (!f) {
		QOI_FREE(encoded);
		return 0;
	}
	
	fwrite(encoded, 1, size, f);
	fclose(f);
	QOI_FREE(encoded);
	return size;
}

void *qoi_read(const char *filename, qoi_desc *desc, int channels) {
	FILE *f = fopen(filename, "rb");
	if (!f) {
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	int size = ftell(f);
	fseek(f, 0, SEEK_SET);

	void *data = QOI_MALLOC(size);
	if (!data) {
		fclose(f);
		return NULL;
	}

	int bytes_read = fread(data, 1, size, f);
	fclose(f);

	void *pixels = qoi_decode(data, bytes_read, desc, channels);
	QOI_FREE(data);
	return pixels;
}

#endif // QOI_NO_STDIO
#endif // QOI_IMPLEMENTATION
