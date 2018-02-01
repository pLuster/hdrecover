/* hdrecover
 *
 * Copyright (C) 2006-2013 Steven Price
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

//Lorin: simply build with g++ -Wall hdrecover.cpp -o hdrecover
// or use the more elaborate 
// g++ -Wall -Wextra -Wcast-align -Wcast-qual -Wctor-dtor-privacy -Wdisabled-optimization -Wformat=2 -Winit-self -Wmissing-include-dirs -Woverloaded-virtual -Wredundant-decls -Wshadow -Wsign-conversion -Wsign-promo -Wstrict-overflow=4 -Wswitch-default -Wundef -Wno-unused -Wzero-as-null-pointer-constant -Wuseless-cast -Wno-unused-parameter -O3 hdrecover.cpp -o hdrecover

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/fs.h>


#include <vector> // Therefore we need a C++ compiler.
#include <iostream>
std::vector<int64_t> badLogicalBlocks;
std::vector<int64_t> recoveredLogicalBlocks;

int64_t nRetry = 1000; // I had cases of successful reading after over 700 tries! Lorin
bool neverErase = false; //Lorin

const char *program_name;

int badblocks = 0;
int recovered = 0;
int destroyed = 0;
bool confirm_all = false;
bool shown_big_warning = false;
unsigned int phys_block_size = 0;

unsigned int logical_block_size = 0; // Lorin

char *buf = NULL;

int fd = 0;

int64_t length = 0;

int correctsector(int64_t sectornum)
{
  ssize_t ret = 0;

  badblocks++;
  printf("Error at sector %ld\n", sectornum);

  printf("Attempting to pounce on it...\n");
  for (int i = 0; i < nRetry && ret != phys_block_size; i++) {
    long int b = random();
    double bd = (double) b;
    bd = double(length) / double(RAND_MAX) * bd;
    b = (long int) bd;
    ret = pread(fd, buf, phys_block_size, b * phys_block_size);

    if (ret != phys_block_size) {
      printf("WARNING: Failed to read the random sector %ld!\n", b);
    }

    ret = pread(fd, buf, phys_block_size, sectornum * phys_block_size);

    printf("Attempt %d of %ld from sector %12ld: %s\n",
           i + 1, nRetry, sectornum, (ret == phys_block_size) ? "SUCCESS!" : "FAILED");
  }
  if (ret != phys_block_size) {
    printf("Couldn't recover sector %ld\n", sectornum);
    if (!confirm_all) {
      printf("The data for this sector could not be recovered. ");
      if(!neverErase)
      {
        printf("However, destroying the\n");
        printf("contents of this sector (ie writing zeros to it) "
               "should cause the hard disk\n");
        printf("to reallocate it making the drive useable again\n");
        printf("Do you really want to destroy the data in sector %ld?"
               "\n [ (y)es / (n)o / (a)ll / (q)uit ]:", sectornum);
      }
    input:
      if (neverErase)
      {
        buf[0] = 'n';
        buf[1] = '\0';
      }
      else
      {
        fgets(buf, 10, stdin);
      }
      switch (*buf) {
      case 'n':
      {
        printf("Not wiping sector %ld, continuing...\n", sectornum);

        int64_t logBlockNum = sectornum * (phys_block_size / logical_block_size);//Lorin
        
        badLogicalBlocks.push_back(logBlockNum);
        
        int64_t nUnrecovered = (int64_t) badLogicalBlocks.size(); // Lorin, stick to signed
        printf("This many sectors have been not recovered: %ld\n", nUnrecovered); // Lorin

        return 0;
      }
      case 'q':
      {
        printf("Not wiping sector %ld, exiting.\n", sectornum);
        return 1;
      }
      case 'a':
      {
        printf("You requested to wipe all bad sectors "
               "without further confirmation.\n");
        confirm_all = true;
        break;
      }
      case 'y':
        break;
      default:
        printf("Illegal input '%s', please retry: "
               "[ (y)es / (n)o / (a)ll / (q)uit ]:\n", buf);
        goto input;
      }
    }
    if (!shown_big_warning) {
      printf("\n\n/---------\\\n");
      printf("| WARNING |\n");
      printf("\\---------/\n\n");
      printf("Up until this point you haven't lost any "
             "data because of this program\n");
      printf("However, if you say yes below YOU WILL LOSE DATA!\n");
      printf("Proceed at your own risk!\n\n");
      printf("Type 'destroy data' to continue "
             "(choice valid for all subsequent errors!)\n");
      fgets(buf, 50, stdin);
      if (strcmp(buf, "destroy data\n")) {
        printf("You didn't type 'destroy data' so I'm aborting\n");
        return 1;
      }
      shown_big_warning = true;
    }

    printf("\nWiping sector %ld...\n", sectornum);
    memset(buf, 0, phys_block_size);
    pwrite(fd, buf, phys_block_size, sectornum * phys_block_size);
    destroyed++;

    printf("Checking sector is now readable...\n");
    ret = pread(fd, buf, phys_block_size, sectornum * phys_block_size);
    if (ret != phys_block_size) {
      printf("I still couldn't read the sector!\n");
      printf("I'm sorry, but even writing to the sector hasn't fixed it"
             " - there's nothing\n");
      printf("more I can do!\n");
      sleep(1);
      return 1;
    } else {
      printf("Sector is now readable. But you have lost data!\n");
    }
  } else {
    recovered++;
    int64_t logBlockNum = sectornum * (phys_block_size / logical_block_size);//Lorin
    recoveredLogicalBlocks.push_back(logBlockNum);
  }
  return 0;
}

void usage()
{
   fprintf(stderr,"\033[1mUsage\033[0m");//print bold
   fprintf(stderr, ": %s [-s <logical start block>] [-e <logical end block>] [-r <number of retries>] [-n] <block device>\nTypically used via\033[1m\n ionice -c 3 ./hdrecover -n /dev/sda\n\033[0m -n enables neverErase mode\n",
          program_name);
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv, char **envp)
 {
  printf("hdrecover version " __DATE__ "\n"); // Lorin 
  printf("By Steven Price, Patrik Lundquist and ... and Lorin Kazaz\n\n");

  if (sizeof(off_t) != sizeof(int64_t)) {
    fprintf(stderr, "Offsets are not 64 bit!\n");
    fprintf(stderr, "This program must be compiled to have 64 bit file offsets\n");
    fprintf(stderr, "Exiting\n");
    return 1;
  }

  program_name = argv[0];
  int64_t logical_start_block = 0;
  int64_t logical_end_block = 0;

  int opt;
  while ((opt = getopt(argc, argv, "s:e:r:d:n")) != -1) { // Lorin
    switch (opt) {
    case 's':
      logical_start_block = atoll(optarg);
      break;
    case 'e':
      logical_end_block = atoll(optarg);
      break;
    case 'r':
      nRetry = atoll(optarg);
    break;
    case 'd':
      if (!strcmp("DESTROY", optarg)) {
        confirm_all = true;
        shown_big_warning = true;
        printf("Destructive mode enabled!\n");
      }
      break;
    case 'n':
      neverErase = true;//Lorin
      printf("Never erase mode enabled!\n"); 
      break;
    default:
      usage();
    }
  }

  if (optind > argc - 1)
    usage();

  fd = open(argv[optind], O_RDWR | O_DIRECT);
  if (fd < 0) {
    fprintf(stderr, "Failed to open device '%s': %s\n", argv[optind], strerror(errno));
    return 1;
  }

  // this is just a seed, thus chopping the value due to cast is fine
  srandom((unsigned int) time(nullptr));//use C++11's nullptr instead of NULL // Lorin

  length = lseek(fd, 0, SEEK_END);

  if (length == -1) {
    fprintf(stderr, "Failed to get size of block device: %s\n", strerror(errno));
    return 1;
  }

  if (ioctl(fd, BLKPBSZGET, &phys_block_size) == -1) {
    fprintf(stderr, "Failed to get physical block size of device: %s\n", strerror(errno));
    return 1;
  }

  if (length % phys_block_size) {
    fprintf(stderr, "Block device size isn't a multiple of %u\n", phys_block_size);
    fprintf(stderr, "Is it really a block device?\n");
    fprintf(stderr, "Or does it have some strange sector size?\n");
    fprintf(stderr, "Since this is unexpected we won't continue\n");
    return 1;
  }
  length /= phys_block_size;

  //unsigned logical_block_size = 0; //Lorin

  if (ioctl(fd, BLKSSZGET, &logical_block_size) == -1) {
    fprintf(stderr, "Failed to get logical block size of device: %s\n", strerror(errno));
    return 1;
  }

  printf("Logical sector size is %d bytes\n", logical_block_size);
  printf("Physical sector size is %d bytes\n", phys_block_size);
  printf("Disk is %ld physical sectors big\n", length);

  time_t starttime;
  time(&starttime);

  time_t lasttime;
  time(&lasttime);

  int64_t sectornum = logical_start_block / (phys_block_size / logical_block_size);

  if (logical_end_block) {
    int64_t new_length = logical_end_block / (phys_block_size / logical_block_size);

    if (new_length > length) {
      fprintf(stderr, "Logical end block is out of range!\n");
      return 1;
    }
  else
      length = new_length;
  }

  if (sectornum >= length) {
    fprintf(stderr, "Logical start block is out of range!\n");
    return 1;
  }

  int64_t blocksize = phys_block_size * 20;

  // Ensure the buffer is block size byte aligned...
  if (posix_memalign((void **) &buf, phys_block_size, blocksize)) {
    fprintf(stderr, "Failed to allocate buffer!\n");
    return 1;
  }

  while (sectornum < length) {
    ssize_t ret = pread(fd, buf, blocksize, sectornum * phys_block_size);
    if (ret == blocksize) { // This 20x physical block size block is fine
      sectornum += blocksize / phys_block_size;
    } else {
      // Somewhere in this block there's something wrong...
      // Or we could just be at the end of the disk

      int64_t endsectornum = sectornum + blocksize / phys_block_size;

      if (endsectornum < length) {
        printf("Failed to read block at sector %ld, "
               "investigating further...\n", sectornum);
      } else {
        endsectornum = length;
      }
      while (sectornum < endsectornum) {
        ret = pread(fd, buf, phys_block_size, sectornum * phys_block_size);
        if (ret != phys_block_size) {
          if (correctsector(sectornum)) {
            goto summary;
          }
        }
        sectornum++;
      }
    }

    if (time(nullptr) != lasttime) {//Lorin
      time(&lasttime);
      char rs[256];
      *rs = 0;
      if (sectornum > 0) {
        time_t now;
        time(&now);
        int64_t remaining = now - starttime;
        remaining *= length;
        remaining /= sectornum;
        remaining -= (now - starttime);

        char *p = rs;

        if (remaining > 3600) {
          sprintf(p, "%ld hours ", remaining / 3600);
          p += strlen(p);
          remaining %= 3600;
        }
        if (remaining > 60) {
          sprintf(p, "%ld minutes ", remaining / 60);
          p += strlen(p);
          remaining %= 60;
        }
        sprintf(p, "%ld seconds", remaining);
      }
      
      printf("Sector %ld (%02d%%) ETR: %s; badLogicalBlocks = ", // Lorin
             sectornum, (int)((sectornum * 100) / length), rs);
      for (auto i = badLogicalBlocks.begin(); i != badLogicalBlocks.end(); ++i) // Lorin
      {
        std::cout << *i << ' ';
      }
      printf("; recoveredLogicalBlocks = "); // Lorin
      for (auto i = recoveredLogicalBlocks.begin(); i != recoveredLogicalBlocks.end(); ++i) // Lorin
        {std::cout << *i << ' ';}
      printf("\n");
    }
  }

summary:
  printf("Summary:\n");
  printf("  %d bad sectors found\n", badblocks);
  if (badblocks) {
    printf("  of those %d were recovered\n", recovered);
  }
  if (destroyed) {
    printf("  and %d could not be recovered and were destroyed "
           "causing data loss\n\n", destroyed);
    printf("*****************************************\n"
           "* You have wiped a sector on this disk! *\n"
           "*****************************************\n"
           "*  If you care about the filesystem on  *\n"
           "*  this disk you should run fsck on it  *\n"
           "*  before mounting it to correct any    *\n"
           "*  potential metadata errors.           *\n"
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
