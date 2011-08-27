#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <netinet/in.h>

#define FOOTER_COOKIE "conectix"

#define FOOTER_VERSION_CURRENT 0x00010000

#define FEATURE_TEMPORARY 0x00000001
#define FEATURE_RESERVED 0x00000002

#define DISK_TYPE_FIXED 2
#define DISK_TYPE_DYNAMIC 3
#define DISK_TYPE_DIFFERENCING 4

#define DYNAMIC_COOKIE "cxsparse"

#define DYNAMIC_VERSION_CURRENT 0x00010000

#define BAT_ENTRY_NULL 0xffffffff

struct footer
{
     char cookie[8];
     uint32_t features;
     uint32_t version;
     uint64_t data_offset;
     uint32_t timestamp;
     char creator_name[4];
     uint32_t creator_version;
     char creator_os[4];
     uint64_t original_size;
     uint64_t current_size;
     uint32_t geometry;
     uint32_t disk_type;
     uint32_t checksum;
     char unique[16];
     char saved_state;
     char reserved[427];     
} __attribute__((packed));

struct dynamic_header
{
     char cookie[8];
     uint64_t data_offset;
     uint64_t table_offset;
     uint32_t version;
     uint32_t max_bat_entries;
     uint32_t block_size;
     uint32_t checksum;
     char parent_id[16];
     uint32_t parent_timestamp;
     uint32_t parent_reserved;
     char parent_name[512];
     char parent_locator[8][24];
     char reserved[256];
} __attribute__((packed));

static void error(char*, ...) __attribute__((noreturn,format(printf,1,2)));
static void error(char* fmt, ...)
{
     va_list argp;
     va_start(argp, fmt);
     vprintf(fmt, argp);
     va_end(argp);
     exit(1);
}

static uint64_t ntohq(uint64_t in)
{
     // assert(*((union { char a[4]; int b })1).a);
     union
     {
	  struct
	  {
	       uint32_t upper;
	       uint32_t lower;
	  } dwords;
	  uint64_t qword;
     } result, qq = { .qword = in };
     result.dwords.lower = ntohl(qq.dwords.upper);
     result.dwords.upper = ntohl(qq.dwords.lower);
     return result.qword;
}

static void xread(int fd, void* buffer, off_t size)
{
     int count = 0;
     while (count < size)
     {
	  int res = read(fd, buffer + count, size - count);
	  if (res > 0)
	  {
	       count += res;
	  }
	  else
	  {
	       break;
	  }
     }
     if (count != size)
     {
	  error("Failed to read data from input: %s\n", strerror(errno));
     }
}

static void xlseek(int fd, off_t dest, int whence)
{
     off_t result = lseek(fd, dest, whence);
     if (result < 0)
     {
	  error("Failed to seek: %s\n", strerror(errno));
     }
     printf("Seeked on fd %i value %lld to: %lld\n", fd, dest, result);
}

static void xwrite(int fd, void* buffer, size_t length)
{
     int res = write(fd, buffer, length);
     if (res != length)
     {
	  error("Failed to write data to output: %s\n", strerror(errno));
     }
}

static void* xmalloc(size_t size)
{
     void* result = malloc(size);
     if (!result)
     {
	  error("Failed to allocate memory.\n");
     }
     return result;
}

static void do_dynamic_convert(int in_fd, int out_fd, struct footer* header)
{     
     // Read the dynamic disk header
     xlseek(in_fd, ntohq(header->data_offset), SEEK_SET);
     struct dynamic_header dynamic_header;
     xread(in_fd, &dynamic_header, sizeof(dynamic_header));
     
     // Check the magic number
     if (strncmp(dynamic_header.cookie, DYNAMIC_COOKIE, sizeof(dynamic_header.cookie)))
     {
	  error("Invalid dynamic disk header.\n");
     }
       
     // Create a block buffer
     uint32_t block_size = ntohl(dynamic_header.block_size);
     void* block_buffer = malloc(block_size);
     
     // Read BAT
     uint32_t bat_count = ntohl(dynamic_header.max_bat_entries);
     uint32_t* bat = xmalloc(sizeof(uint32_t) * bat_count);     
     xlseek(in_fd, ntohq(dynamic_header.table_offset), SEEK_SET);
     xread(in_fd, bat, sizeof(uint32_t) * bat_count);
     
     // Perform conversion
     for (unsigned int i = 0; i < bat_count; i++)
     {
	  printf("%08x: %08x\n", i, ntohl(bat[i]));
	  if (bat[i] == BAT_ENTRY_NULL)
	  {
	       xlseek(out_fd, block_size, SEEK_CUR);
	  }
	  else
	  {
	       uint64_t oh_god_am_i_an_idiot = ntohl(bat[i]);
	       oh_god_am_i_an_idiot = (oh_god_am_i_an_idiot + 1) * 512;
	       xlseek(in_fd, oh_god_am_i_an_idiot, SEEK_SET);
	       xread(in_fd, block_buffer, block_size);
	       xwrite(out_fd, block_buffer, block_size);
	  }
     }
}

static void do_convert(int in_fd, int out_fd)
{
     // Read the footer copy as a header
     xlseek(in_fd, 0, SEEK_SET);
     struct footer image_header;
     xread(in_fd, &image_header, sizeof(image_header));  

     // Check the magic number
     if (strncmp(image_header.cookie, FOOTER_COOKIE, sizeof(image_header.cookie)))
     {
	  error("Invalid file header.\n");
     }

     // Decide what to do with this disk
     switch(ntohl(image_header.disk_type))
     {
     case DISK_TYPE_FIXED:
	  error("Fixed disks are unsupported. Try dd(1).\n");
	  
     case DISK_TYPE_DYNAMIC:
	  do_dynamic_convert(in_fd, out_fd, &image_header);
	  break;

     case DISK_TYPE_DIFFERENCING:
	  error("Differencing disks are unsupported.\n");

     default:
	  error("Unknown or deprecated disk type.\n");
     }
}

int main(int argc, char** argv)
{
     int err;

     // Check arguments
     if (argc != 3)
     {
	  error("Usage:\n\tdevhd <in> <out>\n");
     }

     // Open file parameters
     int in_fd = open(argv[1], O_RDONLY);
     if (in_fd < 0)
     {
	  error("Failed to open input: %s\n", strerror(errno));
     }

     int out_fd = open(argv[2], O_WRONLY, 0644);
     if (out_fd < 0)
     {
	  error("Failed to open output: %s\n", strerror(errno));
     }

     do_convert(in_fd, out_fd);

     return 0;
}

