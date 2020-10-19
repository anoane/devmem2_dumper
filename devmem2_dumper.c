/*
 * devmem2_dumper.c: Simple program, based on devmem2 to dump /dev/mem.
 *
 *  Copyright (C) 2000, Jan-Derk Bakker (J.D.Bakker@its.tudelft.nl)
 *  Copyright (C) 2020, Fabiano Anemone (fabiano.anemone@gmail.com)
 *
 *
 * This software has been developed for the LART computing board
 * (http://www.lart.tudelft.nl/). The development has been sponsored by
 * the Mobile MultiMedia Communications (http://www.mmc.tudelft.nl/)
 * and Ubiquitous Communications (http://www.ubicom.tudelft.nl/)
 * projects.
 *
 * The author can be reached at:
 *
 *  Jan-Derk Bakker
 *  Information and Communication Theory Group
 *  Faculty of Information Technology and Systems
 *  Delft University of Technology
 *  P.O. Box 5031
 *  2600 GA Delft
 *  The Netherlands
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <setjmp.h>

// customize it
#define PATH_DEVMEM_DUMP "/mnt/usb/xxx/"
  
#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
  __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)
 
#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

// sigbus handling
static sigjmp_buf sigjmp_env;
 
static void sigbus_handler(int sig, siginfo_t *siginfo, void *ptr)
{
	siglongjmp(sigjmp_env, 1);
}

void write_to_file(off_t target, char* buff)
{
	// open, write, flush and close each time, cause device might panic
	char file_path[256];
	sprintf(file_path, PATH_DEVMEM_DUMP "devmem_%08X.bin", target);
	FILE* f_usb = fopen(file_path, "a");
	fwrite(buff, 1, MAP_SIZE, f_usb);
	fflush(f_usb);
	fclose (f_usb);
}

// regular memcpy doesn't work well, use volatile
volatile void *memcpy_v(volatile void *dest, const volatile void *src, size_t n)
{
	const volatile char *src_c  = (const volatile char *)src;
	volatile char *dest_c	   = (volatile char *)dest;

	size_t i = 0;
	for (i = 0; i < n; i++)
		dest_c[i]   = src_c[i];

	return  dest;
}

int main(int argc, char **argv) {

	// sigbus handling

	struct sigaction sig_act;
 
	memset (&sig_act, 0, sizeof(sig_act));
	sig_act.sa_sigaction = sigbus_handler;
	sig_act.sa_flags = SA_SIGINFO;
 
	if (sigaction(SIGBUS, &sig_act, 0)) FATAL;

	// end of sigbus handling

	int fd;
	void *map_base, *virt_addr; 
	unsigned long read_result, writeval;
	off_t target;
	off_t starting_target;
	char buff[MAP_SIZE];
	
	if(argc < 2) {
		printf("No argument passed, starting from 0x00000000.\n"); 
		target = 0;
	} else {
		target = strtoul(argv[1], 0, 0);
	}
	starting_target = target;

	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) FATAL;
	printf("/dev/mem opened. let's dump from: %08X\n", target);

	do {
		// Map one page
		map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target & ~MAP_MASK);
		if(map_base == (void *) -1) FATAL;
		//printf("Memory mapped at address %p.\n", map_base); 
		
		virt_addr = map_base + (target & MAP_MASK);
		
		// save state, if memcpy_v and sigbus is catched, we can resume here
		if (sigsetjmp(sigjmp_env, 1)) {
			//printf("Failed to read value at address 0x%X (%p)\n", target, virt_addr); 
			if(munmap(map_base, MAP_SIZE) == -1) FATAL;
			target += MAP_SIZE;
			
			// starting_target is updated so we know from which point
			// we can continue reading properly when sigbus is not catched anymore
			// e.g. files will be saved with this format
			// devmem_00000000.bin
			// (sigbus)
			// devmem_10A20000.bin
			// etc.
			starting_target = target;
			
			continue;
		}

		// read one page at the time
		memcpy_v(buff, virt_addr, MAP_SIZE);
		write_to_file(starting_target, buff);

		if(munmap(map_base, MAP_SIZE) == -1) FATAL;
		target += MAP_SIZE;
		continue;

	} while (target != 0); // stop when it wraps around

	close(fd);
	return 0;
}

