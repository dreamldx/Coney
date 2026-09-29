# 显示方案：LVGL + lv_st7789 + spidev

目标设备：Raspberry Pi Zero 2 W + PiSugar Whisplay HAT（ST7789，240×280，4 线 SPI）。引脚见 [HARDWARE.md](HARDWARE.md)。

## 结论

用 **LVGL 的 `lv_st7789` 驱动 + 用户态 spidev**，不用内核的 `panel-mipi-dbi`（DRM）。

| 方案 | 整屏 | 局部刷新 | 说明 |
|---|---|---|---|
| **LVGL + lv_st7789 + spidev（推荐）** | **82 到 86 fps** | 32×32 约 2,400 次/秒，64×64 约 1,000 次/秒 | 不依赖内核显示栈，接近 SPI 极限 |
| 内核 `panel-mipi-dbi` + DRM 脏区 | 43 到 46 fps | 32×32 约 2,600 次/秒，64×64 约 730 次/秒 | 有标准 `/dev/fb1`、`/dev/dri`，但整屏慢一倍 |
| 内核 `panel-mipi-dbi` + LVGL DRM 后端 | 47 fps | 不支持（每次整屏） | LVGL 不给内核传脏区 |
| 官方 Whisplay Python 库（spidev） | 约 81 fps | 需自己实现 | 已验证硬件正常 |

所有数字都是在这台设备上、SPI 100 MHz 下实测的，测试程序在 [`examples/lvgl_st7789_spidev_bench.c`](examples/lvgl_st7789_spidev_bench.c)。

## 为什么不用内核 DRM 驱动

追踪 `mipi_dbi_fb_dirty`（function_graph）得到整屏一次更新的耗时分解：

| 步骤 | 耗时 |
|---|---|
| `drm_fb_swab`：逐像素字节交换 | 8.48 ms |
| 设置窗口地址 | 0.19 ms |
| SPI 发送 134,400 字节 | 11.39 ms |
| 合计 | 20.17 ms |

- SPI 本身已经接近理论值（100 MHz 下 10.75 ms），传输大小、时钟都不是瓶颈：同样的 100 MHz，用户态 spidev 发 64 KB 是 10.98 ms（约 98 Mbit/s）。
- 慢在字节交换：BCM2835 的 SPI 只支持 8 位字长，内核必须先把每个 RGB565 像素的两个字节对调，而 `drm_fb_swab` 直接读 write-combine（不缓存）的 dumb buffer。单独测这块内存：16 位逐个读要 8.02 ms/帧，`memcpy` 只要 0.79 ms，字节交换本身在普通内存里只要 0.06 ms。
- 这一步是内核代码里的问题，要提速得给内核打补丁（让它先 `memcpy` 到普通内存）。补丁没有做过，预期效果没有验证。

## 部署步骤（树莓派）

系统：Raspberry Pi OS Lite 64 位（Debian 13 trixie，内核 6.18）。

### 1. 打开 SPI，不加载显示 overlay

`/boot/firmware/config.txt`：

```
dtparam=spi=on
```

不要同时加载 `mipi-dbi-spi`，它会占用 SPI0 CS0。

### 2. 把 spidev 单次传输调大到 64 KB

默认 4 KB，整屏要分 33 次传输。调到 64 KB 只要 3 次，实测每帧从 11.94 ms 降到 10.98 ms。

在 `/boot/firmware/cmdline.txt` 那一行的末尾加上 `spidev.bufsiz=65536`（文件必须保持一行），然后重启：

```
... cfg80211.ieee80211_regdom=CN spidev.bufsiz=65536
```

用 `cat /sys/module/spidev/parameters/bufsiz` 确认是 65536。

不要用 `/etc/modprobe.d/spidev.conf` 写 `options spidev bufsiz=...`：设备树里有 spidev 节点，开机早期就会加载 spidev，这个配置读不到，重启后 `bufsiz` 仍然是 4096（已在设备上验证）。内核命令行参数不受这个影响。不设置也能用，程序会退回 4 KB 分块。

### 3. GPIO 用 `/dev/gpiomem` 直接读写寄存器

DC、复位、背光的切换频率很高（每次刷新多次），测试程序直接映射 `/dev/gpiomem` 操作 `GPFSEL`、`GPSET0`、`GPCLR0`，比 gpiod 简单也快。

| 信号 | 物理引脚 | BCM |
|---|---|---|
| 复位 RST | P7 | GPIO4 |
| 数据/命令 DC | P13 | GPIO27 |
| 背光（低电平点亮） | P15 | GPIO22 |

SPI0 CS0 = GPIO8（P24），MOSI = GPIO10（P19），SCLK = GPIO11（P23）。

测试是用 `sudo` 跑的。`/dev/gpiomem` 和 `/dev/spidev0.0` 是否让普通用户直接访问，我没有验证。

### 4. LVGL 配置和编译

LVGL v9.6.0。`lv_conf.h` 需要：

```c
#define LV_USE_GENERIC_MIPI 1
#define LV_USE_ST7789       1
#define LV_MEM_SIZE         (256 * 1024U)
#define LV_DEF_REFR_PERIOD  1      // 基准测试用，正式使用可以不改
```

