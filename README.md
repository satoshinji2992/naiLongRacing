# Racing

Nuttx 上的伪 3D 赛车游戏。**直接写 framebuffer 渲染,不依赖 LVGL**;
目标板为 Allwinner R528(`r528s3-velaevb1`),LCD 走 SPI。

游戏逻辑层(`game.c`)是纯 C、与渲染解耦的;`render_fb.c` 是嵌入式
framebuffer 渲染层;`render_sdl.c` 是桌面 SDL 参考实现(UI/玩法对齐基准)。

## 目录结构

```
Racing/
├── Makefile              # NuttX 应用构建(被 nuttx 的 Application.mk 调用)
├── Kconfig               # 本应用的配置项(设备路径、栈、数据根目录等)
├── Make.defs             # 把本目录注册进 CONFIGURED_APPS
├── CMakeLists.txt        # 桌面 SDL 参考构建(不参与 NuttX 编译)
├── include/
│   ├── config.h          # 屏幕/赛道/帧率等可调宏
│   ├── game.h            # 游戏状态与输入 API(纯核心,无渲染依赖)
│   ├── render_fb.h       # framebuffer 渲染层 API
│   ├── racing_input.h    # 触摸 + GPIO 输入 API
│   └── font.h            # 点阵字体 API(由 gen_font.py 生成)
├── src/
│   ├── game.c            # 玩法/投影/赛道生成/碰撞(纯核心)
│   ├── render_fb.c       # framebuffer 直绘:fb 后端 + 像素/三角/blit + 场景 + HUD
│   ├── font.c            # 点阵字体数据(自动生成,勿手改)
│   ├── racing_input.c    # 触摸(/dev/input0)+ GPIO 按键
│   ├── racing_main.c     # NuttX 主循环(输入→定步长 update→渲染→限帧)
│   ├── render_sdl.c      # 桌面 SDL 渲染(参考实现,UI 对齐基准)
│   ├── desktop_main.c    # SDL 窗口/键盘循环(桌面用)
│   └── main.c            # 桌面烟测入口
├── tools/
│   ├── convert_assets.py # PNG → raw 贴图(放板端数据分区)
│   └── gen_font.py       # Arial.ttf → 点阵字体(font.c / font.h)
└── assets/
    ├── images/           # 原始 PNG(car/nailong/mountain/cloud/...)
    ├── fonts/Arial.ttf   # 字体源
    └── audio/            # MP3(未接线,留作后续音频)
```

## 在 R528 上编译并打包固件

构建走全志 lichee 的 `m` + `pack`(在 `vendor/allwinnertech/lichee/` 下)。
**`source` / `lunch` / `m` / `pack` 必须在同一条 bash 命令里执行**(shell 状态不跨
调用持久):

```bash
cd vendor/allwinnertech/lichee
source tools/scripts/envsetup.sh
lunch_nuttx r528s3-velaevb1   

m                              # 编译 nuttx → strip → 拷 nuttx.bin 到 pack 配置目录
pack                           # 打包生成 .img
```

固件产物:
```
vendor/allwinnertech/lichee/out/r528s3/velaevb1_nand/rtos_nuttx_r528s3-velaevb1_uart0_256Mnand.img
```
用 LiveSuit/PhoenixSuit 烧到 NAND,串口进 NSH 跑 `racing`。

## 启用与配置

### 启用应用
在板子 defconfig(`vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/defconfig`)
里确保:
```
CONFIG_EXAMPLES_RACING=y
CONFIG_R528_UART1=y          # JY60 默认使用 /dev/uart1
```
本应用**不再依赖 LVGL**(`Kconfig` 已去掉 `depends on GRAPHICS_LVGL`)。

### Kconfig 选项(`Kconfig`,可用 menuconfig 改)
| 选项 | 默认 | 说明 |
|---|---|---|
| `EXAMPLES_RACING_FB_DEVPATH` | `/dev/fb0` | framebuffer 设备 |
| `EXAMPLES_RACING_INPUT_DEVPATH` | `/dev/input0` | 触摸屏设备 |
| `EXAMPLES_RACING_BUTTON1_DEVPATH` | `/dev/gpio1` | 按键1:start/pause |
| `EXAMPLES_RACING_BUTTON2_DEVPATH` | `/dev/gpio3` | 按键2:boost(Original Mode) |
| `EXAMPLES_RACING_BUTTON3_DEVPATH` | `/dev/gpio4` | 按键3:fly |
| `EXAMPLES_RACING_JY60_DEVPATH` | `/dev/uart1` | JY60 串口设备 |
| `EXAMPLES_RACING_JY60_BAUD` | `9600` | JY60 串口波特率 |
| `EXAMPLES_RACING_DATA_ROOT` | `/data` | 贴图所在分区挂载点(即 `/data/res/racing`) |
| `EXAMPLES_RACING_PRIORITY` | 100 | 任务优先级 |
| `EXAMPLES_RACING_STACKSIZE` | 327680 | 任务栈 |

