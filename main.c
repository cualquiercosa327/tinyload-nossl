/*
	TinyLoad - a simple region free (original) game launcher in 4k

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

/* This code comes from HBC's stub which was based on dhewg's geckoloader stub */
// Copyright 2008-2009  Andre Heider  <dhewg@wiibrew.org>
// Copyright 2008-2009  Hector Martin  <marcan@marcansoft.com>

#include "utils.h"
#include "debug.h"
#include "usb.h"
#include "ios.h"
#include "cache.h"
#include "di.h"

#define COLOR_BLACK        0x00800080
#define COLOR_GRAY         0x80808080
#define COLOR_WHITE        0xFF80FF80
#define COLOR_RED          0x4C544CFF
#define COLOR_GREEN        0x4B554B4A
#define COLOR_BLUE         0x1DFF1D6B
#define COLOR_YELLOW       0xE100E194

#define IOCTL_ES_LAUNCH					0x08
#define IOCTL_ES_GETVIEWCNT				0x12
#define IOCTL_ES_GETVIEWS				0x13

static struct ioctlv vecs[16] ALIGNED(64);

static u32 disc_id[0x40] ALIGNED(32);

static u8 tmd[0x5000] ALIGNED(64);

static struct {
	u32 count;
	u32 offset;
	u32 pad[6];
} part_table_info ALIGNED(32);

static struct {
	u32 offset;
	u32 type;
} partition_table[32] ALIGNED(32);

static struct
{
	char revision[16];
	void *entry;
	s32 size;
	s32 trailersize;
	s32 padding;
} apploader_hdr ALIGNED(32);

static int es_fd;

void fail(void) __attribute__((noreturn));
void prog10(void) __attribute__((noinline));

static int es_launchtitle(u64 titleID)
{
	static u64 xtitleID __attribute__((aligned(32)));
	static u32 cntviews __attribute__((aligned(32)));
	static u8 tikviews[0xd8*4] __attribute__((aligned(32)));
	int ret;
	
	debug_string("es_fd:");
	debug_uint(es_fd);
	debug_string("\n");

	debug_string("LaunchTitle: ");
	debug_uint(titleID>>32);
	debug_string("-");
	debug_uint(titleID&0xFFFFFFFF);

	xtitleID = titleID;

	debug_string("\nGetTicketViewCount: ");

	vecs[0].data = &xtitleID;
	vecs[0].len = 8;
	vecs[1].data = &cntviews;
	vecs[1].len = 4;
	ret = ios_ioctlv(es_fd, IOCTL_ES_GETVIEWCNT, 1, 1, vecs);
	debug_uint(ret);
	debug_string(", views: ");
	debug_uint(cntviews);
	debug_string("\n");
	if(ret<0) return ret;
	if(cntviews>4) return -1;

	debug_string("GetTicketViews: ");
	//vecs[0].data = &xtitleID;
	//vecs[0].len = 8;
	//vecs[1].data = &cntviews;
	//vecs[1].len = 4;
	vecs[2].data = tikviews;
	vecs[2].len = 0xd8*cntviews;
	ret = ios_ioctlv(es_fd, IOCTL_ES_GETVIEWS, 2, 1, vecs);
	debug_uint(ret);
	debug_string("\n");
	if(ret<0) return ret;
	debug_string("Attempting to launch...\n");
	//vecs[0].data = &xtitleID;
	//vecs[0].len = 8;
	vecs[1].data = tikviews;
	vecs[1].len = 0xd8;
	ret = ios_ioctlvreboot(es_fd, IOCTL_ES_LAUNCH, 2, 0, vecs);
	if(ret < 0) {
		debug_string("Launch failed: ");
		debug_uint(ret);
		debug_string("\r\n");
	}
	udelay(100000);
	return ret;
}

static s32 ios_getversion() __attribute__((unused));
static s32 ios_getversion()
{
	u32 vercode;
	u16 version;
	sync_before_read((void*)0x80003140,8);
	vercode = *((u32*)0x80003140);
	version = vercode >> 16;
	if(version == 0) return -1;
	if(version > 0xff) return -1;
	return version;
}

