/* hdrecover
 *
 * Copyright (C) 2005 Steven Price
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>
#include <time.h>

int main(int argc,char **argv,char **envp) {
	printf(PACKAGE " version " VERSION "\n");
	printf("By Steven Price\n\n");
	
	if (sizeof(off_t) != sizeof(unsigned long long)) {
		printf("Offsets are not 64bit!\n");
		printf("This program must be compiled to have 64 bit file offsets\n");
		printf("Exiting\n");
		return 1;
	}
	if (argc!=2) {
		printf("Usage %s <block device>\n",*argv);
		return 1;
	}
	int fd = open(argv[1],O_RDWR|O_DIRECT);
	if (fd < 0) {
		printf("Failed to open file '%s'\n",argv[1]);
		return 1;
	}

	int randfd = open("/dev/urandom",O_RDONLY);
	if (randfd < 0) {
		printf("Failed to open urandom device\n");
		return 1;
	}

	unsigned long long length = lseek(fd,0,SEEK_END);

	if (length == -1) {
		printf("Failed to get size of block device\n");
		return 1;
	}

	if (length % 512) {
		printf("Block device size isn't a multiple of 512\n");
		printf("Is it really a block device?\n");
		printf("As does it have some strange sector size?\n");
		printf("Since this is unexpected we won't continue\n");
		return 1;
	}
	length /= 512;

	printf("Disk is %Ld sectors big\n",length,errno);

	time_t starttime;
	time(&starttime);

	int shown_big_warning = 0;

	int badblocks = 0;
	int recovered = 0;
	int destroyed = 0;
	
	unsigned long long sectornum = 0;
	char *buf = (char*)malloc(10240);
	buf+=512;
	buf = (char*)(((unsigned long)buf)/512*512);
	int ret;
	while ((ret=pread(fd,buf,512,sectornum*512))!=0) {
		if (!(sectornum % 10000)) {
			char rs[256];*rs=0;
			if (sectornum > 0) {
				time_t now;
				time(&now);
				unsigned long long remaining = now-starttime;
				remaining *= length;
				remaining /= sectornum;
				remaining -= (now-starttime);

				char *p = rs;

				if (remaining > 3600) {
					sprintf(p,"%d hours ", remaining/3600);
					p+=strlen(p);remaining %= 3600;
				}
				if (remaining > 60) {
					sprintf(p,"%d minutes ", remaining/60);
					p+=strlen(p);remaining %= 60;
				}
				sprintf(p,"%d seconds",remaining);
			}
			
			printf("Sector %Ld (%02d%%) ETR: %s\n",
				sectornum,
				(int)((sectornum*100)/length),
				rs);
		}
		if (ret != 512) {
			badblocks++;
			printf("Error at sector %Ld\n",sectornum);

			printf("Attempting to pounce on it...\n");
			for(int i=0;i<20 && ret!=512;i++) {
				unsigned long long b;
				read(randfd,&b,sizeof(b));
				b %= length;
				pread(fd,buf,512,b*512);

				ret = pread(fd,buf,512,sectornum*512);

				printf("Attempt %d from sector %12Ld: %s\n",
					i+1,b,
					(ret==512)?"SUCCESS!":"FAILED");
			}
			if (ret != 512) {
				printf("Couldn't recover sector\n");
				printf("The data for this sector could not be recovered. However, destroying the\n");
				printf("contents of this sector (ie writing zeros to it) should cause the hard disk\n");
				printf("to reallocate it making the drive useable again\n");
				printf("Do you want to destroy the sector? [y/n]:");
				fgets(buf,10,stdin);
				if (*buf != 'y') {
					printf("That wasn't a yes, so I'm aborting\n");
					goto summary;
				}
				if (!shown_big_warning) {
					printf("\n\n/---------\\\n");
					printf("| WARNING |\n");
					printf("\\---------/\n\n");
					printf("Up until this point you haven't lost any data because of this program\n");
					printf("However, if you say yes below YOU WILL LOSE DATA!\n");
					printf("Proceed at your own risk!\n");
					printf("\nType 'destroy data' to continue\n");
					fgets(buf,50,stdin);
					if (strcmp(buf,"destroy data\n")) {
						printf("You didn't type 'destroy data' so I'm aborting\n");
						goto summary;
					}
					shown_big_warning = true;
				}

				printf("\nWiping sector...\n");
				memset(buf,0,512);
				pwrite(fd,buf,512,sectornum*512);
				destroyed++;

				printf("Checking sector is now readable...\n");
				ret = pread(fd,buf,512,sectornum*512);
				if (ret != 512) {
					printf("I still couldn't read the sector!\n");
					printf("I'm sorry, but even writing to the sector hasn't fixed it - there's nothing\n" "more I can do!\n");
					sleep(1);
					goto summary;
				} else {
					printf("Sector is now readable. But you have lost data!\n");
				}
			} else {
				recovered++;
			}
		}
		sectornum++;
	}
summary:
	printf("Summary:\n");
	printf("  %d bad sectors found\n",badblocks);
	if (badblocks) {
		printf("  of those %d were recovered\n",recovered);
	}
	if (destroyed) {
		printf("  and %d could not be recovered and were destroyed causing data loss\n",destroyed);
		printf("\n"
		"*****************************************\n"
		"* You have wiped a sector on this disk! *\n"
		"*****************************************\n"
		"*  If you care about the filesystem on  *\n"
		"*  this disk you should run fsck on it  *\n"
		"*  before mounting it to correct any    *\n"
		"*  potential metadata errors            *\n"
		"*****************************************\n\n");
		return 10;
	}
	if (recovered) {
		return 9;
	}
	if (badblocks) {
		return 8;
	}
	return 0;
}

