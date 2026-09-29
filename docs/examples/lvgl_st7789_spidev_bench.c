#include "lvgl/lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#define RST 4
#define DC 27
#define BL 22
static volatile uint32_t*gpio; static int spi_fd; static size_t chunk=4096;
static uint32_t tick_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000u+t.tv_nsec/1000000u;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void gout(int pin){volatile uint32_t*r=gpio+pin/10;int sh=(pin%10)*3;*r=(*r&~(7u<<sh))|(1u<<sh);}
static void gset(int pin,int v){if(v)gpio[7]=1u<<pin;else gpio[10]=1u<<pin;}
static void spi_write(const uint8_t*p,size_t n){while(n){size_t c=n<chunk?n:chunk;struct spi_ioc_transfer tr={.tx_buf=(unsigned long)p,.len=c};if(ioctl(spi_fd,SPI_IOC_MESSAGE(1),&tr)<0){perror("spi");exit(1);}p+=c;n-=c;}}
static long total_bytes=0; static int flushes=0;
static void send_cmd(lv_display_t*d,const uint8_t*cmd,size_t cs,const uint8_t*par,size_t ps){
  (void)d; gset(DC,0); spi_write(cmd,cs); if(ps){gset(DC,1); spi_write(par,ps);} }
static void send_color(lv_display_t*d,const uint8_t*cmd,size_t cs,uint8_t*par,size_t ps){
  gset(DC,0); spi_write(cmd,cs); gset(DC,1);
  lv_draw_sw_rgb565_swap(par,ps/2);
  spi_write(par,ps); total_bytes+=ps; flushes++;
  lv_display_flush_ready(d); }
int main(int argc,char**argv){
  const char*mode=argc>1?argv[1]:"full"; int secs=argc>2?atoi(argv[2]):8; int lines=argc>3?atoi(argv[3]):280; int mhz=argc>4?atoi(argv[4]):100; int bs=argc>5?atoi(argv[5]):32;
  FILE*f=fopen("/sys/module/spidev/parameters/bufsiz","r"); if(f){size_t b;if(fscanf(f,"%zu",&b)==1)chunk=b;fclose(f);}
  int m=open("/dev/gpiomem",O_RDWR|O_SYNC); gpio=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,m,0); if(gpio==MAP_FAILED){perror("gpiomem");return 1;}
  gout(RST);gout(DC);gout(BL);
  spi_fd=open("/dev/spidev0.0",O_RDWR); if(spi_fd<0){perror("spidev");return 1;}
  uint8_t mode0=0,bpw=8; uint32_t hz=mhz*1000000; ioctl(spi_fd,SPI_IOC_WR_MODE,&mode0);ioctl(spi_fd,SPI_IOC_WR_BITS_PER_WORD,&bpw);ioctl(spi_fd,SPI_IOC_WR_MAX_SPEED_HZ,&hz);
  gset(RST,1);usleep(100000);gset(RST,0);usleep(100000);gset(RST,1);usleep(120000);
  lv_init(); lv_tick_set_cb(tick_ms);
  lv_display_t*d=lv_st7789_create(240,280,LV_LCD_FLAG_MIRROR_X|LV_LCD_FLAG_MIRROR_Y,send_cmd,send_color);
  lv_st7789_set_gap(d,0,20); lv_st7789_set_invert(d,true);
  static const struct {uint8_t c,n,p[14];} regs[]={{0xB2,5,{0x0C,0x0C,0x00,0x33,0x33}},{0xB7,1,{0x35}},{0xBB,1,{0x32}},{0xC2,1,{0x01}},{0xC3,1,{0x15}},{0xC4,1,{0x20}},{0xC6,1,{0x0F}},{0xD0,2,{0xA4,0xA1}},
   {0xE0,14,{0xD0,0x08,0x0E,0x09,0x09,0x05,0x31,0x33,0x48,0x17,0x14,0x15,0x31,0x34}},{0xE1,14,{0xD0,0x08,0x0E,0x09,0x09,0x15,0x31,0x33,0x48,0x17,0x14,0x15,0x31,0x34}}};
  for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);i++) send_cmd(d,&regs[i].c,1,regs[i].p,regs[i].n);
  size_t bufsz=240*lines*2; static uint8_t*b1; b1=aligned_alloc(64,bufsz);
  lv_display_set_buffers(d,b1,NULL,bufsz,LV_DISPLAY_RENDER_MODE_PARTIAL);
  gset(BL,0);
  lv_obj_t*scr=lv_screen_active(); lv_obj_set_style_bg_color(scr,lv_color_black(),0); lv_obj_set_style_bg_opa(scr,LV_OPA_COVER,0);
  lv_obj_t*box=lv_obj_create(scr); lv_obj_set_size(box,bs,bs); lv_obj_set_style_bg_color(box,lv_color_hex(0xff0000),0); lv_obj_set_style_border_width(box,0,0); lv_obj_set_style_radius(box,0,0);
  lv_timer_handler(); flushes=0; total_bytes=0;
  double t0=now(); int n=0;
  while(now()-t0<secs){
    if(mode[0]=='f') lv_obj_set_style_bg_color(scr,lv_color_hex((n&1)?0x202020:0x0000c0),0);
    else lv_obj_set_pos(box,(n*7)%(240-bs),(n*5)%(240-bs));
    n++; lv_refr_now(d);
  }
  double dt=now()-t0;
  printf("%-5s box=%3d buf=%3d lines: %8.1f updates/s | %5.2f flushes/update | %7.2f KB/update | SPI %5.2f MB/s | %6.3f ms/update\n",
         mode,bs,lines,n/dt,(double)flushes/n,total_bytes/1024.0/n,total_bytes/dt/1e6,dt/n*1000);
  return 0;
}
