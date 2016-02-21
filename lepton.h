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

#ifndef LEPTON_H
#define LEPTON_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <iterator>
#include <exception>
#include <string>
#include <cstdint>

#define PACKET_SIZE 164
#define PACKET_HEADER_SIZE 4
#define PACKETS_PER_FRAME 60
#define FRAME_SIZE ((PACKET_SIZE-PACKET_HEADER_SIZE)*PACKETS_PER_FRAME)

//l1 col1   l1 col2   l1 col3 ... l2 col1   l2 col2 ...

class lepton
{
public :
	lepton();
	~lepton();
	
	uint8_t grab_image(uint8_t* to_begin, uint8_t lastseenid);

private :
	void spi_open();
	void spi_close();
	void thread_close_connections();
	
	void threadfunc();

	int spi_cs_fd= -1;
	bool closing= false;
	uint8_t imgid= 0;
	uint8_t imgbuf1[FRAME_SIZE], imgbuf2[FRAME_SIZE];
	uint8_t *pimg;
	std::thread thrd;
	std::mutex mtx;
	std::condition_variable condvar;
};

class lepton_exception : public std::exception
{
public :
	explicit lepton_exception(const char* str) : errstr(std::string("lepton exception : ") + str) {}
	explicit lepton_exception(const std::string& str) : errstr(std::string("lepton exception : ") + str) {}
	virtual const char* what() const throw() { return errstr.c_str(); }
	virtual ~lepton_exception() throw (){}
protected :
	const std::string errstr;
};

#endif