改这些宏会让整个 LVGL 重新编译（Zero 2 W 上要几分钟），因为 `lv_conf.h` 被几乎所有源文件间接包含。

### 5. 屏幕初始化

```c
lv_display_t *d = lv_st7789_create(240, 280,
                                   LV_LCD_FLAG_MIRROR_X | LV_LCD_FLAG_MIRROR_Y,  // 对应 MADCTL 0xC0
                                   send_cmd, send_color);
lv_st7789_set_gap(d, 0, 20);      // 圆角，Y 偏移 20
lv_st7789_set_invert(d, true);    // 对应初始化里的 0x21
```

- 先做一次硬件复位：RST 高 100 ms，低 100 ms，高 120 ms。
- `lv_st7789_create` 之后再补发 Whisplay 的寄存器：`B2 B7 BB C2 C3 C4 C6 D0 E0 E1`，数值见示例程序。
- `send_color` 里要先调用 `lv_draw_sw_rgb565_swap()` 交换字节，再发送，最后调用 `lv_display_flush_ready()`。
- 打开背光：把 GPIO22 拉低。
- 渲染模式用 `LV_DISPLAY_RENDER_MODE_PARTIAL`。缓冲大小对整屏影响不大（28 到 280 行，79 到 86 fps），局部刷新时 28 行也够。

## 实测数据

100 MHz，每项 5 秒。局部刷新测试是一个方块移动，每次更新包含旧位置和新位置两个区域。

整屏更新（不同渲染缓冲）：

| 缓冲 | 每秒更新 | SPI 吞吐 |
|---|---|---|
| 280 行 | 81.9 | 11.0 MB/s |
| 140 行 | 83.8 | 11.3 MB/s |
| 70 行 | 85.6 | 11.5 MB/s |
| 28 行 | 78.9 | 10.6 MB/s |

局部更新（缓冲 280 行）：

| 方块 | 每秒更新 | 每次发送 | SPI 吞吐 |
|---|---|---|---|
| 8×8 | 4,238 | 0.2 KB | 1.08 MB/s |
| 16×16 | 4,398 | 0.9 KB | 4.26 MB/s |
| 32×32 | 2,396 | 2.9 KB | 7.07 MB/s |
| 64×64 | 1,003 | 10.0 KB | 10.24 MB/s |
| 120×120 | 331 | 33.3 KB | 11.29 MB/s |

- 很小的区域被固定开销限制，不是 SPI 带宽：每次 flush 要发两条窗口命令、一条写内存命令和像素数据，共 4 次 SPI 消息。
- 官方库的 `spidev` 速率测试（Whisplay Python，不同 SPI 速率）：100 MHz 80.7 fps，66 MHz 27.1，50 MHz 27.5，33 MHz 15.8，25 MHz 13.9。中间速率的实际时钟我没有确认，要用就用 100 MHz。

## 内核 DRM 方案的数据（备查）

`/dev/fb1` 写入：整屏 48.8 fps（`write()`）、17.0 fps（`mmap`，fbdev 延迟合并刷新）。

DRM 脏区（`drmModeDirtyFB`，自己写的测试）：

| 脏区 | 每秒调用 | 每次发送 | 每次耗时 |
|---|---|---|---|
| 8×8 | 10,150 | 0.1 KB | 0.10 ms |
| 16×16 | 6,124 | 0.5 KB | 0.16 ms |
| 32×32 | 2,592 | 2.0 KB | 0.39 ms |
| 64×64 | 726 | 8.0 KB | 1.38 ms |
| 120×140 | 171 | 32.8 KB | 5.84 ms |
| 整屏 | 42.8 | 131.3 KB | 23.37 ms |

内核路径的耗时与 SPI 速率成比例：100/50/25 MHz 下整屏分别 21.6、40.6、73.2 ms。

## 画面撕裂

移动方块的测试中可以看到撕裂。原因是屏幕自己按约 60 Hz 从显存扫描，我们同时在写显存，两个指针交叉就会断层。

- 真正的垂直同步需要 ST7789 的 TE（撕裂效应）输出脚，但 Whisplay 引脚表里没有这一根线，软件拿不到这个信号，所以做不了。
- 试过把 `MADCTL` 从 `0xC0` 改成 `0xD0`（置 ML 位，反转屏幕刷新方向，想让写入方向和刷新方向一致）：用户看到的效果没有区别，保持 `0xC0`。
- 没有试过的办法：把更新限制在约 60 fps 并固定相位，让撕裂线稳定在同一个位置（只能减轻，不能消除）；查 Whisplay 原理图确认 TE 是否引出到某个 GPIO。

## 还没有验证的地方

- 屏幕显示效果：用户确认 `lv_st7789` 路径（Coney 的 demo，开了 SPI 之后）显示正确。
- `/dev/gpiomem`、`/dev/spidev0.0` 的普通用户权限。
- 内存占用：Coney demo（带状态栏）运行时 RSS 约 2.5 MB，整机 used 约 144 MB，available 约 318 MB（整机数字是没有状态栏时测的，差别不大），还没到 100 MB 目标，主要是系统本身占用。系统内存优化见 [HARDWARE.md](HARDWARE.md) 的 “Memory optimization”。
- 内核补丁方案（预期整屏约 80 fps）：只是根据分解数据的预测。
