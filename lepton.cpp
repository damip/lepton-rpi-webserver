/*
	Lepton thermal camera image capture class by Damir Vodenicarevic
	
	This class runs a thread that continuously reads the latest camera frame into a buffer
	The following method
	uint8_t lepton::grab_image(uint8_t* to_begin, uint8_t lastseenid)
	waits for the arrival of a different frame than the one indexed by lastseenid,
	loads it into to_begin and returns its index 

	Based on the following example :
	 * LeptonModule-master/software/raspberrypi_video
	 * SPI testing utility (using spidev driver)
	 * Copyright (c) 2007  MontaVista Software, Inc.
	 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
	 * This program is free software; you can redistribute it and/or modify
	 * it under the terms of the GNU General Public License as published by
	 * the Free Software Foundation; either version 2 of the License.
	 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
 */

#include "lepton.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <cstring>

lepton::lepton() :
	pimg(imgbuf1)
{
	thrd= std::thread(&lepton::threadfunc, this);
}

void lepton::spi_open()
{
	spi_cs_fd = open("/dev/spidev0.1", O_RDWR);
	if(spi_cs_fd < 0)
		throw lepton_exception("Could not open SPI device");

	int status_value = -1;

	// spi mode
	unsigned char spi_mode = SPI_MODE_3;
	status_value = ioctl(spi_cs_fd, SPI_IOC_WR_MODE, &spi_mode);
	if(status_value < 0)
	{
		close(spi_cs_fd);
		throw lepton_exception("Could not set SPIMode (WR)...ioctl fail");
	}
	status_value = ioctl(spi_cs_fd, SPI_IOC_RD_MODE, &spi_mode);
	if(status_value < 0)
	{
		close(spi_cs_fd);
		throw lepton_exception("Could not set SPIMode (RD)...ioctl fail");
	}
	
	// bits per word
	unsigned char spi_bitsPerWord = 8;
	status_value = ioctl(spi_cs_fd, SPI_IOC_WR_BITS_PER_WORD, &spi_bitsPerWord);
	if(status_value < 0)
	{
		close(spi_cs_fd);
		throw lepton_exception("Could not set SPI bitsPerWord (WR)...ioctl fail");
	}
	status_value = ioctl(spi_cs_fd, SPI_IOC_RD_BITS_PER_WORD, &spi_bitsPerWord);
	if(status_value < 0)
	{
		close(spi_cs_fd);
		throw lepton_exception("Could not set SPI bitsPerWord (RD)...ioctl fail");
	}

	// speed
	unsigned int spi_speed = 10000000;				//1000000 = 1MHz (1uS per bit)
	status_value = ioctl(spi_cs_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed);
	if(status_value < 0)
	{
		close(spi_cs_fd);
		throw lepton_exception("Could not set SPI speed (WR)...ioctl fail");
	}
	status_value = ioctl(spi_cs_fd, SPI_IOC_RD_MAX_SPEED_HZ, &spi_speed);
	if(status_value < 0)
	{
		close(spi_cs_fd);
		throw lepton_exception("Could not set SPI speed (RD)...ioctl fail");
	}
}

void lepton::spi_close()
{
	if(spi_cs_fd < 0) return;
	int status_value= close(spi_cs_fd);
	spi_cs_fd= -1;
	if(status_value < 0)
		throw lepton_exception("Could not close SPI port cleanly");
}

void lepton::threadfunc()
{
	uint8_t *result= imgbuf2;
	uint8_t tmp_packet[PACKET_SIZE];

	bool connected= false;
	for(;;)
	{
		if(!connected)
		{
			try
			{
				spi_open();
			}
			catch(const std::exception& e)
			{
				std::cerr << e.what() << std::endl;
			
				std::unique_lock<std::mutex> lck(mtx);
				if(closing)
					return;
				else
				{
					std::this_thread::sleep_for(std::chrono::seconds(1));
					continue;
				}
			}

			connected= true;
			std::cout << "Connected." << std::endl;
		}

		//read data packets from lepton over SPI
		int resets = 0, curpos= 0;
		for(int j= 0 ; j < PACKETS_PER_FRAME ; ++j)
		{
			tmp_packet[1]= j?0:1;
			read(spi_cs_fd, tmp_packet, PACKET_SIZE); //todo better check errors (return value) / partial reads
			if(tmp_packet[1] != j)
			{
				j = -1; curpos= 0;
				++resets;
				usleep(1000);

				if(resets >= 15*PACKETS_PER_FRAME)
				{
					std::cerr << "SPI read operation failed. Reconnecting..." << std::endl;
					thread_close_connections();
					connected= false;
					std::this_thread::sleep_for(std::chrono::seconds(1));
					break;
				}
				continue;
			}
			std::memcpy(result+curpos, tmp_packet+PACKET_HEADER_SIZE, PACKET_SIZE-PACKET_HEADER_SIZE); //no endianness change necessary : [high low]
			curpos += PACKET_SIZE-PACKET_HEADER_SIZE;
		}
		
		{ //send image
			uint8_t *tmpp;
			std::unique_lock<std::mutex> lck(mtx);
			
			if(closing) break; //if closing, exit
			if(!connected) continue; //if not connected, reconnect

			//swap buffers
			tmpp= pimg;
			pimg= result;
			result= tmpp;

			//update index
			if(++imgid == 0) imgid= 1;
			
			//notify the people waiting for the next image
			condvar.notify_all();
		}
	}

	thread_close_connections();
}

void lepton::thread_close_connections()
{
	try
	{
		spi_close();
	}
	catch(const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
	}
}

uint8_t lepton::grab_image(uint8_t* to_begin, uint8_t lastseenid= 0)
{
	std::unique_lock<std::mutex> lck(mtx);
	
	//wait for new picture with timeout
    if(condvar.wait_for(lck, std::chrono::seconds(5), [this,lastseenid](){return (imgid != lastseenid) && (imgid > 0);}))
    {
		std::memcpy(to_begin, pimg, FRAME_SIZE);
		return imgid;
    }
	else
		throw lepton_exception("Timout while waiting for an image");
}

lepton::~lepton()
{
	{ //launch closing procedure
		std::unique_lock<std::mutex> lck(mtx);
		closing= true;
	}
	
	thrd.join();
}