JY60 接线前请以当前板级 pinmux/原理图为准确认 UART1 TX/RX 引脚;当前固件按
PE10/PE11 的 function 3 配置 UART1,默认打开 `/dev/uart1`、`9600 8N1`,菜单里的 **Gyro Mode** 会用姿态控制左右转;
Gyro Mode 下 boost 改为单点按住屏幕 0.1s 后触发,**Test Mode** 会直接显示解析到的 JY60 数据。

改设备路径:
```bash
cd vendor/allwinnertech/lichee
source tools/scripts/envsetup.sh
lunch_nuttx r528s3-velaevb1
m menuconfig   # 进入 Application Configuration → Examples → Racing Game Demo
```

### SPI 写屏时钟(性能关键)
LCD 走 SPI,写屏时钟决定帧率。已做成正式 Kconfig 项
(`nuttx/drivers/video/Kconfig` 的 `SPI_LCD_FB_FREQUENCY`,默认 40000000):
```
CONFIG_SPI_LCD_FB_FREQUENCY=40000000
```
板子 defconfig 里已显式钉为 40MHz(原板子是 8MHz,约 3fps)。改法二选一:
- menuconfig:`m menuconfig` → Device Drivers → Video support → SPI LCD framebuffer transfer frequency。
- 或直接改 defconfig 里的 `CONFIG_SPI_LCD_FB_FREQUENCY`。
- 若出现花屏/撕裂,下调到 `30000000` / `24000000` / `20000000`。

## 资产管线(改图/改字后必跑,再 m + pack)

贴图运行时从板端 `/data/res/racing/*.raw` 加载,字体烘焙进二进制。两者都由
`tools/` 下的脚本生成(需要 Pillow:`pip install Pillow`)。

```bash
cd apps/examples/Racing

# 1) PNG → raw(8字节头 magic+w/h,后接 RGBA),输出到板端数据分区
python3 tools/convert_assets.py
# 默认写到 vendor/.../lichee/board/common/data/UDISK/res/racing/(板端即 /data/res/racing)
# 改图后编辑 tools/convert_assets.py 的 TARGETS(尺寸/文件名)再跑。

# 2) Arial.ttf → 点阵字体(12x18,ASCII 32~126),生成 src/font.c + include/font.h
python3 tools/gen_font.py
```
然后 `m` + `pack`。**注意:改了 raw 必须重新 pack**(raw 在 usrdata 分区里);
改了字体或 C 代码重新 `m` 即可。

## 玩法与操控

- **自动前进**(固定速度),只需转向 + 收集奶龙 + 躲避。
- 触摸:**左半屏左转 / 右半屏右转 / 屏幕中央 = 开始·暂停**。
- Gyro Mode:单点按住屏幕 0.1s = boost(有能量时耗能加速)。
- GPIO:按键1 = 开始/暂停,按键2 = Original Mode boost,按键3 = fly(能量满起跳)。
- 吃奶龙 +100 能量并触发跳脸闪屏;跑完 3 圈显示成绩。

## 可调旋钮

| 位置 | 旋钮 | 作用 |
|---|---|---|
| `include/config.h` | `WIN_WIDTH/WIN_HEIGHT` | 渲染分辨率(默认 480×320) |
| `include/config.h` | `VIEW_DISTANCE` | 前方渲染段数(越小越快,画面越近) |
| `include/config.h` | `RACING_TARGET_FRAME_MS` | 目标帧节拍(33ms≈30FPS) |
| `src/render_fb.c` | `CAR_DISPLAY_H` | 车内视角车的高度 |
| `src/render_fb.c` | `SKY_DRAW_H` | 背景条带只画到地平线略下(性能) |
| `src/render_fb.c` | `render_sky_decor` 里的 y 偏移 | 云/远近山位置 |
| `src/render_fb.c` | `render_track` 里的 `bankAngle` | 转向时草地/路面倾角 |

## 桌面 SDL 参考(可选)

`render_sdl.c` 是 UI/玩法的对齐基准(桌面用 TTF 字体、硬件加速 blit),用于开发
对照,不参与 NuttX 编译。需要 SDL2 + SDL2_image + SDL2_ttf:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/racing_desktop     # W/S 油门/刹车,A/D 转向,Space boost,F fly,Esc/P 暂停
```

## 备注

- 渲染层每帧全屏重绘(立即模式),所以不做脏矩形;瓶颈在 SPI 写屏带宽,见上面的
  SPI 时钟配置。
- `assets/audio` 里的 MP3 当前未接线(核心不依赖音频),需要时在平台层接。
