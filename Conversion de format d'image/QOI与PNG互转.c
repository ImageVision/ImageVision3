#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define QOI_IMPLEMENTATION
#include "qoi.h"

//PNGÍ¼Ïñ×ªÎªQOIÍ¼Ïñ 
void PNGtoQOI(char* input,char* output) 
{
	void *pixels = NULL;
	int w, h, channels;
	 
	pixels = (void *)stbi_load(input, &w, &h, &channels, 0);
    qoi_write(output, pixels, &(qoi_desc){
			.width = w,
			.height = h, 
			.channels = channels,
			.colorspace = QOI_SRGB
		});

	free(pixels);
}

//QOIÍ¼Ïñ×ªÎªPNGÍ¼Ïñ 
void QOItoPNG(char* input,char* output) 
{
	void *pixels = NULL;
	int w, h, channels;
	
		qoi_desc desc;
		pixels = qoi_read(input, &desc, 0);
		channels = desc.channels;
		w = desc.width;
		h = desc.height;
		
		stbi_write_png(output, w, h, channels, pixels, 0);

	free(pixels);
}