static void printversion(void)
{
	debug_string("IOS version: ");
	debug_uint(ios_getversion());
	debug_string("\n");
}

static int es_init(void)
{
	debug_string("Opening /dev/es: ");
	es_fd = ios_open("/dev/es", 0);
	debug_uint(es_fd);
	debug_string("\n");
	return es_fd;
}

static void simple_report(const char *fmt, ...)
{
	debug_string(fmt);
}

static u8 conf_buffer[0x4000] ALIGNED(32);

static u32 get_counter_bias(void)
{
	int fd;
	u32 bias = 0;

	fd = ios_open("/shared2/sys/SYSCONF", 1);
	if(ios_read(fd, conf_buffer, 0x4000) != 0x4000) {
		debug_string("Failed to get conf buffer\n");
		fail();
		//goto finish;
	}
	debug_string("Read SYSCONF\n");

	u16 count = *((u16*)(&conf_buffer[4]));
	u16 *offset = (u16*)&conf_buffer[6];

	while(count--) {
		if(/*(6 == ((conf_buffer[*offset]&0x0F)+1)) &&*/ !memcmp("IPL.CB", &conf_buffer[*offset+1], 6))
			break;
		offset++;
	}
	if(count == -1) {
		debug_string("Failed to find IPL.CB setting\n");
		fail();
		//goto finish;
	}
	u8 *b = &conf_buffer[*offset];
	//memcpy(&bias, &conf_buffer[*offset+7], 4);
	bias = (b[7]<<24) | (b[8]<<16) | (b[9]<<8) | b[10];
	debug_string("Counter bias: ");
	debug_uint(bias);
	debug_string("\n");

//finish:
	ios_close(fd);
	return bias;
}

static u32 read_rtc(void)
{
	u32 rtc;
	exi_chan0sr = 0x00000130;
	exi_chan0data = 0x20000000;
	exi_chan0cr = 0x39;
	while((exi_chan0cr)&1);
	exi_chan0cr = 0x39;
	while((exi_chan0cr)&1);
	rtc = exi_chan0data;
	debug_string("RTC: ");
	debug_uint(rtc);
	debug_string("\n");
	exi_chan0sr = 0x00000030;
	//udelay(100);
	return rtc;
}

static inline void write32(u32 w, u32 addr)
{
  *((u32 *)(addr + 0x80000000)) = w;
}

static inline void write16(u16 w, u16 addr)
{
  *((u16 *)(addr + 0x80000000)) = w;
}

#define framebuffer ((u32*)(0x81600000))

void memset32(u32 *addr, u32 data, u32 count) __attribute__ ((externally_visible));

void memset32(u32 *addr, u32 data, u32 count) 
{
	int sc = count;
	void *sa = addr;
	while(count--)
		*addr++ = data;
	sync_after_write(sa, 4*sc);
}

static void drawbar(int pix)
{
	int i = 16;
	u32* p = framebuffer + 320 * 400;
	while(i--) {
		memset32(p, COLOR_WHITE, pix);
		p += 320;
	}
}

/*
u32 vir(u32 addr) __attribute__((noinline));
u32 vir(u32 addr)
{
	return *(vu32*)(addr+0xCC002000);
}*/

inline void viw(u32 addr, u32 data) 
{
	*(vu32*)(addr+0xCC002000) = data;
}

void fail(void)
{
	memset32(framebuffer, COLOR_RED, 320*100);
	debug_string("\nFAILURE\n");
	es_launchtitle(0x100014a4f4449LL);
	while(1);
}

#ifdef DEBUG
#define FAILNOTONE(val,msg) do { int __v = (val); if(__v != 1) { debug_string(msg "Value: "); debug_uint(__v); debug_string("\n"); fail(); } } while(0)
#else
#define FAILNOTONE(val,msg) failnotone((val))

