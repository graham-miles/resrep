#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "wishbone_wrapper.h"
#include "config.h"

#define IMAGE_WIDTH 160
#define IMAGE_HEIGHT 120
#define NB_CHAN 2

char text_buffer[512];
char rgb_file_name[128];
char yuv_file_name[128];
char jpeg_file_name[128];

int min(int a, int b)
{
	if (a > b) return b;	
	return a;
}

int get_frame(void)
{
	unsigned short cmd_buffer[8];
	unsigned short vsync1, vsync2, fifo_state, fifo_data;
	FILE * rgb_fd, * yuv_fd, * jpeg_fd;
	int i, j, res, inc;
	unsigned int nbFrames = 1;
	unsigned int pos = 0;
	unsigned int nb = 0;
	unsigned char * image_buffer, * start_buffer, * end_ptr, * rgb_buffer;
	float y, u, v, r, g, b;
	unsigned int retry_counter = 0;
	unsigned int retry_pixel = 0;
	image_buffer = (unsigned char *) malloc(IMAGE_WIDTH*IMAGE_HEIGHT*NB_CHAN*4+1024);
    rgb_buffer = (unsigned char *) malloc(IMAGE_WIDTH*IMAGE_HEIGHT*3);
	if (image_buffer == NULL || rgb_buffer == NULL)
	{
		printf("Malloc fail\n");
		return -1;
	}
	for (inc = 0; inc < (nbFrames && retry_counter < 5); )
	{
		sprintf(jpeg_file_name, "./grabbed_frame%04d.jpg", inc);
		jpeg_fd  = fopen(jpeg_file_name, "wb");
		if (jpeg_fd == NULL)
		{
			printf("Error opening output file \n");
			exit(EXIT_FAILURE);
		}
		printf("issuing reset to fifo \n");
		cmd_buffer[0] = 0;
		cmd_buffer[1] = 0;
		cmd_buffer[2] = 0;
		wishbone_write((unsigned char *) cmd_buffer, 6, FIFO_ADDR+FIFO_CMD_OFFSET);
		wishbone_read((unsigned char *) cmd_buffer, 6, FIFO_ADDR+FIFO_CMD_OFFSET);
		printf("fifo size : %d, free : %d, available : %d \n", cmd_buffer[0], cmd_buffer[1], cmd_buffer[2]);
		nb = 0;
		retry_pixel = 0; 
		while (nb < ((IMAGE_WIDTH)*(IMAGE_HEIGHT)*NB_CHAN)*3 && retry_pixel < 10000)
		{
			wishbone_read((unsigned char *) cmd_buffer, 6, FIFO_ADDR+FIFO_CMD_OFFSET);
			while (cmd_buffer[2] < 1024 && retry_pixel < 10000)
			{
				 wishbone_read((unsigned char *) cmd_buffer, 6, FIFO_ADDR+FIFO_CMD_OFFSET);
				 retry_pixel ++;
			}
			wishbone_read_noinc(&image_buffer[nb], 2048, FIFO_ADDR);
			nb += 2048;
		}
		if (retry_pixel == 10000)
		{
			printf("no camera detected !\n");
            fclose(jpeg_fd);
			return -1;
		}
		printf("nb : %u \n", nb);
		start_buffer = image_buffer;
		end_ptr = &image_buffer[IMAGE_WIDTH*IMAGE_HEIGHT*NB_CHAN*3];
		vsync1 = *((unsigned short *) start_buffer);
		vsync2 = *((unsigned short *) &start_buffer[(IMAGE_WIDTH*IMAGE_HEIGHT*NB_CHAN)+2]);
		while (vsync1 != 0x55AA && vsync2 != 0x55AA && start_buffer < end_ptr)
		{
			start_buffer+=2;
			vsync1 = *((unsigned short *) start_buffer);
			vsync2 = *((unsigned short *) &start_buffer[(IMAGE_WIDTH*IMAGE_HEIGHT*NB_CHAN)+2]);
		}
		if (vsync1 == 0x55AA && vsync2 == 0x55AA)
		{
			inc ++;
			printf("frame found !\n");
		}
		else
		{
			printf("sync not found !\n");
			fclose(jpeg_fd);
			retry_counter ++;
			continue;
		}
		start_buffer += 2;
		for (i = 0; i < IMAGE_WIDTH*IMAGE_HEIGHT; i ++)
		{
			y = (float) start_buffer[(i*NB_CHAN)];
			if (NB_CHAN == 2)
			{
				if (i%2 == 0)
				{
					u = (float) start_buffer[(i*2)+1];
					v = (float) start_buffer[(i*2)+3];
				}
				else
				{
					u = (float) start_buffer[(i*2)-1];
        	        v = (float) start_buffer[(i*2)+1];
				}
			}
			else
			{
				u = 128;
				v = 128;
			}
			r =  y + (1.4075 * (v - 128.0));
			g =  y - (0.3455 * (u - 128.0)) - (0.7169 * (v - 128.0));
			b =  y + (1.7790 * (u - 128.0));
			rgb_buffer[(i*3)] = (unsigned char) abs(min(r, 255));
			rgb_buffer[(i*3)+1] = (unsigned char) abs(min(g, 255));
			rgb_buffer[(i*3)+2] = (unsigned char) abs(min(b, 255));
		} 
		write_jpegfile(rgb_buffer, IMAGE_WIDTH, IMAGE_HEIGHT, 3, jpeg_fd, 100);
		fclose(jpeg_fd);
	}
	if (retry_counter == 5) return -1;
	return 0;
}