static void failnotone(int v) __attribute__((noinline));
static void failnotone(int v) {
	if(v != 1)
		fail();
}
#endif

int progress = 20;

static void prog(int p) __attribute__((noinline));
static void prog(int p) {
	progress += p;
	drawbar(progress);
}

void prog10(void) {
	prog(10);
}

//#define prog10() do{}while(0)
//#define prog(x) do{}while(0)

static int nossl(void *addr, int len)
{
	char *cur = (char *)addr;
	const char *end = cur + len - 8;
	int count = 0;

	do
	{
		if (cur[0] == 'h' && cur[1] == 't' && cur[2] == 't' && cur[3] == 'p' && cur[4] == 's' && cur[5] == ':' && cur[6] == '/' && cur[7] == '/' &&cur[8] != 0)
		{
			int len = strlen(cur);
			memmove(cur + 4, cur + 5, len - 5);
			cur[len - 1] = 0;
			sync_after_write(cur, len);
			cur += len;
			count++;
		}
	} while (++cur < end);

	return count;
}

void _main (void)
{
	int ret;
	int i;
	u64 disc_ios;
	u32 bias;
	u32 rtc = 0;
	u32 rtc2 = 1;
	u64 tbtime;
	u32 tmp;
	u32 vtrdcr;
	u32 oldvtrdcr;
	u32 vimode;
	u32 vto,vte;
	u64 oldvtovte;

	void (*app_init)(void (*report)(const char *fmt, ...));
	int (*app_main)(void **dst, int *size, int *offset);
	void *(*app_final)(void);
	void (*entrypoint)(void) __attribute__((noreturn));
	void (*app_entry)(void(**init)(void (*report)(const char *fmt, ...)), int (**main)(), void *(**final)());

	debug_string("\nTinyLoad v0.2\n");

	memset32(framebuffer, COLOR_BLACK, 320*574);
	memset32(framebuffer + 320 * 398, COLOR_WHITE, 320*2);
	memset32(framebuffer + 320 * 416, COLOR_WHITE, 320*2);

	vtrdcr = oldvtrdcr = *(vu32*)(0xCC002000);
	oldvtovte = *(vu64*)0xCC00200c;
	vtrdcr &= 0xFFFFF;
	vtrdcr |= 0x0F000000;
	vimode = (vtrdcr>>8)&3;
	
	vto = 0x30018;
	vte = 0x20019;
	
	if(vtrdcr & 4) { // progressive
		vto = vte = 0x60030;
		vtrdcr += 0x0F000000;
	} else if(vimode == 1) {
		vto = 0x10023;
		vte = 0x24;
		vtrdcr += 0x02F00000;
	}
	viw(0x0, vtrdcr);
	viw(0xc, vto);
	viw(0x10, vte);
	
	viw(0x1c, (((u32)framebuffer) >> 5) | 0x10000000);
	viw(0x24, (((u32)framebuffer) >> 5) | 0x10000000);
	
	prog(0);

	ipc_init();

	//reset_ios();
	printversion();

	bias = get_counter_bias();
	while(rtc != rtc2) {
		rtc2 = rtc;
		rtc = read_rtc();
	}
	
	tbtime = ((u64)((u32)(rtc+bias)))*(243000000u/4);
	asm volatile(
		"li             %0,0\n\
		mttbl  %0\n\
		mttbu  %1\n\
		mttbl  %2\n"
		: "=&r" (tmp)
		: "b" ((u32)(tbtime >> 32)), "b" ((u32)(tbtime & 0xFFFFFFFF))
	);

	if(es_init() < 0) {
		debug_string("es_init() failed\n");
		fail();
	}
	debug_string("Initializing DI\n");
	if(di_init() < 0) {
		debug_string("di_init() failed\n");
		fail();
	}

	prog10();
	//di_closepartition();
	if((ret = di_getcoverstatus()) != 2) {
		debug_string("di_getcoverstatus() failed\n");
		fail();
	}
	debug_string("Resetting drive\n");
	FAILNOTONE(di_reset(), "di_reset() failed\n");
	prog10();
	debug_string("Identifying disc\n");
	FAILNOTONE(di_identify(), "di_identify() failed\n");
	prog(40);
	debug_string("Reading Disc ID\n");
	FAILNOTONE(di_readdiscid(disc_id), "di_readdiscid() failed\n");
	prog10();
	//*(vu32*)0xcd8000c0 |= 0x100;
	debug_string("Gamecode: ");
	debug_uint(disc_id[0]);
	debug_string("\n");
	if(disc_id[6] != 0x5d1c9ea3) {
		debug_string("Not a valid Wii disc!\n");
		fail();
	}
	debug_string("Reading partition table info\n");
	FAILNOTONE(di_unencryptedread(&part_table_info, sizeof(part_table_info), 0x10000), "Read failed\n");
	prog10();
	debug_string("Reading partition table\n");
	FAILNOTONE(di_unencryptedread(partition_table, sizeof(partition_table), part_table_info.offset), "Read failed\n");
	prog10();

	for(i=0; i<part_table_info.count; i++) {
		if(partition_table[i].type == 0) {
			break;
		}
	}
	if(i >= part_table_info.count) {
		debug_string("Could not find valid partition table\n");
		fail();
	}
	debug_string("Opening partition at ");
	debug_uint(partition_table[i].offset);
	debug_string("\n");
	FAILNOTONE(di_openpartition(partition_table[i].offset, tmd), "Failed to open partition");
	prog10();

	disc_ios = (((u64)*(u32*)&tmd[0x184]) << 32) | *(u32*)&tmd[0x188];
	debug_string("Disc IOS: ");
	debug_uint(disc_ios >> 31);
	debug_string(" ");
	debug_uint(disc_ios & 0xFFFFFFFF);
	debug_string("\n");
	debug_string("Reloading IOS...\n");
	if(es_launchtitle(disc_ios) < 0) {
		debug_string("Failed to launch disc IOS\n");
		fail();
	}
	debug_string("IOS reloaded!\n");
	ipc_init();
	debug_string("IPC reinited.\n");
	printversion();
	if(es_init() < 0) {
		debug_string("es_init() failed\n");
		fail();
	}
	debug_string("Initializing DI\n");
	if(di_init() < 0) {
		debug_string("di_init() failed\n");
		fail();
	}
	prog(20);
	debug_string("Reading Disc ID\n");
	FAILNOTONE(di_readdiscid(disc_id), "di_readdiscid() failed\n");
	prog10();
	debug_string("Opening partition again\n");
	FAILNOTONE(di_openpartition(partition_table[i].offset, tmd), "Failed to open partition the second time");
	prog10();
	debug_string("Reading partition header\n");
	FAILNOTONE(di_read((void*)0x80000000, 0x20, 0), "Failed to read partition header");
	prog10();
	debug_string("Reading apploader header\n");
	FAILNOTONE(di_read(&apploader_hdr, 0x20, 0x910), "Failed to read apploader header");
	prog10();
	debug_string("Reading apploader\n");
	FAILNOTONE(di_read((void*)0x81200000, apploader_hdr.size+apploader_hdr.trailersize, 0x918),"Failed to read apploader ");
	sync_before_exec((void*)0x81200000, apploader_hdr.size+apploader_hdr.trailersize);
	prog10();

	app_entry = apploader_hdr.entry;
	app_entry(&app_init, &app_main, &app_final);
	app_init(simple_report);
	prog10();

	while(1) {
		void *dst;
		int size;
		int offset;
		int res;

		res = app_main(&dst, &size, &offset);
		if(res != 1)
			break;
		debug_string("Req: ");
		debug_uint((u32)dst);debug_string(" ");
		debug_uint((u32)size);debug_string(" ");
		debug_uint((u32)offset);debug_string("\n");
		if(di_read(dst, size, offset) != 1) {
			debug_string("Warning: failed to read apploader request\n");
		}
		if (dst >= (void*)0x80000000 && dst < (void*)0x80F00000) {
			nossl(dst, size);
		}
		prog10();

	}

	debug_string("Apploader complete\n");
	di_shutdown();
	ios_close(es_fd);
	prog10();

	//write16(0x0006, 0x0000);                         // DVDInit

	if(((u8*)disc_id)[3] == 'P' && vimode == 0)
		vimode = 1;

	debug_string("VI mode: ");
	debug_uint(vimode);
	debug_string("\n");

	*(u32*)0x800000cc = vimode;

#define write32(a,b) a,b>>16,b&0xFFFF,
#define clear32(a) (a|0x8000),
	// this is a workaround for an sstrip "feature" that eats NULL bytes at the end of the file
	// this array doesn't end in NULL, so put it last, after strings
	// otherwise LD eats the NULL terminator from some string and causes fail
	static const u16 pokes[] __attribute__((section(".rodata.zzlast")))  = {

		//write32(0x0018, 0x5d1c9ea3)              // magic word it is a wii disc
		write32(0x0020, 0x0d15ea5e)
		write32(0x0024, 0x00000001)
		write32(0x0028, 0x01800000)
		write32(0x002c, 0x00000023)
		//clear32(0x0030)                          // Init
		write32(0x0060, 0x38a00040)              // Exception init
		//write32(0x00cc, 0x00000000)              // VIInit
		write32(0x00e4, 0x8008f7b8)              // Thread Init
		//write32(0x00ec, 0x81800000)
		write32(0x00f0, 0x01800000)
		write32(0x00f8, 0x0e7be2c0)              // EXI
		write32(0x00fc, 0x2b73a840)              // EXI
		clear32(0x30c0)                          // EXI
		clear32(0x30c4)                          // EXI
		clear32(0x30dc)                          // Time
		clear32(0x30d8)                          // Time
		clear32(0x30e0)                          // PADInit
		clear32(0x30e4)                          // Console type
		//write32(0x30f0, 0x0000001C)              // apploader
		write32(0x3100, 0x01800000)              // BAT
		write32(0x3104, 0x01800000)              // BAT
		clear32(0x310c)                          // Init
		clear32(0x3110)                          // Init
		write32(0x311c, 0x04000000)              // BAT
		write32(0x3118, 0x04000000)
		write32(0x3120, 0x93400000)              // BAT
		write32(0x3124, 0x90000800)              // Init - MEM2 low
		write32(0x3128, 0x933e0000)              // Init - MEM2 high
		write32(0x3130, 0x933e0000)              // IPC MEM2 low
		write32(0x3134, 0x93400000)              // IPC MEM2 high
		write32(0x315c, 0x80000113)              // OSInit, apploader
		write32(0x3138, 0x00000011)              // Console type
		//write32(0x3184, 0x80000000)              // Console type
		clear32(0x318c)
		clear32(0x3190)
	};
#undef write32
#undef clear32

	const u16 *p1 = pokes;
	const u16 *p2 = pokes + (sizeof(pokes)/2);

	while(p1 != p2) {
		u16 addr = *p1++;
		if(addr & 0x8000) {
			write32(0, addr & 0x7FFF);
		} else {
			u32 val = (*p1++)<<16;
			val |= *p1++;
			write32(val, addr);
		}
	}


//*(vu32*)0xCD00643C = 0x00000000;        // 32Mhz on Bus
	*(vu32*)0xCD006C00 = 0x00000000;	// deinit audio due to libogc fail

	*(u32*)0x80003180 = *(u32*)0x80000000;

	sync_after_write((void*)0x80000000, 0x3f00);

	progress = 310;
	prog10();
	udelay(60000);

	entrypoint = app_final();
	debug_string("Game entry: ");
	debug_uint((u32)entrypoint);
	debug_string("\n");

	debug_string("ok, taking the plunge\n");
	
	// this sets VI to black, but I can't fit it in yet...
	viw(0x0, oldvtrdcr);
	*(vu64*)(0xCC00200c) = oldvtovte;
	entrypoint();
	fail();
}

